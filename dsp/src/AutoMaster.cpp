#include "polisher/AutoMaster.h"
#include "polisher/FilterDesign.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <numeric>

namespace polisher {

// =================================================================================================
// Tables
// =================================================================================================

const char* genreName (Genre g) noexcept
{
    switch (g)
    {
        case Genre::trapHipHop: return "Trap / Hip-Hop";
        case Genre::rnb:        return "R&B";
        case Genre::pop:        return "Pop";
        case Genre::edm:        return "EDM";
        case Genre::rock:       return "Rock";
        case Genre::acoustic:   return "Acoustic";
    }
    return "";
}

double targetLufs (LoudnessTarget t) noexcept
{
    switch (t)
    {
        case LoudnessTarget::stream: return -14.0;
        case LoudnessTarget::loud:   return -9.0;
        case LoudnessTarget::club:   return -7.0;
    }
    return -9.0;
}

std::array<double, kEqBands> genreTargetCurve (Genre g) noexcept
{
    // Measured the same way as analyseMix(): mean power per Hz in each band, plus a 4.5 dB/oct tilt
    // around 1 kHz. Commercial masters measure roughly flat in this view from ~100 Hz to 4 kHz
    // (their per-Hz spectrum falls ~5 dB/oct there), fall off faster above 4 kHz, and have a
    // low cut around 60 Hz (Pestana et al., AES 2013). Genres differ mostly at the extremes.
    //                    Low End  Bass  LowMid  Mid  UpMid  Pres   Air
    switch (g)
    {
        case Genre::trapHipHop: return { -5.0,  2.0,  0.5,  0.0, -0.5, -2.5, -7.0 };
        case Genre::rnb:        return { -7.0,  1.8,  0.8,  0.0, -0.6, -2.5, -7.0 };
        case Genre::pop:        return { -9.0,  1.5,  0.8,  0.0, -0.4, -2.0, -6.5 };
        case Genre::edm:        return { -6.0,  2.0,  0.3,  0.0, -0.4, -2.0, -6.0 };
        case Genre::rock:       return { -11.0, 1.0,  1.0,  0.0,  0.0, -2.0, -7.5 };
        case Genre::acoustic:   return { -13.0, 0.5,  1.2,  0.0, -0.4, -2.5, -8.0 };
    }
    return {};
}

namespace {

// Band edges for analysis, around the EQ's default band frequencies.
constexpr std::array<double, kEqBands + 1> kEdges { 20.0, 65.0, 180.0, 600.0, 1600.0, 3500.0, 8000.0, 20000.0 };

const char* const kBandNames[kEqBands]  = { "Low End", "Bass", "Low Mid", "Mid", "Upper Mid", "Presence", "Air" };
const char* const kBandPlain[kEqBands]  = { "sub (below 65 Hz)", "bass (65\xE2\x80\x93" "180 Hz)", "low mids (180\xE2\x80\x93" "600 Hz)",
                                            "mids (0.6\xE2\x80\x93" "1.6 kHz)", "upper mids (1.6\xE2\x80\x93" "3.5 kHz)",
                                            "presence (3.5\xE2\x80\x93" "8 kHz)", "air (above 8 kHz)" };
const char* const kBoostEffect[kEqBands] = {
    "More sub weight under the 808 and kick.",
    "More body in the kick and 808.",
    "More warmth and thickness.",
    "Vocals, snares and melodies come forward.",
    "More vocal clarity and snare crack.",
    "More edge and definition on vocals and hi-hats.",
    "More sheen and openness on top." };
const char* const kCutEffect[kEqBands] = {
    "Less sub rumble, and more headroom for loudness.",
    "A tighter, less boomy low end.",
    "Less mud and boxiness, so the mix reads clearer.",
    "Less honk, so the mix sounds less crowded.",
    "Less harshness on earbuds.",
    "Softer 's' sounds and hi-hats.",
    "A smoother, less fizzy top." };
const char* const kCulprit[kEqBands] = {
    "usually the 808 or a sub layer",
    "usually the kick and 808 together",
    "often guitars, pads or a boomy vocal",
    "often a honky synth or vocal",
    "often a bright synth, a harsh vocal or the snare",
    "often hi-hats or vocal sibilance",
    "often hi-hats or a bright reverb" };

struct IntensityRules
{
    const char* name;
    double eqStrength, eqMaxDb, budgetDb;
};

IntensityRules rulesFor (Intensity i) noexcept
{
    switch (i)
    {
        case Intensity::gentle:   return { "Gentle",   0.35, 1.5, 2.0 };
        case Intensity::balanced: return { "Balanced", 0.5,  3.0, 3.5 };
        case Intensity::punchy:   return { "Punchy",   0.6,  4.0, 5.5 };
    }
    return { "Balanced", 0.5, 3.0, 3.5 };
}

const char* targetName (LoudnessTarget t) noexcept
{
    switch (t)
    {
        case LoudnessTarget::stream: return "Stream \xE2\x88\x92" "14";
        case LoudnessTarget::loud:   return "Loud \xE2\x88\x92" "9";
        case LoudnessTarget::club:   return "Club \xE2\x88\x92" "7";
    }
    return "";
}

// Numbers as producers read them: one decimal, a real minus sign, optional forced plus.
std::string num (double v, bool plus = false, int digits = 1)
{
    char buf[32];
    std::snprintf (buf, sizeof buf, "%.*f", digits, std::abs (v));
    const bool zero = std::string (buf).find_first_not_of ("0.") == std::string::npos;
    if (v < 0.0 && ! zero) return std::string ("\xE2\x88\x92") + buf;
    if (plus && ! zero)    return std::string ("+") + buf;
    return buf;
}

std::string pct (double v) { char b[16]; std::snprintf (b, sizeof b, "%d %%", static_cast<int> (std::lround (v))); return b; }
std::string hz (double v)  { char b[16]; std::snprintf (b, sizeof b, "%d Hz", static_cast<int> (std::lround (v))); return b; }

// In-place iterative radix-2 FFT.
void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * std::numbers::pi / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k, w *= wl)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

double percentile (std::vector<double> v, double p)
{
    if (v.empty()) return 0.0;
    std::sort (v.begin(), v.end());
    const double pos = std::clamp (p, 0.0, 1.0) * static_cast<double> (v.size() - 1);
    const auto lo = static_cast<size_t> (std::floor (pos));
    const auto hi = std::min (lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (pos - static_cast<double> (lo));
}

double toDb (double power) { return 10.0 * std::log10 (power + 1.0e-30); }

} // namespace

// =================================================================================================
// Analysis
// =================================================================================================

MixAnalysis analyseMix (const AudioClip& clip)
{
    MixAnalysis a;
    const int nch = clip.numChannels(), n = clip.numSamples();
    const double sr = clip.sampleRate;
    if (nch == 0 || n < static_cast<int> (sr * 2.0))
        return a;

    a.loudness = measure (clip);
    if (! std::isfinite (a.loudness.integratedLufs) || a.loudness.integratedLufs < -60.0)
        return a;
    a.peakToLoudnessDb = a.loudness.truePeakDbtp - a.loudness.integratedLufs;

    // --- Tonal balance: averaged power spectrum of the loud parts ---------------------------------
    // ~5.9 Hz resolution at any sample rate (8192 points at 48 kHz, 32768 at 192 kHz), so the sub
    // band always spans several bins.
    int N = 8192;
    while (N < 65536 && sr / N > 6.0) N *= 2;
    const int hop = N / 2;
    std::vector<double> window (static_cast<size_t> (N));
    for (int i = 0; i < N; ++i)
        window[static_cast<size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / N);

    std::vector<std::array<double, kEqBands>> framePower;
    std::vector<double> frameDb;
    std::vector<std::complex<double>> buf (static_cast<size_t> (N));
    for (int start = 0; start + N <= n; start += hop)
    {
        std::array<double, kEqBands> bands {};
        std::array<int, kEqBands> counts {};
        double total = 0.0;
        for (int c = 0; c < nch; ++c)
        {
            const auto& ch = clip.channels[static_cast<size_t> (c)];
            for (int i = 0; i < N; ++i)
                buf[static_cast<size_t> (i)] = ch[static_cast<size_t> (start + i)] * window[static_cast<size_t> (i)];
            fft (buf);
            for (int k = 2; k < N / 2; ++k)   // skip DC and its window leakage
            {
                const double f = k * sr / N;
                const double p = std::norm (buf[static_cast<size_t> (k)]);
                total += p;
                for (int b = 0; b < kEqBands; ++b)
                    if (f >= kEdges[static_cast<size_t> (b)] && f < kEdges[static_cast<size_t> (b + 1)])
                    {
                        bands[static_cast<size_t> (b)] += p;
                        if (c == 0) ++counts[static_cast<size_t> (b)];
                        break;
                    }
            }
        }
        for (int b = 0; b < kEqBands; ++b)
            if (counts[static_cast<size_t> (b)] > 0)
                bands[static_cast<size_t> (b)] /= counts[static_cast<size_t> (b)];   // mean power per bin (per Hz)
        framePower.push_back (bands);
        frameDb.push_back (toDb (total));
    }
    if (framePower.empty())
        return a;

    const double loudGate = percentile (frameDb, 0.95) - 15.0;
    std::array<double, kEqBands> mean {};
    int loudFrames = 0;
    for (size_t f = 0; f < framePower.size(); ++f)
        if (frameDb[f] >= loudGate)
        {
            for (int b = 0; b < kEqBands; ++b)
                mean[static_cast<size_t> (b)] += framePower[f][static_cast<size_t> (b)];
            ++loudFrames;
        }
    a.seconds = loudFrames * hop / sr;
    for (int b = 0; b < kEqBands; ++b)
    {
        const double fc = std::sqrt (kEdges[static_cast<size_t> (b)] * std::min (kEdges[static_cast<size_t> (b + 1)], 0.5 * sr));
        a.bandDb[static_cast<size_t> (b)] = toDb (mean[static_cast<size_t> (b)] / std::max (1, loudFrames))
                                            + 4.5 * std::log2 (fc / 1000.0);
    }
    const double ref = (a.bandDb[1] + a.bandDb[2] + a.bandDb[3] + a.bandDb[4] + a.bandDb[5]) / 5.0;
    for (auto& v : a.bandDb)
        v -= ref;

    // --- Low end: width, level jumps and punch, in 100 ms blocks of the loud parts -------------------
    std::array<Biquad, 2> lpM {}, lpS {}, lpLow {};
    for (auto& f : lpM) design::apply (f, design::butterworth (false, kBassWidthHz, sr));
    for (auto& f : lpS) design::apply (f, design::butterworth (false, kBassWidthHz, sr));
    for (auto& f : lpLow) design::apply (f, design::butterworth (false, kBassDetectHz, sr));

    const int block = static_cast<int> (sr * 0.1);
    struct Block { double full, mid, side, low, lowPeak; };
    std::vector<Block> blocks;
    Block cur {};
    for (int i = 0; i < n; ++i)
    {
        const double l = clip.channels[0][static_cast<size_t> (i)];
        const double r = nch > 1 ? clip.channels[1][static_cast<size_t> (i)] : l;
        const double m = lpM[1].process (lpM[0].process (0.5 * (l + r)));
        const double s = lpS[1].process (lpS[0].process (0.5 * (l - r)));
        const double lo = lpLow[1].process (lpLow[0].process (0.5 * (l + r)));
        cur.full += 0.5 * (l * l + r * r);
        cur.mid += m * m;
        cur.side += s * s;
        cur.low += lo * lo;
        cur.lowPeak = std::max (cur.lowPeak, std::abs (lo));
        if ((i + 1) % block == 0)
        {
            blocks.push_back (cur);
            cur = {};
        }
    }
    std::vector<double> fullDb;
    for (const auto& b : blocks) fullDb.push_back (toDb (b.full / block));
    const double blockGate = percentile (fullDb, 0.95) - 15.0;

    double em = 0.0, es = 0.0;
    std::vector<double> lowDb, crest;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        if (fullDb[i] < blockGate) continue;
        em += blocks[i].mid;
        es += blocks[i].side;
        lowDb.push_back (toDb (blocks[i].low / block));
    }
    if (em + es > 0.0)
        a.bassWidthPercent = std::min (100.0, 200.0 * es / (em + es));

    // 400 ms windows of the low band: how far the loudest bass moments rise above typical ones,
    // and how spiky (kick-led) versus sustained (808-led) the low end is.
    std::vector<double> lowWin;
    for (size_t i = 0; i + 4 <= blocks.size(); i += 2)
    {
        double e = 0.0, pk = 0.0, full = 0.0;
        for (size_t k = i; k < i + 4; ++k) { e += blocks[k].low; pk = std::max (pk, blocks[k].lowPeak); full += blocks[k].full; }
        if (toDb (full / (4.0 * block)) < blockGate) continue;
        const double rmsDb = toDb (e / (4.0 * block));
        lowWin.push_back (rmsDb);
        if (pk > 1.0e-5)
            crest.push_back (20.0 * std::log10 (pk) - rmsDb);
    }
    if (! lowWin.empty())
    {
        const double loudest = percentile (lowWin, 1.0);
        std::vector<double> present;
        for (double v : lowWin) if (v > loudest - 40.0) present.push_back (v);
        a.lowJumpDb = percentile (present, 0.95) - percentile (present, 0.6);
        a.hasLowEnd = ! crest.empty() && loudest > -60.0;
        a.lowCrestDb = crest.empty() ? 0.0 : percentile (crest, 0.5);
    }

    a.usable = a.seconds >= 3.0;
    return a;
}

// =================================================================================================
// Decisions
// =================================================================================================

AutoMasterResult autoMaster (const AudioClip& clip, const AutoMasterSettings& s)
{
    AutoMasterResult r;
    r.targetLufs = targetLufs (s.target);
    r.analysis = analyseMix (clip);
    const auto& a = r.analysis;
    const auto rules = rulesFor (s.intensity);
    const std::string genre = genreName (s.genre);

    if (! a.usable)
    {
        r.summary = "I couldn't hear enough music to master. Press play on the loudest part of your song "
                    "(the chorus or the drop), then press Auto-Master and let it listen for the whole count.";
        return r;
    }

    auto reason = [&] (const char* module, std::string control, std::string value, std::string why)
    {
        r.reasons.push_back ({ module, std::move (control), std::move (value), std::move (why) });
    };
    auto& chain = r.chain;

    // --- Tonal EQ: move part of the way toward the genre's typical balance --------------------------
    const auto target = genreTargetCurve (s.genre);
    const double tRef = (target[1] + target[2] + target[3] + target[4] + target[5]) / 5.0;
    std::vector<std::string> flat;
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto i = static_cast<size_t> (b);
        const double diff = std::clamp ((target[i] - tRef) - a.bandDb[i], -30.0, 30.0);   // + = your mix has less
        const bool outer = b == 0 || b == kEqBands - 1;
        const double deadband = outer ? 3.0 : 1.5;                     // the outer bands vary most between songs
        const double beyond = std::copysign (std::max (0.0, std::abs (diff) - deadband), diff);
        double gain = std::clamp (rules.eqStrength * beyond, -rules.eqMaxDb, rules.eqMaxDb);
        if (b == 0 && gain > 0.0 && a.bandDb[0] < -18.0)
            gain = std::min (gain, 1.0);                                // don't pump up sub that isn't there
        gain = std::round (gain * 10.0) / 10.0;
        if (std::abs (gain) < 0.4)
            gain = 0.0;
        chain.eq.gainDb[i] = gain;

        if (gain == 0.0)
        {
            flat.push_back (kBandNames[b]);
            continue;
        }
        const std::string more = diff > 0 ? "less" : "more";
        reason ("eq", kBandNames[b], num (gain, true) + " dB",
                "Your mix has about " + num (std::abs (diff)) + " dB " + more + " " + kBandPlain[b] + " than typical " + genre +
                " masters, so a " + num (std::abs (gain)) + " dB " + (gain > 0 ? "boost" : "cut") +
                " closes part of the gap. " + (gain > 0 ? kBoostEffect[b] : kCutEffect[b]));
        if (std::abs (diff) > 7.0)
            r.notes.push_back ("FIX IT IN THE MIX: your mix is " + num (std::abs (diff)) + " dB " + (diff > 0 ? "lower" : "higher") +
                               " in the " + kBandPlain[b] + " than typical " + genre + " masters. The master EQ only fixes part of that; "
                               "adjusting the main source in the mix (" + kCulprit[b] + ") will sound better.");
    }
    if (! flat.empty() && flat.size() < static_cast<size_t> (kEqBands))
    {
        std::string list;
        for (size_t k = 0; k < flat.size(); ++k)
            list += (k ? (k + 1 == flat.size() ? " and " : ", ") : "") + flat[k];
        reason ("eq", "Other bands", "0 dB", list + (flat.size() > 1 ? " are" : " is") + " already close to the " + genre +
                " balance, so " + (flat.size() > 1 ? "they stay" : "it stays") + " flat.");
    }
    else if (flat.size() == static_cast<size_t> (kEqBands))
    {
        reason ("eq", "All bands", "0 dB", "Your tonal balance is already close to typical " + genre +
                " masters, so the EQ stays flat. Nothing to fix is a good sign for your mix.");
    }

    // --- Bass Control ----------------------------------------------------------------------------
    const bool bassHeavy = s.genre == Genre::trapHipHop || s.genre == Genre::rnb || s.genre == Genre::edm || s.genre == Genre::pop;
    const double width = a.bassWidthPercent;
    if (bassHeavy || width >= 20.0)
    {
        chain.bass.monoHz = s.genre == Genre::edm ? 120.0 : bassHeavy ? 100.0 : 80.0;
        reason ("bass", "Mono Below", hz (chain.bass.monoHz),
                width >= 15.0
                  ? "Your bass is " + pct (width) + " stereo below 150 Hz. Club systems and phones play bass in mono, so stereo bass "
                    "partly cancels there. Making it mono below " + hz (chain.bass.monoHz) + " keeps it solid everywhere."
                  : "Your bass is already nearly mono (" + pct (width) + "), so this is a safety net: it changes almost nothing now, "
                    "but keeps any stray stereo sub from cancelling on club systems.");
    }
    else
    {
        reason ("bass", "Mono Below", "Off", "Your bass is already mono (" + pct (width) + " stereo), so there's nothing to fix.");
    }

    double tame = a.lowJumpDb >= 8.0 ? 60.0 : a.lowJumpDb >= 6.0 ? 40.0 : a.lowJumpDb >= 4.5 ? 20.0 : 0.0;
    tame *= s.intensity == Intensity::gentle ? 0.5 : s.intensity == Intensity::punchy ? 1.25 : 1.0;
    tame = std::min (80.0, std::round (tame));
    chain.bass.tame = tame / 100.0;
    reason ("bass", "Sub Tame", pct (tame),
            tame > 0.0 ? "Your loudest bass moments rise about " + num (a.lowJumpDb) + " dB above the typical bass level, so some 808 notes "
                         "leap out. Sub Tame evens those notes out, so the Maximizer doesn't have to turn the whole mix down for them."
                       : "Your bass level is steady (the loudest moments rise only " + num (a.lowJumpDb) + " dB above the rest), so it isn't needed.");

    double punch = 0.0;
    if (s.intensity != Intensity::gentle && a.hasLowEnd)
    {
        if (bassHeavy) punch = a.lowCrestDb < 9.0 ? 40.0 : a.lowCrestDb < 12.0 ? 25.0 : 0.0;
        else if (s.intensity == Intensity::punchy && a.lowCrestDb < 9.0) punch = 20.0;
        if (s.intensity == Intensity::punchy && punch > 0.0) punch = std::min (70.0, punch + 20.0);
    }
    chain.bass.punch = punch / 100.0;
    reason ("bass", "Punch", pct (punch),
            punch > 0.0 ? "Your low end is " + std::string (a.lowCrestDb < 9.0 ? "mostly sustained (808s)" : "fairly sustained") +
                          ", so kicks can get lost under it. Punch adds thump to the start of each kick without turning the sub up."
                        : s.intensity == Intensity::gentle ? "Gentle keeps the kick as it is."
                        : ! a.hasLowEnd ? "There's very little low end in what I heard, so there are no kicks to punch up."
                                        : "Your kicks already hit clearly above the bass, so no extra punch is needed.");

    // --- Soft Clipper ------------------------------------------------------------------------------
    const double plr = a.peakToLoudnessDb;
    const bool loudTarget = s.target != LoudnessTarget::stream;
    chain.clipper.enabled = s.intensity != Intensity::gentle && (loudTarget || plr > 16.0) && plr >= 9.0;
    if (chain.clipper.enabled)
    {
        const bool punchy = s.intensity == Intensity::punchy;
        double level = s.target == LoudnessTarget::club ? (punchy ? -3.5 : -2.0)
                     : s.target == LoudnessTarget::loud ? (punchy ? -2.0 : -1.0)
                                                        : (punchy ? -1.0 : 0.0);
        chain.clipper.thresholdDb = level;   // relative to the ceiling here; the plug-in stores it as Clip Level
        chain.clipper.softness = 0.5;
        chain.clipper.oversampling = 4;
        reason ("clip", "Soft Clipper", "On", "Your mix has " + num (plr) + " dB between its peaks and its average loudness"
                + std::string (plr > 14.0 ? " (spiky drums)" : "") + ". Shaving the sharpest drum peaks before the Maximizer "
                "gets you loud with less limiting, which keeps the punch.");
        reason ("clip", "Clip Level", num (level, true) + " dB",
                level == 0.0 ? "Clipping starts right at the ceiling: only the drum spikes are touched."
                             : "Clipping starts " + num (-level) + " dB below the ceiling for the " + std::string (targetName (s.target)) +
                               " target; the level comes back up so peaks still use the full headroom.");
        reason ("clip", "Softness", "50 %", "A medium knee: rounds peaks gently, heard as punch rather than distortion.");
        reason ("clip", "Oversampling", "4x", "Keeps the clipping clean (no harsh aliasing) at a sensible CPU cost.");
    }
    else
    {
        reason ("clip", "Soft Clipper", "Off",
                s.intensity == Intensity::gentle ? "Gentle leaves peak control to the Maximizer alone, for the most transparent sound."
                : plr < 9.0 ? "Your mix is already dense (only " + num (plr) + " dB between peaks and average), so clipping would only add distortion."
                            : "For a streaming target the Maximizer alone reaches the level cleanly, so no clipping is needed.");
    }

    // --- Maximizer ---------------------------------------------------------------------------------
    chain.limiter.ceilingDbtp = s.target == LoudnessTarget::club ? -0.5 : -1.0;
    chain.limiter.releaseMs = 120.0;
    chain.limiter.adaptiveRelease = true;
    const double ceilingForClip = chain.limiter.ceilingDbtp;
    if (chain.clipper.enabled)
    {
        const double level = chain.clipper.thresholdDb;
        chain.clipper.thresholdDb = ceilingForClip + level;   // the engine wants the absolute threshold
        chain.clipper.makeupDb = std::max (0.0, -level);
    }

    // Loudness: search on the real chain, then respect the intensity's peak-control budget.
    const AudioClip shaped = renderBass (renderEq (clip, chain.eq), chain.bass);
    MasterChainParams rest = chain;
    rest.eq = TonalEQParams {};
    rest.bass = BassParams {};
    const double inLufs = measure (shaped).integratedLufs;
    auto evaluate = [&] (double drive)
    {
        rest.driveDb = drive;
        return measure (renderMaster (shaped, rest));
    };
    // Loudness given up to peak control: the gain put in (Drive plus the clipper's makeup) minus the
    // loudness that actually came out.
    const double makeup = rest.clipper.enabled ? rest.clipper.makeupDb : 0.0;
    auto costOf = [&] (double drive, const LoudnessStats& out) { return std::max (0.0, drive + makeup - (out.integratedLufs - inLufs)); };

    const auto found = findDriveForTarget (shaped, r.targetLufs, rest);
    double drive = std::max (0.0, found.driveDb);
    LoudnessStats out = drive == found.driveDb ? found.output : evaluate (drive);
    double cost = costOf (drive, out);
    bool budgetLimited = false;
    if (cost > rules.budgetDb)
    {
        double lo = 0.0, hi = drive;
        for (int it = 0; it < 8; ++it)
        {
            const double mid = 0.5 * (lo + hi);
            const auto o = evaluate (mid);
            (costOf (mid, o) > rules.budgetDb ? hi : lo) = mid;
        }
        drive = lo;
        out = evaluate (drive);
        cost = costOf (drive, out);
        budgetLimited = true;
    }
    drive = std::round (drive * 10.0) / 10.0;
    out = evaluate (drive);
    cost = costOf (drive, out);
    chain.driveDb = drive;
    r.output = out;
    r.loudnessCostDb = cost;
    r.reachedTarget = std::abs (out.integratedLufs - r.targetLufs) <= 0.3;

    std::string driveWhy;
    if (found.driveDb < 0.0 && drive == 0.0)
        driveWhy = "Your mix is already louder (" + num (a.loudness.integratedLufs) + " LUFS) than the " + std::string (targetName (s.target)) +
                   " target. Polisher doesn't turn masters down; streaming services will do that for you.";
    else if (r.reachedTarget)
        driveWhy = num (drive, true) + " dB of Drive lands this part at " + num (out.integratedLufs) + " LUFS, your " + targetName (s.target) +
                   " target. Holding the peaks down costs about " + num (cost) + " dB of loudness, within the " + rules.name + " limit of " +
                   num (rules.budgetDb) + " dB, so the drums keep their punch.";
    else if (budgetLimited)
        driveWhy = "Reaching " + num (r.targetLufs) + " LUFS would squash this mix too hard for " + std::string (rules.name) +
                   ", so I stopped at " + num (out.integratedLufs) + " LUFS, where holding the peaks down costs " + num (cost) + " dB (the " +
                   rules.name + " limit). " +
                   (s.intensity == Intensity::punchy ? "To go louder, tame the drum peaks in the mix."
                                                     : "Choose Punchy to go louder, or tame the drum peaks in the mix.");
    else
        driveWhy = "Even the maximum Drive only reaches " + num (out.integratedLufs) + " LUFS on this mix.";
    reason ("max", "Drive", num (drive, true) + " dB", driveWhy);
    reason ("max", "Ceiling", num (chain.limiter.ceilingDbtp, false) + " dBTP",
            s.target == LoudnessTarget::club ? "Club masters go to DJs and playback systems, not streaming encoders, so a little more headroom is used."
                                             : "Leaves room for the MP3/AAC conversion streaming services do, so the streamed version won't clip.");
    reason ("max", "Release", "120 ms adaptive", "Recovers fast after drum hits and slowly on sustained bass, so the master doesn't pump.");

    if (budgetLimited && s.intensity != Intensity::punchy)
        r.notes.push_back ("HEADS UP: this mix has big peaks for its loudness (" + num (plr) + " dB). Controlling the drum peaks in the mix "
                           "(a clipper or limiter on the drum bus) would let the master get louder without losing punch.");

    // --- Summary -------------------------------------------------------------------------------------
    char head[512];
    std::snprintf (head, sizeof head, "Listened to %.0f s of the loudest part. ", a.seconds);
    r.summary = head + std::string ("Your mix measured ") + num (a.loudness.integratedLufs) + " LUFS with peaks at " +
                num (a.loudness.truePeakDbtp) + " dBTP. Settings: " + genre + ", " + rules.name + ", " + targetName (s.target) +
                ". The master now measures " + num (out.integratedLufs) + " LUFS with true peak " + num (out.truePeakDbtp) +
                " dBTP. The full song will read about 1 LU lower than this loud section, which is normal.";
    r.ok = true;
    return r;
}

} // namespace polisher

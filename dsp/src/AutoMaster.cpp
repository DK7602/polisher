#include "polisher/AutoMaster.h"
#include "polisher/Denormals.h"
#include "polisher/FilterDesign.h"
#include "polisher/MixHandoff.h"
#include "polisher/SongListener.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
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

double genreVocalLevelDb (Genre g) noexcept
{
    switch (g)
    {
        case Genre::trapHipHop: return -3.0;
        case Genre::rnb:        return -3.0;
        case Genre::pop:        return -4.5;
        case Genre::edm:        return -7.0;
        case Genre::rock:       return -6.0;
        case Genre::acoustic:   return -4.0;
    }
    return -4.5;
}

double suggestVocalMoveDb (double level, Genre g) noexcept
{
    if (level < -15.0)
        return 0.0;                                   // hardly any vocal: an instrumental
    const double target = genreVocalLevelDb (g);
    double move = 0.0;
    if (level < target - 1.5)      move = std::min (3.0, target - level);
    else if (level > target + 2.5) move = -std::min (3.0, level - (target + 1.0));
    return std::round (move * 2.0) / 2.0;
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
    "More sub weight under the kick and bass.",
    "More body in the kick and bass.",
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

// How far one band may rise above a genre's balance before the audio reads as a single instrument.
// Finished mixes stay within about 6 dB of it; a lone synth or vocal measures 20 dB and more.
constexpr double kNotAMixExcessDb = 14.0;

// When a mix is louder than the target, Input Trim turns it this much further down than the bare
// minimum, so Drive has a little to do and the target is still reached after the modules re-adapt.
constexpr double kTrimMarginDb = 0.5;
// How close to the target counts as landing on it.
constexpr double kTargetToleranceLu = 0.3;
// The Unlimiter is only switched on when, after the Maximizer has made the song as loud as the
// target, its hits still jump out at least this much further than without it. Below about 1 dB you
// can't hear the difference, and the Maximizer just works harder.
constexpr double kMinPunchKeptDb = 1.0;

struct IntensityRules
{
    const char* name;
    double eqStrength, eqMaxDb, budgetDb, peakBudgetDb;   // peakBudget: how deep recurring peaks may be shaved
};

IntensityRules rulesFor (Intensity i) noexcept
{
    switch (i)
    {
        case Intensity::gentle:   return { "Gentle",   0.35, 1.5, 2.0, 3.0 };
        case Intensity::balanced: return { "Balanced", 0.5,  3.0, 3.5, 6.0 };
        case Intensity::punchy:   return { "Punchy",   0.6,  4.0, 5.5, 9.0 };
    }
    return { "Balanced", 0.5, 3.0, 3.5, 6.0 };
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

/** The level of the peaks that keep coming back, in dBFS: the 10 ms peaks of the loud parts, the mean of the
    loudest 2.5 % of them (about the 99th percentile, but not one single peak's level), on two 10 ms grids half a
    block apart, averaged. One stray spike can be clipped hard unheard; what's audible is how deep the
    recurring peaks are shaved. Steadier against where the clip starts (the plug-in's 12 s never start on the
    same sample twice): on one chorus heard from 13 different starts, the 99th percentile on one grid moved by
    0.65 dB and swung the Clip Level by 1 dB; this moves by 0.37 dB, the Clip Level by 0.4 dB (re-audit
    2026-10, trap-like song with its stored tone). */
double recurringPeakDb (const AudioClip& clip)
{
    const int n = clip.numSamples(), block = std::max (2, static_cast<int> (clip.sampleRate * 0.01));
    auto onGrid = [&] (int offset)
    {
        std::vector<double> peaks, energy;
        for (int start = offset; start + block <= n; start += block)
        {
            double pk = 0.0, e = 0.0;
            for (const auto& ch : clip.channels)
                for (int i = start; i < start + block; ++i)
                {
                    const double v = ch[static_cast<size_t> (i)];
                    pk = std::max (pk, std::abs (v));
                    e += v * v;
                }
            peaks.push_back (20.0 * std::log10 (pk + 1.0e-12));
            energy.push_back (toDb (e / block));
        }
        if (peaks.empty()) return -100.0;
        const double gate = percentile (energy, 0.95) - 20.0;
        std::vector<double> loud;
        for (size_t i = 0; i < peaks.size(); ++i)
            if (energy[i] >= gate) loud.push_back (peaks[i]);
        if (loud.empty()) loud = peaks;
        std::sort (loud.begin(), loud.end(), std::greater<double>());
        const size_t top = std::max<size_t> (1, static_cast<size_t> (std::lround (0.025 * static_cast<double> (loud.size()))));
        return std::accumulate (loud.begin(), loud.begin() + static_cast<std::ptrdiff_t> (top), 0.0) / static_cast<double> (top);
    };
    if (n < block) return -100.0;
    return 0.5 * (onGrid (0) + onGrid (block / 2));
}

/** How far the hits jump out of what plays just before them (dB): the level of each 5 ms against
    the average of the 20 ... 100 ms before it, 99th percentile over the clip. A limiter shrinks it;
    the Unlimiter grows it back. */
double hitJumpDb (const AudioClip& clip)
{
    const int n = clip.numSamples(), frame = std::max (1, static_cast<int> (clip.sampleRate * 0.005));
    std::vector<double> level, jumps;
    for (int start = 0; start + frame <= n; start += frame)
    {
        double e = 0.0;
        for (const auto& ch : clip.channels)
            for (int i = start; i < start + frame; ++i)
                e += static_cast<double> (ch[static_cast<size_t> (i)]) * ch[static_cast<size_t> (i)];
        level.push_back (e / frame);
    }
    for (size_t i = 20; i < level.size(); ++i)
    {
        double before = 0.0;
        for (size_t k = i - 20; k < i - 4; ++k) before += level[k];
        jumps.push_back (toDb (level[i]) - toDb (before / 16.0));
    }
    return percentile (jumps, 0.99);
}

/** hitJumpDb after the Maximizer has made the clip targetLufs loud, as the master will be. */
double hitJumpAtTargetDb (const AudioClip& clip, double targetLufs, double ceilingDbtp)
{
    LimiterParams lp;
    lp.ceilingDbtp = ceilingDbtp;
    lp.releaseMs = 120.0;
    lp.adaptiveRelease = true;
    // Start well below the target, so Drive does all the work whatever level the clip arrives at.
    const AudioClip quiet = withGain (clip, targetLufs - 12.0 - measure (clip).integratedLufs);
    lp.driveDb = findDriveForTarget (quiet, targetLufs, lp).driveDb;
    return hitJumpDb (renderLimited (quiet, lp));
}

} // namespace

// =================================================================================================
// Song styles: the suite's 75 styles move their genre's target curve
// =================================================================================================

namespace {

/** Mixology's style curve without its common parts (Analysis::targetDb / the Tone page): bass weight around
    55 Hz, the low-mid scoop around 350 Hz, the top sheen rising from 4 kHz. */
double mixologyShapeDb (double bass, double scoop, double air, double hz) noexcept
{
    const double lf = std::log2 (hz / 1000.0);
    return bass * std::exp (-std::pow (std::log2 (hz / 55.0), 2.0) / 2.2)
         - scoop * std::exp (-std::pow (std::log2 (hz / 350.0), 2.0) / 1.2)
         + air * std::clamp ((lf - 2.0) / 3.0, 0.0, 1.0);
}

/** That curve averaged over each analysis band (log-spaced points). */
std::array<double, kEqBands> mixologyBandsDb (double bass, double scoop, double air) noexcept
{
    std::array<double, kEqBands> out {};
    constexpr int points = 32;
    for (size_t b = 0; b < out.size(); ++b)
    {
        const double lo = std::log2 (kEdges[b]), hi = std::log2 (kEdges[b + 1]);
        double sum = 0.0;
        for (int j = 0; j < points; ++j)
            sum += mixologyShapeDb (bass, scoop, air, std::exp2 (lo + (hi - lo) * (j + 0.5) / points));
        out[b] = sum / points;
    }
    return out;
}

} // namespace

std::array<double, kEqBands> styleToneOffsetDb (Genre g, const std::string& styleKey) noexcept
{
    std::array<double, kEqBands> off {};
    const auto* style = handoff::mixStyle (styleKey);
    if (style == nullptr || style->genre != static_cast<int> (g))
        return off;
    // The genre's curve stands for its styles on average.
    double bass = 0.0, scoop = 0.0, air = 0.0;
    int n = 0;
    for (const auto& st : handoff::kMixStyles)
        if (st.genre == style->genre) { bass += st.bass; scoop += st.scoop; air += st.air; ++n; }
    const auto mine = mixologyBandsDb (style->bass, style->scoop, style->air);
    const auto usual = mixologyBandsDb (bass / n, scoop / n, air / n);
    double ref = 0.0;
    for (size_t b = 0; b < off.size(); ++b)
    {
        off[b] = mine[b] - usual[b];
        if (b >= 1 && b <= 5) ref += off[b] / 5.0;
    }
    for (size_t b = 0; b < off.size(); ++b)
        off[b] = std::clamp (off[b] - ref, -kMaxStyleOffsetDb[b], kMaxStyleOffsetDb[b]);
    return off;
}

std::array<double, kEqBands> styleTargetCurve (Genre g, const std::string& styleKey) noexcept
{
    auto curve = genreTargetCurve (g);
    const auto off = styleToneOffsetDb (g, styleKey);
    for (size_t b = 0; b < curve.size(); ++b) curve[b] += off[b];
    return curve;
}

// =================================================================================================
// Analysis
// =================================================================================================

MixAnalysis analyseMix (const AudioClip& clip)
{
    throwIfCancelled();
    const ScopedFlushDenormals noDenormals;
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
    std::vector<std::array<double, 2>> frameLow;   // energy at 30-60 Hz and 60-120 Hz (the low end's shape)
    std::vector<double> frameDb;
    // The spectrum folded into BandSpectrum points per frame: power, and power x frequency.
    using Folded = std::array<std::array<double, BandSpectrum::kPoints>, kEqBands>;
    std::vector<std::pair<Folded, Folded>> frameFolded;
    std::vector<std::complex<double>> buf (static_cast<size_t> (N));
    // Each bin's point within its band (log-spaced), worked out once.
    std::vector<int> binPoint (static_cast<size_t> (N / 2), 0);
    for (int k = 2; k < N / 2; ++k)
    {
        const double f = k * sr / N;
        for (size_t b = 0; b < static_cast<size_t> (kEqBands); ++b)
            if (f >= kEdges[b] && f < kEdges[b + 1])
            {
                binPoint[static_cast<size_t> (k)] = std::clamp (static_cast<int> (BandSpectrum::kPoints * std::log (f / kEdges[b]) / std::log (kEdges[b + 1] / kEdges[b])),
                                                                0, BandSpectrum::kPoints - 1);
                break;
            }
    }
    for (int start = 0; start + N <= n; start += hop)
    {
        std::array<double, kEqBands> bands {};
        std::array<int, kEqBands> counts {};
        std::array<double, 2> low {};
        Folded foldP {}, foldF {};
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
                if (f >= 30.0 && f < 120.0)
                    low[f < 60.0 ? 0 : 1] += p;
                for (int b = 0; b < kEqBands; ++b)
                    if (f >= kEdges[static_cast<size_t> (b)] && f < kEdges[static_cast<size_t> (b + 1)])
                    {
                        const auto bi = static_cast<size_t> (b);
                        bands[bi] += p;
                        if (c == 0) ++counts[bi];
                        const auto point = static_cast<size_t> (binPoint[static_cast<size_t> (k)]);
                        foldP[bi][point] += p;
                        foldF[bi][point] += p * f;
                        break;
                    }
            }
        }
        for (int b = 0; b < kEqBands; ++b)
            if (counts[static_cast<size_t> (b)] > 0)
                bands[static_cast<size_t> (b)] /= counts[static_cast<size_t> (b)];   // mean power per bin (per Hz)
        framePower.push_back (bands);
        frameLow.push_back (low);
        frameDb.push_back (toDb (total));
        frameFolded.emplace_back (foldP, foldF);
    }
    if (framePower.empty())
        return a;

    const double loudGate = percentile (frameDb, 0.95) - 15.0;
    std::array<double, kEqBands> mean {};
    std::array<double, 2> lowSum {};
    int loudFrames = 0;
    Folded sumF {};
    for (size_t f = 0; f < framePower.size(); ++f)
        if (frameDb[f] >= loudGate)
        {
            for (int b = 0; b < kEqBands; ++b)
                mean[static_cast<size_t> (b)] += framePower[f][static_cast<size_t> (b)];
            lowSum[0] += frameLow[f][0];
            lowSum[1] += frameLow[f][1];
            for (size_t b = 0; b < static_cast<size_t> (kEqBands); ++b)
                for (size_t k = 0; k < static_cast<size_t> (BandSpectrum::kPoints); ++k)
                {
                    a.spectrum.power[b][k] += frameFolded[f].first[b][k];
                    sumF[b][k] += frameFolded[f].second[b][k];
                }
            ++loudFrames;
        }
    for (size_t b = 0; b < static_cast<size_t> (kEqBands); ++b)
        for (size_t k = 0; k < static_cast<size_t> (BandSpectrum::kPoints); ++k)
            a.spectrum.hz[b][k] = a.spectrum.power[b][k] > 0.0 ? sumF[b][k] / a.spectrum.power[b][k] : 0.0;
    a.spectrum.valid = loudFrames > 0;
    // Both halves are an octave wide, so this compares the deep sub with the low bass note by note
    // the way you hear them (an 808 that reaches lower reads higher).
    if (lowSum[0] > 0.0 && lowSum[1] > 0.0)
        a.subVsBassDb = toDb (lowSum[0]) - toDb (lowSum[1]);
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
    // Width of the mids (250 Hz - 5 kHz) and highs (above 5 kHz), the Stereo Imager's bands.
    std::array<Biquad, 4> midM {}, midS {};
    std::array<Biquad, 2> highM {}, highS {};
    for (auto* f : { &midM, &midS })
    {
        design::apply ((*f)[0], design::butterworth (true, 250.0, sr));
        design::apply ((*f)[1], design::butterworth (true, 250.0, sr));
        design::apply ((*f)[2], design::butterworth (false, 5000.0, sr));
        design::apply ((*f)[3], design::butterworth (false, 5000.0, sr));
    }
    for (auto* f : { &highM, &highS })
        for (auto& q : *f) design::apply (q, design::butterworth (true, 5000.0, sr));

    const int block = static_cast<int> (sr * 0.1);
    struct Block { double full, mid, side, low, lowPeak, mM, mS, hM, hS, lr, ll, rr; };
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
        {
            double vm = 0.5 * (l + r), vs = 0.5 * (l - r), hm = vm, hs = vs;
            for (auto& f : midM) vm = f.process (vm);
            for (auto& f : midS) vs = f.process (vs);
            for (auto& f : highM) hm = f.process (hm);
            for (auto& f : highS) hs = f.process (hs);
            cur.mM += vm * vm; cur.mS += vs * vs; cur.hM += hm * hm; cur.hS += hs * hs;
            cur.lr += l * r; cur.ll += l * l; cur.rr += r * r;
        }
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

    double em = 0.0, es = 0.0, wmM = 0.0, wmS = 0.0, whM = 0.0, whS = 0.0, clr = 0.0, cll = 0.0, crr = 0.0;
    std::vector<double> lowDb, crest;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        if (fullDb[i] < blockGate) continue;
        em += blocks[i].mid;
        wmM += blocks[i].mM; wmS += blocks[i].mS; whM += blocks[i].hM; whS += blocks[i].hS;
        clr += blocks[i].lr; cll += blocks[i].ll; crr += blocks[i].rr;
        es += blocks[i].side;
        lowDb.push_back (toDb (blocks[i].low / block));
    }
    if (wmM > 0.0) a.midSideDb = toDb (wmS / wmM);
    if (whM > 0.0) a.highSideDb = toDb (whS / whM);
    if (cll > 0.0 && crr > 0.0) a.correlation = clr / std::sqrt (cll * crr);
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
        // Only windows where the bass is actually playing (within 20 dB of its loudest), so bass that
        // simply comes and goes isn't mistaken for notes that leap out.
        for (double v : lowWin) if (v > loudest - 20.0) present.push_back (v);
        a.lowJumpDb = percentile (present, 0.9) - percentile (present, 0.5);
        a.hasLowEnd = ! crest.empty() && loudest > -60.0;
        a.lowCrestDb = crest.empty() ? 0.0 : percentile (crest, 0.5);
    }

    a.usable = a.seconds >= 3.0;
    return a;
}

// =================================================================================================
// Reference songs ("Match ref")
// =================================================================================================

void LoudestPartFinder::prepare (double sr, int numChannels)
{
    sampleRate = sr;
    channelCount = std::clamp (numChannels, 1, 2);
    hopLength = std::max (1, static_cast<int> (std::lround (sr * 0.1)));
    hopPos = 0;
    samples = 0;
    hopSum = 0.0;
    for (auto& k : weighting) { k.prepare (sr); k.reset(); }
    hopEnergy.clear();
    meter.prepare (sr, channelCount);
    meter.reset();
}

void LoudestPartFinder::process (const float* const* channels, int numChannels, int numSamples)
{
    const int nch = std::min (numChannels, channelCount);
    if (nch <= 0 || numSamples <= 0)
        return;
    meter.process (channels, nch, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        for (int c = 0; c < nch; ++c)
        {
            const double k = weighting[static_cast<size_t> (c)].process (static_cast<double> (channels[c][i]));
            hopSum += k * k;
        }
        if (++hopPos == hopLength)
        {
            hopEnergy.push_back (hopSum / hopLength);
            hopSum = 0.0;
            hopPos = 0;
        }
    }
    samples += numSamples;
}

std::int64_t LoudestPartFinder::loudestStart (double windowSeconds) const
{
    const auto window = static_cast<size_t> (std::max (1L, std::lround (windowSeconds / 0.1)));
    if (hopEnergy.size() <= window)
        return 0;
    // Sliding sum over the window; strictly greater keeps the earliest of equally loud parts.
    double sum = 0.0;
    for (size_t i = 0; i < window; ++i) sum += hopEnergy[i];
    double best = sum;
    size_t bestAt = 0;
    for (size_t i = window; i < hopEnergy.size(); ++i)
    {
        sum += hopEnergy[i] - hopEnergy[i - window];
        if (sum > best * (1.0 + 1.0e-12)) { best = sum; bestAt = i + 1 - window; }
    }
    return static_cast<std::int64_t> (bestAt) * hopLength;
}

double LoudestPartFinder::loudestLufs (double windowSeconds) const
{
    const auto window = static_cast<size_t> (std::max (1L, std::lround (windowSeconds / 0.1)));
    const size_t first = static_cast<size_t> (loudestStart (windowSeconds) / std::max (1, hopLength));
    const size_t count = std::min (window, hopEnergy.size() - std::min (first, hopEnergy.size()));
    // BS.1770 over the stretch: 400 ms blocks every 100 ms, -70 LUFS absolute and -10 LU relative gates.
    std::vector<double> blocks;
    for (size_t j = first; j + 4 <= first + count; ++j)
    {
        const double z = (hopEnergy[j] + hopEnergy[j + 1] + hopEnergy[j + 2] + hopEnergy[j + 3]) / 4.0;
        if (energyToLufs (z) > -70.0) blocks.push_back (z);
    }
    if (blocks.empty())
        return -100.0;
    double sum = 0.0;
    for (double z : blocks) sum += z;
    const double gate = energyToLufs (sum / static_cast<double> (blocks.size())) - 10.0;
    double gated = 0.0;
    int n = 0;
    for (double z : blocks)
        if (energyToLufs (z) > gate) { gated += z; ++n; }
    return n > 0 ? energyToLufs (gated / n) : -100.0;
}

AudioClip loudestPart (const AudioClip& song, double& startSeconds)
{
    startSeconds = 0.0;
    const int n = song.numSamples();
    if (n == 0)
        return song;
    LoudestPartFinder finder;
    finder.prepare (song.sampleRate, song.numChannels());
    std::vector<const float*> ptrs;
    for (const auto& ch : song.channels) ptrs.push_back (ch.data());
    finder.process (ptrs.data(), song.numChannels(), n);
    const auto start = static_cast<size_t> (finder.loudestStart (kReferencePartSeconds));
    const auto len = std::min (static_cast<size_t> (n) - start, static_cast<size_t> (song.sampleRate * kReferencePartSeconds));
    startSeconds = static_cast<double> (start) / song.sampleRate;
    AudioClip part;
    part.sampleRate = song.sampleRate;
    for (const auto& ch : song.channels)
        part.channels.emplace_back (ch.begin() + static_cast<std::ptrdiff_t> (start), ch.begin() + static_cast<std::ptrdiff_t> (start + len));
    return part;
}

std::vector<double> spectrumDisplayCurve (const AudioClip& clip, int fftSize, int points)
{
    std::vector<double> out (static_cast<size_t> (std::max (0, points)), -120.0);
    const int n = clip.numSamples(), nch = clip.numChannels();
    if (n < fftSize || nch == 0 || points < 2 || fftSize < 16)
        return out;
    // Exactly what PolisherWebEditor::makeSpectrum does with each frame (JUCE's Hann window, magnitude
    // spectrum, full-scale sine = 0 dB), so the curve sits where a live curve of this audio would.
    const double sr = clip.sampleRate, binHz = sr / fftSize, fullScale = fftSize * 0.25;
    const int lastBin = fftSize / 2 - 1;
    std::vector<int> lo (static_cast<size_t> (points)), hi (static_cast<size_t> (points));
    std::vector<double> tilt (static_cast<size_t> (points));
    for (int i = 0; i < points; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, static_cast<double> (i) / (points - 1));
        const auto k = static_cast<size_t> (i);
        lo[k] = std::clamp (static_cast<int> (std::floor (f * std::pow (2.0, -1.0 / 12.0) / binHz)), 1, lastBin);
        hi[k] = std::clamp (static_cast<int> (std::ceil (f * std::pow (2.0, 1.0 / 12.0) / binHz)), lo[k], lastBin);
        tilt[k] = 4.5 * std::log2 (f / 1000.0);
    }
    std::vector<double> window (static_cast<size_t> (fftSize));
    for (int i = 0; i < fftSize; ++i)
        window[static_cast<size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / (fftSize - 1));

    // The display refreshes 30 times a second, rising fast and falling slowly; average what it shows.
    const int hop = std::max (1, static_cast<int> (std::lround (sr / 30.0)));
    std::vector<std::complex<double>> buf (static_cast<size_t> (fftSize));
    std::vector<double> smoothed (static_cast<size_t> (points)), sum (static_cast<size_t> (points));
    int frames = 0;
    for (int start = 0; start + fftSize <= n; start += hop)
    {
        for (int i = 0; i < fftSize; ++i)
        {
            double v = 0.0;
            for (const auto& ch : clip.channels) v += ch[static_cast<size_t> (start + i)];
            buf[static_cast<size_t> (i)] = v / nch * window[static_cast<size_t> (i)];
        }
        fft (buf);
        for (size_t k = 0; k < static_cast<size_t> (points); ++k)
        {
            double power = 0.0;
            for (int b = lo[k]; b <= hi[k]; ++b) power += std::norm (buf[static_cast<size_t> (b)]);
            const double mag = std::sqrt (power / (hi[k] - lo[k] + 1)) / fullScale;
            const double db = std::clamp (20.0 * std::log10 (mag + 1.0e-9) + tilt[k], -120.0, 12.0);
            auto& sm = smoothed[k];
            if (frames == 0)   sm = db;
            else if (db > sm)  sm = 0.55 * db + 0.45 * sm;
            else               sm = 0.88 * sm + 0.12 * db;
            sum[k] += sm;
        }
        ++frames;
    }
    for (size_t k = 0; k < out.size(); ++k)
        out[k] = std::round (sum[k] / frames * 10.0) / 10.0;
    return out;
}

// =================================================================================================
// A genre's typical balance on the spectrum display
// =================================================================================================

BalanceCurve::BalanceCurve (const std::array<double, kEqBands>& bands) noexcept
{
    constexpr auto n = static_cast<size_t> (kEqBands);
    for (size_t b = 0; b < n; ++b)
        x[b] = std::log2 (std::sqrt (kEdges[b] * kEdges[b + 1]));
    y = bands;
    // A curve through the band values doesn't measure them exactly (a band's mean power per Hz leans
    // toward its top end, and the curve bends inside it): move each node by its band's error.
    for (int pass = 0; pass < 40; ++pass)
    {
        updateSlopes();
        double worst = 0.0;
        std::array<double, kEqBands> err {};
        for (int b = 0; b < kEqBands; ++b)
        {
            const auto i = static_cast<size_t> (b);
            err[i] = bands[i] - bandDb (b);
            worst = std::max (worst, std::abs (err[i]));
        }
        if (worst < 1.0e-6)
            break;
        for (size_t b = 0; b < n; ++b)
            y[b] += err[b];
    }
    updateSlopes();
}

void BalanceCurve::updateSlopes() noexcept
{
    // Monotone piecewise cubic (Fritsch-Carlson, with the shape-preserving end slopes of MATLAB's pchip).
    constexpr auto n = static_cast<size_t> (kEqBands);
    std::array<double, n - 1> h {}, d {};
    for (size_t k = 0; k + 1 < n; ++k)
    {
        h[k] = x[k + 1] - x[k];
        d[k] = (y[k + 1] - y[k]) / h[k];
    }
    for (size_t k = 1; k + 1 < n; ++k)
    {
        if (d[k - 1] * d[k] <= 0.0)
            slope[k] = 0.0;
        else
        {
            const double w1 = 2.0 * h[k] + h[k - 1], w2 = h[k] + 2.0 * h[k - 1];
            slope[k] = (w1 + w2) / (w1 / d[k - 1] + w2 / d[k]);
        }
    }
    auto endSlope = [] (double h0, double h1, double d0, double d1)
    {
        double s = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
        if (s * d0 <= 0.0)
            s = 0.0;
        else if (d0 * d1 < 0.0 && std::abs (s) > std::abs (3.0 * d0))
            s = 3.0 * d0;
        return s;
    };
    slope[0] = endSlope (h[0], h[1], d[0], d[1]);
    slope[n - 1] = endSlope (h[n - 2], h[n - 3], d[n - 2], d[n - 3]);
}

double BalanceCurve::db (double hz) const noexcept
{
    if (! (hz > 0.0))
        return -300.0;
    const double t = std::log2 (hz);
    constexpr auto last = static_cast<size_t> (kEqBands - 1);
    double v;
    if (t <= x[0])
        v = y[0] + slope[0] * (t - x[0]);
    else if (t >= x[last])
        v = y[last] + slope[last] * (t - x[last]);
    else
    {
        size_t k = 0;
        while (k + 1 < last && t > x[k + 1]) ++k;
        const double h = x[k + 1] - x[k], s = (t - x[k]) / h;
        const double s2 = s * s, s3 = s2 * s;
        v = (2.0 * s3 - 3.0 * s2 + 1.0) * y[k] + (s3 - 2.0 * s2 + s) * h * slope[k]
          + (-2.0 * s3 + 3.0 * s2) * y[k + 1] + (s3 - s2) * h * slope[k + 1];
    }
    if (hz < 20.0)
        v -= 24.0 * std::log2 (20.0 / hz);   // a master's low cut
    return v;
}

double BalanceCurve::bandDb (int band) const noexcept
{
    // Mean power per Hz over the band (power per Hz = the tilted value minus the tilt), integrated on
    // a log grid, plus the tilt at the band's centre: what analyseMix() reads from a mix's bins.
    const auto i = static_cast<size_t> (band);
    const double lo = kEdges[i], hi = kEdges[i + 1];
    constexpr int kSteps = 256;
    double sum = 0.0;
    for (int k = 0; k < kSteps; ++k)
    {
        const double f = lo * std::pow (hi / lo, (k + 0.5) / kSteps);
        sum += std::pow (10.0, (db (f) - 4.5 * std::log2 (f / 1000.0)) / 10.0) * f;
    }
    const double mean = sum * std::log (hi / lo) / kSteps / (hi - lo);
    return 10.0 * std::log10 (mean) + 4.5 * std::log2 (std::sqrt (lo * hi) / 1000.0);
}

std::vector<double> balanceDisplayCurve (const std::array<double, kEqBands>& bandDb, double sampleRate,
                                         double lufs, int fftSize, int points)
{
    std::vector<double> out (static_cast<size_t> (std::max (0, points)), -120.0);
    if (points < 2 || fftSize < 16 || ! (sampleRate >= 8000.0))
        return out;
    const BalanceCurve curve (bandDb);
    const double nyquist = 0.5 * sampleRate, top = std::min (20000.0, nyquist);
    // One channel's power per Hz (one-sided), before scaling to the loudness asked for.
    auto power = [&] (double f) { return f > 0.0 && f < top ? std::pow (10.0, (curve.db (f) - 4.5 * std::log2 (f / 1000.0)) / 10.0) : 0.0; };

    // Scale: two channels carrying this spectrum, K-weighted (BS.1770), measure `lufs`.
    KWeighting k;
    k.prepare (sampleRate);
    double weighted = 0.0;
    constexpr int kLoudSteps = 4000;
    const double fLo = 2.0, span = std::log (top / fLo);
    for (int i = 0; i < kLoudSteps; ++i)
    {
        const double f = fLo * std::exp (span * (i + 0.5) / kLoudSteps);
        weighted += power (f) * k.powerGain (f) * f;
    }
    weighted *= span / kLoudSteps;
    if (! (weighted > 0.0))
        return out;
    const double scale = lufsToEnergy (lufs) / (2.0 * weighted);

    // Each bin's expected power: the spectrum seen through the Hann window's response, |W(d)| =
    // (N/2) |sinc(d) / (1 - d^2)| at d bins away, summed out to +/- 8 bins (the rest is < -90 dB).
    // The mono sum (L + R) / 2 of two equal channels is that channel; negative frequencies mirror.
    constexpr int kSub = 16, kReach = 8 * kSub;
    std::vector<double> kernel (2 * kReach + 1);
    double kernelSum = 0.0;
    for (int j = -kReach; j <= kReach; ++j)
    {
        const double d = static_cast<double> (j) / kSub;
        double g;
        if (j == 0)                     g = 1.0;
        else if (std::abs (j) == kSub)  g = 0.5;
        else                            g = std::sin (std::numbers::pi * d) / (std::numbers::pi * d * (1.0 - d * d));
        kernel[static_cast<size_t> (j + kReach)] = g * g;
        kernelSum += g * g / kSub;
    }
    const double N = fftSize, binHz = sampleRate / N;
    // Normalised so that white noise reads exactly as the window's power sum predicts (3N/8 per bin).
    const double toBin = scale * 0.5 * (N * N / 4.0) * binHz * (1.5 / kernelSum) / kSub;
    const int lastBin = fftSize / 2 - 1;
    std::vector<double> grid (static_cast<size_t> ((lastBin + 9) * kSub + 1));
    for (size_t i = 0; i < grid.size(); ++i)
        grid[i] = power (static_cast<double> (i) * binHz / kSub);
    auto binPower = [&] (int bin)
    {
        double e = 0.0;
        for (int j = -kReach; j <= kReach; ++j)
            e += kernel[static_cast<size_t> (j + kReach)] * grid[static_cast<size_t> (std::abs (bin * kSub + j))];
        return e * toBin;
    };

    // The display's points, exactly as spectrumDisplayCurve() and the plug-in's editor compute them.
    const double fullScale = N * 0.25;
    for (int i = 0; i < points; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, static_cast<double> (i) / (points - 1));
        const int lo = std::clamp (static_cast<int> (std::floor (f * std::pow (2.0, -1.0 / 12.0) / binHz)), 1, lastBin);
        const int hi = std::clamp (static_cast<int> (std::ceil (f * std::pow (2.0, 1.0 / 12.0) / binHz)), lo, lastBin);
        double e = 0.0;
        for (int b = lo; b <= hi; ++b) e += binPower (b);
        const double mag2 = e / (hi - lo + 1) / (fullScale * fullScale);
        const double db = std::clamp (10.0 * std::log10 (mag2 + 1.0e-18) + 4.5 * std::log2 (f / 1000.0), -120.0, 12.0);
        out[static_cast<size_t> (i)] = std::round (db * 10.0) / 10.0;
    }
    return out;
}

std::vector<double> genreDisplayCurve (Genre g, double sampleRate, double lufs, int fftSize, int points)
{
    return balanceDisplayCurve (genreTargetCurve (g), sampleRate, lufs, fftSize, points);
}

ReferenceProfile analyseReference (const AudioClip& part, double trackSeconds, double trackLufs, double startSeconds, double hookLufs)
{
    const ScopedFlushDenormals noDenormals;
    ReferenceProfile p;
    const MixAnalysis a = analyseMix (part);
    if (! a.usable)
        return p;
    p.valid = true;
    p.trackSeconds = trackSeconds;
    p.startSeconds = startSeconds;
    p.partSeconds = static_cast<double> (part.numSamples()) / part.sampleRate;
    p.trackLufs = std::isfinite (trackLufs) ? trackLufs : a.loudness.integratedLufs;
    p.lufs = a.loudness.integratedLufs;
    p.sampleRate = part.sampleRate;
    if (! std::isfinite (hookLufs))
    {
        // Only the part is at hand: its own loudest kHookSeconds.
        LoudestPartFinder finder;
        finder.prepare (part.sampleRate, part.numChannels());
        std::vector<const float*> ptrs;
        for (const auto& ch : part.channels) ptrs.push_back (ch.data());
        finder.process (ptrs.data(), part.numChannels(), part.numSamples());
        hookLufs = finder.loudestLufs (kHookSeconds);
    }
    p.hookLufs = hookLufs > -70.0 ? hookLufs : p.lufs;
    p.truePeakDbtp = a.loudness.truePeakDbtp;
    p.peakToLoudnessDb = a.peakToLoudnessDb;
    p.bandDb = a.bandDb;
    p.bassWidthPercent = a.bassWidthPercent;
    p.midSideDb = a.midSideDb;
    p.highSideDb = a.highSideDb;
    p.correlation = a.correlation;
    p.subVsBassDb = a.subVsBassDb;
    p.displayDb = spectrumDisplayCurve (part);
    return p;
}

ReferenceProfile analyseReference (const AudioClip& song)
{
    LoudestPartFinder finder;
    finder.prepare (song.sampleRate, song.numChannels());
    std::vector<const float*> ptrs;
    for (const auto& ch : song.channels) ptrs.push_back (ch.data());
    finder.process (ptrs.data(), song.numChannels(), song.numSamples());
    double start = 0.0;
    const AudioClip part = loudestPart (song, start);
    return analyseReference (part, finder.seconds(), finder.integratedLufs(), start, finder.loudestLufs (kHookSeconds));
}

double referenceLoudness (const ReferenceProfile& p) noexcept
{
    return p.hookLufs > -99.0 ? p.hookLufs : p.lufs;
}

double displayRateShiftDb (double fileRate, double displayRate) noexcept
{
    return fileRate > 0.0 && displayRate > 0.0 ? 10.0 * std::log10 (displayRate / fileRate) : 0.0;
}

// =================================================================================================
// EQ band interaction
// =================================================================================================

std::array<double, kEqBands> eqBandChangeDb (const TonalEQParams& eq, double sampleRate, const BandSpectrum* spectrum) noexcept
{
    std::array<double, kEqBands> out {};
    if (! eq.enabled || sampleRate <= 0.0)
        return out;
    std::array<BiquadCoefficients, kEqBands> bands;
    std::array<bool, kEqBands> on {};
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto i = static_cast<size_t> (b);
        on[i] = eq.gainDb[i] != 0.0;
        if (on[i])
            bands[i] = TonalEQ::designBand (b, eq.gainDb[i], eq.freqHz[i], sampleRate);
    }
    const bool subsonic = TonalEQ::subsonicEngaged (eq);
    const auto hp = TonalEQ::designSubsonic (sampleRate);
    auto powerGain = [&] (double f)
    {
        double db = 0.0;
        for (int j = 0; j < kEqBands; ++j)
            if (on[static_cast<size_t> (j)])
                db += bands[static_cast<size_t> (j)].magnitudeDb (f, sampleRate);
        if (subsonic)
            db += hp[0].magnitudeDb (f, sampleRate) + hp[1].magnitudeDb (f, sampleRate);
        return std::pow (10.0, db / 10.0);
    };

    constexpr int kPoints = 32;
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto bi = static_cast<size_t> (b);
        const double lo = kEdges[bi];
        const double hi = std::min (kEdges[bi + 1], 0.5 * sampleRate);
        if (hi <= lo)
            continue;
        double sumW = 0.0, sumP = 0.0;
        if (spectrum != nullptr && spectrum->valid)
        {
            // The mix's own spectrum: exactly how analyseMix() would read this band after the EQ.
            for (size_t k = 0; k < static_cast<size_t> (BandSpectrum::kPoints); ++k)
                if (const double w = spectrum->power[bi][k]; w > 0.0)
                {
                    sumW += w;
                    sumP += w * powerGain (spectrum->hz[bi][k]);
                }
        }
        if (sumW <= 0.0)
        {
            // A typical master's spectrum: power per Hz falling 4.5 dB/oct (~ f^-1.5), integrated on a
            // log grid (so each point also carries a factor f).
            for (int k = 0; k < kPoints; ++k)
            {
                const double f = lo * std::pow (hi / lo, (k + 0.5) / kPoints);
                const double w = 1.0 / std::sqrt (f);
                sumW += w;
                sumP += w * powerGain (f);
            }
        }
        out[bi] = 10.0 * std::log10 (sumP / sumW);
    }
    return out;
}

std::array<double, kEqBands> solveEqGains (const std::array<double, kEqBands>& wanted,
                                           const std::array<double, kEqBands>& limitDb, double sampleRate,
                                           const BandSpectrum* spectrum) noexcept
{
    constexpr double kRegularisation = 0.02;   // per dB^2 of gain: prefer the smaller of near-equal fits
    constexpr int kN = kEqBands;
    using Vec = std::array<double, kN>;
    Vec gains {};
    auto changeFor = [&] (const Vec& g)
    {
        TonalEQParams p;
        p.gainDb = g;
        return eqBandChangeDb (p, sampleRate, spectrum);
    };
    auto lowerOf = [&] (size_t b) { return wanted[b] < 0.0 ? -limitDb[b] : 0.0; };
    auto upperOf = [&] (size_t b) { return wanted[b] > 0.0 ?  limitDb[b] : 0.0; };

    // Band-interaction matrix: M[i][j] = change in analysis band i per dB of EQ band j.
    std::array<Vec, kN> M {};
    for (size_t j = 0; j < kN; ++j)
    {
        Vec up {}, down {};
        up[j] = 1.0;
        down[j] = -1.0;
        const auto ru = changeFor (up), rd = changeFor (down);
        for (size_t i = 0; i < kN; ++i)
            M[i][j] = 0.5 * (ru[i] - rd[i]);
    }

    std::array<bool, kN> movable {};
    for (size_t b = 0; b < kN; ++b)
        movable[b] = wanted[b] != 0.0 && limitDb[b] > 0.0;

    // One regularised least-squares step on the free bands, from the current gains toward the wish:
    // minimise |r - M d|^2 + lambda |g + d|^2, with r the remaining error on the real response.
    auto step = [&] (const std::array<bool, kN>& free)
    {
        const auto now = changeFor (gains);
        std::array<size_t, kN> idx {};
        size_t n = 0;
        for (size_t b = 0; b < kN; ++b)
            if (free[b])
                idx[n++] = b;
        Vec delta {};
        if (n == 0)
            return delta;
        std::array<std::array<double, kN + 1>, kN> m {};   // augmented normal equations
        for (size_t r = 0; r < n; ++r)
        {
            for (size_t c = 0; c < n; ++c)
            {
                double v = 0.0;
                for (size_t i = 0; i < kN; ++i)
                    v += M[i][idx[r]] * M[i][idx[c]];
                m[r][c] = v + (r == c ? kRegularisation : 0.0);
            }
            double rhs = -kRegularisation * gains[idx[r]];
            for (size_t i = 0; i < kN; ++i)
                rhs += M[i][idx[r]] * (wanted[i] - now[i]);
            m[r][kN] = rhs;
        }
        // Gaussian elimination with partial pivoting (small, symmetric positive definite).
        for (size_t c = 0; c < n; ++c)
        {
            size_t piv = c;
            for (size_t r = c + 1; r < n; ++r)
                if (std::abs (m[r][c]) > std::abs (m[piv][c]))
                    piv = r;
            std::swap (m[c], m[piv]);
            for (size_t r = c + 1; r < n; ++r)
            {
                const double f = m[r][c] / m[c][c];
                for (size_t k = c; k < n; ++k)
                    m[r][k] -= f * m[c][k];
                m[r][kN] -= f * m[c][kN];
            }
        }
        for (size_t r = n; r-- > 0;)
        {
            double v = m[r][kN];
            for (size_t k = r + 1; k < n; ++k)
                v -= m[r][k] * delta[idx[k]];
            delta[idx[r]] = v / m[r][r];
        }
        return delta;
    };

    // Clamped solve: bands that would cross their bound are pinned there and the rest re-solved;
    // a few outer passes re-linearise on the real (slightly non-linear) response.
    auto solve = [&]
    {
        for (int pass = 0; pass < 4; ++pass)
        {
            auto free = movable;
            for (size_t inner = 0; inner < kN; ++inner)
            {
                const auto delta = step (free);
                bool pinned = false;
                auto next = gains;
                for (size_t b = 0; b < kN; ++b)
                {
                    if (! free[b])
                        continue;
                    next[b] = gains[b] + delta[b];
                    if (next[b] < lowerOf (b) || next[b] > upperOf (b))
                    {
                        gains[b] = std::clamp (next[b], lowerOf (b), upperOf (b));   // pinned at its bound
                        free[b] = false;
                        pinned = true;
                    }
                }
                if (! pinned)
                {
                    gains = next;
                    break;
                }
            }
        }
    };

    for (size_t b = 0; b < kN; ++b)
        if (movable[b])
            gains[b] = std::clamp (wanted[b], lowerOf (b), upperOf (b));
    for (size_t round = 0; round <= kN; ++round)
    {
        solve();
        bool dropped = false;
        for (size_t b = 0; b < kN; ++b)
        {
            gains[b] = std::round (gains[b] * 10.0) / 10.0;
            if (movable[b] && std::abs (gains[b]) < 0.4)
            {
                gains[b] = 0.0;
                movable[b] = false;   // too small to bother: leave it flat and re-solve the rest
                dropped = true;
            }
        }
        if (! dropped)
            break;
    }
    return gains;
}

// =================================================================================================
// Decisions
// =================================================================================================

namespace {

/** One Auto-Master pass on the mix turned down by trimDb at the input (Input Trim, <= 0). wantTrimDb
    returns how much further down the input must go for the master to reach the target at Drive 0
    (0 when no further trim is needed). */
AutoMasterResult autoMasterAt (const AudioClip& raw, const AutoMasterSettings& s, double trimDb, double& wantTrimDb)
{
    const AudioClip input = withGain (raw, trimDb);
    wantTrimDb = 0.0;
    // Stem EQ is the user's call: Auto-Master keeps their moves and hears the mix with them, except
    // for the tone. In the plug-in the Tonal EQ comes before Stem EQ, so it is set for the mix as it
    // reaches it: without the user's Stem moves (Bass +12 must not read as "20 dB more sub than
    // typical" and be EQ'd back down). Everything after Stem EQ is decided on the mix with them.
    const AudioClip heard = renderStemEq (input, s.stem);
    const bool stemMoved = ! StemEQ::isNeutral (s.stem);
    AutoMasterResult r;
    // Match ref: a loaded reference song replaces the genre's tone, the target loudness, the stereo width
    // and the low end. The loudness tier it falls in (Stream / Loud / Club) picks the ceiling and the
    // clipping rules, the same as choosing that target by hand.
    const auto& ref = s.reference;
    const bool useRef = ref.valid;
    // Its loudest kHookSeconds: the master is set from the loudest kHookSeconds of your song, so the two
    // hooks are compared like for like (the loudest 30 s also hold some of the verse around the hook).
    const double refLufs = useRef ? referenceLoudness (ref) : 0.0;
    r.targetLufs = useRef ? std::clamp (std::round (refLufs * 10.0) / 10.0, kRefMinLufs, kRefMaxLufs) : targetLufs (s.target);
    // Stream: streaming services measure the whole song, not its loudest part. When the whole song was heard, the
    // loudest part goes as far above -14 as needed for the whole song to read about -14 (Loud and Club masters are
    // turned down by the services anyway, and a reference is compared hook to hook).
    const double streamLift = ! useRef && s.target == LoudnessTarget::stream && s.songGapLu > 0.0
                                ? std::round (std::min (kMaxStreamLiftLu, kStreamGapShare * s.songGapLu) * 10.0) / 10.0 : 0.0;
    r.targetLufs += streamLift;
    const LoudnessTarget tier = ! useRef ? s.target : r.targetLufs <= -12.0 ? LoudnessTarget::stream
                              : r.targetLufs <= -8.0 ? LoudnessTarget::loud : LoudnessTarget::club;
    const std::string tname = useRef ? "Ref " + num (r.targetLufs) : std::string (targetName (s.target));
    const std::string refName = "\xE2\x80\x9C" + ref.name + "\xE2\x80\x9D";   // in curly quotes
    const MixAnalysis plain = stemMoved ? analyseMix (input) : MixAnalysis {};
    r.analysis = analyseMix (heard);
    if (stemMoved && r.analysis.usable)
    {
        r.analysis.bandDb = plain.bandDb;      // the tone as the Tonal EQ hears it: before Stem EQ
        r.analysis.spectrum = plain.spectrum;
    }
    const auto& a = r.analysis;
    // The mix as it arrives, before the Input Trim and the user's Stem EQ: what the report quotes. A Stem
    // EQ boost (Bass +6 with Keep Loudness off) makes what the chain hears louder than that.
    const LoudnessStats beforeStem = stemMoved ? plain.loudness : a.loudness;
    const double arrivesLufs = beforeStem.integratedLufs - trimDb, arrivesPeak = beforeStem.truePeakDbtp - trimDb;
    const double stemLiftDb = stemMoved ? a.loudness.integratedLufs - beforeStem.integratedLufs : 0.0;
    const bool stemPush = stemLiftDb >= 1.0;
    const std::string arrivesText = "Your mix arrives at " + num (arrivesLufs) + " LUFS" +
        (stemPush ? ", and your Stem EQ moves raise it to about " + num (arrivesLufs + stemLiftDb) + " LUFS before the rest of the chain" : std::string());
    const auto rules = rulesFor (s.intensity);
    const std::string genre = genreName (s.genre);
    // The song's style (shared by the suite) when Auto-Master masters it as this genre: the EQ aims for the style's
    // own tone, and its feel scales the clipping and saturation a little. A style of another genre (you picked a
    // different genre here) is left out.
    const auto* style = handoff::mixStyle (s.styleKey);
    if (style != nullptr && style->genre != static_cast<int> (s.genre))
        style = nullptr;
    const std::string styleName = style != nullptr ? std::string (style->name) : genre;   // what the sound is compared with
    const auto feel = style != nullptr ? style->feel : handoff::Feel::normal;

    if (! a.usable)
    {
        r.summary = "I couldn't hear enough music to master. Press Auto-Master, then play your song from the start "
                    "and let it listen until the song ends.";
        r.tipTitle = "TRY AGAIN";
        r.tip = "Press Auto-Master, then play your song from the start. It counts only music, and needs at least 12 seconds of it.";
        return r;
    }

    // --- One instrument, not a mix -------------------------------------------------------------------
    // A finished mix spreads its energy across the bands; even a muddy or honky one sits within a
    // few dB of a genre's balance. When one band towers far above it, Masterology is almost certainly on
    // a single track (a synth, a vocal, an 808), and mastering that to a genre curve would do harm.
    {
        const auto target = genreTargetCurve (s.genre);
        const double tRef = (target[1] + target[2] + target[3] + target[4] + target[5]) / 5.0;
        int worst = -1;
        double excess = 0.0;
        for (int b = 1; b < kEqBands - 1; ++b)
        {
            const auto i = static_cast<size_t> (b);
            const double over = a.bandDb[i] - (target[i] - tRef);
            if (over > excess) { excess = over; worst = b; }
        }
        if (worst >= 0 && excess >= kNotAMixExcessDb)
        {
            r.notAFullMix = true;
            r.summary = "This doesn't sound like a full mix. Almost all of its energy is in the " + std::string (kBandPlain[worst]) +
                        ", " + (excess > 30.0 ? std::string ("more than 30") : "about " + num (excess)) + " dB above the balance of a finished " + genre + " song. That's what a "
                        "single instrument or vocal sounds like. Mastering one track would pile on EQ it doesn't need, so I haven't "
                        "changed anything.";
            r.tipTitle = "PUT MASTEROLOGY ON THE MASTER";
            r.tip = "Masterology belongs on the channel every track flows into ({daw:master}). In {daw}: {daw:loadOnMaster}, "
                    "as the last plug-in there. Remove it from this track, then press play on the chorus and run Auto-Master again.";
            return r;
        }
    }

    auto reason = [&] (const char* module, std::string control, std::string value, std::string why)
    {
        r.reasons.push_back ({ module, std::move (control), std::move (value), std::move (why) });
    };
    auto& chain = r.chain;

    // --- Unlimiter: only when the audio arrives already squashed, and only if the punch survives ----
    // An unmastered mix keeps its peaks well above the average level (Squash 12.5 dB or more) or at
    // least doesn't press them flat against one ceiling; a beat or mix that was limited or clipped
    // before Masterology has lost that. The Maximizer then has to make the restored hits loud again, and
    // the closer the target is to the loudness the audio arrives at, the more of them it presses
    // straight back down. So the Unlimiter is tried at the target and kept only when the hits still
    // jump out clearly further than without it. Everything after this hears the restored mix.
    // MP3/AAC/Ogg copies of a loud master peak higher than the master itself, and the louder the master,
    // the more. Measured on test masters (trap-like and music-like) at -9 and -7 LUFS: with a -1.0 dBTP
    // ceiling, Ogg q5 copies decoded at up to +0.6 (-9) and +1.1 dBTP (-7); with -1.5, at most -0.2
    // (-9) and +0.4 (-7); with -2.0, -0.5 and +0.1. MP3 320k stayed below 0 at every ceiling. So Loud
    // and Club both get -1.5 (louder never gets a higher ceiling); Spotify's own advice for anything
    // louder than -14 LUFS is -2, which the report mentions.
    const double maxCeiling = tier == LoudnessTarget::stream ? -1.0 : -1.5;
    {
        const double squash = Unlimiter::measureSquashDb (input.channels, input.sampleRate);   // as the plug-in sees it, before Stem EQ
        auto& ul = chain.unlimiter;
        ul = UnlimiterParams {};
        ul.squashDb = squash;   // the strength comes from this reading, in playback and export alike
        const double tryAmount = s.intensity == Intensity::gentle ? 50.0 : s.intensity == Intensity::punchy ? 100.0 : 75.0;
        const bool wall = squash <= Unlimiter::kFullSquashDb + 0.5;
        const std::string arrives = std::string ("Your audio arrives ") + (wall ? "brickwalled" : "already squashed") + " (Squash " + num (squash) +
                                    " dB; an unmastered mix reads 12.5 dB or more), so something limited or clipped it before Masterology. ";
        if (Unlimiter::squashScale (squash) < 0.25)
            reason ("ul", "Amount", "0 %", "Your mix still has its punch (Squash " + num (squash) + " dB, like an unmastered mix: its peaks stand well "
                    "above the average level, or don't press flat against one ceiling the way a limiter leaves them), so there is nothing "
                    "to undo and the Unlimiter stays off.");
        else
        {
            UnlimiterParams on = ul;
            on.amount = tryAmount;
            const double kept = hitJumpAtTargetDb (renderUnlimiter (input, on), r.targetLufs, maxCeiling)
                              - hitJumpAtTargetDb (input, r.targetLufs, maxCeiling);
            if (kept < kMinPunchKeptDb)
            {
                // How the target sits against the level the audio arrives at (+ = it arrives louder).
                const double gap = arrivesLufs - r.targetLufs;
                const std::string tried = "tried at " + pct (tryAmount) + ", the hits would jump out only " + num (std::max (0.0, kept)) +
                                          " dB further than without it, too little to hear, so the Unlimiter stays off.";
                std::string why, why2;
                if (gap >= 3.0)
                {
                    why = arrives + "Even with your " + tname + " target " + num (gap) + " dB below the " + num (arrivesLufs) +
                          " LUFS it arrives at, " + tried;
                    why2 = "the Unlimiter can win back only a little of its punch";
                }
                else
                {
                    why = arrives + "But at your " + tname + " target the Maximizer would press the restored hits straight back down: " + tried;
                    if (gap <= -1.0)
                        why += " Your audio arrives at " + num (arrivesLufs) + " LUFS, quieter than the target, so it has to be pushed louder still.";
                    if (! useRef && s.target != LoudnessTarget::stream && arrivesLufs - targetLufs (LoudnessTarget::stream) >= 3.0)
                        why += " It pays off when the target is a few dB below the " + num (arrivesLufs) + " LUFS your audio arrives at, "
                               "for example Stream \xE2\x88\x92" "14.";
                    why2 = std::string ("it arrives ") + (gap > 1.0 ? "only a little louder than" : gap >= -1.0 ? "about as loud as" : "quieter than") +
                           " your target, so there is little punch left to win back";
                }
                reason ("ul", "Amount", "0 %", why);
                r.notes.push_back ("SQUASHED INPUT: your audio was already limited or clipped before Masterology (Squash " + num (squash) +
                                   " dB), and " + why2 + "."
                                   "\nNEED: Optional. The master works as it is."
                                   "\nSTEP: If it's your own mix, turn off the limiter or clipper on the {daw:master} channel before Masterology (or export the beat without it)."
                                   "\nSTEP: Then run Auto-Master again.");
            }
            else
            {
                ul.amount = tryAmount;
                reason ("ul", "Amount", pct (ul.amount), arrives + "Tried at your " + tname + " target, " +
                        pct (ul.amount) + " makes the hits jump out about " + num (kept) + " dB further than without it, after the Maximizer has made "
                        "the song loud again. Compare at the final loudness with MATCH on: the drums should hit harder while the level "
                        "between them stays the same.");
                r.notes.push_back ("SQUASHED INPUT: your audio was already limited or clipped before Masterology (Squash " + num (squash) +
                                   " dB). The Unlimiter wins back part of the punch, but the cleanest result comes from the unlimited mix."
                                   "\nNEED: Optional. The master works as it is."
                                   "\nSTEP: If it's your own mix, turn off the limiter or clipper on the {daw:master} channel before Masterology (or export the beat without it)."
                                   "\nSTEP: Then set the Unlimiter Amount back to 0 and run Auto-Master again.");
            }
        }
    }
    // Plug-in order: Unlimiter first, then the user's Stem EQ (the Unlimiter is not linear, so the order matters).
    const AudioClip restored = Unlimiter::isNeutral (chain.unlimiter) ? input : renderUnlimiter (input, chain.unlimiter);
    const AudioClip clip = Unlimiter::isNeutral (chain.unlimiter) ? heard : renderStemEq (restored, s.stem);
    if (! Unlimiter::isNeutral (chain.unlimiter))
    {
        // The rest of the chain is set for the restored mix; the report still quotes the loudness of
        // the mix as it arrives (the same whether or not the Unlimiter ends up on).
        const auto arriving = r.analysis.loudness;
        r.analysis = analyseMix (clip);
        r.analysis.loudness = arriving;
        if (stemMoved)   // tone: before Stem EQ, as above
        {
            const auto restoredPlain = analyseMix (restored);
            r.analysis.bandDb = restoredPlain.bandDb;
            r.analysis.spectrum = restoredPlain.spectrum;
        }
    }

    // 808-led genres, where the low end is programmed rather than played.
    const bool bassHeavy = s.genre == Genre::trapHipHop || s.genre == Genre::rnb || s.genre == Genre::edm || s.genre == Genre::pop;

    // --- Tonal EQ: move part of the way toward the genre's typical balance (or the reference's) -------
    // First what each analysis band should change by, then the band gains that achieve that together:
    // neighbouring bands overlap, so setting each band on its own would over- or under-shoot. A
    // reference's balance is measured exactly like the mix's, so it drops in for the genre curve; the
    // same intensity limits apply, so the EQ still only goes part of the way.
    const auto target = useRef ? ref.bandDb : styleTargetCurve (s.genre, s.styleKey);
    // What the report compares the mix with.
    const std::string versus = useRef ? refName : "typical " + styleName + " masters";
    const double tRef = (target[1] + target[2] + target[3] + target[4] + target[5]) / 5.0;
    std::array<double, kEqBands> diffs {}, wanted {}, limits {};
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto i = static_cast<size_t> (b);
        diffs[i] = std::clamp ((target[i] - tRef) - a.bandDb[i], -30.0, 30.0);   // + = your mix has less
        const bool outer = b == 0 || b == kEqBands - 1;
        const double deadband = outer ? 3.0 : 1.5;                     // the outer bands vary most between songs
        const double beyond = std::copysign (std::max (0.0, std::abs (diffs[i]) - deadband), diffs[i]);
        wanted[i] = std::clamp (rules.eqStrength * beyond, -rules.eqMaxDb, rules.eqMaxDb);
        limits[i] = rules.eqMaxDb;
        if (b == 0 && wanted[i] > 0.0 && a.bandDb[0] < -18.0)
            limits[i] = 1.0;                                           // don't pump up sub that isn't there
    }
    // Solved on this mix's own spectrum (the bands' overlap depends on where its energy sits: an 808
    // at 45-60 Hz takes a Low End shelf differently from an evenly falling sub).
    chain.eq.gainDb = solveEqGains (wanted, limits, clip.sampleRate, &a.spectrum);
    const auto moved = eqBandChangeDb (chain.eq, clip.sampleRate, &a.spectrum);   // what the EQ does to each band of this mix

    std::vector<std::string> flat, covered, held;
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto i = static_cast<size_t> (b);
        const double diff = diffs[i], gain = chain.eq.gainDb[i];
        if (gain == 0.0)
        {
            // A wished-for change the neighbours' moves deliver (at least half of it, the right way) is
            // covered; otherwise the band was left flat because moving it would undo more than it fixes.
            if (std::abs (wanted[i]) < 0.4)
                flat.push_back (kBandNames[b]);
            else if (eqBandCovered (wanted[i], moved[i]))
                covered.push_back (kBandNames[b]);
            else
                held.push_back (kBandNames[b]);
            continue;
        }
        const std::string more = diff > 0 ? "less" : "more";
        reason ("eq", kBandNames[b], num (gain, true) + " dB",
                "Your mix has about " + num (std::abs (diff)) + " dB " + more + " " + kBandPlain[b] + " than " + versus +
                ", so a " + num (std::abs (gain)) + " dB " + (gain > 0 ? "boost" : "cut") +
                " closes part of the gap. " + (gain > 0 ? kBoostEffect[b] : kCutEffect[b]));
        if (std::abs (diff) > 7.0)
            r.notes.push_back ("FIX IT IN THE MIX: your mix is " + num (std::abs (diff)) + " dB " + (diff > 0 ? "lower" : "higher") +
                               " in the " + kBandPlain[b] + " than " + versus + ". The master EQ only fixes part of that; "
                               "adjusting the main source in the mix (" + (b <= 1 && ! bassHeavy ? "usually the bass or the kick" : kCulprit[b]) +
                               ") will sound better."
                               "\nNEED: Optional. The master EQ already corrects part of it, and the master works as it is."
                               "\nSTEP: In your mix, solo the main sound in the " + std::string (kBandPlain[b]) + " (" +
                               (b <= 1 && ! bassHeavy ? "usually the bass or the kick" : kCulprit[b]) + ")."
                               "\nSTEP: Turn it " + (diff > 0 ? "up" : "down") + " a few dB, or EQ just that track in this range."
                               "\nSTEP: Run Auto-Master again: its EQ move here will get smaller.");
    }
    if (! covered.empty())
    {
        std::string list;
        for (size_t k = 0; k < covered.size(); ++k)
            list += (k ? (k + 1 == covered.size() ? " and " : ", ") : "") + covered[k];
        const bool many = covered.size() > 1;
        reason ("eq", many ? "Covered bands" : "Covered band", "0 dB",
                list + (many ? " need" : " needs") + " a small change too, but the neighbouring bands' moves already reach into " +
                (many ? "them" : "it") + ", so " + (many ? "they stay" : "it stays") + " flat.");
    }
    if (! held.empty())
    {
        std::string list;
        for (size_t k = 0; k < held.size(); ++k)
            list += (k ? (k + 1 == held.size() ? " and " : ", ") : "") + held[k];
        const bool many = held.size() > 1;
        reason ("eq", many ? "Bands left flat" : "Band left flat", "0 dB",
                list + " could use a small change too, but EQ bands overlap: moving " + (many ? "them" : "it") +
                " would also shift the bands next to " + (many ? "them" : "it") + ", and the closest overall balance leaves " +
                (many ? "them" : "it") + " flat.");
    }
    if (! flat.empty() && flat.size() < static_cast<size_t> (kEqBands))
    {
        std::string list;
        for (size_t k = 0; k < flat.size(); ++k)
            list += (k ? (k + 1 == flat.size() ? " and " : ", ") : "") + flat[k];
        reason ("eq", "Other bands", "0 dB", list + (flat.size() > 1 ? " are" : " is") +  " already close to " + (useRef ? "the reference's" : "the " + styleName) +
                " balance, so " + (flat.size() > 1 ? "they stay" : "it stays") + " flat.");
    }
    else if (flat.size() == static_cast<size_t> (kEqBands))
    {
        reason ("eq", "All bands", "0 dB", "Your tonal balance is already close to " + versus +
                ", so the EQ stays flat. Nothing to fix is a good sign for your mix.");
    }

    // --- Bass Control ----------------------------------------------------------------------------
    const double width = a.bassWidthPercent;
    const std::string refBass = useRef ? " " + refName + "'s bass is " + pct (ref.bassWidthPercent) + " stereo." : std::string();
    if (useRef && ref.bassWidthPercent >= 20.0 && (bassHeavy || width >= 20.0))
    {
        // The reference keeps stereo in its bass: make only the deep sub mono, as it most likely does.
        chain.bass.monoHz = 60.0;
        reason ("bass", "Mono Below", hz (chain.bass.monoHz),
                refName + " keeps some stereo in its bass (" + pct (ref.bassWidthPercent) + " below 150 Hz; yours is " + pct (width) +
                "), so only the deep sub below " + hz (chain.bass.monoHz) + " is made mono. That keeps the sub solid on club systems and phones, "
                "while the bass above it stays as wide as in the reference.");
    }
    else if (bassHeavy || width >= 20.0)
    {
        chain.bass.monoHz = s.genre == Genre::edm ? 120.0 : bassHeavy ? 100.0 : 80.0;
        reason ("bass", "Mono Below", hz (chain.bass.monoHz),
                width >= 15.0
                  ? "Your bass is " + pct (width) + " stereo below 150 Hz. Club systems and phones play bass in mono, so stereo bass "
                    "partly cancels there. Making it mono below " + hz (chain.bass.monoHz) + " keeps it solid everywhere."
                  : "Your bass is already nearly mono (" + pct (width) + "), so this is a safety net: you'll hardly hear it now, "
                    "but keeps any stray stereo sub from cancelling on club systems." + refBass);
    }
    else
    {
        reason ("bass", "Mono Below", "Off", (width < 10.0 ? "Your bass is already mono (" + pct (width) + " stereo), so there's nothing to fix."
                                                         : "Your bass is mostly mono (" + pct (width) + " stereo), which is fine for " + genre + ", so it's left as it is.") + refBass);
    }

    // Sub Tame is an 808 tool: in genres with played bass, level changes are part of the performance.
    double tame = 0.0;
    if (bassHeavy)
    {
        tame = a.lowJumpDb >= 8.0 ? 60.0 : a.lowJumpDb >= 6.0 ? 40.0 : a.lowJumpDb >= 4.5 ? 20.0 : 0.0;
        tame *= s.intensity == Intensity::gentle ? 0.5 : s.intensity == Intensity::punchy ? 1.25 : 1.0;
        tame = std::min (80.0, std::round (tame));
    }
    chain.bass.tame = tame / 100.0;
    reason ("bass", "Sub Tame", pct (tame),
            tame > 0.0 ? "Your loudest bass moments rise about " + num (a.lowJumpDb) + " dB above the typical bass level, so some 808 notes "
                         "leap out. Sub Tame evens those notes out, so the Maximizer doesn't have to turn the whole mix down for them."
            : ! bassHeavy ? "In " + genre + " the bass is played, so its ups and downs are part of the performance. Sub Tame is for 808s that leap out, so it stays off."
                          : "Your bass level is steady (the loudest moments rise only " + num (a.lowJumpDb) + " dB above the rest), so it isn't needed.");

    double punch = 0.0;
    if (s.intensity != Intensity::gentle && a.hasLowEnd)
    {
        if (bassHeavy) punch = a.lowCrestDb < 9.0 ? 40.0 : a.lowCrestDb < 12.0 ? 25.0 : 0.0;
        else if (s.intensity == Intensity::punchy && a.lowCrestDb < 9.0) punch = 20.0;
        if (s.intensity == Intensity::punchy && punch > 0.0 && bassHeavy) punch = std::min (70.0, punch + 20.0);
    }
    chain.bass.punch = punch / 100.0;
    reason ("bass", "Punch", pct (punch),
            punch > 0.0 ? "Your low end is " + std::string (a.lowCrestDb < 9.0 ? (bassHeavy ? "mostly sustained (808s)" : "mostly sustained") : "fairly sustained") +
                          ", so kicks can get lost under it. Punch adds thump to the start of each kick (it spots kicks by their click), so 808 notes and the sub aren't turned up."
                        : s.intensity == Intensity::gentle ? "Gentle keeps the kick as it is."
                        : ! a.hasLowEnd ? "There's very little low end in what I heard, so there are no kicks to punch up."
                                        : "Your kicks already hit clearly above the bass, so no extra punch is needed.");

    // --- Dynamics: gentle per-band glue ----------------------------------------------------------------
    // Each band is held down by a set amount on its loud moments (the threshold is found by running
    // the real compressor on what Auto-Master heard), and what the band loses on average is given
    // back, so the tone stays put while the loud moments get steadier.
    // --- Resonance Tamer and Tone Steady: measured at full strength on the EQ'd mix ----------------------
    // How much they would have to do tells how ringy the mix is and how much its tone swings.
    {
        const AudioClip eqd = renderEq (clip, chain.eq);
        ToneSteadyParams tsProbe; tsProbe.amount = 100.0; tsProbe.rangeDb = 6.0; tsProbe.speed = 1;
        ResonanceParams rtProbe; rtProbe.amount = 100.0;
        const SpectralStats st = measureSpectral (eqd, tsProbe, rtProbe);
        const bool gentle = s.intensity == Intensity::gentle, punchy = s.intensity == Intensity::punchy;

        auto& rt = chain.resonance;
        rt = ResonanceParams {};
        // fullCut is weighted by loudness (A-weighting), so an 808 carrying most of the energy can't
        // hide a ring the ear picks out at once. On that scale a 3 kHz ring reads ~7x and a 300 Hz
        // ring ~1.5x what plain power gave; 0.3 dB keeps the boxiness decisions where the old 0.15
        // dB had them and catches harsh rings a little earlier (and now also over an 808).
        // Checked against clean harmonic lines (2026-10 re-audit, synthetic): a sung line (harmonics,
        // 1 % vibrato) reads under 0.3 when it sits 9 LU or more under the mix, but 0.5-1.2 dB at
        // lead-vocal level, while a steady whistle 15-20 LU under the mix reads 0.3-2 dB. No
        // threshold tells a loud clean line from a faint ring, so it stays at 0.3: on such a line the
        // tamer trims the harmonics by about half a dB, and with Focus Boxiness (where such a line
        // mostly reads) the report says to check the vocal with MATCH.
        const double fullCut = -st.resonanceCutDb;
        if (fullCut < 0.3)
            reason ("rt", "Amount", "0 %", "I hardly hear any tones ringing out of your mix (at full strength the tamer would cut only " +
                    num (fullCut, false, 2) + " dB on average, weighted by how loud each range sounds), so Resonance Tamer stays off.");
        else
        {
            const double aim = gentle ? 0.6 : punchy ? 1.6 : 1.0;   // average trim, dB (loudness-weighted)
            // Mild ringing gets a mild Amount: capped at 60 % rather than chasing the average trim.
            rt.amount = std::clamp (std::round (100.0 * aim / fullCut / 5.0) * 5.0, 15.0, 60.0);
            const char* where = st.boxinessShare > 0.65 ? "the low mids (boxiness)" : st.harshnessShare > 0.65 ? "the upper mids (harshness)" : "several places across the range";
            reason ("rt", "Amount", pct (rt.amount), "Some tones in your mix ring out above the sound around them, mostly in " + std::string (where) +
                    ". At full strength the tamer would cut " + num (fullCut) + " dB on average (weighted by how loud each range sounds); " + pct (rt.amount) + " trims them by about " +
                    num (std::min (aim, fullCut * rt.amount / 100.0)) + " dB, enough to clean them up without dulling the sound.");
            if (st.boxinessShare > 0.65)
            {
                rt.focus = ResonanceFocus::boxiness;
                reason ("rt", "Focus", "Boxiness", "Most of the ringing sits between 150 and 800 Hz, so the tamer only looks there and leaves the highs (hats, S's, vocal presence) alone. "
                        "The body of a vocal lives in that range too, so listen to the vocal with MATCH: if it thins out, lower Amount.");
            }
            else if (st.harshnessShare > 0.65)
            {
                rt.focus = ResonanceFocus::harshness;
                reason ("rt", "Focus", "Harshness", "Most of the ringing sits between 1.5 and 8 kHz, so the tamer only looks there and leaves the low end alone.");
            }
            else
                reason ("rt", "Focus", "Full", "The ringing is spread out, so the tamer watches the whole range.");
        }

        auto& ts = chain.steady;
        ts = ToneSteadyParams {};
        const double swing = st.steadyMoveDb;
        if (swing < 0.35)
            reason ("ts", "Amount", "0 %", "Your tone stays consistent through what I heard (at full strength Tone Steady would move only " +
                    num (swing, false, 2) + " dB on average), so it stays off.");
        else
        {
            ts.amount = gentle ? 25.0 : punchy ? 55.0 : 40.0;
            ts.speed = swing > 1.2 ? 1 : 0;
            reason ("ts", "Amount", pct (ts.amount), "Your tone shifts from moment to moment: at full strength Tone Steady would move " + num (swing) +
                    " dB on average. " + pct (ts.amount) + " keeps the parts consistent while they keep their own character. Range stays at 3 dB (boosts at most 1.5 dB).");
            reason ("ts", "Speed", ts.speed == 1 ? "Medium" : "Slow", ts.speed == 1 ? "The shifts are large (over 1.2 dB on average at full strength), so Medium, which reacts within about half a second, catches them before they're heard."
                                                                                    : "The shifts are small, so Slow, which reacts over about a second, is enough and is the gentlest setting.");
        }
        // The song's own tone, learned while it played: Tone Steady holds every part to it (as the stage
        // hears it, after this EQ), so playback from any point and the export sound the same.
        if (s.songTone.valid)
        {
            ts.target = toneTargetAtStage (s.songTone, chain.eq, clip.sampleRate, trimDb);
            const int secs = static_cast<int> (std::lround (s.songTone.seconds));
            char heardText[16];
            std::snprintf (heardText, sizeof heardText, "%d:%02d", secs / 60, secs % 60);
            // A tone learned earlier from more of the song (kept because what was heard now matches it) says
            // so, with how much this listen heard: the report's "Listened to" line quotes this listen.
            const bool earlier = s.songSeconds > 0.0 && s.songTone.seconds > s.songSeconds + 1.0;
            char thisText[16];
            const int now = static_cast<int> (std::lround (s.songSeconds));
            std::snprintf (thisText, sizeof thisText, "%d:%02d", now / 60, now % 60);
            reason ("ts", "Target", "Your song",
                    (earlier ? std::string ("Tone Steady keeps the overall tone it learned earlier from ") + heardText + " of your song in a longer listen "
                               "(this one heard " + thisText + ", and what I heard now matches it). "
                             : std::string ("I learned your song's overall tone from the ") + heardText + " I heard. ") +
                    "Tone Steady holds every part to it instead of the last few seconds, so what you hear from any start point is what you export. "
                    "A part that comes or goes in the low end (an 808 entering after the intro) is the arrangement, not a drift, and is left alone. "
                    "A part that comes in above the bass (a lead in the chorus) is treated like a drift: it is pulled down at most half the Range while it plays." +
                    (ts.amount < 0.5 ? " It's ready for when you turn Amount up." : ""));
        }
    }

    const AudioClip toneShaped = renderBass (renderSpectral (renderEq (clip, chain.eq), {}, chain.steady, chain.resonance), chain.bass);
    // The compressor's thresholds are found on the loudest 6 s (quicker, and that's what it must hold).
    const AudioClip loudest = [&]
    {
        const int n = toneShaped.numSamples(), len = std::min (n, static_cast<int> (toneShaped.sampleRate * 6.0));
        const int hopLen = std::max (1, static_cast<int> (toneShaped.sampleRate * 0.5));
        int best = 0;
        double bestE = -1.0;
        for (int start = 0; start + len <= n; start += hopLen)
        {
            double e = 0.0;
            for (const auto& ch : toneShaped.channels)
                for (int i = start; i < start + len; i += 16) e += static_cast<double> (ch[static_cast<size_t> (i)]) * ch[static_cast<size_t> (i)];
            if (e > bestE) { bestE = e; best = start; }
        }
        AudioClip out;
        out.sampleRate = toneShaped.sampleRate;
        for (const auto& ch : toneShaped.channels)
            out.channels.emplace_back (ch.begin() + best, ch.begin() + best + len);
        return out;
    }();
    {
        const bool dense = a.peakToLoudnessDb < 9.0;
        const bool acoustic = s.genre == Genre::acoustic;
        double ratio = s.intensity == Intensity::gentle ? 1.5 : s.intensity == Intensity::punchy ? 2.5 : 2.0;
        double targetGr = s.intensity == Intensity::gentle ? 1.0 : s.intensity == Intensity::punchy ? 2.5 : 1.5;
        if (acoustic) { ratio -= 0.25; targetGr *= 0.7; }

        auto& dyn = chain.dynamics;
        dyn = DynamicsParams {};
        std::array<bool, kDynBands> use { true, true, true, true };
        if (tame > 0.0) use[0] = false;   // Sub Tame already steadies the 808
        if (dense) use = { false, false, false, false };

        std::array<double, kDynBands> lo {}, hi {}, thr {}, medianGr {};
        for (size_t k = 0; k < kDynBands; ++k) { lo[k] = -48.0; hi[k] = 0.0; }   // the plug-in's threshold range

        // Runs the compressor with the given thresholds; returns each band's 90th-percentile and
        // median reduction over the moments the band is actually playing.
        auto run = [&] (const std::array<double, kDynBands>& thresholds, std::array<double, kDynBands>& p90)
        {
            throwIfCancelled();
            DynamicsParams p = dyn;
            for (size_t k = 0; k < kDynBands; ++k)
                p.bands[k] = { thresholds[k], use[k] ? ratio : 1.0, p.bands[k].attackMs, p.bands[k].releaseMs, 0.0 };
            MultibandDynamics engine;
            engine.prepare (loudest.sampleRate, loudest.numChannels());
            engine.setParams (p);
            engine.reset();
            AudioClip work = loudest;
            std::vector<float*> ptrs;
            for (auto& ch : work.channels) ptrs.push_back (ch.data());
            const int n = work.numSamples(), step = std::max (1, static_cast<int> (work.sampleRate * 0.005));
            std::array<std::vector<double>, kDynBands> gr, level;
            for (int start = 0; start < n; start += step)
            {
                const int len = std::min (step, n - start);
                std::vector<float*> at;
                for (auto* q : ptrs) at.push_back (q + start);
                engine.process (at.data(), work.numChannels(), len);
                for (int k = 0; k < kDynBands; ++k)
                {
                    gr[static_cast<size_t> (k)].push_back (engine.currentGrDb (k));
                    level[static_cast<size_t> (k)].push_back (engine.currentLevelDb (k));
                }
            }
            for (size_t k = 0; k < kDynBands; ++k)
            {
                const double top = percentile (level[k], 0.99);
                std::vector<double> playing;
                for (size_t i = 0; i < gr[k].size(); ++i)
                    if (level[k][i] > top - 20.0) playing.push_back (-gr[k][i]);
                p90[k] = playing.empty() ? 0.0 : percentile (playing, 0.9);
                medianGr[k] = playing.empty() ? 0.0 : percentile (playing, 0.5);
            }
        };

        if (use[0] || use[1] || use[2] || use[3])
        {
            std::array<double, kDynBands> p90 {};
            for (int it = 0; it < 9; ++it)
            {
                for (size_t k = 0; k < kDynBands; ++k) thr[k] = 0.5 * (lo[k] + hi[k]);
                run (thr, p90);
                for (size_t k = 0; k < kDynBands; ++k)
                    (p90[k] > targetGr ? lo[k] : hi[k]) = thr[k];   // too much reduction: raise the threshold
            }
            for (size_t k = 0; k < kDynBands; ++k) thr[k] = std::round (0.5 * (lo[k] + hi[k]) * 10.0) / 10.0;
            run (thr, p90);
            for (size_t k = 0; k < kDynBands; ++k)
            {
                if (! use[k]) continue;
                dyn.bands[k].thresholdDb = thr[k];
                dyn.bands[k].ratio = ratio;
                dyn.bands[k].gainDb = std::round (std::min (3.0, medianGr[k]) * 10.0) / 10.0;   // give back the average loss
            }
            const char* names[] = { "Low", "Low Mid", "High Mid", "High" };
            const char* holds[] = { bassHeavy ? "kick and 808" : "kick and bass", "body, snare and vocal warmth", "vocal presence and snare crack", "hats, cymbals and air" };
            for (size_t k = 0; k < kDynBands; ++k)
            {
                if (! use[k])
                {
                    if (k == 0)
                        reason ("dyn", names[k], "1:1", "Sub Tame in Bass Control already evens out the 808, so the low band isn't compressed twice.");
                    continue;
                }
                reason ("dyn", names[k], num (ratio) + ":1 at " + num (thr[k]) + " dB",
                        "Holds the " + std::string (holds[k]) + " down by up to about " + num (p90[k]) + " dB on the loudest moments, and gives back " +
                        num (dyn.bands[k].gainDb) + " dB so the band's level stays where it was. The loud moments get steadier without changing the tone.");
            }
            reason ("dyn", "Attack / Release", "per band", "Slow attack in the lows (30 ms) lets kicks through; faster in the highs (5 ms) catches harsh hits. "
                    "Adaptive release recovers quickly after short hits, so nothing pumps.");
        }
        else
        {
            reason ("dyn", "All bands", "1:1",
                    dense ? "Your mix is already dense (only " + num (a.peakToLoudnessDb) + " dB between its peaks and its average), so more compression would only flatten it."
                          : "Nothing to hold down.");
        }
    }

    // --- Stereo Imager: open up narrow highs, never touch the lows ------------------------------------
    {
        auto& im = chain.imager;
        im = ImagerParams {};
        const double scale = s.intensity == Intensity::gentle ? 0.5 : s.intensity == Intensity::punchy ? 1.3 : 1.0;
        const double genreScale = s.genre == Genre::acoustic ? 0.6 : s.genre == Genre::rock ? 0.8 : 1.0;
        const bool risky = a.correlation < 0.25;
        const bool mono = std::max (a.midSideDb, a.highSideDb) < -35.0;   // (almost) no stereo to widen
        auto widen = [&] (double sideDb, double narrowDb, double maxPct)
        {
            // How far below "already wide" this band sits decides how much it opens (0 .. maxPct).
            const double room = std::clamp ((narrowDb - sideDb) / 9.0, 0.0, 1.0);
            return std::round (100.0 + maxPct * room * scale * genreScale);
        };
        const auto sideText = [] (double db) { return db <= -40.0 ? std::string ("almost mono") : num (-db) + " dB below the center"; };
        if (useRef && ! risky && ! mono)
        {
            // Match ref: move each band's stereo part part of the way toward the reference's (narrower
            // too, when the mix is wider). Width scales the side: 10^(dB / 20) x 100 %.
            auto toward = [&] (double sideDb, double refDb, double lo, double hi)
            {
                const double diff = refDb - sideDb;
                const double beyond = std::copysign (std::max (0.0, std::abs (diff) - 1.0), diff);   // within 1 dB: close enough
                return std::clamp (std::round (100.0 * std::pow (10.0, 0.6 * scale * beyond / 20.0)), lo, hi);
            };
            im.highWidth = toward (a.highSideDb, ref.highSideDb, 85.0, 125.0);
            im.midWidth = toward (a.midSideDb, ref.midSideDb, 90.0, 110.0);
            auto explain = [&] (const char* band, double mixDb, double refDb, double w, const char* what)
            {
                const std::string compare = "The stereo part of your " + std::string (band) + " is " + sideText (mixDb) + "; in " + refName +
                                            " it is " + sideText (refDb) + ". ";
                if (w > 100.0) return compare + "Widening to " + pct (w) + " moves part of the way toward it (" + what + "), staying mono-safe.";
                if (w < 100.0) return compare + "Yours is wider, so " + pct (w) + " pulls it in part of the way, toward the reference's tighter image.";
                return compare + "That's close enough, so it stays as it is.";
            };
            reason ("img", "Lows", "100 %", "The lows stay as they are: wide bass costs punch and cancels on mono speakers (Mono Below in Bass Control handles the sub).");
            reason ("img", "Mids", pct (im.midWidth), explain ("mids", a.midSideDb, ref.midSideDb, im.midWidth, "space around the vocal"));
            reason ("img", "Highs", pct (im.highWidth), explain ("highs", a.highSideDb, ref.highSideDb, im.highWidth, "hats, reverbs and ad-libs spread out"));
        }
        else
        {
            if (! risky && ! mono)
            {
                im.highWidth = widen (a.highSideDb, -6.0, 18.0);
                im.midWidth = widen (a.midSideDb, -12.0, 8.0);
                if (a.midSideDb > -3.0) im.midWidth = 92.0;   // very wide mids: pull the center back together
            }
            reason ("img", "Lows", "100 %", "The lows stay as they are: wide bass costs punch and cancels on mono speakers (Mono Below in Bass Control handles the sub).");
            reason ("img", "Mids", pct (im.midWidth),
                    mono ? "Your mix is (almost) mono. The Imager widens what is already stereo, so there's nothing for it to open up; widen in the mix with stereo reverbs, doubles or panning."
                    : risky ? "Your mix's correlation is low (" + num (a.correlation) + "), so widening could make parts cancel on phones and club systems. Left alone."
                    : im.midWidth > 100.0 ? "The stereo part of your mids is " + sideText (a.midSideDb) + ", fairly narrow, so a small lift adds space around the vocal without loosening it."
                    : im.midWidth < 100.0 ? "Your mids are already very wide (stereo part only " + num (-a.midSideDb) + " dB below the center), so they're pulled in a little to keep the vocal solid."
                                          : "Your mids already have a healthy width, so they stay as they are.");
            reason ("img", "Highs", pct (im.highWidth),
                    mono ? "Left alone: there's no stereo in the highs to widen."
                    : risky ? "Left alone for mono safety (low correlation)."
                    : im.highWidth > 100.0 ? "The stereo part of your highs is " + sideText (a.highSideDb) + ". Opening them to " + pct (im.highWidth) +
                                             " spreads hats, reverbs and ad-libs, where width sounds best and stays mono-safe."
                                           : "Your highs are already wide, so they stay as they are.");
        }
    }

    // --- Saturation: harmonics for density and small-speaker bass ------------------------------------
    {
        auto& sat = chain.saturation;
        sat = SaturationParams {};
        const bool tubeGenre = s.genre == Genre::trapHipHop || s.genre == Genre::rnb || s.genre == Genre::edm || s.genre == Genre::pop;
        sat.mode = tubeGenre ? SaturationMode::tube : SaturationMode::tape;
        sat.mix = 50.0;
        // 808-led genres: harmonics on the low end only, so the 808 reads on phone speakers while vocals
        // and hats stay clean. The same harmonics target applies (Bass only's Drive is calibrated to read
        // about the same on the Harmonics meter as Full).
        const bool bassLed = s.genre == Genre::trapHipHop || s.genre == Genre::rnb;
        sat.focus = bassLed ? SaturationFocus::bass : SaturationFocus::full;
        // Harmonics level to land on (dB below the music, at this Mix). Trap / Balanced is set by ear:
        // Tube at about -25 dB was the favourite in testing.
        double harmTarget = 0.0;
        switch (s.genre)
        {
            case Genre::trapHipHop: case Genre::edm: harmTarget = -25.0; break;
            case Genre::rnb: case Genre::pop:        harmTarget = -28.0; break;
            case Genre::rock:                        harmTarget = -28.0; break;
            case Genre::acoustic:                    harmTarget = -37.0; break;
        }
        harmTarget += s.intensity == Intensity::gentle ? -7.0 : s.intensity == Intensity::punchy ? 3.0 : 0.0;
        harmTarget += feel == handoff::Feel::soft ? -5.0 : feel == handoff::Feel::natural ? -2.0 : feel == handoff::Feel::hard ? 1.0 : 0.0;
        const bool dense = a.peakToLoudnessDb < 9.0;
        if (dense) harmTarget -= 4.0;

        MasterChainParams pre {};
        pre.dynamics = chain.dynamics;
        pre.imager = chain.imager;
        const AudioClip beforeSat = renderPreDrive (toneShaped, pre);
        auto harmonicsAt = [&] (double drive)
        {
            SaturationParams p = sat;
            p.driveDb = drive;
            return Saturation::estimateHarmonicsDb (beforeSat.channels, p, beforeSat.sampleRate);
        };
        // Too little low end for Bass only to work on (no 808 in what I heard): the whole mix gets the
        // harmonics instead. Judged by the low end's share of the mix (sub + bass bands against the genre's
        // usual share), not by the harmonics it makes: a quiet mix makes fewer harmonics at any Drive, but
        // its 808 is just as much there. 6 dB under the usual share: a mix high-passed around 150 Hz
        // (measured: 7.7 dB under; at 100 Hz, 4.6 dB under, where Bass only still reaches the target).
        auto lowShareDb = [] (const std::array<double, kEqBands>& bands, double offset)
        {
            return 10.0 * std::log10 (std::pow (10.0, (bands[0] - offset) / 10.0) + std::pow (10.0, (bands[1] - offset) / 10.0));
        };
        const auto norm = genreTargetCurve (s.genre);
        const double normRef = (norm[1] + norm[2] + norm[3] + norm[4] + norm[5]) / 5.0;
        const bool thinLow = bassLed && (! a.hasLowEnd || lowShareDb (a.bandDb, 0.0) < lowShareDb (norm, normRef) - 6.0);
        if (thinLow) sat.focus = SaturationFocus::full;
        const bool bassOnly = sat.focus == SaturationFocus::bass;
        double dlo = 0.0, dhi = Saturation::kMaxDriveDb;
        if (harmonicsAt (dhi) < harmTarget)
            sat.driveDb = dhi;
        else
        {
            for (int it = 0; it < 9; ++it)
            {
                const double mid = 0.5 * (dlo + dhi);
                (harmonicsAt (mid) < harmTarget ? dlo : dhi) = mid;
            }
            sat.driveDb = std::round (0.5 * (dlo + dhi) * 10.0) / 10.0;
        }
        const double got = harmonicsAt (sat.driveDb);
        const std::string modeName = sat.mode == SaturationMode::tube ? "Tube" : "Tape";
        reason ("sat", "Mode", modeName,
                sat.mode != SaturationMode::tube ? "Tape rounds off the loudest moments and adds smooth odd harmonics: warmth without changing the character of " + genre + "."
                : bassOnly ? "Tube leans on the 2nd harmonic: the 808 sounds fuller and richer, which " + genre + " needs."
                           : "Tube leans on the 2nd harmonic: the bass and vocals sound fuller and the bass shows up on phone speakers, which " + genre + " needs.");
        reason ("sat", "Focus", bassOnly ? "Bass only" : "Full",
                bassOnly ? "Harmonics go on the 808 and kick only, so the 808 shows up on phone and laptop speakers (they can't play its low notes, "
                           "but they play its harmonics). Vocals and hats stay clean."
                : thinLow ? "There's very little low end in what I heard (no 808 to speak of), so Bass only would have nothing to work on: "
                            "the whole mix gets the harmonics instead."
                          : "The whole mix gets a little density. In " + genre + " the low end doesn't carry the song alone, so there's no need to aim the harmonics at it.");
        reason ("sat", "Drive", num (sat.driveDb, true) + " dB",
                "Set by measuring your mix: at this Drive the harmonics sit about " + num (-got) + " dB below your music" +
                (got > -30.0 ? (bassOnly ? ", enough for the 808 to come through on small speakers without sounding distorted."
                                         : ", enough to make the master denser without sounding distorted.")
                             : (bassOnly ? ", a light touch: the 808 gets a little more presence on small speakers." : ", density you feel more than hear.")) +
                (dense ? " Your mix is already dense, so it's kept lighter." : ""));
        reason ("sat", "Mix", "50 %", "Half the harmonics are blended in and the rest stays clean, so drum hits keep their snap while the harmonics fill in underneath.");
    }

    // --- Soft Clipper: on or off ------------------------------------------------------------------------
    const double plr = a.peakToLoudnessDb;
    const bool loudTarget = tier != LoudnessTarget::stream;
    const bool punchy = s.intensity == Intensity::punchy;
    chain.clipper.enabled = s.intensity != Intensity::gentle && (loudTarget || plr > 16.0) && plr >= 9.0;
    chain.clipper.softness = 0.5;
    chain.clipper.oversampling = 4;

    // How many dB of the biggest peaks the clipper may take before the Maximizer takes the rest.
    // Dense, drum-led genres hide clipping well; acoustic and rock expose it.
    double clipShare = 0.0;
    switch (s.genre)
    {
        case Genre::trapHipHop: case Genre::edm: clipShare = 3.5; break;
        case Genre::rnb: case Genre::pop:        clipShare = 3.0; break;
        case Genre::rock:                        clipShare = 2.0; break;
        case Genre::acoustic:                    clipShare = 1.5; break;
    }
    // The style's feel: a soft style (Lo-Fi, Ambient) hides less clipping than its genre, a hard one (Rage, Metal) a little more.
    clipShare = feel == handoff::Feel::soft ? clipShare * 0.5 : feel == handoff::Feel::natural ? clipShare * 0.75
              : feel == handoff::Feel::hard ? clipShare + 0.5 : clipShare;
    clipShare *= tier == LoudnessTarget::stream ? 0.6 : tier == LoudnessTarget::club ? 1.3 : 1.0;
    if (punchy) clipShare += 1.5;
    const double minLevel = (punchy || tier == LoudnessTarget::club) ? -2.0 : 0.0;   // below 0 = extra density
    const double peakBudget = rules.peakBudgetDb + (s.genre == Genre::acoustic ? -1.5 : s.genre == Genre::rock ? -0.5
                                                    : (s.genre == Genre::trapHipHop || s.genre == Genre::edm) ? 1.0 : 0.0);

    // --- Maximizer ---------------------------------------------------------------------------------
    chain.limiter.ceilingDbtp = maxCeiling;
    chain.limiter.releaseMs = 120.0;
    chain.limiter.adaptiveRelease = true;
    const double ceiling = chain.limiter.ceilingDbtp;

    // Loudness: search on the real chain, then respect the intensity's peak-control budget.
    // The whole pre-drive chain from the raw input, in the plug-in's order (Stem EQ included).
    const AudioClip shaped = [&] { MasterChainParams full = chain; full.stem = s.stem; return renderPreDrive (input, full); }();
    const auto shapedStats = measure (shaped);
    const double inLufs = shapedStats.integratedLufs;
    const double recurring = recurringPeakDb (shaped);   // the peaks that keep coming back
    MasterChainParams rest = chain;
    rest.unlimiter = UnlimiterParams {};
    rest.eq = TonalEQParams {};
    rest.bass = BassParams {};
    rest.steady = ToneSteadyParams {};
    rest.resonance = ResonanceParams {};
    rest.dynamics = DynamicsParams {};
    rest.imager = ImagerParams {};
    rest.saturation = SaturationParams {};
    auto setClipLevel = [&] (double level)
    {
        rest.clipper.thresholdDb = ceiling + level;          // the engine wants the absolute threshold
        rest.clipper.makeupDb = std::max (0.0, -level);
    };
    auto evaluate = [&] (double drive)
    {
        rest.driveDb = drive;
        return measure (renderMaster (shaped, rest));
    };
    // Loudness given up to peak control: the gain put in (Drive plus the clipper's makeup) minus the
    // loudness that actually came out.
    auto costOf = [&] (double drive, const LoudnessStats& o)
    {
        const double makeup = rest.clipper.enabled ? rest.clipper.makeupDb : 0.0;
        return std::max (0.0, drive + makeup - (o.integratedLufs - inLufs));
    };

    // With the clipper on, set its level so it takes about clipShare dB of the peaks at the drive
    // the target needs (two refinements: the level changes the drive, the drive changes the level).
    double clipLevel = 0.0;
    setClipLevel (clipLevel);
    auto found = findDriveForTarget (shaped, r.targetLufs, rest);
    if (rest.clipper.enabled)
        for (int pass = 0; pass < 2; ++pass)
        {
            const double overshoot = recurring + std::max (0.0, found.driveDb) - ceiling;
            clipLevel = std::round (std::clamp (overshoot - clipShare, minLevel, 3.0) * 10.0) / 10.0;
            setClipLevel (clipLevel);
            found = findDriveForTarget (shaped, r.targetLufs, rest);
        }

    double drive = std::max (0.0, found.driveDb);

    // Limit 1: how deep the recurring peaks get shaved (clipper + Maximizer together, plus any
    // clipper makeup). Past this you hear it, whatever the loudness numbers say.
    bool peakLimited = false;
    const double peakShave = [&] (double d) { return recurring + d - ceiling + (rest.clipper.enabled ? rest.clipper.makeupDb : 0.0); } (drive);
    if (peakShave > peakBudget)
    {
        drive = std::max (0.0, drive - (peakShave - peakBudget));
        peakLimited = true;
        if (rest.clipper.enabled)
        {
            clipLevel = std::round (std::clamp (recurring + drive - ceiling - clipShare, minLevel, 3.0) * 10.0) / 10.0;
            setClipLevel (clipLevel);
        }
    }

    // Limit 2: loudness given up to peak control.
    LoudnessStats out = evaluate (drive);
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
        budgetLimited = true;
    }
    // A clipper whose level the recurring peaks never reach would only be makeup gain (and its report
    // would read "shaves about 0.0 dB"): put that gain into Drive instead and leave the clipper off.
    // Below its threshold the clipper is exactly that gain, so the master stays the same.
    if (rest.clipper.enabled && recurring + drive - (ceiling + clipLevel) < 0.3)
    {
        drive += rest.clipper.makeupDb;
        rest.clipper.enabled = false;
        clipLevel = 0.0;
        setClipLevel (0.0);
    }
    drive = std::round (drive * 10.0) / 10.0;
    out = evaluate (drive);
    cost = costOf (drive, out);
    chain.driveDb = drive;
    chain.clipper = rest.clipper;
    r.output = out;
    r.loudnessCostDb = cost;
    r.reachedTarget = std::abs (out.integratedLufs - r.targetLufs) <= kTargetToleranceLu;

    // Explain the clipper with the peak work it actually does at this drive (on the recurring peaks).
    const double peakIn = recurring + drive;
    if (chain.clipper.enabled)
    {
        const double clipDb = std::max (0.0, peakIn - (ceiling + clipLevel));
        const double limitDb = std::max (0.0, clipLevel);
        const bool drumsy = s.genre == Genre::trapHipHop || s.genre == Genre::edm || s.genre == Genre::pop || s.genre == Genre::rnb;
        reason ("clip", "Soft Clipper", "On",
                std::string (chain.unlimiter.amount > 0.0 ? "With the punch the Unlimiter puts back, your mix has " : "Your mix has ") +
                num (plr) + " dB between its peaks and its average loudness" + std::string (plr > 14.0 ? " (sharp peaks)" : "") +
                ". The clipper shaves about " + num (clipDb) + " dB off the " + (drumsy ? "drum hits" : "peaks") + " that keep coming back" +
                (limitDb > 0.05 ? ", and the Maximizer handles the last " + num (limitDb) + " dB" : "") +
                ". Sharing the work this way gets you loud without either one being heard.");
        reason ("clip", "Clip Level", num (clipLevel, true) + " dB",
                clipLevel > 0.05 ? "Clipping starts " + num (clipLevel) + " dB above the ceiling, so only the biggest peaks are touched: about as much clipping as " +
                                   styleName + " hides well (" + num (clipShare) + " dB)."
                : clipLevel > -0.05 ? "The clip level sits right at the ceiling (Softness starts rounding peaks a few dB below it): the clipper takes the peaks and the Maximizer just polishes what's left."
                : "Clipping starts " + num (-clipLevel) + " dB below the ceiling for extra density (" + std::string (rules.name) + ", " +
                  tname + "); the level comes back up so peaks still use the full headroom.");
        reason ("clip", "Softness", "50 %", "A medium knee: rounds peaks gently, heard as punch rather than distortion.");
        reason ("clip", "Oversampling", "4x", "Keeps the clipping clean (far less harsh aliasing) at a sensible CPU cost.");
    }
    else
    {
        reason ("clip", "Soft Clipper", "Off",
                s.intensity == Intensity::gentle ? "Gentle leaves peak control to the Maximizer alone, for the most transparent sound."
                : plr < 9.0 ? "Your mix is already dense (only " + num (plr) + " dB between peaks and average), so clipping would only add distortion."
                : loudTarget || plr > 16.0 ? "At the Drive this mix gets (" + num (drive, true) + " dB), no peak reaches the clip level, so the Maximizer handles the peaks alone."
                            : "For a streaming target the Maximizer alone reaches the level cleanly, so no clipping is needed.");
    }

    if (arrivesPeak > 0.0)
        r.notes.push_back ("HEADS UP: your mix peaks at " + num (arrivesPeak, true) + " dBTP before Masterology. That's fine inside your DAW, "
                           "but if you export the unmastered mix (for example to send to someone), turn the master fader down so it peaks below "
                           "\xE2\x88\x92" "1 dB, or the file will clip."
                           "\nNEED: Not for the master: Masterology handles it. Only before exporting the unmastered mix."
                           "\nSTEP: Put a gain plug-in (or lower every track fader) before Masterology by about " + num (std::ceil (arrivesPeak + 1.0), false, 0) + " dB."
                           "\nSTEP: Run Auto-Master again; the master comes out the same.");

    std::string driveWhy;
    // Drive can't go below 0 dB: when the target needs less than that by more than the landing tolerance,
    // the caller turns the input down (Input Trim) and decides everything again.
    if (found.driveDb < -kTargetToleranceLu && drive == 0.0)
        wantTrimDb = found.driveDb - kTrimMarginDb;
    if (found.driveDb < 0.0 && drive == 0.0 && ! r.reachedTarget)
        driveWhy = trimDb <= kMinTrimDb
            ? arrivesText + ", so loud that even with Input Trim at its " + num (kMinTrimDb) +
              " dB limit the master lands at " + num (out.integratedLufs) + " LUFS, above the " + tname +
              " target. Turn the mix down before Masterology."
            : "With Drive at 0 dB the master lands at " + num (out.integratedLufs) + " LUFS, " + num (out.integratedLufs - r.targetLufs) +
              " dB above the " + tname + " target. Lower Input Trim by that much, or run Auto-Master again.";
    else if (r.reachedTarget)
        driveWhy = num (drive, true) + " dB of Drive lands this part at " + num (out.integratedLufs) + " LUFS" +
                   (streamLift > 0.0 ? ", so your whole song should read about " + num (out.integratedLufs - streamLift) + " LUFS, your " + tname +
                                       " target (streaming services measure the whole song, and your quieter parts sit under this one). "
                                     : ", your " + tname + " target. ") + (cost < 0.3 ? std::string ("At this level the peaks barely need controlling, so the master stays completely clean.")
                                             : "Holding the peaks down costs about " + num (cost) + " dB of loudness, within the " + rules.name +
                                               " limit of " + num (rules.budgetDb) + " dB, so " +
                                               (bassHeavy ? "the drums keep their punch." : "the attacks stay natural."));
    else if (peakLimited && ! budgetLimited)
        driveWhy = "Reaching " + num (r.targetLufs) + " LUFS would mean shaving about " + num (recurring + found.driveDb - ceiling) +
                   " dB off your recurring peaks, which you'd hear on " + genre + ". I stopped at " + num (out.integratedLufs) +
                   " LUFS, where the shaving is " + num (peakBudget) + " dB (the " + rules.name + " limit for " + genre + "). " +
                   (s.intensity == Intensity::punchy ? "To go louder, control the peaks in the mix."
                                                     : "Choose Punchy to go louder, or control the peaks in the mix.");
    else if (budgetLimited)
        driveWhy = "Reaching " + num (r.targetLufs) + " LUFS would squash this mix too hard for " + std::string (rules.name) +
                   ", so I stopped at " + num (out.integratedLufs) + " LUFS, where holding the peaks down costs " + num (cost) + " dB (the " +
                   rules.name + " limit). " +
                   (streamLift > 0.0 ? "Your whole song should read about " + num (out.integratedLufs - streamLift) + " LUFS. " : std::string()) +
                   (s.intensity == Intensity::punchy ? "To go louder, tame the drum peaks in the mix."
                                                     : "Choose Punchy to go louder, or tame the drum peaks in the mix.");
    else
    {
        driveWhy = "Even the maximum Drive only reaches " + num (out.integratedLufs) + " LUFS on this mix.";
        r.notes.push_back ("HEADS UP: your mix arrives very quiet (" + num (arrivesLufs) + " LUFS), so even the maximum Drive leaves the master at "
                           + num (out.integratedLufs) + " LUFS, short of the " + tname + " target (" + num (r.targetLufs) + " LUFS)."
                           "\nNEED: Yes, if you want the target loudness."
                           "\nSTEP: Turn the mix up before Masterology: raise the track faders together (not the {daw:master} fader: it comes after Masterology) by about "
                           + num (std::ceil (r.targetLufs - out.integratedLufs), false, 0) + " dB."
                           "\nSTEP: Run Auto-Master again.");
    }
    if (trimDb < 0.0)
        reason ("max", "Input Trim", num (trimDb, true) + " dB",
                arrivesText +
                (arrivesLufs + stemLiftDb > r.targetLufs ? ", already louder than the " + tname + " target (" + num (r.targetLufs) + " LUFS)"
                                                         : "; with this chain's EQ and make-up gain it would land above the " + tname +
                                                           " target (" + num (r.targetLufs) + " LUFS) even at Drive 0") +
                ". Drive can only push up, so Input Trim turns the mix down " + num (-trimDb) + " dB before the whole chain: the master lands on "
                "the target instead of overshooting it, and every module works at the level it was set for." +
                (stemPush && ! s.stem.keepLoudness ? std::string (" Turning on Keep Loudness in Stem EQ would hold the level steady instead, so your "
                                                                  "Stem EQ moves wouldn't need the trim.") : std::string()));
    reason ("max", "Drive", num (drive, true) + " dB", driveWhy);
    reason ("max", "Ceiling", num (chain.limiter.ceilingDbtp, false) + " dBTP",
            (useRef ? "Set for a master as loud as your reference (" + num (r.targetLufs) + " LUFS). " : std::string()) +
            (tier == LoudnessTarget::stream
                 ? std::string ("Leaves room for the MP3/AAC conversion streaming services do, so the streamed version is very unlikely to clip.")
                 : std::string (tier == LoudnessTarget::club ? "DJs mostly play MP3/AAC files, and streaming services convert too. "
                                                             : "Streaming services convert your master to MP3/AAC/Ogg. ") +
                   "Masters this loud peak higher after that conversion, so the ceiling sits at \xE2\x88\x92" "1.5 instead of the usual \xE2\x88\x92" "1: "
                   "in our tests the converted copies then peaked within about 0.4 dB of 0 dBTP, where \xE2\x88\x92" "1 let them go up to about 1 dB over. "
                   "Spotify's own advice for masters louder than \xE2\x88\x92" "14 LUFS is \xE2\x88\x92" "2 dBTP: set the Ceiling to \xE2\x88\x92" "2 if you "
                   "want to follow it to the letter (it costs a little loudness)."));
    reason ("max", "Release", "120 ms adaptive", "Recovers fast after drum hits and slowly on sustained bass, so the master doesn't pump.");

    if ((budgetLimited || peakLimited) && ! r.reachedTarget && s.intensity != Intensity::punchy && ! useRef)   // (with a reference: see below)
        r.notes.push_back ("LOUDER, IF YOU WANT: this mix has big peaks for its loudness (" + num (plr) + " dB). " +
                           std::string (bassHeavy ? "Controlling the drum peaks in the mix (a clipper or limiter on the drum bus)"
                                                  : "Taming the biggest peaks in the mix (strums, drum hits, vocal peaks: a compressor on those tracks)") +
                           " would let the master get louder without losing punch."
                           "\nNEED: No. The master is fine as it is; this is only how to get louder without losing punch."
                           "\nSTEP: " + std::string (bassHeavy ? "In your mix, put a clipper or limiter on the drum bus that takes 2\xE2\x80\x93" "3 dB off the hits."
                                                               : "In your mix, put a compressor on the peakiest tracks (strums, drums or vocal) taking 2\xE2\x80\x93" "3 dB off.") +
                           "\nSTEP: Run Auto-Master again: it will get closer to your target."
                           "\nSTEP: Or choose Punchy (it allows more peak control) and run Auto-Master again.");
    if ((s.genre == Genre::acoustic || feel == handoff::Feel::soft) && ! useRef && tier != LoudnessTarget::stream)
        r.notes.push_back ("HEADS UP: " + (feel == handoff::Feel::soft ? styleName + " is meant to sound soft: its releases usually sit around "
                                           : style != nullptr ? styleName + " releases usually sit around " : std::string ("acoustic releases usually sit around ")) +
                           "\xE2\x88\x92" "14 to \xE2\x88\x92" "11 LUFS. Stream \xE2\x88\x92" "14 keeps the most life in " +
                           (feel == handoff::Feel::soft ? std::string ("it") : std::string ("the guitars and vocal")) + "; streaming services turn louder masters down anyway."
                           "\nNEED: Your choice. Louder is fine if that's the sound you want."
                           "\nSTEP: For a more natural sound, click Stream \xE2\x88\x92" "14 as the Target."
                           "\nSTEP: Run Auto-Master again and compare with MATCH on.");

    // --- Match ref: honest about loudness and the low end; the master measured like the mix -------------
    if (useRef)
    {
        if (refLufs > kRefMaxLufs + 0.05)
            r.notes.push_back ("REFERENCE LOUDNESS: " + refName + " measures " + num (refLufs) + " LUFS in its loudest 12 seconds. That's louder than Masterology "
                               "will aim for (" + num (kRefMaxLufs) + " LUFS at most): past that, every mix loses its punch and starts to distort, "
                               "so Auto-Master aims for " + num (kRefMaxLufs) + "."
                               "\nNEED: No. Streaming services turn masters this loud down anyway, so the extra loudness wouldn't be heard there."
                               "\nSTEP: Compare with MATCH on: your master should sound as full as the reference at the same level.");
        else if (refLufs < kRefMinLufs - 0.05)
            r.notes.push_back ("REFERENCE LOUDNESS: " + refName + " measures only " + num (refLufs) + " LUFS in its loudest 12 seconds, quieter than any "
                               "release Masterology aims for (" + num (kRefMinLufs) + " LUFS at least), so Auto-Master aims for " + num (kRefMinLufs) + ". "
                               "It may be a turned-down copy, a live recording or a rough mix."
                               "\nNEED: Only if this isn't the song you meant to load."
                               "\nSTEP: Load the released version of the song (a WAV, FLAC or high-quality MP3) with Match ref.");
        if (! r.reachedTarget && out.integratedLufs < r.targetLufs - kTargetToleranceLu && (peakLimited || budgetLimited))
            r.notes.push_back ("NOT AS LOUD AS THE REFERENCE: " + refName + " measures " + num (refLufs) + " LUFS in its loudest 12 seconds; your master's "
                               "loudest 12 seconds stop at " + num (out.integratedLufs) + " LUFS. Getting all the way there would " +
                               (peakLimited && ! budgetLimited ? "shave your drum peaks deeper than " + std::string (rules.name) + " allows"
                                                               : "squash your mix harder than " + std::string (rules.name) + " allows") +
                               ", and you'd hear the drums lose their punch. The reference most likely had its drum peaks controlled in the mix."
                               "\nNEED: No. A master a little quieter than the reference is fine, and streaming services play both at the same level anyway."
                               "\nSTEP: To get closer, control the drum peaks in your mix: a clipper or limiter on the drum bus taking 2\xE2\x80\x93" "3 dB off the hits." +
                               std::string (s.intensity == Intensity::punchy ? "" : "\nSTEP: Or choose Punchy (it allows more peak control).") +
                               "\nSTEP: Run Auto-Master again.");
        // Low-end shape: the deep sub (30-60 Hz) against the low bass (60-120 Hz). The Low End band moves the
        // whole sub range; where the 808's energy sits within it comes from the mix (its tuning and sound).
        const double shape = ref.subVsBassDb - a.subVsBassDb;
        if (a.hasLowEnd && bassHeavy && std::abs (shape) > 3.0)
            r.notes.push_back (shape > 0.0
                ? "LOW END SHAPE: compared with its low bass (60\xE2\x80\x93" "120 Hz), " + refName + " has about " + num (shape) +
                  " dB more deep sub (30\xE2\x80\x93" "60 Hz) than your mix: its 808 reaches lower. The master EQ lifts the sub range as a whole; "
                  "where your 808's weight sits comes from the 808 itself."
                  "\nNEED: Optional. The master works as it is; this is how to get the reference's deep weight."
                  "\nSTEP: In your mix, check the 808: a lower note, or an 808 sample with a longer sub tail, gives that deep weight."
                  "\nSTEP: Or boost 40\xE2\x80\x93" "50 Hz by a few dB on the 808 track only."
                  "\nSTEP: Run Auto-Master again."
                : "LOW END SHAPE: compared with its low bass (60\xE2\x80\x93" "120 Hz), " + refName + " has about " + num (-shape) +
                  " dB less deep sub (30\xE2\x80\x93" "60 Hz) than your mix: its low end sits higher and punchier, while yours leans on the deep sub, "
                  "which headphones show far more than phones and laptops."
                  "\nNEED: Optional. The master works as it is; this is how to get the reference's punchier low end."
                  "\nSTEP: On the 808 track, cut 30\xE2\x80\x93" "45 Hz a little, or boost 80\xE2\x80\x93" "100 Hz."
                  "\nSTEP: Or give the 808 more harmonics so it reads on small speakers: Saturation with Focus Bass only and a little more Drive."
                  "\nSTEP: Run Auto-Master again.");
        r.masterAnalysis = analyseMix (renderMaster (shaped, rest));   // rest is at the final Drive and clip level
    }

    // --- Summary -------------------------------------------------------------------------------------
    char head[512];
    std::snprintf (head, sizeof head, "Listened to %.0f s of your mix. ", a.seconds);
    if (s.songSeconds > 0.0)
        head[0] = '\0';   // the plug-in's report already says how much of the song it heard and which part this is
    const bool wholeSong = s.songSeconds >= a.seconds + 2.0;
    if (useRef)
        r.summary = std::string (head) + "Matched to your reference " + refName + " (its loudest 12 seconds measure " + num (refLufs) +
                    " LUFS, compared with the loudest 12 seconds of your song) instead of typical " + styleName + " masters: tone, loudness, "
                    "stereo width and low end aim for it, part of the way. "
                    "The genre still sets what a reference can't show: compression, saturation, clipping and punch. ";
    else
        r.summary = head;
    r.summary += std::string ("Your mix measured ") + num (arrivesLufs) + " LUFS with peaks at " +
                num (arrivesPeak) + " dBTP" + (stemPush ? " (about " + num (arrivesLufs + stemLiftDb) + " LUFS with your Stem EQ moves)" : std::string()) +
                ". Settings: " + (style != nullptr ? styleName + " (mastered as " + genre + ")" : genre) + ", " + rules.name + ", " + tname +
                (trimDb < 0.0 ? ". To land on the target, Input Trim turns it down " + num (-trimDb) + " dB first" : std::string()) +
                ". The master now measures " + num (out.integratedLufs) + " LUFS with true peak " + num (out.truePeakDbtp) +
                (streamLift > 0.0 ? " dBTP in this loudest part. Streaming services measure the whole song, so this part is set " + num (streamLift) +
                                    " dB above " + num (targetLufs (LoudnessTarget::stream)) + ": your whole song should read about " +
                                    num (out.integratedLufs - streamLift) + " LUFS."
                 : wholeSong ? " dBTP in this loudest part. The whole song reads lower than its loudest part, which is normal."
                             : " dBTP. The full song will read about 1 LU lower than this loud section, which is normal.");
    // --- Stem EQ: suggest a vocal move, never apply it -------------------------------------------------
    // Only the vocal: drums, bass and "other" swing too much with the arrangement (no drums in a ballad,
    // a kick but no 808...) for a usual level to mean anything.
    {
        r.analysis.stemLevelDb = measureStemLevels (heard);
        const double vox = r.analysis.stemLevelDb[kStemVocals], usual = genreVocalLevelDb (s.genre);
        const double move = suggestVocalMoveDb (vox, s.genre);
        const double current = s.stem.enabled ? s.stem.gainDb[kStemVocals] : 0.0;
        const std::string hears = "The model hears your vocal at " + num (vox) + " dB against the whole mix; in " + genre +
                                  " masters it usually sits around " + num (usual) + " dB";
        if (vox < -15.0)
            reason ("stem", "Vocals", "kept", "I hardly hear a lead vocal here (an instrumental, or a section without vocals), so there's nothing to suggest.");
        else if (move == 0.0)
            reason ("stem", "Vocals", "kept", hears + ". Yours is right in that range, so no move is needed.");
        else
        {
            const double to = std::clamp (current + move, -StemEQ::kMaxGainDb, StemEQ::kMaxGainDb);
            r.hasStemSuggestion = true;
            // With Stem EQ off, the mix was heard without its knobs, so the suggestion starts from 0 dB for the other
            // stems: applying it (which switches Stem EQ on) must not bring back moves you had switched off.
            r.stemSuggestion = s.stem;
            if (! s.stem.enabled)
                r.stemSuggestion.gainDb.fill (0.0);
            r.stemSuggestion.enabled = true;
            r.stemSuggestion.gainDb[kStemVocals] = to;
            reason ("stem", "Vocals", "try " + num (to, true) + " dB",
                    hears + (move > 0 ? ", so yours sits a little low under the beat. " : ", so yours sits louder than usual over the beat. ") +
                    "This is a suggestion only: Auto-Master never moves Stem EQ. Press Apply in Stem EQ's Learn to try it, then compare with MATCH. "
                    "Vocal level is taste, so trust your ears over this number.");
        }
    }

    chain.stem = s.stem;
    chain.trimDb = trimDb;
    r.ok = true;
    return r;
}

} // namespace

AutoMasterResult autoMaster (const AudioClip& input, const AutoMasterSettings& s)
{
    const ScopedFlushDenormals noDenormals;
    // A mix already louder than the target at Drive 0 is turned down at the input (Input Trim), then
    // every module is decided again for the level it will hear: Dynamics thresholds, Saturation Drive
    // and the loudness search all depend on it. At most two refinements.
    double want = 0.0, trim = 0.0;
    auto r = autoMasterAt (input, s, 0.0, want);
    for (int pass = 0; pass < 2 && r.ok && want < 0.0 && trim > kMinTrimDb; ++pass)
    {
        trim = std::max (kMinTrimDb, std::round ((trim + want) * 10.0) / 10.0);
        r = autoMasterAt (input, s, trim, want);
    }
    // The mix as it arrives: before Input Trim and before the user's Stem EQ (the report's "your mix").
    r.analysis.loudness = measure (input);
    r.capturedLufs = r.analysis.loudness.integratedLufs;
    return r;
}

} // namespace polisher

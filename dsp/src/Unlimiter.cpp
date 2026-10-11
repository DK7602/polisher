#include "polisher/Unlimiter.h"

#include <numbers>

namespace polisher {

namespace {
    double coeff (double ms, double sr) { return 1.0 - std::exp (-1.0 / (ms * 0.001 * sr)); }
    constexpr double kFastAttackMs = 0.5;
    constexpr double kReleaseMs = 80.0;
    constexpr double kGainSmoothMs = 0.3;
    constexpr double kFloor = 1.0e-4;          // -80 dBFS: quieter than this is never lifted
    constexpr double kHoldMs = 25.0;
    constexpr double kSlowAttackMs = 50.0;
    constexpr double kRmsMs = 2.5;
    constexpr double kSplitFadeMs = 20.0;      // the plain <-> split cross-fade (only ever while parked), and the lift's band shares
    constexpr double kSplitSettleMs = 30.0;    // after a Focus change or a split glide, the detector settles before it lifts again
    constexpr double kSvfK = 1.4142135623730951;   // state-variable filter damping: 1 / Q, Butterworth
    constexpr int kSplitUpdate = 16;           // the split's state and coefficients move every 16 samples
    constexpr double kBlockSilence = 1.0e-6;   // blocks below -60 dBFS RMS: silence and fades don't count

    // Limiter check: on limited or clipped audio the loudest fifth of the 50 ms blocks all sit within
    // kCeilingDb of the highest peak; on an open mix only a few happen to get that close.
    constexpr double kCeilingDb = 0.5;
    constexpr double kOpenShare = 0.15, kLimitedShare = 0.5;
}

void Unlimiter::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    channelCount = std::clamp (numChannels, 0, kMaxChannels);
    lookahead = static_cast<int> (std::lround (kLookaheadMs * 0.001 * sr));
    delaySize = lookahead + 1;
    const auto ringSize = static_cast<size_t> (delaySize * std::max (channelCount, 1));
    delay.assign (ringSize, 0.0);
    delayLow.assign (ringSize, 0.0);
    delayHigh.assign (ringSize, 0.0);
    aFastAtk = coeff (kFastAttackMs, sr);
    aRel = coeff (kReleaseMs, sr);
    aGain = coeff (kGainSmoothMs, sr);
    aAmount = coeff (20.0, sr);
    splitStep = 1.0 / std::max (1.0, kSplitFadeMs * 0.001 * sr);
    aWeight = coeff (kSplitFadeMs, sr);
    settleSamples = static_cast<int> (std::lround (kSplitSettleMs * 0.001 * sr));
    aSquash = coeff (500.0, sr);
    aSlowAtk = coeff (kSlowAttackMs, sr);
    holdSamples = static_cast<int> (std::lround (kHoldMs * 0.001 * sr));
    rmsRing.assign (static_cast<size_t> (std::max (1L, std::lround (kRmsMs * 0.001 * sr))), 0.0);
    blockLen = std::max (1, static_cast<int> (std::lround (0.05 * sr)));
    lengthSet = -1.0;
    reset();
}

void Unlimiter::updateCoefficients() noexcept
{
    // Length sets how long each lift lasts: held for Length after the hit's peak, then let go over a
    // quarter of that.
    const double len = std::clamp (params.lengthMs, 5.0, 50.0);
    if (len != lengthSet)
    {
        liftHoldSamples = static_cast<int> (std::lround (len * 0.001 * sr));
        aLift = coeff (0.25 * len, sr);
        lengthSet = len;
    }
}

void Unlimiter::setSplitFilters (bool clearState) noexcept
{
    // Trapezoidal (TPT) state-variable filter, Butterworth: g = tan (pi f / sr).
    const double g = std::tan (std::numbers::pi * std::min (std::exp (splitLog), kParkFraction * sr) / sr);
    svfA1 = 1.0 / (1.0 + g * (g + kSvfK));
    svfA2 = g * svfA1;
    svfA3 = g * svfA2;
    if (clearState)
        for (auto& ch : svf)
            ch = {};
}

void Unlimiter::updateSplit() noexcept
{
    // The band split joins the path (or leaves it) only while parked; once fully in, it glides to the
    // Focus frequency, and back to the park before it leaves. Kick <-> Snap glides directly (the split
    // stays in, so nothing is cross-faded).
    const bool want = wantsBandPath();
    const double parkLog = std::log (kParkFraction * sr);
    const bool parked = splitLog == parkLog;
    if (want && (parked || split > 0.0)) splitTarget = 1.0;
    if (! want && parked) splitTarget = 0.0;
    const double goal = want && split >= 1.0 ? std::log (params.focus == UnlimiterFocus::snap ? kSplitSnapHz : kSplitKickHz) : parkLog;

    // What the detector listens to: everything (Full), the low band (Kick) or the high band (Snap).
    if (params.focus != heard)
    {
        heard = params.focus;
        settleLeft = settleSamples;
    }
    if (splitLog != goal)
    {
        const double step = kGlidePerHz * std::exp (splitLog) * kSplitUpdate / sr;
        const bool down = goal < splitLog;
        splitLog = down ? std::max (goal, splitLog - step) : std::min (goal, splitLog + step);
        setSplitFilters (false);
        // While the band the detector listens to widens, its level climbs without a hit: no new lifts.
        if ((heard == UnlimiterFocus::kick && ! down) || (heard == UnlimiterFocus::snap && down))
            settleLeft = settleSamples;
    }
}

void Unlimiter::reset() noexcept
{
    std::fill (delay.begin(), delay.end(), 0.0);
    std::fill (delayLow.begin(), delayLow.end(), 0.0);
    std::fill (delayHigh.begin(), delayHigh.end(), 0.0);
    writePos = 0;
    updateCoefficients();
    // Start in the set state: the split in at the Focus frequency, or out and parked.
    const bool want = wantsBandPath();
    splitLog = std::log (! want ? kParkFraction * sr : params.focus == UnlimiterFocus::snap ? kSplitSnapHz : kSplitKickHz);
    setSplitFilters (true);
    split = splitTarget = want ? 1.0 : 0.0;
    liftLow = params.focus == UnlimiterFocus::snap ? 0.0 : 1.0;
    liftHigh = params.focus == UnlimiterFocus::kick ? 0.0 : 1.0;
    heard = params.focus;
    settleLeft = 0;
    splitCountdown = 0;
    fastEnv = slowEnv = 0.0;
    holdLeft = 0;
    std::fill (rmsRing.begin(), rmsRing.end(), 0.0);
    rmsSum = 0.0; rmsPos = 0;
    gainDb = hitLift = 0.0;
    liftLeft = 0;
    amountNow = isNeutral (params) ? 0.0 : std::clamp (params.amount, 0.0, 100.0) / 100.0;
    maxBoost = 0.0;
    blockPeak.fill (0.0); blockEnergy.fill (0.0);
    for (auto& t : truePeak) t.reset();
    blockFill = blockIndex = blocksSeen = 0;
    curPeak = curEnergy = 0.0;
    squash = 0.0;
    // Strength starts from the reading in hand (Auto-Master's, or the last live one) instead of from
    // nothing, so the first bars after a restart or at the start of an export are lifted like the rest.
    const double reading = params.squashDb > 0.0 ? params.squashDb : seedSquash;
    squashNow = reading > 0.0 ? squashScale (reading) : 0.0;
}

void Unlimiter::processFrame (double* frame, int numChannels) noexcept
{
    const int nch = std::min (numChannels, channelCount);
    if (nch <= 0) return;
    if ((writePos & 63) == 0) updateCoefficients();   // cheap enough; keeps parameter moves click-free
    if (--splitCountdown <= 0)
    {
        updateSplit();
        splitCountdown = kSplitUpdate;
    }

    // Band split: the Linkwitz-Riley low band and the rest of the all-pass, so they always add up to the
    // all-pass. Always running (two state-variable filters per channel), so the detector can switch
    // bands without a filter start-up. The detector listens to the focused band only; with Focus Full,
    // to the whole signal.
    const int writeAt = writePos;
    double sc = 0.0, pk = 0.0, e = 0.0;
    for (int c = 0; c < nch; ++c)
    {
        const double x = frame[c];
        auto& st = svf[static_cast<size_t> (c)];
        double band1 = 0.0, low1 = 0.0, lo = 0.0;
        {
            auto& s = st[0];
            const double v3 = x - s.ic2, v1 = svfA1 * s.ic1 + svfA2 * v3, v2 = s.ic2 + svfA2 * s.ic1 + svfA3 * v3;
            s.ic1 = 2.0 * v1 - s.ic1;
            s.ic2 = 2.0 * v2 - s.ic2;
            band1 = v1;
            low1 = v2;
        }
        {
            auto& s = st[1];
            const double v3 = low1 - s.ic2, v1 = svfA1 * s.ic1 + svfA2 * v3, v2 = s.ic2 + svfA2 * s.ic1 + svfA3 * v3;
            s.ic1 = 2.0 * v1 - s.ic1;
            s.ic2 = 2.0 * v2 - s.ic2;
            lo = v2;
        }
        double hi = x - 2.0 * kSvfK * band1 - lo;   // the all-pass (x - 2 k band) minus the low band
        if (! std::isfinite (lo) || ! std::isfinite (hi))   // a NaN / inf input sample must not stall the filters
        {
            st = {};
            lo = hi = 0.0;
        }
        delayLow[static_cast<size_t> (c * delaySize + writeAt)] = lo;
        delayHigh[static_cast<size_t> (c * delaySize + writeAt)] = hi;
        const double y = heard == UnlimiterFocus::kick ? lo : heard == UnlimiterFocus::snap ? hi : x;
        sc = std::max (sc, y * y);
        pk = std::max (pk, truePeak[static_cast<size_t> (c)].process (x));
        e += x * x;
    }
    // Fast follower: up in 0.5 ms, holds its peak for 25 ms (longer than one cycle of a 40 Hz 808, so
    // a held note gives a flat line), then falls. Slow follower: rises toward it over 50 ms, falls
    // with it at once. They differ only while the level is climbing: the start of a hit.
    // Level, not peaks: a limiter leaves every peak at the ceiling, but a hit still carries more
    // energy than what plays between hits. RMS over the last 2.5 ms (running sum, refreshed now and
    // then so rounding can't build up).
    rmsSum += sc - rmsRing[static_cast<size_t> (rmsPos)];
    rmsRing[static_cast<size_t> (rmsPos)] = sc;
    if (++rmsPos >= static_cast<int> (rmsRing.size()))
    {
        rmsPos = 0;
        rmsSum = 0.0;
        for (double v : rmsRing) rmsSum += v;
    }
    sc = std::sqrt (std::max (0.0, rmsSum) / static_cast<double> (rmsRing.size()));
    if (sc > fastEnv) { fastEnv += (sc - fastEnv) * aFastAtk; holdLeft = holdSamples; }
    else if (holdLeft > 0) --holdLeft;
    else fastEnv += (sc - fastEnv) * aRel;
    if (! std::isfinite (fastEnv) || ! std::isfinite (rmsSum))   // a NaN / inf input sample must not stall detection
    {
        fastEnv = slowEnv = 0.0;
        rmsSum = 0.0;
        std::fill (rmsRing.begin(), rmsRing.end(), 0.0);
    }
    slowEnv = fastEnv > slowEnv ? slowEnv + (fastEnv - slowEnv) * aSlowAtk : fastEnv;
    if (settleLeft > 0)
    {
        // The detector's band just changed or is moving: its level jumps say nothing about a hit, so the
        // followers settle on it together and no new lift starts.
        --settleLeft;
        slowEnv = fastEnv;
    }

    const double target = isNeutral (params) ? 0.0 : std::clamp (params.amount, 0.0, 100.0) / 100.0;
    amountNow += (target - amountNow) * aAmount;
    if (amountNow < 1.0e-6 && target == 0.0) amountNow = 0.0;

    // Whole-signal path or band path: a 20 ms linear cross-fade, only ever while the split is parked
    // (Amount 0 and Focus Full use the whole-signal path, which is the exact input when nothing is
    // lifted). Each band's share of the lift glides too (Kick: the low band, Snap: the high band).
    if (split != splitTarget)
        split = splitTarget > split ? std::min (splitTarget, split + splitStep) : std::max (splitTarget, split - splitStep);
    const double lowShare = params.focus == UnlimiterFocus::snap ? 0.0 : 1.0, highShare = params.focus == UnlimiterFocus::kick ? 0.0 : 1.0;
    liftLow += (lowShare - liftLow) * aWeight;
    liftHigh += (highShare - liftHigh) * aWeight;
    if (std::abs (liftLow - lowShare) < 1.0e-6) liftLow = lowShare;
    if (std::abs (liftHigh - highShare) < 1.0e-6) liftHigh = highShare;

    // Only what was lost comes back: full strength on brickwalled audio (Squash kFullSquashDb or
    // less), fading to nothing on an open mix (kOpenSquashDb or more). The reading is Auto-Master's
    // when it set one; otherwise the live meter's, or the last one it had while it warms up. Glides
    // so the strength never jumps.
    const double reading = params.squashDb > 0.0 ? params.squashDb : blocksSeen >= kMinBlocks ? squash : seedSquash;
    const double lost = reading > 0.0 ? squashScale (reading) : 0.0;
    squashNow += (lost - squashNow) * aSquash;

    const double strength = amountNow * squashNow;
    double want = 0.0;
    if (amountNow > 0.0 && slowEnv > kFloor && fastEnv > slowEnv)
        want = liftForRiseDb (20.0 * std::log10 (fastEnv / slowEnv), strength);
    // Each hit's lift comes up with it (0.3 ms), is held for Length after its peak, then lets go over a
    // quarter of Length. Once the hold is over, the lift a hit asks for (which falls as the slow follower
    // catches up) must climb a quarter of the ceiling above its lowest point to start a new hold: a new
    // hit, not the ripple of the one that is fading.
    if (want > hitLift + (liftLeft > 0 ? 0.0 : 0.25 * kMaxBoostDb * strength)) { hitLift = want; liftLeft = liftHoldSamples; }
    else if (liftLeft == 0) hitLift = std::min (hitLift, want);
    const double hold = liftLeft > 0 ? hitLift : 0.0;
    if (liftLeft > 0) --liftLeft;
    gainDb += (hold - gainDb) * (hold > gainDb ? aGain : aLift);
    if (amountNow == 0.0 && gainDb < 1.0e-9) gainDb = 0.0;
    maxBoost = std::max (maxBoost, gainDb);
    const double g = gainDb == 0.0 ? 1.0 : std::pow (10.0, gainDb / 20.0);

    // Lookahead: the gain worked out from the newest sample is applied to audio kLookaheadMs older,
    // so the lift is already up when the hit reaches the output.
    const int readPos = (writeAt + 1) % delaySize;
    for (int c = 0; c < nch; ++c)
    {
        const size_t base = static_cast<size_t> (c * delaySize);
        delay[base + static_cast<size_t> (writeAt)] = frame[c];
        const double whole = delay[base + static_cast<size_t> (readPos)] * g;
        if (split == 0.0)
        {
            frame[c] = whole;
            continue;
        }
        const double banded = delayLow[base + static_cast<size_t> (readPos)] * (1.0 + (g - 1.0) * liftLow)
                            + delayHigh[base + static_cast<size_t> (readPos)] * (1.0 + (g - 1.0) * liftHigh);
        frame[c] = split == 1.0 ? banded : (1.0 - split) * whole + split * banded;
    }
    writePos = (writeAt + 1) % delaySize;

    // Squash meter on the incoming audio.
    curPeak = std::max (curPeak, pk);
    curEnergy += e / nch;
    if (++blockFill >= blockLen)
    {
        const double ms = curEnergy / blockLen;
        if (ms > kBlockSilence)
        {
            blockPeak[static_cast<size_t> (blockIndex)] = curPeak;
            blockEnergy[static_cast<size_t> (blockIndex)] = ms;
            blockIndex = (blockIndex + 1) % kSquashBlocks;
            blocksSeen = std::min (blocksSeen + 1, kSquashBlocks);
            if (blocksSeen >= kMinBlocks)   // 1 s of audio
            {
                squash = squashFromBlocks (blockPeak.data(), blockEnergy.data(), blocksSeen);
                seedSquash = squash;
            }
        }
        blockFill = 0;
        curPeak = curEnergy = 0.0;
    }
}

double Unlimiter::squashFromBlocks (const double* peaks, const double* energies, int numBlocks) noexcept
{
    if (numBlocks <= 0) return 0.0;
    double p = 0.0, sum = 0.0;
    for (int b = 0; b < numBlocks; ++b) { p = std::max (p, peaks[b]); sum += energies[b]; }
    if (p <= 0.0 || sum <= 0.0) return 0.0;
    const double crest = 20.0 * std::log10 (p) - 10.0 * std::log10 (sum / numBlocks);

    // How many of the loudest fifth of the blocks press against the highest peak (the highest itself
    // not counted). A loud 808 can sit close to an open mix's peaks without any limiter; then its
    // peaks still wander and this stays low.
    const int top = std::max (2, numBlocks / 5);
    int near = -1;
    const double edge = p * std::pow (10.0, -kCeilingDb / 20.0);
    for (int b = 0; b < numBlocks; ++b) near += peaks[b] >= edge ? 1 : 0;
    const double share = static_cast<double> (std::min (near, top - 1)) / (top - 1);
    const double limited = std::clamp ((share - kOpenShare) / (kLimitedShare - kOpenShare), 0.0, 1.0);
    return crest + (1.0 - limited) * std::max (0.0, kOpenSquashDb - crest);
}

double Unlimiter::measureSquashDb (const std::vector<std::vector<float>>& channels, double sampleRate)
{
    if (channels.empty() || channels[0].empty()) return 0.0;
    // 50 ms blocks as the live meter sees them (true peak, mean square over the channels).
    const size_t n = channels[0].size(), len = static_cast<size_t> (std::max (1L, std::lround (0.05 * sampleRate))), nb = n / len;
    std::vector<double> peak (nb, 0.0), energy (nb, 0.0);
    for (const auto& ch : channels)
    {
        TruePeakDetector tp;
        for (size_t i = 0; i < nb * len; ++i)
        {
            const double x = ch[i];
            peak[i / len] = std::max (peak[i / len], tp.process (x));
            energy[i / len] += x * x / static_cast<double> (len * channels.size());
        }
    }
    // 3 s windows, half overlapping, of the blocks with signal.
    std::vector<std::pair<double, double>> w;   // (rms dB, squash dB)
    std::vector<double> p, e;
    for (size_t a = 0; a < nb; a += kSquashBlocks / 2)
    {
        p.clear(); e.clear();
        for (size_t b = a; b < std::min (nb, a + kSquashBlocks); ++b)
            if (energy[b] > kBlockSilence) { p.push_back (peak[b]); e.push_back (energy[b]); }
        if (static_cast<int> (p.size()) >= kMinBlocks)
        {
            double sum = 0.0;
            for (double v : e) sum += v;
            w.emplace_back (10.0 * std::log10 (sum / static_cast<double> (e.size())), squashFromBlocks (p.data(), e.data(), static_cast<int> (p.size())));
        }
        if (a + kSquashBlocks >= nb) break;
    }
    if (w.empty()) return 0.0;
    // Louder windows only (within 10 dB of the loudest): a quiet intro says little about the master.
    double loudest = -400.0;
    for (auto& x : w) loudest = std::max (loudest, x.first);
    std::vector<double> s;
    for (auto& x : w) if (x.first > loudest - 10.0) s.push_back (x.second);
    std::nth_element (s.begin(), s.begin() + static_cast<long> (s.size() / 2), s.end());
    return s[s.size() / 2];
}

} // namespace polisher

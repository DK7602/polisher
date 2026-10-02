#include "polisher/Unlimiter.h"
#include "polisher/FilterDesign.h"

namespace polisher {

namespace {
    double coeff (double ms, double sr) { return 1.0 - std::exp (-1.0 / (ms * 0.001 * sr)); }
    constexpr double kFastAttackMs = 0.5;
    constexpr double kReleaseMs = 80.0;
    constexpr double kGainSmoothMs = 0.3;
    constexpr double kFloor = 1.0e-4;          // -80 dBFS: quieter than this is never lifted
    constexpr double kSlope = 6.0;             // dB of lift per dB the hit stands out, at 100 % (a beat pushed 14 dB
                                               // into a fast limiter gets about 3/4 of its hit contrast back)
    constexpr double kDeadDb = 1.5;            // rises smaller than this are the music's own ripple
    constexpr double kHoldMs = 25.0;
    constexpr double kSlowAttackMs = 50.0;
    constexpr double kRmsMs = 5.0;
}

void Unlimiter::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    channelCount = std::clamp (numChannels, 0, kMaxChannels);
    lookahead = static_cast<int> (std::lround (kLookaheadMs * 0.001 * sr));
    delaySize = lookahead + 1;
    delay.assign (static_cast<size_t> (delaySize * std::max (channelCount, 1)), 0.0);
    aFastAtk = coeff (kFastAttackMs, sr);
    aRel = coeff (kReleaseMs, sr);
    aGain = coeff (kGainSmoothMs, sr);
    aSlowAtk = coeff (kSlowAttackMs, sr);
    aAmount = coeff (20.0, sr);
    aSquash = coeff (500.0, sr);
    holdSamples = static_cast<int> (std::lround (kHoldMs * 0.001 * sr));
    rmsRing.assign (static_cast<size_t> (std::max (1L, std::lround (kRmsMs * 0.001 * sr))), 0.0);
    blockLen = std::max (1, static_cast<int> (std::lround (0.05 * sr)));
    lengthSet = -1.0;
    updateCoefficients();
    reset();
}

void Unlimiter::updateCoefficients() noexcept
{
    const double len = std::clamp (params.lengthMs, 5.0, 50.0);
    if (len != lengthSet) { aLift = coeff (len, sr); lengthSet = len; }
    if (params.focus != scFocus)
    {
        scFocus = params.focus;
        for (auto& f : scFilter) for (auto& b : f) b.reset();
    }
    const auto c = scFocus == UnlimiterFocus::kick ? design::butterworth (false, 150.0, sr)
                 : scFocus == UnlimiterFocus::snap ? design::butterworth (true, 1000.0, sr) : design::Coeffs {};
    for (auto& f : scFilter) for (auto& b : f) design::apply (b, c);
}

void Unlimiter::reset() noexcept
{
    std::fill (delay.begin(), delay.end(), 0.0);
    writePos = 0;
    scFocus = params.focus;
    for (auto& f : scFilter) for (auto& b : f) b.reset();
    updateCoefficients();
    fastEnv = slowEnv = 0.0;
    holdLeft = 0;
    std::fill (rmsRing.begin(), rmsRing.end(), 0.0);
    rmsSum = 0.0; rmsPos = 0;
    gainDb = 0.0;
    amountNow = isNeutral (params) ? 0.0 : std::clamp (params.amount, 0.0, 100.0) / 100.0;   // start in the set state
    maxBoost = 0.0;
    blockPeak.fill (0.0); blockEnergy.fill (0.0);
    blockFill = blockIndex = blocksSeen = 0;
    curPeak = curEnergy = 0.0;
    squash = 0.0;
    squashNow = 0.0;
}

void Unlimiter::processFrame (double* frame, int numChannels) noexcept
{
    const int nch = std::min (numChannels, channelCount);
    if (nch <= 0) return;
    if ((writePos & 63) == 0) updateCoefficients();   // cheap enough; keeps parameter moves click-free

    // Detector: the loudest channel after the Focus filter.
    double sc = 0.0, pk = 0.0, e = 0.0;
    for (int c = 0; c < nch; ++c)
    {
        const double x = frame[c];
        double y = x;
        if (scFocus != UnlimiterFocus::full)
            y = scFilter[c][1].process (scFilter[c][0].process (x));
        sc = std::max (sc, y * y);
        pk = std::max (pk, std::abs (x));
        e += x * x;
    }
    // Fast follower: up in 0.5 ms, holds its peak for 25 ms (longer than one cycle of a 40 Hz 808, so
    // a held note gives a flat line), then falls. Slow follower: rises toward it over Length, falls
    // with it at once. They differ only while the level is climbing: the start of a hit.
    // Level, not peaks: a limiter leaves every peak at the ceiling, but a hit still carries more
    // energy than what plays between hits. RMS over the last 10 ms (running sum, refreshed now and
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
    slowEnv = fastEnv > slowEnv ? slowEnv + (fastEnv - slowEnv) * aSlowAtk : fastEnv;

    const double target = isNeutral (params) ? 0.0 : std::clamp (params.amount, 0.0, 100.0) / 100.0;
    amountNow += (target - amountNow) * aAmount;
    if (amountNow < 1.0e-6 && target == 0.0) amountNow = 0.0;

    // Only what was lost comes back: full strength on brickwalled audio (Squash kFullSquashDb or
    // less), fading to nothing on an open mix (kOpenSquashDb or more). Silent until the meter has
    // heard a second of audio; glides so the strength never jumps.
    const double lost = blocksSeen >= kMinBlocks ? squashScale (squash) : 0.0;
    squashNow += (lost - squashNow) * aSquash;

    double want = 0.0;
    if (amountNow > 0.0 && slowEnv > kFloor && fastEnv > slowEnv)
        want = std::min (kMaxBoostDb, kSlope * amountNow * squashNow * std::max (0.0, 20.0 * std::log10 (fastEnv / slowEnv) - kDeadDb));
    // The lift comes up with the hit (0.3 ms) and lets go over Length, as long as the hit lasts.
    gainDb += (want - gainDb) * (want > gainDb ? aGain : aLift);
    if (amountNow == 0.0 && gainDb < 1.0e-9) gainDb = 0.0;
    maxBoost = std::max (maxBoost, gainDb);
    const double g = gainDb == 0.0 ? 1.0 : std::pow (10.0, gainDb / 20.0);

    // Lookahead: the gain worked out from the newest sample is applied to audio kLookaheadMs older,
    // so the lift is already up when the hit reaches the output.
    const int readPos = (writePos + 1) % delaySize;
    for (int c = 0; c < nch; ++c)
    {
        double* d = delay.data() + c * delaySize;
        d[writePos] = frame[c];
        frame[c] = d[readPos] * g;
    }
    writePos = (writePos + 1) % delaySize;

    // Squash meter on the incoming audio.
    curPeak = std::max (curPeak, pk);
    curEnergy += e / nch;
    if (++blockFill >= blockLen)
    {
        const double ms = curEnergy / blockLen;
        if (ms > 1.0e-6)   // blocks above -60 dBFS RMS: silence and fades don't count
        {
            blockPeak[static_cast<size_t> (blockIndex)] = curPeak;
            blockEnergy[static_cast<size_t> (blockIndex)] = ms;
            blockIndex = (blockIndex + 1) % kSquashBlocks;
            blocksSeen = std::min (blocksSeen + 1, kSquashBlocks);
            if (blocksSeen >= kMinBlocks)   // 1 s of audio
            {
                double p = 0.0, sum = 0.0;
                for (int b = 0; b < blocksSeen; ++b) { p = std::max (p, blockPeak[static_cast<size_t> (b)]); sum += blockEnergy[static_cast<size_t> (b)]; }
                squash = 20.0 * std::log10 (p) - 10.0 * std::log10 (sum / blocksSeen);
            }
        }
        blockFill = 0;
        curPeak = curEnergy = 0.0;
    }
}

double Unlimiter::measureSquashDb (const std::vector<std::vector<float>>& channels, double sampleRate)
{
    if (channels.empty() || channels[0].empty()) return 0.0;
    const size_t n = channels[0].size(), win = static_cast<size_t> (3.0 * sampleRate), hop = win / 2;
    std::vector<std::pair<double, double>> w;   // (rms dB, squash dB)
    for (size_t a = 0; a + win <= std::max (n, win); a += hop)
    {
        double p = 0.0, e = 0.0;
        const size_t b = std::min (n, a + win);
        for (const auto& ch : channels)
            for (size_t i = a; i < b; ++i) { p = std::max (p, std::abs (static_cast<double> (ch[i]))); e += static_cast<double> (ch[i]) * ch[i]; }
        e /= static_cast<double> ((b - a) * channels.size());
        if (e > 1.0e-6) w.emplace_back (10.0 * std::log10 (e), 20.0 * std::log10 (p) - 10.0 * std::log10 (e));
        if (b == n) break;
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

#include "polisher/StemEQ.h"

#include "StemNetWeights.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace polisher {

namespace sn = stemnet;
static_assert (sn::kIn == StemNet::kIn && sn::kBands == StemNet::kBands && sn::kStems == kStems,
               "StemNetWeights.h does not match StemNet - re-run tools/stem/export.py");
static_assert (sn::kTaps == 2 * StemNet::kLookahead + 1, "the model's look-ahead must match kLookahead");

namespace {
inline float sigmoid (float v) noexcept { return 1.0f / (1.0f + std::exp (-v)); }
}

// ---------------------------------------------------------------------------------------------
// StemNet

StemNet::StemNet()
    : hist (static_cast<size_t> (sn::kTaps * kIn)), h (static_cast<size_t> (sn::kHidden)), x (static_cast<size_t> (sn::kHidden)),
      gi (static_cast<size_t> (3 * sn::kHidden)), gh (static_cast<size_t> (3 * sn::kHidden)),
      logits (static_cast<size_t> (kStems * kBands))
{
}

void StemNet::reset() noexcept
{
    std::fill (hist.begin(), hist.end(), 0.0f);
    std::fill (h.begin(), h.end(), 0.0f);
    head = filled = 0;
}

bool StemNet::push (const float* features, float* out) noexcept
{
    constexpr int H = sn::kHidden, T = sn::kTaps;
    float* slot = hist.data() + head * kIn;
    for (int i = 0; i < kIn; ++i)
        slot[i] = sn::normW[i] * features[i] + sn::normB[i];          // learned per-feature scale / shift
    head = (head + 1) % T;
    if (filled < T) ++filled;
    if (filled < T)
        return false;

    // Conv over the last T frames (oldest first: the ring's head is the oldest), then ReLU.
    std::array<const float*, sn::kTaps> frame {};
    for (int j = 0; j < T; ++j) frame[static_cast<size_t> (j)] = hist.data() + ((head + j) % T) * kIn;
    for (int o = 0; o < H; ++o)
    {
        float acc = sn::convB[o];
        const float* w = sn::convW + o * kIn * T;                      // [out][in][tap]
        for (int i = 0; i < kIn; ++i, w += T)
            for (int j = 0; j < T; ++j)
                acc += w[j] * frame[static_cast<size_t> (j)][i];
        x[static_cast<size_t> (o)] = std::max (acc, 0.0f);
    }

    // GRU step (PyTorch gate order r, z, n).
    for (int r = 0; r < 3 * H; ++r)
    {
        float a = sn::gruBih[r], b = sn::gruBhh[r];
        const float* wi = sn::gruWih + r * H;
        const float* wh = sn::gruWhh + r * H;
        for (int k = 0; k < H; ++k)
        {
            a += wi[k] * x[static_cast<size_t> (k)];
            b += wh[k] * h[static_cast<size_t> (k)];
        }
        gi[static_cast<size_t> (r)] = a;
        gh[static_cast<size_t> (r)] = b;
    }
    for (int k = 0; k < H; ++k)
    {
        const auto K = static_cast<size_t> (k);
        const float rg = sigmoid (gi[K] + gh[K]);
        const float zg = sigmoid (gi[K + H] + gh[K + H]);
        const float ng = std::tanh (gi[K + 2 * H] + rg * gh[K + 2 * H]);
        h[K] = (1.0f - zg) * ng + zg * h[K];
    }

    // Dense layer, then softmax over the four stems in every band.
    for (int r = 0; r < kStems * kBands; ++r)
    {
        float a = sn::outB[r];
        const float* w = sn::outW + r * H;
        for (int k = 0; k < H; ++k) a += w[k] * h[static_cast<size_t> (k)];
        logits[static_cast<size_t> (r)] = a;
    }
    for (int b = 0; b < kBands; ++b)
    {
        float m = logits[static_cast<size_t> (b)];
        for (int s = 1; s < kStems; ++s) m = std::max (m, logits[static_cast<size_t> (s * kBands + b)]);
        float e[kStems], sum = 0.0f;
        for (int s = 0; s < kStems; ++s) sum += (e[s] = std::exp (logits[static_cast<size_t> (s * kBands + b)] - m));
        for (int s = 0; s < kStems; ++s) out[s * kBands + b] = e[s] / sum;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// StemEQ

bool StemEQ::isNeutral (const StemEQParams& p) noexcept
{
    if (! p.enabled) return true;
    for (double g : p.gainDb)
        if (std::abs (g) >= 0.05) return false;
    return true;
}

void StemEQ::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    channelCount = std::clamp (numChannels, 1, kMaxChannels);
    fftSize = fftSizeFor (sampleRate);
    hopSize = fftSize / 4;
    latency = latencyFor (sampleRate);
    const int N = fftSize, bins = N / 2 + 1;
    const auto n = static_cast<size_t> (N);

    window.assign (n, 0.0);
    for (int i = 0; i < N; ++i) window[static_cast<size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / N);
    twiddle.assign (n / 2, {});
    for (int k = 0; k < N / 2; ++k) twiddle[static_cast<size_t> (k)] = std::polar (1.0, -2.0 * std::numbers::pi * k / N);
    bitrev.assign (n, 0);
    int bits = 0;
    while ((1 << bits) < N) ++bits;
    for (int i = 0; i < N; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
        bitrev[static_cast<size_t> (i)] = r;
    }

    // Triangular ERB bands, columns normalised to 1 (tools/stem/common.py: band_centres, tri_weights).
    constexpr int B = StemNet::kBands;
    const auto erb = [] (double f) { return 21.4 * std::log10 (1.0 + 0.00437 * f); };
    const double e0 = erb (20.0), e1 = erb (std::min (sampleRate / 2.0, 20000.0));
    std::array<double, B> cf {};
    for (int b = 0; b < B; ++b)
        cf[static_cast<size_t> (b)] = (std::pow (10.0, (e0 + (e1 - e0) * b / (B - 1)) / 21.4) - 1.0) / 0.00437;
    bandCf.assign (cf.begin(), cf.end());
    bandW.assign (static_cast<size_t> (B * bins), 0.0);
    for (int k = 0; k < bins; ++k)
    {
        const double f = k * sampleRate / N;
        double col = 0.0;
        for (int b = 0; b < B; ++b)
        {
            const auto i = static_cast<size_t> (b);
            double w = b == 0 ? (f <= cf[0] ? 1.0 : 0.0)
                              : std::clamp ((f - cf[i - 1]) / (cf[i] - cf[i - 1]), 0.0, 1.0) * (f <= cf[i] ? 1.0 : 0.0);
            w += b == B - 1 ? (f > cf[i] ? 1.0 : 0.0)
                            : std::clamp ((cf[i + 1] - f) / (cf[i + 1] - cf[i]), 0.0, 1.0) * (f > cf[i] ? 1.0 : 0.0);
            bandW[i * bins + static_cast<size_t> (k)] = w;
            col += w;
        }
        for (int b = 0; b < B; ++b) bandW[static_cast<size_t> (b * bins + k)] /= col;
    }
    bandLo.assign (B, bins);
    bandHi.assign (B, 0);
    for (int b = 0; b < B; ++b)
        for (int k = 0; k < bins; ++k)
            if (bandW[static_cast<size_t> (b * bins + k)] > 0.0)
            {
                bandLo[static_cast<size_t> (b)] = std::min (bandLo[static_cast<size_t> (b)], k);
                bandHi[static_cast<size_t> (b)] = k + 1;
            }

    for (int c = 0; c < 2; ++c)
    {
        inRing[static_cast<size_t> (c)].assign (n, 0.0);
        acc[static_cast<size_t> (c)].assign (n, 0.0);
        outQueue[static_cast<size_t> (c)].assign (static_cast<size_t> (hopSize), 0.0);
        spectra[static_cast<size_t> (c)].assign (4 * n, {});
    }
    work.assign (n, {});
    binGain.assign (static_cast<size_t> (bins), 1.0);
    for (auto* v : { &power, &levelDb, &rtCut, &rtTarget, &rtSmooth, &tsBin }) v->assign (static_cast<size_t> (bins), 0.0);
    rtLin.assign (static_cast<size_t> (bins), 1.0);
    prefix.assign (static_cast<size_t> (bins + 1), 0.0);
    framesPerSecond = sampleRate / hopSize;
    scratch.assign (n, 0.0);
    for (auto& d : dry) d.clear();
    for (int c = 0; c < channelCount; ++c) dry[static_cast<size_t> (c)].assign (static_cast<size_t> (latency), 0.0);

    mixCoeff = 1.0 - std::exp (-1.0 / (0.020 * sampleRate));
    gainCoeff = 1.0 - std::exp (-hopSize / (0.030 * sampleRate));
    reset();
}

void StemEQ::reset() noexcept
{
    for (auto* v : { &inRing[0], &inRing[1], &acc[0], &acc[1], &outQueue[0], &outQueue[1] })
        std::fill (v->begin(), v->end(), 0.0);
    for (auto& s : spectra) std::fill (s.begin(), s.end(), std::complex<double> {});
    for (auto& d : dry) std::fill (d.begin(), d.end(), 0.0);
    net.reset();
    shares.fill (0.25f);
    stemEnergy.fill (0.0);
    inPos = queuePos = dryPos = 0;
    frameCount = analysed = 0;
    for (auto* v : { &rtCut, &rtTarget, &rtSmooth, &tsBin }) std::fill (v->begin(), v->end(), 0.0);
    std::fill (rtLin.begin(), rtLin.end(), 1.0);
    tsShort.fill (0.0); tsLong.fill (0.0); tsGain.fill (0.0);
    tsLevelShort = tsLevelLong = 0.0;
    tsFrames = 0;
    rtDeepest = rtDeepestHz = tsLargest = tsLargestHz = 0.0;
    statPower = statCut = statCutLow = statCutHigh = statSteady = 0.0;
    statSteadyFrames = 0;
    // Start fully in the set state.
    running = ! allNeutral();
    mix = running ? 1.0 : 0.0;
    const bool stemOn = ! isNeutral (params);
    for (int s = 0; s < kStems; ++s)
        gain[static_cast<size_t> (s)] = stemOn ? std::pow (10.0, std::clamp (params.gainDb[static_cast<size_t> (s)], -kMaxGainDb, kMaxGainDb) / 20.0) : 1.0;
    rtAmount = isNeutral (rtParams) ? 0.0 : std::clamp (rtParams.amount, 0.0, 100.0) / 100.0;
    tsAmount = isNeutral (tsParams) ? 0.0 : std::clamp (tsParams.amount, 0.0, 100.0) / 100.0;
}

void StemEQ::fft (std::complex<double>* d, bool inverse) const noexcept
{
    const int n = fftSize;
    for (int i = 0; i < n; ++i)
    {
        const int j = bitrev[static_cast<size_t> (i)];
        if (i < j) std::swap (d[i], d[j]);
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2, step = n / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < half; ++k)
            {
                auto w = twiddle[static_cast<size_t> (k * step)];
                if (inverse) w = std::conj (w);
                const auto u = d[i + k], v = d[i + k + half] * w;
                d[i + k] = u + v;
                d[i + k + half] = u - v;
            }
    }
}

void StemEQ::bandEnergies (const std::complex<double>* a, const std::complex<double>* b, double sign, float* out) const noexcept
{
    // Energy of 0.5 (a + sign b) per band: mid (sign +1) or side (sign -1) of the two channels.
    const int bins = fftSize / 2 + 1;
    for (int band = 0; band < StemNet::kBands; ++band)
    {
        double e = 0.0;
        const double* w = bandW.data() + band * bins;
        for (int k = bandLo[static_cast<size_t> (band)]; k < bandHi[static_cast<size_t> (band)]; ++k)
            e += w[k] * std::norm (0.5 * (a[k] + sign * b[k]));
        out[band] = static_cast<float> (std::log10 (e + 1.0e-9));
    }
}

void StemEQ::computeFeatures (const double* left, const double* right, float* features) noexcept
{
    const int N = fftSize;
    auto* L = spectra[0].data();   // borrows slot 0 of the spectrum store (tests / offline only)
    auto* R = spectra[1].data();
    for (int i = 0; i < N; ++i) L[i] = { left[i] * window[static_cast<size_t> (i)], 0.0 };
    for (int i = 0; i < N; ++i) R[i] = { right[i] * window[static_cast<size_t> (i)], 0.0 };
    fft (L, false);
    fft (R, false);
    bandEnergies (L, R, 1.0, features);
    bandEnergies (L, R, -1.0, features + StemNet::kBands);
}

namespace {
// 1 inside [lo, hi] Hz, fading to 0 over half an octave outside.
inline double focusWeight (double f, double lo, double hi) noexcept
{
    if (f <= 0.0) return 0.0;
    return std::clamp (1.0 + 2.0 * std::log2 (f / lo), 0.0, 1.0) * std::clamp (1.0 + 2.0 * std::log2 (hi / f), 0.0, 1.0);
}
}

void StemEQ::resonanceGains (const std::complex<double>* L, const std::complex<double>* R) noexcept
{
    const int half = fftSize / 2, bins = half + 1;
    const double binHz = sampleRate / fftSize;
    rtAmount += ((isNeutral (rtParams) ? 0.0 : std::clamp (rtParams.amount, 0.0, 100.0) / 100.0) - rtAmount) * gainCoeff;
    const double thr = 12.0 - 9.0 * std::clamp (rtParams.sensitivity, 0.0, 100.0) / 100.0;
    // Detect on power smoothed over ~40 ms: a resonance rings on, random flicker averages out.
    const double aP = 1.0 - std::exp (-hopSize / (0.040 * sampleRate));
    double pmax = 0.0;
    for (int k = 0; k < bins; ++k)
    {
        const auto K = static_cast<size_t> (k);
        const double g = binGain[K];
        rtSmooth[K] += ((std::norm (L[k]) + std::norm (R[k])) * g * g - rtSmooth[K]) * aP;
        if (rtSmooth[K] < 1.0e-30) rtSmooth[K] = 0.0;   // no denormals in silence (offline callers too)
        power[K] = rtSmooth[K];
        pmax = std::max (pmax, power[K]);
        levelDb[K] = 10.0 * std::log10 (power[K] + 1.0e-20);
        prefix[K + 1] = prefix[K] + power[K];
    }
    const double floorDb = 10.0 * std::log10 (pmax + 1.0e-20) - 60.0;
    double lo = 80.0, hi = 16000.0;
    if (rtParams.focus == ResonanceFocus::boxiness) { lo = 150.0; hi = 800.0; }
    else if (rtParams.focus == ResonanceFocus::harshness) { lo = 1500.0; hi = 8000.0; }

    // A peak is measured against the average power around it (about +/- 0.3 octave, itself left out).
    for (int k = 0; k < bins; ++k)
    {
        const auto K = static_cast<size_t> (k);
        rtTarget[K] = 0.0;
        if (k == 0 || k == half || levelDb[K] < floorDb) continue;
        const int w = std::max (4, static_cast<int> (k * 0.2));
        const int a = std::max (1, k - w), b = std::min (half - 1, k + w);
        if (b - a < 2) continue;
        const double env = 10.0 * std::log10 ((prefix[static_cast<size_t> (b + 1)] - prefix[static_cast<size_t> (a)] - power[K]) / (b - a) + 1.0e-20);
        const double excess = levelDb[K] - env - thr;
        if (excess > 0.0)
            rtTarget[K] = -std::min (12.0, rtAmount * excess * focusWeight (k * binHz, lo, hi));
    }
    // A ringing tone covers its neighbours too (the window's main lobe): cut them alike.
    const double attack = 1.0 - std::exp (-hopSize / (0.003 * sampleRate));
    const double release = 1.0 - std::exp (-hopSize / (std::clamp (rtParams.releaseMs, 20.0, 300.0) * 0.001 * sampleRate));
    for (int k = 0; k < bins; ++k)
    {
        const auto K = static_cast<size_t> (k);
        double t = rtTarget[K];
        if (k > 0) t = std::min (t, rtTarget[K - 1]);
        if (k < half) t = std::min (t, rtTarget[K + 1]);
        double& g = rtCut[K];
        g += (t - g) * (t < g ? attack : release);
        if (g > -1.0e-6) g = 0.0;
        rtLin[K] = g == 0.0 ? 1.0 : std::exp (g * (std::numbers::ln10 / 20.0));
        if (g < 0.0)
        {
            const double f = k * binHz, c = -g * power[K];
            statCut += c;
            if (f >= 150.0 && f <= 800.0) statCutLow += c;
            else if (f >= 1500.0 && f <= 8000.0) statCutHigh += c;
        }
        statPower += power[K];
        if (g < rtDeepest) { rtDeepest = g; rtDeepestHz = k * binHz; }
    }
}

void StemEQ::toneSteadyGains (const std::complex<double>* L, const std::complex<double>* R) noexcept
{
    constexpr int B = StemNet::kBands;
    const int bins = fftSize / 2 + 1;
    tsAmount += ((isNeutral (tsParams) ? 0.0 : std::clamp (tsParams.amount, 0.0, 100.0) / 100.0) - tsAmount) * gainCoeff;

    // Band energies after the Stem EQ move and the resonance cuts.
    std::array<double, B> e {};
    for (int band = 0; band < B; ++band)
    {
        const double* w = bandW.data() + band * bins;
        double sum = 0.0;
        for (int k = bandLo[static_cast<size_t> (band)]; k < bandHi[static_cast<size_t> (band)]; ++k)
        {
            const auto K = static_cast<size_t> (k);
            const double g = binGain[K] * rtLin[K];
            sum += w[k] * (std::norm (L[k]) + std::norm (R[k])) * g * g;
        }
        e[static_cast<size_t> (band)] = sum;
    }
    static constexpr double kShortS[3] = { 1.0, 0.4, 0.15 };
    const double shortS = kShortS[std::clamp (tsParams.speed, 0, 2)];
    const double aS = 1.0 - std::exp (-1.0 / (shortS * framesPerSecond));
    // The top end (hats, cymbals, S's) comes and goes from beat to beat in most songs; short-term
    // cuts there land on the loud moments and dull the song. Above ~2.5 kHz the short-term average
    // stretches to 2 s, the dead band widens to 3 dB and cuts are capped like boosts, so only a
    // real drift (a section that stays harsher) is touched. topW: 0 below 2.5 kHz, 1 above 5 kHz.
    auto topW = [this] (size_t b) { return std::clamp ((bandCf[b] - 2500.0) / 2500.0, 0.0, 1.0); };
    // Both averages are of dB levels: a loud new passage moves the slow reference by its dB, not
    // by its (much bigger) energy, so the song's own balance stays put for ~10 s.
    double eTotal = 0.0;
    for (double v : e) eTotal += v;
    const double totalDb = 10.0 * std::log10 (eTotal + 1.0e-20);
    // Silence and near-silence (stopped transport, a gap, a fade) must not teach the reference
    // anything: the averages start at the first frame with signal, and the slow reference and its
    // clock freeze while the frame is 30 dB or more under the song.
    const bool signal = totalDb > -80.0 && (tsFrames == 0 || totalDb > tsLevelLong - 30.0);
    if (tsFrames == 0 && signal)
    {
        for (size_t b = 0; b < static_cast<size_t> (B); ++b)
            tsShort[b] = tsLong[b] = std::max (10.0 * std::log10 (e[b] + 1.0e-20), totalDb - 90.0);
        tsLevelShort = tsLevelLong = totalDb;
    }
    if (tsFrames > 0 || signal)
    {
        const bool warming = tsFrames < static_cast<long> (2.0 * framesPerSecond);   // both averages follow fast at first
        const double aL = warming ? aS : 1.0 - std::exp (-1.0 / (10.0 * framesPerSecond));
        for (size_t b = 0; b < static_cast<size_t> (B); ++b)
        {
            const double db = std::max (10.0 * std::log10 (e[b] + 1.0e-20), totalDb - 90.0);   // empty bands: 90 dB under the frame
            const double aB = topW (b) > 0.0 && ! warming ? 1.0 - std::exp (-1.0 / ((shortS + (2.0 - shortS) * topW (b)) * framesPerSecond)) : aS;
            if (! signal) continue;   // band balances freeze in silence (the slow top bands would lag after it)
            tsShort[b] += (db - tsShort[b]) * aB;
            tsLong[b] += (db - tsLong[b]) * aL;
        }
        tsLevelShort += (totalDb - tsLevelShort) * aS;
        if (signal) { tsLevelLong += (totalDb - tsLevelLong) * aL; ++tsFrames; }
    }
    double maxLong = -400.0;
    for (double v : tsLong) maxLong = std::max (maxLong, v);

    // Drift of each band from the song's own balance, with the overall level change taken out.
    // Quiet passages (more than 15 dB under the song) are left alone: a breakdown is not a drift.
    std::array<double, B> t {};
    const bool active = tsAmount > 1.0e-4 && tsLevelLong > -150.0 && tsLevelShort > tsLevelLong - 15.0
                        && tsFrames > static_cast<long> (framesPerSecond);
    if (active)
    {
        std::array<double, B> d {};
        double wsum = 0.0, mean = 0.0;
        for (size_t b = 0; b < static_cast<size_t> (B); ++b)
        {
            if (tsLong[b] < maxLong - 60.0) continue;
            d[b] = tsLong[b] - tsShort[b];
            const double w = std::pow (10.0, (tsLong[b] - maxLong) / 20.0);   // louder bands weigh more
            mean += w * d[b]; wsum += w;
        }
        mean = wsum > 0.0 ? mean / wsum : 0.0;
        const double range = std::clamp (tsParams.rangeDb, 1.0, 6.0);
        for (size_t b = 0; b < static_cast<size_t> (B); ++b)
        {
            if (tsLong[b] < maxLong - 60.0) continue;
            const double x = d[b] - mean;
            const double db = 1.0 + 2.0 * topW (b);                                                // 1 dB dead band (3 dB up top)
            const double dead = x > 0.0 ? std::max (0.0, x - db) : std::min (0.0, x + db);
            t[b] = std::clamp (tsAmount * dead, -range * (1.0 - 0.5 * topW (b)), range * 0.5);
        }
    }
    const double aG = 1.0 - std::exp (-1.0 / (0.08 * framesPerSecond));
    for (size_t b = 0; b < static_cast<size_t> (B); ++b)
    {
        const double sm = (t[b > 0 ? b - 1 : b] + 2.0 * t[b] + t[b + 1 < static_cast<size_t> (B) ? b + 1 : b]) / 4.0;
        tsGain[b] += (sm - tsGain[b]) * aG;
        if (std::abs (tsGain[b]) < 1.0e-6) tsGain[b] = 0.0;
        if (std::abs (tsGain[b]) > std::abs (tsLargest)) { tsLargest = tsGain[b]; tsLargestHz = bandCf[b]; }
    }
    if (active)
    {
        double num = 0.0, den = 0.0;
        for (size_t b = 0; b < static_cast<size_t> (B); ++b) { num += e[b] * std::abs (tsGain[b]); den += e[b]; }
        if (den > 0.0) { statSteady += num / den; ++statSteadyFrames; }
    }
    std::fill (tsBin.begin(), tsBin.end(), 0.0);
    for (int band = 0; band < B; ++band)
    {
        const double g = tsGain[static_cast<size_t> (band)];
        if (g == 0.0) continue;
        const double* w = bandW.data() + band * bins;
        for (int k = bandLo[static_cast<size_t> (band)]; k < bandHi[static_cast<size_t> (band)]; ++k)
            tsBin[static_cast<size_t> (k)] += w[k] * g;
    }
}

SpectralStats StemEQ::takeSpectralStats() noexcept
{
    SpectralStats s;
    if (statPower > 0.0) s.resonanceCutDb = -statCut / statPower;
    if (statCut > 0.0) { s.boxinessShare = statCutLow / statCut; s.harshnessShare = statCutHigh / statCut; }
    if (statSteadyFrames > 0) s.steadyMoveDb = statSteady / static_cast<double> (statSteadyFrames);
    statPower = statCut = statCutLow = statCutHigh = statSteady = 0.0;
    statSteadyFrames = 0;
    return s;
}

void StemEQ::hop() noexcept
{
    const int N = fftSize, half = N / 2, bins = half + 1;
    const auto slot = static_cast<size_t> (frameCount % 4) * static_cast<size_t> (N);
    auto* L = spectra[0].data() + slot;
    auto* R = spectra[1].data() + slot;

    // Both channels in one complex FFT (L real, R imaginary), then split.
    for (int i = 0; i < N; ++i)
    {
        const auto r = static_cast<size_t> ((inPos + i) % N);   // inPos is the oldest sample
        const double w = window[static_cast<size_t> (i)];
        work[static_cast<size_t> (i)] = { inRing[0][r] * w, inRing[1][r] * w };
    }
    fft (work.data(), false);
    for (int k = 0; k < N; ++k)
    {
        const auto z = work[static_cast<size_t> (k)], zc = std::conj (work[static_cast<size_t> ((N - k) % N)]);
        L[k] = 0.5 * (z + zc);
        R[k] = std::complex<double> (0.0, -0.5) * (z - zc);
    }
    bandEnergies (L, R, 1.0, feat.data());
    bandEnergies (L, R, -1.0, feat.data() + StemNet::kBands);
    const bool ready = net.push (feat.data(), shares.data());
    if (ready)
    {
        ++analysed;
        // Meter: credit each band's energy to the stems by their shares (the shares are 3 hops
        // older than these energies, which doesn't matter for a meter averaged over a second).
        for (int band = 0; band < StemNet::kBands; ++band)
        {
            // Minus the 1e-9 floor the features carry, so silence credits nothing to any stem.
            const double e = std::max (0.0, std::pow (10.0, static_cast<double> (feat[static_cast<size_t> (band)])) - 1.0e-9)
                           + std::max (0.0, std::pow (10.0, static_cast<double> (feat[static_cast<size_t> (band + StemNet::kBands)])) - 1.0e-9);
            for (int st = 0; st < kStems; ++st)
                stemEnergy[static_cast<size_t> (st)] += e * shares[static_cast<size_t> (st * StemNet::kBands + band)];
        }
    }

    std::array<double, kStems> target {};
    const bool stemOn = ! isNeutral (params);
    for (int s = 0; s < kStems; ++s)
    {
        const auto i = static_cast<size_t> (s);
        target[i] = stemOn ? std::pow (10.0, std::clamp (params.gainDb[i], -kMaxGainDb, kMaxGainDb) / 20.0) : 1.0;
        gain[i] += (target[i] - gain[i]) * gainCoeff;
    }

    // Re-synthesise the frame 3 hops back (the one the model just scored): gain per band = stem
    // gains mixed by the shares, spread over the bins. Until the model has 7 frames, gain 1.
    if (frameCount >= StemNet::kLookahead)
    {
        std::fill (binGain.begin(), binGain.end(), 0.0);
        for (int band = 0; band < StemNet::kBands; ++band)
        {
            double g = 0.0;
            if (ready)
                for (int s = 0; s < kStems; ++s) g += gain[static_cast<size_t> (s)] * shares[static_cast<size_t> (s * StemNet::kBands + band)];
            else
                g = 1.0;
            const double* w = bandW.data() + band * bins;
            for (int k = bandLo[static_cast<size_t> (band)]; k < bandHi[static_cast<size_t> (band)]; ++k)
                binGain[static_cast<size_t> (k)] += w[k] * g;
        }
        const auto old = static_cast<size_t> ((frameCount - StemNet::kLookahead) % 4) * static_cast<size_t> (N);
        const auto* Lo = spectra[0].data() + old;
        const auto* Ro = spectra[1].data() + old;
        // Resonance Tamer and Tone Steady listen to this frame after the Stem EQ move; their dB
        // gains multiply onto it.
        resonanceGains (Lo, Ro);
        toneSteadyGains (Lo, Ro);
        for (int k = 0; k < bins; ++k)
        {
            const auto K = static_cast<size_t> (k);
            binGain[K] *= rtLin[K];
            if (tsBin[K] != 0.0) binGain[K] *= std::exp (tsBin[K] * (std::numbers::ln10 / 20.0));
        }
        for (int k = 0; k < N; ++k)
        {
            const double g = binGain[static_cast<size_t> (k <= half ? k : N - k)];
            work[static_cast<size_t> (k)] = (Lo[k] + std::complex<double> (0.0, 1.0) * Ro[k]) * g;   // L + iR
        }
        fft (work.data(), true);
        const double norm = 1.0 / (1.5 * N);                    // Hann^2 at hop N/4 sums to 1.5
        for (int i = 0; i < N; ++i)
        {
            const double w = window[static_cast<size_t> (i)] * norm;
            acc[0][static_cast<size_t> (i)] += work[static_cast<size_t> (i)].real() * w;
            acc[1][static_cast<size_t> (i)] += work[static_cast<size_t> (i)].imag() * w;
        }
    }

    // The first hop of the accumulator is complete: it becomes the next hop of output.
    for (int c = 0; c < 2; ++c)
    {
        auto& a = acc[static_cast<size_t> (c)];
        std::copy (a.begin(), a.begin() + hopSize, outQueue[static_cast<size_t> (c)].begin());
        std::copy (a.begin() + hopSize, a.end(), a.begin());
        std::fill (a.end() - hopSize, a.end(), 0.0);
    }
    ++frameCount;
}

void StemEQ::processFrame (double* frame, int numChannels) noexcept
{
    const int nch = std::min (numChannels, channelCount);
    if (nch <= 0) return;
    const auto ip = static_cast<size_t> (inPos);
    inRing[0][ip] = frame[0];
    inRing[1][ip] = nch > 1 ? frame[1] : frame[0];

    running = ! allNeutral();
    mix += ((running ? 1.0 : 0.0) - mix) * mixCoeff;
    if (! running && mix < 1.0e-6) mix = 0.0;

    const auto dp = static_cast<size_t> (dryPos), qp = static_cast<size_t> (queuePos);
    for (int c = 0; c < nch; ++c)
    {
        auto& d = dry[static_cast<size_t> (c)];
        const double delayed = d[dp];
        d[dp] = frame[c];
        frame[c] = (c < 2 && mix > 0.0) ? delayed + (outQueue[static_cast<size_t> (c)][qp] - delayed) * mix : delayed;
    }
    dryPos = dryPos + 1 == latency ? 0 : dryPos + 1;
    inPos = inPos + 1 == fftSize ? 0 : inPos + 1;
    if (++queuePos == hopSize)
    {
        queuePos = 0;
        hop();
    }
}

} // namespace polisher

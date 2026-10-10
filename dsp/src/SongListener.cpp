#include "polisher/SongListener.h"

#include "polisher/LoudnessMeter.h"
#include "polisher/TonalEQ.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace polisher {

void SongListener::prepare (double sampleRate, double windowSeconds, double maxSeconds)
{
    sr = sampleRate;
    hopLen = std::max (1, static_cast<int> (std::lround (sr * kHopSeconds)));
    windowHops = std::max (1, static_cast<int> (std::lround (windowSeconds / kHopSeconds)));
    maxHops = std::max (windowHops, static_cast<int> (std::lround (maxSeconds / kHopSeconds)));
    pieces = 2 * windowHops + 2;   // the last window + the loudest one + the piece being filled (+1 spare)
    for (auto& k : weighting) k.prepare (sr);

    hopEnergy.assign (static_cast<size_t> (maxHops), 0.0);
    hopTime.assign (static_cast<size_t> (maxHops), -1.0);
    pool.assign (static_cast<size_t> (pieces) * static_cast<size_t> (hopLen) * 2, 0.0f);
    recent.assign (static_cast<size_t> (windowHops), 0);
    best.assign (static_cast<size_t> (windowHops), 0);
    freeList.assign (static_cast<size_t> (pieces), 0);
    inRecent.assign (static_cast<size_t> (pieces), 0);
    inBest.assign (static_cast<size_t> (pieces), 0);

    // Tone: the spectral stage's own FFT size, window and bands, so the target reads like Tone Steady.
    fftSize = StemEQ::fftSizeFor (sr);
    const int N = fftSize;
    window.assign (static_cast<size_t> (N), 0.0);
    for (int i = 0; i < N; ++i) window[static_cast<size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / N);
    StemEQ::makeBands (sr, N, bandCf, bandW, bandLo, bandHi);
    for (auto& f : frameIn) f.assign (static_cast<size_t> (N), 0.0);
    work.assign (static_cast<size_t> (N), {});
    binPow.assign (static_cast<size_t> (N / 2 + 1), 0.0);
    twiddle.assign (static_cast<size_t> (N / 2), {});
    for (int k = 0; k < N / 2; ++k) twiddle[static_cast<size_t> (k)] = std::polar (1.0, -2.0 * std::numbers::pi * k / N);
    bitrev.assign (static_cast<size_t> (N), 0);
    int bits = 0;
    while ((1 << bits) < N) ++bits;
    for (int i = 0; i < N; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
        bitrev[static_cast<size_t> (i)] = r;
    }
    maxBlocks = maxHops / kToneBlockHops + 1;
    toneBlocks.assign (static_cast<size_t> (maxBlocks) * kToneBands, 0.0f);
    toneLevel.assign (static_cast<size_t> (maxBlocks), 0.0f);
    reset();
}

void SongListener::reset() noexcept
{
    for (auto& k : weighting) k.reset();
    hopSum = 0.0;
    hopPos = 0;
    std::fill (inRecent.begin(), inRecent.end(), std::uint8_t { 0 });
    std::fill (inBest.begin(), inBest.end(), std::uint8_t { 0 });
    freeCount = 0;
    for (int p = pieces - 1; p >= 0; --p) giveBack (p);
    current = pieces > 0 ? takeFree() : 0;
    recentHead = recentCount = 0;
    bestSum = 0.0;
    framePos = frameFill = sinceFrame = 0;
    for (auto& f : frameIn) std::fill (f.begin(), f.end(), 0.0);
    blockSum.fill (0.0);
    blockEnergy = 0.0;
    blockFrames = 0;
    toneCount = 0;
    heard.store (0);
    silent.store (0);
    hopsDone.store (0);
    bestStart.store (-1);
    bestLufs.store (-100.0);
}

void SongListener::process (const float* left, const float* right, int numSamples, double hostSeconds) noexcept
{
    processAny (left, right, numSamples, hostSeconds);
}

void SongListener::process (const double* left, const double* right, int numSamples, double hostSeconds) noexcept
{
    processAny (left, right, numSamples, hostSeconds);
}

template <typename Sample>
void SongListener::processAny (const Sample* left, const Sample* right, int numSamples, double hostSeconds) noexcept
{
    if (hopLen <= 0 || numSamples <= 0 || left == nullptr)
        return;
    if (right == nullptr)
        right = left;
    double peak = 0.0;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max ({ peak, std::abs (static_cast<double> (left[i])), std::abs (static_cast<double> (right[i])) });
    if (peak < kSilencePeak)
    {
        silent.store (silent.load (std::memory_order_relaxed) + numSamples, std::memory_order_release);
        return;
    }
    silent.store (0, std::memory_order_release);
    if (hopsDone.load (std::memory_order_relaxed) >= maxHops)
        return;

    int done = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        if (hopPos == 0)
            hopTime[static_cast<size_t> (hopsDone.load (std::memory_order_relaxed))] = hostSeconds >= 0.0 ? hostSeconds + i / sr : -1.0;
        const double l = static_cast<double> (left[i]), r = static_cast<double> (right[i]);
        float* piece = pool.data() + static_cast<size_t> (current) * static_cast<size_t> (hopLen) * 2;
        piece[hopPos] = static_cast<float> (l);
        piece[hopLen + hopPos] = static_cast<float> (r);
        const double kl = weighting[0].process (l), kr = weighting[1].process (r);
        hopSum += kl * kl + kr * kr;

        const auto fp = static_cast<size_t> (framePos);
        frameIn[0][fp] = l;
        frameIn[1][fp] = r;
        framePos = framePos + 1 == fftSize ? 0 : framePos + 1;
        if (frameFill < fftSize) ++frameFill;
        if (++sinceFrame >= fftSize / 2 && frameFill == fftSize)
        {
            sinceFrame = 0;
            toneFrame();
        }

        done = i + 1;
        if (++hopPos == hopLen)
        {
            completeHop();
            if (hopsDone.load (std::memory_order_relaxed) >= maxHops)
                break;   // as long as it may listen: from here on it only waits to be finished
        }
    }
    heard.store (heard.load (std::memory_order_relaxed) + done, std::memory_order_release);
}

void SongListener::completeHop() noexcept
{
    const int h = hopsDone.load (std::memory_order_relaxed);
    hopEnergy[static_cast<size_t> (h)] = hopSum / hopLen;
    hopSum = 0.0;
    hopPos = 0;

    // The piece just filled joins the last window; the oldest leaves it (back to the pool unless it
    // belongs to the loudest window).
    const auto W = static_cast<size_t> (windowHops);
    if (recentCount == windowHops)
    {
        const int old = recent[static_cast<size_t> (recentHead)];
        inRecent[static_cast<size_t> (old)] = 0;
        if (inBest[static_cast<size_t> (old)] == 0)
            giveBack (old);
        recent[static_cast<size_t> (recentHead)] = current;
        recentHead = recentHead + 1 == windowHops ? 0 : recentHead + 1;
    }
    else
    {
        recent[static_cast<size_t> (recentCount)] = current;
        ++recentCount;
    }
    inRecent[static_cast<size_t> (current)] = 1;

    if (recentCount == windowHops)
    {
        // The last window's energy, summed afresh (exact, whatever the song's length). Strictly louder
        // replaces the loudest, so the earliest of equally loud parts stays (as LoudestPartFinder).
        double sum = 0.0;
        for (size_t j = 0; j < W; ++j) sum += hopEnergy[static_cast<size_t> (h) + 1 - W + j];
        const int had = bestStart.load (std::memory_order_relaxed);
        if (had < 0 || sum > bestSum * (1.0 + 1.0e-12))
        {
            if (had >= 0)
                for (int p : best)
                {
                    inBest[static_cast<size_t> (p)] = 0;
                    if (inRecent[static_cast<size_t> (p)] == 0)
                        giveBack (p);
                }
            for (size_t j = 0; j < W; ++j)
            {
                const int p = recent[(static_cast<size_t> (recentHead) + j) % W];
                best[j] = p;
                inBest[static_cast<size_t> (p)] = 1;
            }
            bestSum = sum;
            bestLufs.store (energyToLufs (std::max (sum / windowHops, 1.0e-30)), std::memory_order_relaxed);
            bestStart.store (h + 1 - windowHops, std::memory_order_release);
        }
    }
    current = takeFree();
    hopsDone.store (h + 1, std::memory_order_release);
    if ((h + 1) % kToneBlockHops == 0)
        closeToneBlock();
}

void SongListener::toneFrame() noexcept
{
    const int N = fftSize, half = N / 2;
    // The last N samples, oldest first (framePos is the oldest), windowed; both channels in one complex FFT.
    for (int i = 0; i < N; ++i)
    {
        const auto r = static_cast<size_t> ((framePos + i) % N);
        const double w = window[static_cast<size_t> (i)];
        work[static_cast<size_t> (i)] = { frameIn[0][r] * w, frameIn[1][r] * w };
    }
    for (int i = 0; i < N; ++i)
    {
        const int j = bitrev[static_cast<size_t> (i)];
        if (i < j) std::swap (work[static_cast<size_t> (i)], work[static_cast<size_t> (j)]);
    }
    for (int len = 2; len <= N; len <<= 1)
    {
        const int h = len / 2, step = N / len;
        for (int i = 0; i < N; i += len)
            for (int k = 0; k < h; ++k)
            {
                const auto u = work[static_cast<size_t> (i + k)], v = work[static_cast<size_t> (i + k + h)] * twiddle[static_cast<size_t> (k * step)];
                work[static_cast<size_t> (i + k)] = u + v;
                work[static_cast<size_t> (i + k + h)] = u - v;
            }
    }
    // |L|^2 + |R|^2 of the two real spectra packed as L + iR: (|Z_k|^2 + |Z_(N-k)|^2) / 2.
    for (int k = 0; k <= half; ++k)
        binPow[static_cast<size_t> (k)] = 0.5 * (std::norm (work[static_cast<size_t> (k)]) + std::norm (work[static_cast<size_t> ((N - k) % N)]));

    const int bins = half + 1;
    std::array<double, kToneBands> e {};
    double total = 0.0;
    for (int band = 0; band < kToneBands; ++band)
    {
        const double* w = bandW.data() + band * bins;
        double sum = 0.0;
        for (int k = bandLo[static_cast<size_t> (band)]; k < bandHi[static_cast<size_t> (band)]; ++k)
            sum += w[k] * binPow[static_cast<size_t> (k)];
        e[static_cast<size_t> (band)] = sum;
        total += sum;
    }
    // As Tone Steady reads a frame: each band relative to the whole frame, -90 dB at most below it;
    // near-silent frames teach nothing.
    const double totalDb = 10.0 * std::log10 (total + 1.0e-20);
    if (totalDb <= -80.0)
        return;
    for (size_t b = 0; b < e.size(); ++b)
        blockSum[b] += std::max (10.0 * std::log10 (e[b] + 1.0e-20) - totalDb, -90.0);
    blockEnergy += total;
    ++blockFrames;
}

void SongListener::closeToneBlock() noexcept
{
    if (blockFrames >= 4 && toneCount < maxBlocks)
    {
        float* row = toneBlocks.data() + static_cast<size_t> (toneCount) * kToneBands;
        for (size_t b = 0; b < blockSum.size(); ++b)
            row[b] = static_cast<float> (blockSum[b] / blockFrames);
        toneLevel[static_cast<size_t> (toneCount)] = static_cast<float> (10.0 * std::log10 (blockEnergy / blockFrames + 1.0e-30));
        ++toneCount;
    }
    blockSum.fill (0.0);
    blockEnergy = 0.0;
    blockFrames = 0;
}

double SongListener::loudestLufs() const noexcept
{
    return bestStart.load (std::memory_order_acquire) >= 0 ? bestLufs.load (std::memory_order_relaxed) : -100.0;
}

double SongListener::loudestStartSeconds() const noexcept
{
    const int b = bestStart.load (std::memory_order_acquire);
    if (b < 0)
        return -1.0;
    const double t = hopTime[static_cast<size_t> (b)];
    return t >= 0.0 ? t : b * kHopSeconds;
}

bool SongListener::loudestFromHost() const noexcept
{
    const int b = bestStart.load (std::memory_order_acquire);
    return b >= 0 && hopTime[static_cast<size_t> (b)] >= 0.0;
}

AudioClip SongListener::loudestWindow() const
{
    AudioClip clip;
    clip.sampleRate = sr;
    clip.channels.assign (2, {});
    if (hopLen <= 0)
        return clip;
    std::vector<int> list;
    int tail = 0;   // samples of a piece still being filled
    if (bestStart.load() >= 0)
        list = best;
    else
    {
        for (int j = 0; j < recentCount; ++j) list.push_back (recent[static_cast<size_t> ((recentHead + j) % windowHops)]);
        tail = hopPos;
    }
    const auto len = static_cast<size_t> (hopLen);
    for (auto& ch : clip.channels) ch.reserve (list.size() * len + static_cast<size_t> (tail));
    auto append = [&] (int piece, size_t n)
    {
        const float* p = pool.data() + static_cast<size_t> (piece) * len * 2;
        clip.channels[0].insert (clip.channels[0].end(), p, p + n);
        clip.channels[1].insert (clip.channels[1].end(), p + len, p + len + n);
    };
    for (int p : list) append (p, len);
    if (tail > 0) append (current, static_cast<size_t> (tail));
    return clip;
}

SongStats SongListener::stats() const
{
    SongStats s;
    s.heardSeconds = heardSeconds();
    const int H = hopsDone.load();
    const auto e = [this] (int j) { return hopEnergy[static_cast<size_t> (j)]; };

    // Integrated (BS.1770): 400 ms blocks every 100 ms, -70 LUFS absolute and -10 LU relative gates.
    std::vector<double> blocks;
    for (int j = 0; j + 4 <= H; ++j)
    {
        const double z = (e (j) + e (j + 1) + e (j + 2) + e (j + 3)) / 4.0;
        if (energyToLufs (z) > -70.0) blocks.push_back (z);
    }
    if (! blocks.empty())
    {
        double sum = 0.0;
        for (double z : blocks) sum += z;
        const double gate = energyToLufs (sum / static_cast<double> (blocks.size())) - 10.0;
        double gated = 0.0;
        int n = 0;
        for (double z : blocks)
            if (energyToLufs (z) > gate) { gated += z; ++n; }
        if (n > 0) s.integratedLufs = energyToLufs (gated / n);
    }

    // Loudness range (EBU Tech 3342): 3 s windows every 100 ms, -70 LUFS absolute and -20 LU relative
    // gates, 95th minus 10th percentile.
    std::vector<double> st;
    double stSum = 0.0;
    for (int j = 0; j + 30 <= H; ++j)
    {
        double z = 0.0;
        for (int k = 0; k < 30; ++k) z += e (j + k);
        z /= 30.0;
        if (energyToLufs (z) > -70.0) { st.push_back (z); stSum += z; }
    }
    if (! st.empty())
    {
        const double gate = energyToLufs (stSum / static_cast<double> (st.size())) - 20.0;
        std::vector<double> lu;
        for (double z : st)
            if (energyToLufs (z) > gate) lu.push_back (energyToLufs (z));
        if (! lu.empty())
        {
            std::sort (lu.begin(), lu.end());
            auto pct = [&] (double p) { return lu[static_cast<size_t> (std::floor (p * static_cast<double> (lu.size() - 1)))]; };
            s.loudnessRangeLu = std::max (0.0, pct (0.95) - pct (0.10));
        }
    }

    if (hasWindow())
    {
        s.loudestLufs = loudestLufs();
        s.loudestStartSeconds = loudestStartSeconds();
        s.positionFromHost = loudestFromHost();
        s.loudestSeconds = windowSeconds();
    }
    else
    {
        s.loudestLufs = s.integratedLufs;
        s.loudestStartSeconds = H > 0 && hopTime[0] >= 0.0 ? hopTime[0] : 0.0;
        s.positionFromHost = H > 0 && hopTime[0] >= 0.0;
        s.loudestSeconds = s.heardSeconds;
    }
    return s;
}

ToneTarget SongListener::toneTarget() const
{
    ToneTarget t;
    if (toneCount < kMinToneBlocks)
        return t;
    // Only the music at the song's level: quiet intros, breakdowns and fades (more than 15 dB under
    // its power-mean level) don't describe its tone, and Tone Steady leaves them alone anyway.
    double sum = 0.0;
    for (int i = 0; i < toneCount; ++i) sum += std::pow (10.0, toneLevel[static_cast<size_t> (i)] / 10.0);
    const double songDb = 10.0 * std::log10 (sum / toneCount);
    std::vector<int> use;
    for (int i = 0; i < toneCount; ++i)
        if (toneLevel[static_cast<size_t> (i)] >= songDb - 15.0) use.push_back (i);
    if (static_cast<int> (use.size()) < kMinToneBlocks)
        return t;
    // The median of each band: the song's typical tone. A part that plays in most of the song (the
    // 808 of a trap song) is in it; one that comes and goes doesn't pull it halfway.
    std::vector<float> v (use.size());
    for (int b = 0; b < kToneBands; ++b)
    {
        for (size_t i = 0; i < use.size(); ++i)
            v[i] = toneBlocks[static_cast<size_t> (use[i]) * kToneBands + static_cast<size_t> (b)];
        const size_t mid = v.size() / 2;
        std::nth_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (mid), v.end());
        double m = v[mid];
        if (v.size() % 2 == 0)
            m = 0.5 * (m + *std::max_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (mid)));
        t.bandDb[static_cast<size_t> (b)] = static_cast<float> (m);
    }
    t.valid = true;
    t.seconds = heardSeconds();
    return t;
}

std::size_t SongListener::memoryBytes() const noexcept
{
    auto bytes = [] (const auto& v) { return v.capacity() * sizeof (v[0]); };
    return bytes (hopEnergy) + bytes (hopTime) + bytes (pool) + bytes (recent) + bytes (best) + bytes (freeList)
         + bytes (inRecent) + bytes (inBest) + bytes (window) + bytes (bandW) + bytes (bandCf) + bytes (bandLo)
         + bytes (bandHi) + bytes (frameIn[0]) + bytes (frameIn[1]) + bytes (work) + bytes (twiddle) + bytes (bitrev)
         + bytes (binPow) + bytes (toneBlocks) + bytes (toneLevel) + sizeof (*this);
}

ToneTarget toneTargetAtStage (const ToneTarget& song, const TonalEQParams& eq, double sampleRate) noexcept
{
    ToneTarget t = song;
    if (! song.valid || ! eq.enabled || sampleRate <= 0.0)
        return t;
    std::array<BiquadCoefficients, kEqBands> bands {};
    std::array<bool, kEqBands> on {};
    bool any = false;
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto i = static_cast<size_t> (b);
        on[i] = eq.gainDb[i] != 0.0;
        if (on[i]) { bands[i] = TonalEQ::designBand (b, eq.gainDb[i], eq.freqHz[i], sampleRate); any = true; }
    }
    const bool subsonic = TonalEQ::subsonicEngaged (eq);
    if (! any && ! subsonic)
        return t;
    const auto hp = TonalEQ::designSubsonic (sampleRate);
    for (int band = 0; band < kToneBands; ++band)
    {
        const double f = StemEQ::bandCentreHz (band, sampleRate);
        double db = 0.0;
        for (size_t j = 0; j < bands.size(); ++j)
            if (on[j]) db += bands[j].magnitudeDb (f, sampleRate);
        if (subsonic) db += hp[0].magnitudeDb (f, sampleRate) + hp[1].magnitudeDb (f, sampleRate);
        t.bandDb[static_cast<size_t> (band)] = static_cast<float> (song.bandDb[static_cast<size_t> (band)] + db);
    }
    return t;
}

} // namespace polisher

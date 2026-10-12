#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Signals.h"
#include "polisher/FilterDesign.h"
#include "polisher/Saturation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <numbers>

using namespace polisher;
using Catch::Matchers::WithinAbs;

namespace {

AudioClip saturate (AudioClip c, const SaturationParams& p)
{
    Saturation s;
    s.prepare (c.sampleRate, c.numChannels());
    s.setParams (p);
    s.reset();
    std::vector<float*> ch;
    for (auto& v : c.channels) ch.push_back (v.data());
    s.process (ch.data(), c.numChannels(), c.numSamples());
    return c;
}

/** Level (dB) of frequency f in the last second of channel 0, relative to a full-scale sine. */
double levelAt (const AudioClip& c, double f)
{
    const int n = static_cast<int> (c.sampleRate);
    const int start = c.numSamples() - n;
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double x = c.channels[0][static_cast<size_t> (start + i)];
        const double ph = 2.0 * std::numbers::pi * f * i / c.sampleRate;
        re += x * std::cos (ph);
        im -= x * std::sin (ph);
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / n + 1e-12);
}

double folded (double f, double sr)
{
    f = std::fmod (f, sr);
    return f > sr / 2 ? sr - f : f;
}

SaturationParams with (SaturationMode m, double drive, double mix = 100.0, SaturationFocus focus = SaturationFocus::full)
{
    SaturationParams p;
    p.mode = m;
    p.driveDb = drive;
    p.mix = mix;
    p.focus = focus;
    return p;
}

SaturationParams bassOnly (SaturationMode m, double drive, double mix = 100.0) { return with (m, drive, mix, SaturationFocus::bass); }

/** Saturation exactly as it was before Focus (v0.15.0), kept as the reference that Full must still match
    bit for bit. Same curves (Saturation::shape), same filters, same order of operations.
    One later change is copied in on purpose (2026-10 re-audit): started by reset() (no warm-up, so no DC
    measured), the tube DC filter starts at Saturation::kDcStartHz and glides down to 1 Hz, so a render
    that starts mid-song lets out no thump. That only touches the first ~0.5 s after reset() in Tube; once
    it lands on 1 Hz the filter is the v0.15.0 one again. So "bit for bit" here means: the steady-state
    algorithm is unchanged. (Mode changes are no longer instant: they cross-fade like Focus, tested on
    their own below.) */
class LegacySaturation
{
public:
    static inline bool copiedDcStart = true;   // false: exactly v0.15.0 (the DC filter starts cold at 1 Hz after reset())

    void prepare (double sampleRate, int numChannels)
    {
        channelCount = numChannels;
        channelState.assign (static_cast<size_t> (channelCount), Channel {});
        for (auto& ch : channelState) ch.dry.assign (Saturation::kLatency + 1, 0.0);
        glideCoeff = 1.0 - std::exp (-1.0 / (0.030 * sampleRate));
        mixCoeff = 1.0 - std::exp (-4.0 / (0.020 * sampleRate));
        hpCoeff = std::exp (-2.0 * std::numbers::pi * 1.0 / sampleRate);
        dcWindow = static_cast<int> (0.05 * sampleRate);
        rate = sampleRate;
        dcGlideCoeff = 1.0 - std::exp (-1.0 / (Saturation::kDcGlideSeconds * sampleRate));
        reset();
    }
    void setParams (const SaturationParams& p) { params = p; }
    void reset()
    {
        for (auto& ch : channelState)
        {
            for (auto& st : ch.stages) st.reset();
            std::fill (ch.dry.begin(), ch.dry.end(), 0.0);
            ch.dryPos = 0;
            ch.hpX = ch.hpY = ch.dcSum = 0.0;
        }
        dcCount = 0;
        bendTarget = std::pow (10.0, (Saturation::kHeadroomDb - std::clamp (params.driveDb, 0.0, Saturation::kMaxDriveDb)) / 20.0);
        bend = bendTarget;
        running = ! Saturation::isNeutral (params);
        mix = Saturation::effectiveMix (params);
        warmup = 0;
        dcHz = running && copiedDcStart ? Saturation::kDcStartHz : 1.0;   // (the copied-in reset() start, see above)
    }
    void processFrame (double* frame, int nch)
    {
        const double mixTarget = Saturation::effectiveMix (params);
        bendTarget = std::pow (10.0, (Saturation::kHeadroomDb - std::clamp (params.driveDb, 0.0, Saturation::kMaxDriveDb)) / 20.0);
        const bool idle = mix < 1.0e-6 && mixTarget == 0.0 && warmup == 0;
        if (idle)
        {
            if (mix != 0.0 || running)
            {
                mix = 0.0;
                running = false;
                for (auto& ch : channelState)
                {
                    for (auto& st : ch.stages) st.reset();
                    ch.hpX = ch.hpY = ch.dcSum = 0.0;
                }
                dcHz = 1.0;
            }
        }
        else
        {
            if (! running)
            {
                running = true;
                warmup = 2 * Saturation::kLatency + dcWindow;
                bend = bendTarget;
            }
            if (warmup > 0) --warmup;
            else mix += (mixTarget - mix) * mixCoeff;
            bend += (bendTarget - bend) * glideCoeff;
        }
        const bool measuringDc = ! idle && warmup > 0 && warmup <= dcWindow;
        const bool seedDc = ! idle && warmup == 0 && dcCount > 0;
        const double dcWeight = measuringDc ? std::pow (std::sin (std::numbers::pi * (dcCount + 0.5) / dcWindow), 2.0) : 0.0;
        if (! idle && dcHz > 1.0)
        {
            dcHz += (1.0 - dcHz) * dcGlideCoeff;
            if (dcHz < 1.0001) dcHz = 1.0;
        }
        const double dcCoeff = dcHz > 1.0 ? std::exp (-2.0 * std::numbers::pi * dcHz / rate) : hpCoeff;
        for (int c = 0; c < nch; ++c)
        {
            auto& ch = channelState[static_cast<size_t> (c)];
            const double x = frame[c];
            const int size = static_cast<int> (ch.dry.size());
            ch.dry[static_cast<size_t> (ch.dryPos)] = x;
            const double dry = ch.dry[static_cast<size_t> ((ch.dryPos + 1) % size)];
            ch.dryPos = (ch.dryPos + 1) % size;
            if (idle) { frame[c] = dry; continue; }
            double a, b, u[4];
            ch.stages[0].upsample (x, a, b);
            ch.stages[1].upsample (a, u[0], u[1]);
            ch.stages[1].upsample (b, u[2], u[3]);
            for (double& v : u) v = Saturation::shape (v, params.mode, bend) - v;
            const double ra = ch.stages[1].downsample (u[0], u[1]);
            const double rb = ch.stages[1].downsample (u[2], u[3]);
            const double res = ch.stages[0].downsample (ra, rb);
            if (measuringDc) ch.dcSum += dcWeight * res;
            if (seedDc) { ch.hpX = ch.dcSum / (0.5 * dcCount); ch.hpY = 0.0; ch.dcSum = 0.0; }
            if (params.mode == SaturationMode::tube) { ch.hpY = dcCoeff * (ch.hpY + res - ch.hpX); ch.hpX = res; }
            else { ch.hpY = res; ch.hpX = 0.0; }
            frame[c] = dry + mix * ch.hpY;
        }
        dcCount = measuringDc ? dcCount + 1 : 0;
    }

private:
    struct Channel
    {
        std::array<HalfbandStage, 2> stages { HalfbandStage (95), HalfbandStage (33) };
        std::vector<double> dry;
        int dryPos = 0;
        double hpX = 0.0, hpY = 0.0, dcSum = 0.0;
    };
    SaturationParams params;
    int channelCount = 2;
    std::vector<Channel> channelState;
    double bend = 4.0, bendTarget = 4.0, glideCoeff = 0.0, mix = 0.0, mixCoeff = 0.0, hpCoeff = 0.0;
    double rate = 48000.0, dcHz = 1.0, dcGlideCoeff = 0.0;
    int warmup = 0, dcWindow = 1200, dcCount = 0;
    bool running = false;
};

/** Runs a processor block by block (256), asking for the params at each block's first sample. */
template <typename Sat>
AudioClip render (AudioClip c, const std::function<SaturationParams (int)>& paramsAt, bool startWithReset)
{
    Sat s;
    s.prepare (c.sampleRate, c.numChannels());
    s.setParams (paramsAt (0));
    if (startWithReset) s.reset();
    const int n = c.numSamples(), block = 256;
    for (int start = 0; start < n; start += block)
    {
        s.setParams (paramsAt (start));
        for (int i = start; i < std::min (n, start + block); ++i)
        {
            double frame[2] = { c.channels[0][static_cast<size_t> (i)], c.channels[1][static_cast<size_t> (i)] };
            s.processFrame (frame, 2);
            for (int ch = 0; ch < 2; ++ch) c.channels[static_cast<size_t> (ch)][static_cast<size_t> (i)] = static_cast<float> (frame[ch]);
        }
    }
    return c;
}

/** Harmonics meter reading (dB) for a whole clip, starting from reset(). */
double meterDb (const AudioClip& clip, const SaturationParams& p)
{
    Saturation s;
    s.prepare (clip.sampleRate, clip.numChannels());
    s.setParams (p);
    s.reset();
    auto c = clip;
    std::vector<float*> ch;
    for (auto& v : c.channels) ch.push_back (v.data());
    s.process (ch.data(), c.numChannels(), c.numSamples());
    return s.takeHarmonicsDb();
}

} // namespace

TEST_CASE ("Saturation at Drive 0 or Mix 0 is an exact, delayed pass-through", "[saturation]")
{
    const auto mix = testsig::musicLike (48000.0, 1.0, -3.0);
    for (const auto& p : { SaturationParams {}, with (SaturationMode::tube, 12.0, 0.0), [] { auto q = with (SaturationMode::clip, 12.0); q.enabled = false; return q; }(),
                           bassOnly (SaturationMode::tube, 0.0), bassOnly (SaturationMode::tube, 12.0, 0.0),
                           [] { auto q = bassOnly (SaturationMode::clip, 12.0); q.enabled = false; return q; }() })
    {
        const auto out = saturate (mix, p);
        for (int c = 0; c < 2; ++c)
            for (int i = Saturation::kLatency; i < mix.numSamples(); ++i)
                REQUIRE (out.channels[static_cast<size_t> (c)][static_cast<size_t> (i)]
                         == mix.channels[static_cast<size_t> (c)][static_cast<size_t> (i - Saturation::kLatency)]);
    }
}

TEST_CASE ("Saturation keeps the reported latency and leaves quiet signals alone", "[saturation]")
{
    // A quiet mix at full Drive: the curve is linear there, so the output is the input, delayed.
    const auto quiet = testsig::musicLike (48000.0, 1.0, -50.0);
    for (auto focus : { SaturationFocus::full, SaturationFocus::bass })
    for (auto m : { SaturationMode::tape, SaturationMode::tube, SaturationMode::clip })
    {
        const auto out = saturate (quiet, with (m, Saturation::kMaxDriveDb, 100.0, focus));
        double err = 0.0, sig = 0.0;
        for (int i = 4800; i < quiet.numSamples(); ++i)
        {
            const double d = out.channels[0][static_cast<size_t> (i)] - quiet.channels[0][static_cast<size_t> (i - Saturation::kLatency)];
            err += d * d;
            sig += static_cast<double> (quiet.channels[0][static_cast<size_t> (i)]) * quiet.channels[0][static_cast<size_t> (i)];
        }
        INFO ("mode " << static_cast<int> (m) << " focus " << static_cast<int> (focus));
        CHECK (10.0 * std::log10 (err / sig) < -50.0);
    }
}

TEST_CASE ("Tape and clip add odd harmonics, tube adds mostly the 2nd", "[saturation]")
{
    const auto tone = testsig::sine (48000.0, 2, 1000.0, -3.0, 2.0);
    const auto tape = saturate (tone, with (SaturationMode::tape, 12.0));
    const auto tube = saturate (tone, with (SaturationMode::tube, 12.0));
    const auto clip = saturate (tone, with (SaturationMode::clip, 12.0));

    INFO ("tape h2 " << levelAt (tape, 2000) << " h3 " << levelAt (tape, 3000));
    INFO ("tube h2 " << levelAt (tube, 2000) << " h3 " << levelAt (tube, 3000));
    INFO ("clip h2 " << levelAt (clip, 2000) << " h3 " << levelAt (clip, 3000));
    CHECK (levelAt (tape, 3000) > -45.0);
    CHECK (levelAt (tape, 2000) < -90.0);
    CHECK (levelAt (clip, 3000) > -45.0);
    CHECK (levelAt (clip, 2000) < -90.0);
    CHECK (levelAt (tube, 2000) > -40.0);
    CHECK (levelAt (tube, 2000) > levelAt (tube, 3000) + 3.0);

    // More Drive, more harmonics; the fundamental stays within a couple of dB.
    const auto hot = saturate (tone, with (SaturationMode::tape, 18.0));
    CHECK (levelAt (hot, 3000) > levelAt (tape, 3000) + 3.0);
    CHECK_THAT (levelAt (saturate (tone, with (SaturationMode::tape, 6.0)), 1000), WithinAbs (-3.0, 1.0));

    // Mix 50 %: half the harmonics.
    const auto half = saturate (tone, with (SaturationMode::tape, 12.0, 50.0));
    CHECK_THAT (levelAt (half, 3000), WithinAbs (levelAt (tape, 3000) - 6.02, 0.3));
}

TEST_CASE ("Saturation doesn't alias and adds no DC", "[saturation]")
{
    for (double sr : { 44100.0, 48000.0 })
    {
        const double f = 7000.0;
        const auto tone = testsig::sine (sr, 2, f, -1.0, 2.0);
        for (auto m : { SaturationMode::tape, SaturationMode::tube, SaturationMode::clip })
        {
            const auto out = saturate (tone, with (m, Saturation::kMaxDriveDb));
            double worst = -200.0;
            for (int k = 2; k <= 40; ++k)
            {
                const double h = k * f;
                if (h < sr / 2) continue;                       // real harmonics are fine
                const double a = folded (h, sr);
                if (std::abs (a - f) < 50.0) continue;
                worst = std::max (worst, levelAt (out, std::round (a)));
            }
            INFO ("sr " << sr << " mode " << static_cast<int> (m) << " worst alias " << worst);
            CHECK (worst < -80.0);

            double dc = 0.0;
            for (int i = out.numSamples() / 2; i < out.numSamples(); ++i) dc += out.channels[0][static_cast<size_t> (i)];
            CHECK (std::abs (dc / (out.numSamples() / 2)) < 1e-4);
        }

        // Bass only, pushed as hard as it goes on a full-scale low note: its harmonics reach far up
        // (3 kHz and beyond) but nothing folds back, and no DC.
        const double fb = 97.0;
        const auto low = testsig::sine (sr, 2, fb, -1.0, 2.0);
        for (auto m : { SaturationMode::tape, SaturationMode::tube, SaturationMode::clip })
        {
            const auto out = saturate (low, bassOnly (m, Saturation::kMaxDriveDb));
            const int first = static_cast<int> (std::ceil (sr / 2 / fb));
            double worst = -200.0;
            for (int k = first; k <= first + 40; ++k)
            {
                const double a = folded (k * fb, sr);
                if (std::abs (std::remainder (a, fb)) < 3.0) continue;   // lands on a real harmonic
                worst = std::max (worst, levelAt (out, std::round (a)));
            }
            INFO ("Bass only: sr " << sr << " mode " << static_cast<int> (m) << " worst alias " << worst << ", 31st harmonic " << levelAt (out, 31 * fb));
            CHECK (worst < -100.0);
            CHECK (levelAt (out, 31 * fb) > -120.0);   // the harmonics really are generated up there
            double dc = 0.0;
            for (int i = out.numSamples() / 2; i < out.numSamples(); ++i) dc += out.channels[0][static_cast<size_t> (i)];
            CHECK (std::abs (dc / (out.numSamples() / 2)) < 1e-4);
        }
    }
}

TEST_CASE ("Saturation glides: no clicks when Drive or Mix move, stable at every rate", "[saturation]")
{
    for (auto focus : { SaturationFocus::full, SaturationFocus::bass })
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto c = testsig::sine (sr, 2, 100.0, -6.0, 1.0);
        Saturation s;
        s.prepare (sr, 2);
        const int n = c.numSamples();
        float* ch[2] = { c.channels[0].data(), c.channels[1].data() };
        const int block = 256;
        for (int start = 0; start < n; start += block)
        {
            // Jump between off and full Drive every 0.1 s.
            const bool on = (start / static_cast<int> (0.1 * sr)) % 2 == 1;
            s.setParams (with (SaturationMode::clip, on ? Saturation::kMaxDriveDb : 0.0, 100.0, focus));
            float* p[2] = { ch[0] + start, ch[1] + start };
            s.process (p, 2, std::min (block, n - start));
        }
        double maxStep = 0.0;
        for (int i = Saturation::kLatency + 1; i < n; ++i)
        {
            const double v = c.channels[0][static_cast<size_t> (i)];
            REQUIRE (std::isfinite (v));
            maxStep = std::max (maxStep, std::abs (v - c.channels[0][static_cast<size_t> (i - 1)]));
        }
        // A 100 Hz sine at -6 dBFS moves at most 2*pi*100/sr*0.5 per sample; allow a little more.
        INFO ("focus " << static_cast<int> (focus) << " sr " << sr << " max step " << maxStep);
        CHECK (maxStep < 1.3 * 2.0 * std::numbers::pi * 100.0 / sr * 0.5);
    }
}

TEST_CASE ("Saturation reports the harmonics it adds", "[saturation]")
{
    Saturation s;
    s.prepare (48000.0, 2);
    auto tone = testsig::sine (48000.0, 2, 1000.0, -3.0, 0.5);
    float* ch[2] = { tone.channels[0].data(), tone.channels[1].data() };
    s.process (ch, 2, tone.numSamples());
    CHECK (s.takeHarmonicsDb() <= -99.0);
    s.setParams (with (SaturationMode::tape, 12.0));
    s.process (ch, 2, tone.numSamples());
    const double h = s.takeHarmonicsDb();
    INFO (h);
    CHECK (h > -40.0);
    CHECK (h < -5.0);
}

TEST_CASE ("Saturation follows its curve on bass too (no phase shift), and starts at once offline", "[saturation]")
{
    for (auto m : { SaturationMode::tape, SaturationMode::clip, SaturationMode::tube })
        for (double f : { 30.0, 50.0 })
        {
            const auto tone = testsig::sine (48000.0, 2, f, 20.0 * std::log10 (0.9), 3.0);
            const auto out = saturate (tone, with (m, Saturation::kMaxDriveDb));
            const double s = std::pow (10.0, (Saturation::kHeadroomDb - Saturation::kMaxDriveDb) / 20.0);
            // Compare with the DC removed (the tube curve's DC is taken out on purpose).
            double wantDc = 0.0, outDc = 0.0;
            const int from = 96000, n = tone.numSamples() - from;
            for (int i = from; i < tone.numSamples(); ++i)
            {
                wantDc += Saturation::shape (tone.channels[0][static_cast<size_t> (i - Saturation::kLatency)], m, s) / n;
                outDc += out.channels[0][static_cast<size_t> (i)] / static_cast<double> (n);
            }
            double err = 0.0, sig = 0.0;
            for (int i = from; i < tone.numSamples(); ++i)
            {
                const double want = Saturation::shape (tone.channels[0][static_cast<size_t> (i - Saturation::kLatency)], m, s) - wantDc;
                const double d = out.channels[0][static_cast<size_t> (i)] - outDc - want;
                err += d * d;
                sig += want * want;
            }
            INFO ("mode " << static_cast<int> (m) << " f " << f << " err " << 10.0 * std::log10 (err / sig));
            CHECK (10.0 * std::log10 (err / sig) < -25.0);   // tube: its 1 Hz DC filter
        }

    // After reset() the set drive applies from the first samples (offline renders).
    const auto tone = testsig::sine (48000.0, 2, 1000.0, -1.0, 0.1);
    const auto out = saturate (tone, with (SaturationMode::clip, Saturation::kMaxDriveDb));
    double early = 0.0;
    for (int i = Saturation::kLatency + 100; i < Saturation::kLatency + 600; ++i)
        early = std::max (early, static_cast<double> (std::abs (out.channels[0][static_cast<size_t> (i)])));
    CHECK (20.0 * std::log10 (early) < -2.0);   // peaks already rounded (input peaks at -1 dBFS)
}

TEST_CASE ("Saturation Drive is calibrated for a mix peaking near full scale", "[saturation]")
{
    // A mix like the v0.9 test: about -17 LUFS, peaks around -1 dBFS.
    auto mix = testsig::musicLike (48000.0, 4.0, -6.0);
    double peak = 0.0;
    for (auto& ch : mix.channels) for (float v : ch) peak = std::max (peak, static_cast<double> (std::abs (v)));
    const auto g = static_cast<float> (std::pow (10.0, -1.0 / 20.0) / peak);
    for (auto& ch : mix.channels) for (auto& v : ch) v *= g;

    auto harmonics = [&] (SaturationParams p)
    {
        Saturation s;
        s.prepare (48000.0, 2);
        s.setParams (p);
        s.reset();
        auto c = mix;
        float* ch[2] = { c.channels[0].data(), c.channels[1].data() };
        s.process (ch, 2, c.numSamples());
        return s.takeHarmonicsDb();   // what is added at this Mix
    };
    const double light = harmonics (with (SaturationMode::tape, 6.0, 50.0));
    const double medium = harmonics (with (SaturationMode::tape, 12.0, 100.0));
    const double tubeLight = harmonics (with (SaturationMode::tube, 6.0, 50.0));
    INFO ("tape +6 50% " << light << ", tape +12 " << medium << ", tube +6 50% " << tubeLight);
    CHECK (light > -45.0);        // audible as density...
    CHECK (light < -28.0);        // ...not as distortion
    CHECK (medium > -30.0);       // clearly audible
    CHECK (tubeLight > -45.0);
}

TEST_CASE ("Tube saturation engages without a low thump", "[saturation]")
{
    // The tube curve's DC is removed by a 1 Hz high-pass; started cold, that filter let the DC out as a
    // slow thump (-20 dBFS here, decaying over ~0.2 s) every time Tube engaged. Averaged over whole 60 Hz
    // cycles (which removes the tone and all its harmonics), the output must stay near zero once the
    // 20 ms fade-in is over.
    const double sr = 48000.0;
    const int period = 800, n = static_cast<int> (sr * 2.0);
    for (auto focus : { SaturationFocus::full, SaturationFocus::bass })
    for (double drive : { 6.0, 12.0, 18.0 })
    {
        Saturation sat; sat.prepare (sr, 2);
        SaturationParams p; p.mode = SaturationMode::tube; p.driveDb = 0.0; p.mix = 100.0; p.focus = focus;
        sat.setParams (p); sat.reset();
        std::vector<double> out (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i)
        {
            if (i == n / 4) { p.driveDb = drive; sat.setParams (p); }
            const double x = 0.9 * std::sin (2.0 * std::numbers::pi * 60.0 * i / sr);
            double frame[2] = { x, x };
            sat.processFrame (frame, 2);
            out[static_cast<size_t> (i)] = frame[0];
        }
        double sum = 0.0, worst = 0.0;
        for (int i = 0; i < n; ++i)
        {
            sum += out[static_cast<size_t> (i)];
            if (i >= period) sum -= out[static_cast<size_t> (i - period)];
            if (i >= n / 4 + static_cast<int> (0.1 * sr)) worst = std::max (worst, std::abs (sum / period));
        }
        INFO ("focus " << static_cast<int> (focus) << " Drive " << drive << ": thump " << 20.0 * std::log10 (worst + 1e-12) << " dBFS");
        CHECK (20.0 * std::log10 (worst + 1e-12) < -40.0);
    }
}

TEST_CASE ("Saturation Full is bit for bit what it was before Focus", "[saturation][focus]")
{
    // The v0.15.0 algorithm (LegacySaturation above) against Full through Drive sweeps, on / off and Mix
    // moves in every Mode (scenario 3: Tube playing from reset(), through the DC filter's start), from
    // reset() and from a cold start, at three rates: identical samples. (Mode changes cross-fade now:
    // tested below.)
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int sig = 0; sig < 3; ++sig)
        {
            const AudioClip in = sig == 0 ? testsig::musicLike (sr, 2.0, -3.0) : sig == 1 ? testsig::trapLike (sr, 2.0) : testsig::sine (sr, 2, 60.0, -1.0, 2.0);
            for (int scenario = 0; scenario < 4; ++scenario)
            {
                const auto paramsAt = [&] (int i)
                {
                    const double t = i / sr;
                    const auto m = scenario == 3 ? SaturationMode::tube : static_cast<SaturationMode> (scenario % 3);
                    const double d = scenario == 0 ? 9.0 : scenario == 1 ? 18.0 * std::fmod (t, 1.0) : scenario == 3 ? 12.0 + 6.0 * std::sin (t * 5.0)
                                   : (std::fmod (t, 0.6) < 0.3 ? 12.0 : 0.0);
                    auto p = with (m, d, scenario == 2 ? 50.0 + 50.0 * std::sin (t * 7.0) : 60.0);
                    p.enabled = ! (scenario == 1 && t > 1.2 && t < 1.5);
                    return p;
                };
                for (bool fromReset : { true, false })
                {
                    const auto now = render<Saturation> (in, paramsAt, fromReset);
                    const auto before = render<LegacySaturation> (in, paramsAt, fromReset);
                    int differ = 0;
                    for (size_t c = 0; c < 2; ++c)
                        differ += std::memcmp (now.channels[c].data(), before.channels[c].data(), sizeof (float) * static_cast<size_t> (in.numSamples())) != 0 ? 1 : 0;
                    INFO ("sr " << sr << " signal " << sig << " scenario " << scenario << " reset " << fromReset);
                    CHECK (differ == 0);
                }
            }
        }
}

TEST_CASE ("Saturation Bass only gives the 808 harmonics and leaves the vocal clean", "[saturation][focus]")
{
    // An 808 note (45 Hz): harmonics at 90, 135, 180 Hz appear; the note itself keeps its level.
    const auto sub = testsig::sine (48000.0, 2, 45.0, -6.0, 3.0);
    CHECK (levelAt (sub, 135.0) < -100.0);   // (none of them were there before)
    for (auto m : { SaturationMode::tube, SaturationMode::tape })
    {
        const auto out = saturate (sub, bassOnly (m, 12.0));
        INFO ("mode " << static_cast<int> (m) << ": 45 Hz " << levelAt (out, 45) << ", 90 Hz " << levelAt (out, 90) << ", 135 Hz " << levelAt (out, 135)
                      << ", 180 Hz " << levelAt (out, 180) << ", 225 Hz " << levelAt (out, 225));
        CHECK_THAT (levelAt (out, 45.0), WithinAbs (-6.0, 0.5));   // adds no sub, takes none away
        CHECK (levelAt (out, 135.0) > -40.0);                       // the 3rd (every curve)
        CHECK (levelAt (out, 225.0) > -60.0);                       // and on up the series
        if (m == SaturationMode::tube)
        {
            CHECK (levelAt (out, 90.0) > -40.0);                    // tube: the 2nd and 4th too
            CHECK (levelAt (out, 180.0) > -50.0);
        }
    }

    // A vocal-like 2 kHz tone at -3 dBFS, Drive +18: Full distorts it clearly, Bass only doesn't touch it.
    const auto voice = testsig::sine (48000.0, 2, 2000.0, -3.0, 2.0);
    for (auto m : { SaturationMode::tape, SaturationMode::tube, SaturationMode::clip })
    {
        const auto thd = [&] (const SaturationParams& p)
        {
            const auto out = saturate (voice, p);
            double h = 0.0;
            for (int k = 2; k <= 10; ++k) h += std::pow (10.0, levelAt (out, 2000.0 * k) / 10.0);
            return 10.0 * std::log10 (h + 1e-30) - levelAt (out, 2000.0);
        };
        const double full = thd (with (m, Saturation::kMaxDriveDb)), bass = thd (bassOnly (m, Saturation::kMaxDriveDb));
        INFO ("mode " << static_cast<int> (m) << ": THD Full " << full << " dB, Bass only " << bass << " dB");
        CHECK (full > -30.0);
        CHECK (bass < -80.0);
    }

    // The 808 under the vocal: in Full the 808 modulates the vocal (sidebands at 1 kHz +/- n x 45 Hz);
    // Bass only keeps the vocal clean.
    auto both = testsig::sine (48000.0, 2, 45.0, -4.0, 2.0);
    const auto tone = testsig::sine (48000.0, 2, 1000.0, -20.0, 2.0);
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < both.channels[c].size(); ++i) both.channels[c][i] += tone.channels[c][i];
    const auto sidebands = [&] (const SaturationParams& p)
    {
        const auto out = saturate (both, p);
        double e = 0.0;
        for (int k = -4; k <= 4; ++k)
            if (k != 0) e += std::pow (10.0, levelAt (out, 1000.0 + 45.0 * k) / 10.0);
        return 10.0 * std::log10 (e + 1e-30) - levelAt (out, 1000.0);
    };
    const double imFull = sidebands (with (SaturationMode::tube, 12.0)), imBass = sidebands (bassOnly (SaturationMode::tube, 12.0));
    INFO ("808 sidebands on the vocal: Full " << imFull << " dB, Bass only " << imBass << " dB");
    CHECK (imFull > -30.0);
    CHECK (imBass < -70.0);
}

TEST_CASE ("Saturation Bass only Drive reads like Full on a trap beat, and the level stays put", "[saturation][focus]")
{
    const auto beat = testsig::trapLike (48000.0, 6.0);
    for (auto m : { SaturationMode::tube, SaturationMode::tape })
        for (double drive : { 6.0, 12.0 })
        {
            const double full = meterDb (beat, with (m, drive, 50.0)), bass = meterDb (beat, bassOnly (m, drive, 50.0));
            const double estimate = Saturation::estimateHarmonicsDb (beat.channels, bassOnly (m, drive, 50.0), beat.sampleRate);
            INFO ("mode " << static_cast<int> (m) << " Drive " << drive << ": Full " << full << " dB, Bass only " << bass << " dB (estimate " << estimate << ")");
            CHECK (std::abs (bass - full) < 2.5);
            CHECK (std::abs (estimate - bass) < 0.5);   // Auto-Master's quick estimate matches the meter
        }

    // Tube +12, Mix 50 % (Auto-Master's usual trap setting): loudness and peaks hardly move.
    const auto out = saturate (beat, bassOnly (SaturationMode::tube, 12.0, 50.0));
    AudioClip aligned = beat;
    for (auto& ch : aligned.channels)
    {
        ch.insert (ch.begin(), static_cast<size_t> (Saturation::kLatency), 0.0f);
        ch.resize (ch.size() - static_cast<size_t> (Saturation::kLatency));
    }
    const auto a = measure (aligned), b = measure (out);
    INFO ("loudness " << b.integratedLufs - a.integratedLufs << " LU, true peak " << b.truePeakDbtp - a.truePeakDbtp << " dB");
    CHECK (std::abs (b.integratedLufs - a.integratedLufs) < 0.3);
    CHECK (b.truePeakDbtp - a.truePeakDbtp < 0.5);
}

TEST_CASE ("Saturation Focus switches without clicks or thumps, at constant latency", "[saturation][focus]")
{
    const double sr = 48000.0;
    // Second difference: the sharpest bend in the waveform. A click bends far more sharply than the music.
    const auto sharpest = [] (const AudioClip& c, int from)
    {
        const auto& x = c.channels[0];
        double worst = 0.0;
        for (size_t i = static_cast<size_t> (std::max (2, from)); i < x.size(); ++i)
            worst = std::max (worst, std::abs (static_cast<double> (x[i]) - 2.0 * x[i - 1] + x[i - 2]));
        return worst;
    };
    for (auto m : { SaturationMode::tube, SaturationMode::clip })
    {
        const auto note = testsig::sine (sr, 2, 50.0, -3.0, 2.0);
        const auto steadyFull = render<Saturation> (note, [&] (int) { return with (m, 18.0); }, true);
        const auto steadyBass = render<Saturation> (note, [&] (int) { return bassOnly (m, 18.0); }, true);
        const auto switching = render<Saturation> (note, [&] (int i) { return (i / static_cast<int> (0.2 * sr)) % 2 ? bassOnly (m, 18.0) : with (m, 18.0); }, true);
        const double limit = std::max (sharpest (steadyFull, 4800), sharpest (steadyBass, 4800));
        INFO ("mode " << static_cast<int> (m) << ": sharpest bend switching " << sharpest (switching, 4800) << ", steady " << limit);
        CHECK (sharpest (switching, 4800) < 1.1 * limit);
        // Each switch lands on the other setting's sound within 0.15 s (the cross-fade finishes).
        double err = 0.0, sig = 0.0;
        for (size_t i = static_cast<size_t> (0.35 * sr); i < static_cast<size_t> (0.4 * sr); ++i)
        {
            const double d = switching.channels[0][i] - steadyBass.channels[0][i];
            err += d * d;
            sig += static_cast<double> (steadyBass.channels[0][i]) * steadyBass.channels[0][i];
        }
        INFO ("after the switch: " << 10.0 * std::log10 (err / sig + 1e-30) << " dB from steady Bass only");
        CHECK (10.0 * std::log10 (err / sig + 1e-30) < -60.0);
    }

    // Tube, Drive +18 on a loud 60 Hz note: switching Full -> Bass only -> Full lets out no DC (averaged over
    // whole cycles, which removes the note and all its harmonics), once each change is through (warm-up and
    // cross-fade, 0.1 s; during the cross-fade the two sounds' notes don't cancel within a cycle, which isn't DC).
    // Engaged while playing, as in the test above.
    const int period = 800, n = static_cast<int> (sr * 2.0);
    const int changes[] = { 1, n / 4, 5 * n / 8 };   // engage (Full), to Bass only, back to Full
    Saturation sat;
    sat.prepare (sr, 2);
    auto p = with (SaturationMode::tube, 0.0);
    sat.setParams (p);
    sat.reset();
    std::vector<double> out (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        if (std::find (std::begin (changes), std::end (changes), i) != std::end (changes))
        {
            p.driveDb = 18.0;
            p.focus = i == n / 4 ? SaturationFocus::bass : SaturationFocus::full;
            sat.setParams (p);
        }
        const double x = 0.9 * std::sin (2.0 * std::numbers::pi * 60.0 * i / sr);
        double frame[2] = { x, x };
        sat.processFrame (frame, 2);
        out[static_cast<size_t> (i)] = frame[0];
    }
    double sum = 0.0, worst = 0.0;
    for (int i = 0; i < n; ++i)
    {
        sum += out[static_cast<size_t> (i)];
        if (i >= period) sum -= out[static_cast<size_t> (i - period)];
        const bool settled = std::all_of (std::begin (changes), std::end (changes), [&] (int at) { return i < at || i >= at + static_cast<int> (0.1 * sr); });
        if (settled && i >= period) worst = std::max (worst, std::abs (sum / period));
    }
    INFO ("thump while switching Focus: " << 20.0 * std::log10 (worst + 1e-12) << " dBFS");
    CHECK (20.0 * std::log10 (worst + 1e-12) < -40.0);

    // Latency: the same in both settings and while switching (a quiet signal passes delayed, unchanged).
    const auto quiet = testsig::musicLike (sr, 1.0, -50.0);
    const auto moved = render<Saturation> (quiet, [&] (int i) { return (i / 4800) % 2 ? bassOnly (SaturationMode::tube, 18.0) : with (SaturationMode::tube, 18.0); }, true);
    double err = 0.0, sig = 0.0;
    for (size_t i = 4800; i < quiet.channels[0].size(); ++i)
    {
        const double d = moved.channels[0][i] - quiet.channels[0][i - static_cast<size_t> (Saturation::kLatency)];
        err += d * d;
        sig += static_cast<double> (quiet.channels[0][i]) * quiet.channels[0][i];
    }
    CHECK (10.0 * std::log10 (err / sig) < -50.0);
}

TEST_CASE ("Saturation Tube started mid-song (reset) lets out no thump, and changes nothing later", "[saturation]")
{
    // An export or a playback start in the middle of a song begins with reset(): there is no warm-up, so the
    // tube curve's DC can't be measured first. The 1 Hz DC filter then started cold and let the DC out as a
    // slow offset (-43 dBFS at Drive +12, -39 at +18, peaking ~0.1 s in). It now starts at 40 Hz and glides
    // down to 1 Hz: below 10 Hz the render that starts 4 s in stays within -50 dBFS of the continuous one.
    const double sr = 48000.0;
    const auto song = testsig::trapLike (sr, 8.0);
    const int start = static_cast<int> (4.0 * sr) / 256 * 256;
    AudioClip part = song;
    for (auto& ch : part.channels) ch.erase (ch.begin(), ch.begin() + start);
    const auto subsonic = [&] (std::vector<double> x)   // 4th-order low-pass at 10 Hz
    {
        polisher::Biquad a, b;
        polisher::design::apply (a, polisher::design::butterworth (false, 10.0, sr));
        b = a;
        for (double& v : x) v = b.process (a.process (v));
        return x;
    };
    for (double drive : { 12.0, 18.0 })
    {
        const auto p = with (SaturationMode::tube, drive, 50.0);
        const auto whole = render<Saturation> (song, [&] (int) { return p; }, true);
        const auto late = render<Saturation> (part, [&] (int) { return p; }, true);
        std::vector<double> d (late.channels[0].size());
        for (size_t i = 0; i < d.size(); ++i)
            d[i] = static_cast<double> (late.channels[0][i]) - whole.channels[0][i + static_cast<size_t> (start)];
        double peak = 0.0;
        for (double v : subsonic (d)) peak = std::max (peak, std::abs (v));
        INFO ("Drive +" << drive << ": below 10 Hz the late start differs by " << 20.0 * std::log10 (peak + 1e-15) << " dBFS");
        CHECK (20.0 * std::log10 (peak + 1e-15) < -50.0);

        // Played from the start, it is the v0.15.0 sound (the copy without the new start) once the filter
        // has landed on 1 Hz: the glide only shapes the first moments.
        LegacySaturation::copiedDcStart = false;
        const auto v015 = render<LegacySaturation> (song, [&] (int) { return p; }, true);
        LegacySaturation::copiedDcStart = true;
        double diff = 0.0;
        for (size_t i = static_cast<size_t> (2.0 * sr); i < static_cast<size_t> (song.numSamples()); ++i)
            diff = std::max (diff, std::abs (static_cast<double> (whole.channels[0][i]) - v015.channels[0][i]));
        INFO ("after 2 s, against v0.15.0: " << diff);
        CHECK (diff < 1.0e-6);
    }
}

TEST_CASE ("Saturation Mode changes cross-fade: no click, no thump, constant latency", "[saturation][mode]")
{
    // Changing Mode while playing (a click, a snapshot, undo, Auto-Master) used to swap the curve from one
    // sample to the next, and switching to Tube let the DC out as an offset. Now the new Mode warms up
    // next to the one playing (its DC measured meanwhile) and cross-fades in over 30 ms, like Focus.
    const double sr = 48000.0;
    const auto sharpest = [] (const AudioClip& c, int from)
    {
        const auto& x = c.channels[0];
        double worst = 0.0;
        for (size_t i = static_cast<size_t> (std::max (2, from)); i < x.size(); ++i)
            worst = std::max (worst, std::abs (static_cast<double> (x[i]) - 2.0 * x[i - 1] + x[i - 2]));
        return worst;
    };
    for (auto focus : { SaturationFocus::full, SaturationFocus::bass })
    {
        const auto note = testsig::sine (sr, 2, 50.0, -3.0, 2.0);
        double steady = 0.0;
        for (int m = 0; m < 3; ++m)
            steady = std::max (steady, sharpest (render<Saturation> (note, [&] (int) { return with (static_cast<SaturationMode> (m), 18.0, 100.0, focus); }, true), 4800));
        const auto cycling = render<Saturation> (note, [&] (int i) { return with (static_cast<SaturationMode> ((i / static_cast<int> (0.2 * sr)) % 3), 18.0, 100.0, focus); }, true);
        INFO ("focus " << static_cast<int> (focus) << ": sharpest bend while Mode cycles " << sharpest (cycling, 4800) << ", steady " << steady);
        CHECK (sharpest (cycling, 4800) < 1.1 * steady);

        // Each change lands on the new Mode's sound within 0.15 s (Tape -> Clip: no DC filter in either).
        const auto toClip = render<Saturation> (note, [&] (int i) { return with (i < static_cast<int> (0.5 * sr) ? SaturationMode::tape : SaturationMode::clip, 18.0, 100.0, focus); }, true);
        const auto clip = render<Saturation> (note, [&] (int) { return with (SaturationMode::clip, 18.0, 100.0, focus); }, true);
        double err = 0.0, sig = 0.0;
        for (size_t i = static_cast<size_t> (0.65 * sr); i < static_cast<size_t> (0.7 * sr); ++i)
        {
            const double d = toClip.channels[0][i] - clip.channels[0][i];
            err += d * d;
            sig += static_cast<double> (clip.channels[0][i]) * clip.channels[0][i];
        }
        INFO ("0.15 s after Tape -> Clip: " << 10.0 * std::log10 (err / sig + 1e-30) << " dB from steady Clip");
        CHECK (10.0 * std::log10 (err / sig + 1e-30) < -60.0);
    }

    // A loud 60 Hz note, Drive +12 / +18: changing to Tube (from Tape or Clip) lets out no DC once the change
    // is through (averaged over whole cycles, which removes the note and all its harmonics). It used to
    // leave -25 to -36 dBFS.
    const int period = 800, n = static_cast<int> (sr * 2.0), at = n / 4;
    for (auto from : { SaturationMode::tape, SaturationMode::clip })
        for (double drive : { 12.0, 18.0 })
        {
            Saturation sat;
            sat.prepare (sr, 2);
            sat.setParams (with (from, drive));
            sat.reset();
            std::vector<double> out (static_cast<size_t> (n));
            for (int i = 0; i < n; ++i)
            {
                if (i == at) sat.setParams (with (SaturationMode::tube, drive));
                const double x = 0.9 * std::sin (2.0 * std::numbers::pi * 60.0 * i / sr);
                double frame[2] = { x, x };
                sat.processFrame (frame, 2);
                out[static_cast<size_t> (i)] = frame[0];
            }
            double sum = 0.0, worst = 0.0;
            for (int i = 0; i < n; ++i)
            {
                sum += out[static_cast<size_t> (i)];
                if (i >= period) sum -= out[static_cast<size_t> (i - period)];
                if (i >= at + static_cast<int> (0.1 * sr)) worst = std::max (worst, std::abs (sum / period));
            }
            INFO ("Mode " << static_cast<int> (from) << " -> Tube at Drive +" << drive << ": DC " << 20.0 * std::log10 (worst + 1e-12) << " dBFS");
            CHECK (20.0 * std::log10 (worst + 1e-12) < -40.0);
        }

    // Latency: the same while the Mode changes (a quiet signal passes delayed, unchanged).
    const auto quiet = testsig::musicLike (sr, 1.0, -50.0);
    const auto moved = render<Saturation> (quiet, [&] (int i) { return with (static_cast<SaturationMode> ((i / 4800) % 3), 18.0); }, true);
    double err = 0.0, sig = 0.0;
    for (size_t i = 4800; i < quiet.channels[0].size(); ++i)
    {
        const double d = moved.channels[0][i] - quiet.channels[0][i - static_cast<size_t> (Saturation::kLatency)];
        err += d * d;
        sig += static_cast<double> (quiet.channels[0][i]) * quiet.channels[0][i];
    }
    CHECK (10.0 * std::log10 (err / sig) < -50.0);
}

TEST_CASE ("Saturation Highs only adds sheen up top and leaves the vocal and 808 clean; Bass + Highs does both", "[saturation][focus]")
{
    // A hat-like 6 kHz tone, a vocal-like 1 kHz tone and an 808-like 45 Hz note, together.
    const double sr = 48000.0;
    auto mixed = testsig::sine (sr, 2, 6000.0, -12.0, 2.0);
    for (auto [hz, db] : { std::pair { 1000.0, -9.0 }, std::pair { 45.0, -6.0 } })
    {
        const auto t = testsig::sine (sr, 2, hz, db, 2.0);
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < mixed.channels[c].size(); ++i) mixed.channels[c][i] += t.channels[c][i];
    }
    for (auto m : { SaturationMode::tape, SaturationMode::tube })
    {
        const auto highs = saturate (mixed, with (m, 12.0, 100.0, SaturationFocus::highs));
        const auto both = saturate (mixed, with (m, 12.0, 100.0, SaturationFocus::bassHighs));
        const double sheen = m == SaturationMode::tube ? 12000.0 : 18000.0;   // tube: the 2nd; tape: the 3rd
        INFO ("mode " << static_cast<int> (m) << ": Highs only " << levelAt (highs, sheen) << " dB at " << sheen << " Hz, 2 kHz "
              << levelAt (highs, 2000.0) << ", 135 Hz " << levelAt (highs, 135.0) << "; Bass + Highs 135 Hz " << levelAt (both, 135.0)
              << ", " << sheen << " Hz " << levelAt (both, sheen) << ", 2 kHz " << levelAt (both, 2000.0));
        CHECK (levelAt (highs, sheen) > -60.0);          // sheen on the hat
        CHECK (levelAt (highs, 2000.0) < -100.0);        // the vocal gets no harmonics
        CHECK (levelAt (highs, 135.0) < -100.0);         // the 808 gets none either
        CHECK (levelAt (highs, 5000.0) < -70.0);         // no audible difference tone under the hat (6 kHz - 1 kHz): > 60 dB down
        CHECK_THAT (levelAt (highs, 1000.0), WithinAbs (-9.0, 0.1));
        CHECK (levelAt (both, 135.0) > -40.0);           // Bass + Highs: the 808's 3rd...
        CHECK (levelAt (both, sheen) > -60.0);           // ...and the sheen...
        CHECK (levelAt (both, 2000.0) < -60.0);          // ...and the vocal still (almost) clean
    }
}

TEST_CASE ("Saturation switches between all four Focus settings without clicks, at constant latency", "[saturation][focus]")
{
    const double sr = 48000.0;
    const auto sharpest = [] (const AudioClip& c, int from)
    {
        const auto& x = c.channels[0];
        double worst = 0.0;
        for (size_t i = static_cast<size_t> (std::max (2, from)); i < x.size(); ++i)
            worst = std::max (worst, std::abs (static_cast<double> (x[i]) - 2.0 * x[i - 1] + x[i - 2]));
        return worst;
    };
    auto sig = testsig::sine (sr, 2, 50.0, -3.0, 2.4);
    const auto hat = testsig::sine (sr, 2, 7000.0, -14.0, 2.4);
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < sig.channels[c].size(); ++i) sig.channels[c][i] += hat.channels[c][i];
    const SaturationFocus order[] = { SaturationFocus::full, SaturationFocus::highs, SaturationFocus::bassHighs, SaturationFocus::bass };
    const auto pick = [&] (int i) { return with (SaturationMode::tube, 15.0, 100.0, order[(i / static_cast<int> (0.3 * sr)) % 4]); };
    const auto switching = render<Saturation> (sig, pick, true);
    double limit = 0.0;
    for (auto f : order)
        limit = std::max (limit, sharpest (render<Saturation> (sig, [&] (int) { return with (SaturationMode::tube, 15.0, 100.0, f); }, true), 4800));
    INFO ("sharpest bend switching " << sharpest (switching, 4800) << ", steady " << limit);
    CHECK (sharpest (switching, 4800) < 1.1 * limit);

    // Latency: a quiet signal passes delayed and unchanged whatever the Focus.
    const auto quiet = testsig::musicLike (sr, 1.2, -50.0);
    const auto moved = render<Saturation> (quiet, [&] (int i) { return with (SaturationMode::tape, 18.0, 100.0, order[(i / 4800) % 4]); }, true);
    double err = 0.0, s = 0.0;
    for (size_t i = 4800; i < quiet.channels[0].size(); ++i)
    {
        const double d = moved.channels[0][i] - quiet.channels[0][i - static_cast<size_t> (Saturation::kLatency)];
        err += d * d;
        s += static_cast<double> (quiet.channels[0][i]) * quiet.channels[0][i];
    }
    CHECK (10.0 * std::log10 (err / s) < -50.0);
}

TEST_CASE ("Saturation Highs only adds sheen and keeps the top's level", "[saturation][focus]")
{
    // Review find: the top band is phase-turned against the dry path, so the curve's rounding of a loud hat
    // came out as a boost (+2.6 dB at 6 kHz). Only the new harmonics are added now: the hat keeps its level.
    const double sr = 48000.0;
    for (double drive : { 12.0, 18.0 })
        for (double hz : { 5000.0, 6000.0, 8000.0, 16000.0 })
        {
            const auto hat = testsig::sine (sr, 2, hz, -12.0, 1.0);
            const auto out = saturate (hat, with (SaturationMode::tape, drive, 100.0, SaturationFocus::highs));
            INFO ("Drive " << drive << ", " << hz << " Hz: " << levelAt (out, hz) << " dB (in -12), 3rd " << levelAt (out, 3.0 * hz));
            CHECK_THAT (levelAt (out, hz), WithinAbs (-12.0, 0.3));
            if (3.0 * hz < 0.45 * sr) CHECK (levelAt (out, 3.0 * hz) > -60.0);   // the sheen is there
        }
    // On a mix, the estimate Auto-Master would use lands near the real meter.
    const auto mix = testsig::musicLike (sr, 3.0, -1.0);
    for (auto f : { SaturationFocus::highs, SaturationFocus::bassHighs })
    {
        const auto p = with (SaturationMode::tape, 12.0, 100.0, f);
        Saturation s;
        s.prepare (sr, 2);
        s.setParams (p);
        s.reset();
        auto c = mix;
        float* ch[2] = { c.channels[0].data(), c.channels[1].data() };
        s.process (ch, 2, c.numSamples());
        const double real = s.takeHarmonicsDb(), est = Saturation::estimateHarmonicsDb (mix.channels, p, sr);
        INFO ("focus " << static_cast<int> (f) << ": meter " << real << " dB, estimate " << est << " dB");
        CHECK (std::abs (real - est) < 3.0);
    }
}

#include "polisher/Saturation.h"

#include "polisher/FilterDesign.h"

#include <numbers>

namespace polisher {

double Saturation::shape (double x, SaturationMode mode, double s) noexcept
{
    const double u = x / s;
    switch (mode)
    {
        case SaturationMode::tape:
            return s * std::tanh (u);
        case SaturationMode::tube:
        {
            const double tb = std::tanh (kTubeBias);
            return s * (std::tanh (u + kTubeBias) - tb) / (1.0 - tb * tb);
        }
        case SaturationMode::clip:
        {
            const double u2 = u * u;
            return x / std::sqrt (std::sqrt (1.0 + u2 * u2));
        }
    }
    return x;
}

double Saturation::estimateHarmonicsDb (const std::vector<std::vector<float>>& channels, const SaturationParams& p, double sampleRate)
{
    const double m = effectiveMix (p);
    if (m <= 0.0)
        return -100.0;
    const bool bassOnly = p.focus == SaturationFocus::bass;
    const double s = bendFor (p.driveDb, p.focus);
    const auto lp = design::butterworth (false, kBassSplitHz, sampleRate);
    const auto hp = design::butterworth (true, kBassResidualHz, sampleRate);
    double res = 0.0, sig = 0.0;
    for (const auto& ch : channels)
    {
        double sum = 0.0, sumSq = 0.0;
        if (bassOnly)
        {
            // The same filters as the plug-in (the high-pass also removes the tube curve's DC).
            Biquad split[2], post;
            for (auto& f : split) design::apply (f, lp);
            design::apply (post, hp);
            for (float v : ch)
            {
                const double x = v, low = split[1].process (split[0].process (x));
                const double r = post.process (shape (low, p.mode, s) - low);
                sumSq += r * r;
                sig += x * x;
            }
            res += sumSq;
            continue;
        }
        for (float v : ch)
        {
            const double x = v, r = shape (x, p.mode, s) - x;
            sum += r;
            sumSq += r * r;
            sig += x * x;
        }
        const double n = std::max<double> (1.0, static_cast<double> (ch.size()));
        res += sumSq - sum * sum / n;   // without the DC the tube curve makes (removed in the plug-in)
    }
    const double ratio = sig > 0.0 ? m * m * res / sig : 0.0;
    return ratio > 1.0e-10 ? 10.0 * std::log10 (ratio) : -100.0;
}

void Saturation::prepare (double sampleRate, int numChannels)
{
    channelCount = std::clamp (numChannels, 1, kMaxChannels);
    channelState.assign (static_cast<size_t> (channelCount), Channel {});
    const auto lp = design::butterworth (false, kBassSplitHz, sampleRate);
    const auto hp = design::butterworth (true, kBassResidualHz, sampleRate);
    for (auto& ch : channelState)
    {
        ch.dry.assign (kLatency + 1, 0.0);
        for (auto& f : ch.split) design::apply (f, lp);
        design::apply (ch.bassHp, hp);
    }
    glideCoeff = 1.0 - std::exp (-1.0 / (0.030 * sampleRate));
    mixCoeff = 1.0 - std::exp (-4.0 / (0.020 * sampleRate));   // settles within ~20 ms
    hpCoeff = std::exp (-2.0 * std::numbers::pi * 1.0 / sampleRate);
    dcWindow = static_cast<int> (0.05 * sampleRate);   // two cycles of the lowest 808 note
    bassBendScale = std::pow (10.0, -kBassDriveDb / 20.0);
    focusStep = 1.0 / (kFocusFadeSeconds * sampleRate);
    reset();
}

void Saturation::startPath (bool bass, int warmSamples) noexcept
{
    auto& path = bass ? bassPath : fullPath;
    path.active = true;
    path.warm = warmSamples;
}

void Saturation::stopPath (bool bass) noexcept
{
    auto& path = bass ? bassPath : fullPath;
    path.active = false;
    path.warm = 0;
    for (auto& ch : channelState)   // start clean next time
    {
        if (bass)
        {
            for (auto& st : ch.bassStages) st.reset();
            for (auto& f : ch.split) f.reset();
            ch.bassHp.reset();
        }
        else
        {
            for (auto& st : ch.stages) st.reset();
            ch.hpX = ch.hpY = ch.dcSum = 0.0;
        }
    }
    if (! bass)
        dcCount = 0;
}

void Saturation::reset() noexcept
{
    for (auto& ch : channelState)
    {
        std::fill (ch.dry.begin(), ch.dry.end(), 0.0);
        ch.dryPos = 0;
    }
    stopPath (false);
    stopPath (true);
    dcCount = 0;
    bendTarget = std::pow (10.0, (kHeadroomDb - std::clamp (params.driveDb, 0.0, kMaxDriveDb)) / 20.0);
    bend = bendTarget;
    // Start fully in the requested state (offline renders and a fresh prepare): no fade-in.
    running = ! isNeutral (params);
    mix = effectiveMix (params);
    focus = params.focus == SaturationFocus::bass ? 1.0 : 0.0;
    if (running)
        startPath (focus > 0.5, 0);
    resEnergy = sigEnergy = 0.0;
}

void Saturation::processFrame (double* frame, int numChannels) noexcept
{
    const int nch = std::min (numChannels, channelCount);
    const double mixTarget = effectiveMix (params);
    const double focusTarget = params.focus == SaturationFocus::bass ? 1.0 : 0.0;
    bendTarget = std::pow (10.0, (kHeadroomDb - std::clamp (params.driveDb, 0.0, kMaxDriveDb)) / 20.0);

    // Let the filters fill, then (Full, tube) measure the curve's DC for dcWindow, before a path is heard.
    const int warmSamples = 2 * kLatency + dcWindow;
    const bool idle = mix < 1.0e-6 && mixTarget == 0.0 && fullPath.warm == 0 && bassPath.warm == 0;
    if (idle)
    {
        if (mix != 0.0 || running)
        {
            mix = 0.0;
            running = false;
            stopPath (false);
            stopPath (true);
        }
        focus = focusTarget;   // nothing is heard: the next start begins in the right Focus
    }
    else
    {
        if (! running)
        {
            running = true;
            focus = focusTarget;
            startPath (focus > 0.5, warmSamples);
            bend = bendTarget;
        }
        else if (! (focusTarget > 0.5 ? bassPath : fullPath).active)
            startPath (focusTarget > 0.5, warmSamples);   // Focus changed: warm the other path up next to this one

        // Mix waits while a path that is heard warms up (only at the start); the Focus cross-fade waits
        // for the path it goes to.
        const bool heardWarming = (focus < 1.0 && fullPath.warm > 0) || (focus > 0.0 && bassPath.warm > 0);
        for (auto* path : { &fullPath, &bassPath })
            if (path->warm > 0)
                --path->warm;
        if (! heardWarming)
            mix += (mixTarget - mix) * mixCoeff;
        if (focus != focusTarget && (focusTarget > 0.5 ? bassPath : fullPath).warm == 0)
            focus = focusTarget > focus ? std::min (focusTarget, focus + focusStep) : std::max (focusTarget, focus - focusStep);
        if (focus == focusTarget && (focusTarget > 0.5 ? fullPath : bassPath).active)
            stopPath (focusTarget < 0.5);   // faded out (or switched back before it was heard)
        bend += (bendTarget - bend) * glideCoeff;
    }

    // The 1 Hz DC filter would need ~0.5 s to settle from cold and let the DC out as a slow thump;
    // instead it starts from the DC measured during the warm-up, as if it had been running all along.
    const bool runFull = ! idle && fullPath.active, runBass = ! idle && bassPath.active;
    const bool measuringDc = runFull && fullPath.warm > 0 && fullPath.warm <= dcWindow;
    const bool seedDc = runFull && fullPath.warm == 0 && dcCount > 0;
    // A Hann-weighted mean, so the bass itself (whatever its period) hardly leaks into the DC estimate.
    const double dcWeight = measuringDc ? std::pow (std::sin (std::numbers::pi * (dcCount + 0.5) / dcWindow), 2.0) : 0.0;
    const double bassBend = bend * bassBendScale;
    // The Focus cross-fade follows a raised cosine: it starts and ends gently, so the waveform never kinks.
    const double bassWeight = focus > 0.0 && focus < 1.0 ? 0.5 - 0.5 * std::cos (std::numbers::pi * focus) : focus;

    for (int c = 0; c < nch; ++c)
    {
        auto& ch = channelState[static_cast<size_t> (c)];
        const double x = frame[c];

        // Dry path: a plain delay, so pass-through is exact.
        const int size = static_cast<int> (ch.dry.size());
        ch.dry[static_cast<size_t> (ch.dryPos)] = x;
        const double dry = ch.dry[static_cast<size_t> ((ch.dryPos + 1) % size)];   // kLatency samples ago
        ch.dryPos = (ch.dryPos + 1) % size;

        if (idle)
        {
            frame[c] = dry;
            continue;
        }

        if (runFull)
        {
            // Up 4x, keep only what the curve adds (the clean signal never goes through the filters),
            // down 4x, remove DC.
            double a, b, u[4];
            ch.stages[0].upsample (x, a, b);
            ch.stages[1].upsample (a, u[0], u[1]);
            ch.stages[1].upsample (b, u[2], u[3]);
            for (double& v : u) v = curve (v) - v;
            const double ra = ch.stages[1].downsample (u[0], u[1]);
            const double rb = ch.stages[1].downsample (u[2], u[3]);
            const double res = ch.stages[0].downsample (ra, rb);
            // Only the tube curve is asymmetric and makes DC; a 1 Hz high-pass removes it without
            // shifting the bass part of the residual against the dry signal.
            if (measuringDc) ch.dcSum += dcWeight * res;
            if (seedDc)
            {
                ch.hpX = ch.dcSum / (0.5 * dcCount);   // the Hann weights sum to half the window
                ch.hpY = 0.0;
                ch.dcSum = 0.0;
            }
            if (params.mode == SaturationMode::tube)
            {
                ch.hpY = hpCoeff * (ch.hpY + res - ch.hpX);
                ch.hpX = res;
            }
            else
            {
                ch.hpY = res;
                ch.hpX = 0.0;
            }
        }

        double wet = ch.hpY;
        if (runBass)
        {
            // Bass only: the same, on the low end alone; the high-pass keeps the harmonics above the
            // 808's fundamental (and removes the tube curve's DC within a few ms).
            const double low = ch.split[1].process (ch.split[0].process (x));
            double a, b, u[4];
            ch.bassStages[0].upsample (low, a, b);
            ch.bassStages[1].upsample (a, u[0], u[1]);
            ch.bassStages[1].upsample (b, u[2], u[3]);
            for (double& v : u) v = shape (v, params.mode, bassBend) - v;
            const double ra = ch.bassStages[1].downsample (u[0], u[1]);
            const double rb = ch.bassStages[1].downsample (u[2], u[3]);
            const double bassWet = ch.bassHp.process (ch.bassStages[0].downsample (ra, rb));
            wet = focus >= 1.0 ? bassWet : (1.0 - bassWeight) * ch.hpY + bassWeight * bassWet;
        }

        resEnergy += mix * mix * wet * wet;   // what is actually added at this Mix
        sigEnergy += dry * dry;
        frame[c] = dry + mix * wet;
    }
    dcCount = measuringDc ? dcCount + 1 : 0;
}

} // namespace polisher

#include "polisher/Saturation.h"

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

void Saturation::prepare (double sampleRate, int numChannels)
{
    channelCount = std::clamp (numChannels, 1, kMaxChannels);
    channelState.assign (static_cast<size_t> (channelCount), Channel {});
    for (auto& ch : channelState)
        ch.dry.assign (kLatency + 1, 0.0);
    glideCoeff = 1.0 - std::exp (-1.0 / (0.030 * sampleRate));
    mixCoeff = 1.0 - std::exp (-4.0 / (0.020 * sampleRate));   // settles within ~20 ms
    hpCoeff = std::exp (-2.0 * std::numbers::pi * 1.0 / sampleRate);
    reset();
}

void Saturation::reset() noexcept
{
    for (auto& ch : channelState)
    {
        for (auto& s : ch.stages) s.reset();
        std::fill (ch.dry.begin(), ch.dry.end(), 0.0);
        ch.dryPos = 0;
        ch.hpX = ch.hpY = 0.0;
    }
    bendTarget = std::pow (10.0, (kHeadroomDb - std::clamp (params.driveDb, 0.0, kMaxDriveDb)) / 20.0);
    bend = bendTarget;
    // Start fully in the requested state (offline renders and a fresh prepare): no fade-in.
    running = ! isNeutral (params);
    mix = running ? std::clamp (params.mix, 0.0, 100.0) / 100.0 : 0.0;
    warmup = 0;
    resEnergy = sigEnergy = 0.0;
}

void Saturation::processFrame (double* frame, int numChannels) noexcept
{
    const int nch = std::min (numChannels, channelCount);
    const double mixTarget = isNeutral (params) ? 0.0 : std::clamp (params.mix, 0.0, 100.0) / 100.0;
    bendTarget = std::pow (10.0, (kHeadroomDb - std::clamp (params.driveDb, 0.0, kMaxDriveDb)) / 20.0);

    const bool idle = mix < 1.0e-6 && mixTarget == 0.0 && warmup == 0;
    if (idle)
    {
        if (mix != 0.0 || running)
        {
            mix = 0.0;
            running = false;
            for (auto& ch : channelState)
            {
                for (auto& st : ch.stages) st.reset();   // start clean next time
                ch.hpX = ch.hpY = 0.0;
            }
        }
    }
    else
    {
        if (! running)
        {
            running = true;
            warmup = 2 * kLatency;   // let the filters fill before any of the wet path is heard
            bend = bendTarget;
        }
        if (warmup > 0)
            --warmup;
        else
            mix += (mixTarget - mix) * mixCoeff;
        bend += (bendTarget - bend) * glideCoeff;
    }

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

        resEnergy += ch.hpY * ch.hpY;
        sigEnergy += dry * dry;
        frame[c] = dry + mix * ch.hpY;
    }
}

} // namespace polisher

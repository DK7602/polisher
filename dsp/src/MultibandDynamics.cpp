#include "polisher/MultibandDynamics.h"
#include "polisher/FilterDesign.h"

namespace polisher {

namespace {
    double perSample (double seconds, double sr) noexcept { return 1.0 - std::exp (-1.0 / (std::max (seconds, 1.0e-5) * sr)); }

    constexpr std::array<double, kDynBands> kRmsWindowMs { 30.0, 10.0, 5.0, 3.0 };
}

bool MultibandDynamics::isNeutral (const DynamicsParams& p) noexcept
{
    if (! p.enabled)
        return true;
    for (const auto& b : p.bands)
        if (b.ratio > 1.0 || std::abs (b.gainDb) >= kDynGainNeutralDb)
            return false;
    return true;
}

void MultibandDynamics::prepare (double sr, int numChannels)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    channelCount = std::clamp (numChannels, 1, 2);
    mixCoeff = perSample (0.02, sampleRate);
    makeupCoeff = perSample (0.02, sampleRate);
    averageCoeff = perSample (0.4, sampleRate);
    glideCoeff = 1.0 - std::exp (-static_cast<double> (kUpdateInterval) / (0.02 * sampleRate));
    for (size_t k = 0; k < kDynBands; ++k)
    {
        det[k].rmsCoeff = perSample (kRmsWindowMs[k] / 1000.0, sampleRate);
        det[k].setAttackMs = det[k].setReleaseMs = -1.0;   // force coefficient refresh
    }
    reset();
}

void MultibandDynamics::resetFilters() noexcept
{
    for (auto& c : chan)
    {
        for (auto* g : { &c.x1lp, &c.x1hp, &c.x2lp, &c.x2hp, &c.x3lp, &c.x3hp })
            for (auto& f : *g) f.reset();
        c.apX1.reset();
        c.apX3.reset();
    }
}

void MultibandDynamics::resetDetectors() noexcept
{
    for (auto& d : det)
    {
        d.meanSquare = 0.0;
        d.channelMeanSquare = {};
        d.grDb = d.grAverage = 0.0;
    }
}

void MultibandDynamics::reset() noexcept
{
    resetFilters();
    resetDetectors();
    for (size_t k = 0; k < kDynBands - 1; ++k)
        logFreq[k] = targetLogFreq[k] = std::log (std::clamp (params.crossoverHz[k], kDynCrossoverRange[k][0], kDynCrossoverRange[k][1]));
    applyCrossovers();
    running = ! isNeutral (params);
    clocking = running || monitorBand >= 0;
    mix = mixTarget = running ? 1.0 : 0.0;
    for (size_t k = 0; k < kDynBands; ++k)
    {
        const double g = params.bands[k].gainDb;
        det[k].makeupDb = det[k].targetMakeupDb = running && std::abs (g) >= kDynGainNeutralDb ? std::clamp (g, -24.0, 24.0) : 0.0;
        det[k].maxGr = 0.0;
    }
    countdown = 0;
}

void MultibandDynamics::applyCrossovers() noexcept
{
    const double f1 = std::exp (logFreq[0]), f2 = std::exp (logFreq[1]), f3 = std::exp (logFreq[2]);
    const auto lp1 = design::butterworth (false, f1, sampleRate), hp1 = design::butterworth (true, f1, sampleRate);
    const auto lp2 = design::butterworth (false, f2, sampleRate), hp2 = design::butterworth (true, f2, sampleRate);
    const auto lp3 = design::butterworth (false, f3, sampleRate), hp3 = design::butterworth (true, f3, sampleRate);
    const auto ap1 = design::allpass (f1, sampleRate), ap3 = design::allpass (f3, sampleRate);
    for (auto& c : chan)
    {
        for (auto& f : c.x1lp) design::apply (f, lp1);
        for (auto& f : c.x1hp) design::apply (f, hp1);
        for (auto& f : c.x2lp) design::apply (f, lp2);
        for (auto& f : c.x2hp) design::apply (f, hp2);
        for (auto& f : c.x3lp) design::apply (f, lp3);
        for (auto& f : c.x3hp) design::apply (f, hp3);
        design::apply (c.apX1, ap1);
        design::apply (c.apX3, ap3);
    }
}

void MultibandDynamics::update() noexcept
{
    const bool neutral = isNeutral (params);

    // Crossover targets and glide.
    bool moved = false;
    for (size_t k = 0; k < kDynBands - 1; ++k)
    {
        targetLogFreq[k] = std::log (std::clamp (params.crossoverHz[k], kDynCrossoverRange[k][0], kDynCrossoverRange[k][1]));
        const double d = targetLogFreq[k] - logFreq[k];
        if (d != 0.0)
        {
            logFreq[k] = std::abs (d) < 1.0e-5 ? targetLogFreq[k] : logFreq[k] + d * glideCoeff;
            moved = true;
        }
    }

    if (! neutral)
    {
        mixTarget = 1.0;
        if (! running)
        {
            // Starting from a clean state; the cross-fade hides the filters settling. (Filters a solo
            // is already running are left alone below, so the solo doesn't click.)
            resetDetectors();
            running = true;
        }
    }
    else
    {
        mixTarget = 0.0;
        if (running && mix < 1.0e-5)
        {
            running = false;   // faded out: exact pass-through again
            mix = 0.0;
        }
    }
    const bool split = running || monitorBand >= 0;
    if (split && ! clocking)
    {
        resetFilters();
        logFreq = targetLogFreq;
        moved = true;
    }
    clocking = split;
    if (moved)
        applyCrossovers();

    // Snap tiny state to zero so silence never decays into denormals, which are very slow on some
    // CPUs (offline renders run without the host's flush-to-zero).
    const auto flush = [] (double& v) { if (std::abs (v) < 1.0e-20) v = 0.0; };
    for (auto& c : chan)
    {
        for (auto* g : { &c.x1lp, &c.x1hp, &c.x2lp, &c.x2hp, &c.x3lp, &c.x3hp })
            for (auto& f : *g) { flush (f.z1); flush (f.z2); }
        for (auto* f : { &c.apX1, &c.apX3 }) { flush (f->z1); flush (f->z2); }
    }
    for (auto& d : det)
    {
        flush (d.grDb);
        flush (d.grAverage);
        if (std::abs (d.makeupDb - d.targetMakeupDb) < 1.0e-9) d.makeupDb = d.targetMakeupDb;
    }

    for (size_t k = 0; k < kDynBands; ++k)
    {
        auto& d = det[k];
        const auto& b = params.bands[k];
        d.targetMakeupDb = params.enabled && std::abs (b.gainDb) >= kDynGainNeutralDb ? std::clamp (b.gainDb, -24.0, 24.0) : 0.0;
        if (b.attackMs != d.setAttackMs)
        {
            d.setAttackMs = b.attackMs;
            d.attackCoeff = perSample (std::clamp (b.attackMs, 0.1, 500.0) / 1000.0, sampleRate);
        }
        if (b.releaseMs != d.setReleaseMs)
        {
            d.setReleaseMs = b.releaseMs;
            const double rel = std::clamp (b.releaseMs, 5.0, 5000.0) / 1000.0;
            d.releaseCoeff = perSample (rel, sampleRate);
            d.fastReleaseCoeff = perSample (std::max (0.01, rel * 0.3), sampleRate);
        }
    }
}

} // namespace polisher

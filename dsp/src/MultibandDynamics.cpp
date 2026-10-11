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
    fadeStep = 1.0 / (ParkGlide::kFadeSeconds * sampleRate);
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

namespace {
    void resetSplit (auto& c) noexcept
    {
        for (auto* g : { &c.x1lp, &c.x1hp, &c.x2lp, &c.x2hp, &c.x3lp, &c.x3hp })
            for (auto& f : *g) f.reset();
        c.apX1.reset();
        c.apX3.reset();
    }
    void designSplit (auto& c, double f1, double f2, double f3, double sampleRate) noexcept
    {
        // (Exactly the shared designs at the settings; ParkGlide's also reach the park just under Nyquist.)
        const auto lp1 = ParkGlide::butterworth (false, f1, sampleRate), hp1 = ParkGlide::butterworth (true, f1, sampleRate);
        const auto lp2 = ParkGlide::butterworth (false, f2, sampleRate), hp2 = ParkGlide::butterworth (true, f2, sampleRate);
        const auto lp3 = ParkGlide::butterworth (false, f3, sampleRate), hp3 = ParkGlide::butterworth (true, f3, sampleRate);
        for (auto& f : c.x1lp) design::apply (f, lp1);
        for (auto& f : c.x1hp) design::apply (f, hp1);
        for (auto& f : c.x2lp) design::apply (f, lp2);
        for (auto& f : c.x2hp) design::apply (f, hp2);
        for (auto& f : c.x3lp) design::apply (f, lp3);
        for (auto& f : c.x3hp) design::apply (f, hp3);
        design::apply (c.apX1, ParkGlide::allpass (f1, sampleRate));
        design::apply (c.apX3, ParkGlide::allpass (f3, sampleRate));
    }
    void copyCoefficients (const auto& from, auto& to) noexcept
    {
        const auto one = [] (const Biquad& f, Biquad& t) { t.b0 = f.b0; t.b1 = f.b1; t.b2 = f.b2; t.a1 = f.a1; t.a2 = f.a2; };
        for (size_t i = 0; i < 2; ++i)
        {
            one (from.x1lp[i], to.x1lp[i]); one (from.x1hp[i], to.x1hp[i]);
            one (from.x2lp[i], to.x2lp[i]); one (from.x2hp[i], to.x2hp[i]);
            one (from.x3lp[i], to.x3lp[i]); one (from.x3hp[i], to.x3hp[i]);
        }
        one (from.apX1, to.apX1);
        one (from.apX3, to.apX3);
    }
}

void MultibandDynamics::resetFilters() noexcept
{
    for (auto& c : chan)
        resetSplit (c);
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
    for (auto& sg : segment) sg.stop();
    gliding = false;
    applyCrossovers();
    running = engaged = ! isNeutral (params);   // start fully in the set state
    clocking = running || monitorBand >= 0;
    mix = fade = fadeGoal = running ? 1.0 : 0.0;
    compWeight = comp = compGoal = running ? 1.0 : 0.0;
    bandsWeight = bands = bandsGoal = 1.0;
    auxRunning = false;
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
    designSplit (chan[0], std::exp (logFreq[0]), std::exp (logFreq[1]), std::exp (logFreq[2]), sampleRate);
    copyCoefficients (chan[0], chan[1]);
}

void MultibandDynamics::applyAuxCrossovers() noexcept
{
    auxLogFreq = targetLogFreq;
    designSplit (chan[0].a, std::exp (targetLogFreq[0]), std::exp (targetLogFreq[1]), std::exp (targetLogFreq[2]), sampleRate);
    copyCoefficients (chan[0].a, chan[1].a);
}

void MultibandDynamics::update() noexcept
{
    const bool wanted = ! isNeutral (params);
    const bool canPark = monitorBand < 0;   // a soloed band is heard: it stays in place even while switching
    const double park = ParkGlide::parkLog (sampleRate);
    bool moved = false;
    for (size_t k = 0; k < kDynBands - 1; ++k)
        targetLogFreq[k] = std::log (std::clamp (params.crossoverHz[k], kDynCrossoverRange[k][0], kDynCrossoverRange[k][1]));

    if (wanted && ! running)
    {
        running = true;
        engaged = false;
        resetDetectors();   // compression starts from a clean state
        if (! clocking)
        {
            // Start clean, parked (park and glide, see ParkGlide). Filters a solo is already running are
            // left alone, so the solo doesn't click.
            resetFilters();
            for (size_t k = 0; k < kDynBands - 1; ++k)
            {
                logFreq[k] = canPark ? park : targetLogFreq[k];
                segment[k].stop();
            }
            gliding = false;
            moved = true;
        }
    }
    if (! running && monitorBand >= 0 && ! clocking)
    {
        resetFilters();   // a solo of a resting module: the split starts at the settings
        logFreq = targetLogFreq;
        for (auto& sg : segment) sg.stop();
        gliding = false;
        moved = true;
    }

    if (running)
    {
        // On: blend in at the park, glide down, then fade the compression and band gains in.
        // Off: fade them out, glide up to the park, then blend out.
        if (! wanted)
            engaged = false;
        const bool down = wanted && fade >= 1.0;
        const bool holding = ! wanted && comp > 0.0;
        std::array<double, kDynBands - 1> goal {};
        int moving = 0;
        for (size_t k = 0; k < kDynBands - 1; ++k)
        {
            goal[k] = down || ! canPark ? targetLogFreq[k] : park;
            moving += logFreq[k] != goal[k] ? 1 : 0;
        }
        bool inPlace = true, parked = true;
        for (size_t k = 0; k < kDynBands - 1; ++k)
        {
            const double d = goal[k] - logFreq[k];
            if (! holding && d != 0.0)
            {
                if (engaged)
                {
                    logFreq[k] = std::abs (d) < 1.0e-5 ? goal[k] : logFreq[k] + d * glideCoeff;   // a crossover knob move
                    moved = true;
                }
                else
                {
                    segment[k].start (logFreq[k], ParkGlide::step (logFreq[k], goal[k], kUpdateInterval / sampleRate, sampleRate, moving), kUpdateInterval);
                    gliding = true;
                }
            }
            inPlace = inPlace && logFreq[k] == targetLogFreq[k];
            parked = parked && logFreq[k] == park;
        }
        if (down && inPlace)
            engaged = true;
        // Until the crossovers are in place, the detectors hear a second split at the settings, and the
        // gains apply as one wideband gain (they turn into per-band gains once the crossovers are in place).
        const bool needAux = ! engaged && (wanted || comp > 0.0);
        if (needAux && ! auxRunning)
        {
            for (auto& c : chan)
                resetSplit (c.a);
            applyAuxCrossovers();
            if (comp <= 0.0)
            {
                resetDetectors();
                bands = bandsWeight = bandsGoal = 0.0;
            }
            auxRunning = true;
        }
        else if (needAux && auxLogFreq != targetLogFreq)
            applyAuxCrossovers();   // (a crossover knob moved while switching)
        auxRunning = needAux;
        if (engaged)
            bandsGoal = 1.0;
        compGoal = wanted ? 1.0 : 0.0;
        fadeGoal = wanted || comp > 0.0 || (canPark && ! parked) ? 1.0 : 0.0;
        if (! wanted && fade <= 0.0 && comp <= 0.0)
        {
            running = false;   // blended out: exact pass-through again
            mix = 0.0;
        }
    }
    else
    {
        // Resting: the crossovers follow the settings (for a solo).
        for (size_t k = 0; k < kDynBands - 1; ++k)
        {
            const double d = targetLogFreq[k] - logFreq[k];
            if (d != 0.0)
            {
                logFreq[k] = std::abs (d) < 1.0e-5 ? targetLogFreq[k] : logFreq[k] + d * glideCoeff;
                moved = true;
            }
        }
    }
    clocking = running || monitorBand >= 0;
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

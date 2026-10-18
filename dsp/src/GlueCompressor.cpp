#include "polisher/GlueCompressor.h"

#include "polisher/FilterDesign.h"

namespace polisher {

namespace {

struct Voice
{
    double kneeDb, rmsMs, attackScale, minAttackMs, releaseScale;
};

// Bus: firm knee, quick detector, the times as set. Opto: soft knee, RMS detector, never faster than
// 10 ms, slower to let go. Tube: the widest knee (the ratio grows gently with level), slow and round.
constexpr Voice kVoices[] = {
    { 4.0, 3.0, 1.0, 0.1, 1.0 },
    { 10.0, 10.0, 1.0, 10.0, 1.5 },
    { 18.0, 20.0, 1.5, 1.0, 1.3 },
};

double coeffFor (double ms, double sampleRate) noexcept
{
    return 1.0 - std::exp (-1.0 / (std::max (0.01, ms) * 0.001 * sampleRate));
}

const Voice& voiceOf (GlueCharacter c) noexcept
{
    return kVoices[static_cast<size_t> (std::clamp (static_cast<int> (c), 0, 2))];
}

} // namespace

void GlueCompressor::prepare (double rate, int numChannels)
{
    sampleRate = rate;
    (void) numChannels;   // the same gain goes on every channel; the first two are detected
    fadeStep = 1.0 / (kFadeMs * 0.001 * sampleRate);
    glideCoeff = coeffFor (kFadeMs, sampleRate);
    averageCoeff = coeffFor (1000.0, sampleRate);
    fastReleaseCoeff = coeffFor (kFastReleaseMs, sampleRate);
    reset();
}

void GlueCompressor::reset() noexcept
{
    setTimes();
    sidechainSetHz = -1.0;
    sidechainOn = false;
    for (auto& ch : sidechain) for (auto& f : ch) f.reset();
    meanSquare = {};
    grDb = grAverage = maxReduction = 0.0;
    running = ! isNeutral (params);
    weight = weightGoal = running ? 1.0 : 0.0;
    mix = mixGoal = std::clamp (params.mix, 0.0, 1.0);
    gainDb = gainGoalDb = std::clamp (params.gainDb, -24.0, 24.0);
    countdown = 0;
    update();
}

void GlueCompressor::guardDetector() noexcept
{
    double sum = meanSquare[0] + meanSquare[1] + grDb + grAverage;
    for (const auto& ch : sidechain) for (const auto& f : ch) sum += f.stateSum();
    if (std::isfinite (sum))
        return;
    for (auto& ch : sidechain) for (auto& f : ch) f.reset();
    meanSquare = {};
    grDb = grAverage = 0.0;
}

void GlueCompressor::setTimes() noexcept
{
    const auto& v = voiceOf (params.character);
    kneeDb = v.kneeDb;
    rmsCoeff = coeffFor (v.rmsMs, sampleRate);
    attackCoeff = coeffFor (std::max (v.minAttackMs, std::clamp (params.attackMs, 0.1, 30.0) * v.attackScale), sampleRate);
    releaseCoeff = coeffFor (std::clamp (params.releaseMs, 50.0, 1200.0) * v.releaseScale, sampleRate);
    setCharacter = params.character;
    setAttackMs = params.attackMs;
    setReleaseMs = params.releaseMs;
}

double GlueCompressor::gainComputer (double levelDb) const noexcept
{
    const double ratio = std::clamp (params.ratio, 1.0, 10.0);
    if (ratio <= kGlueRatioNeutral)
        return 0.0;
    const double over = levelDb - std::clamp (params.thresholdDb, -40.0, 0.0);
    const double slope = 1.0 / ratio - 1.0;
    if (2.0 * over <= -kneeDb)
        return 0.0;
    if (2.0 * over >= kneeDb)
        return slope * over;
    const double x = over + 0.5 * kneeDb;
    return slope * x * x / (2.0 * kneeDb);
}

void GlueCompressor::update() noexcept
{
    if (params.character != setCharacter || params.attackMs != setAttackMs || params.releaseMs != setReleaseMs)
        setTimes();

    const bool scOn = params.sidechainHz > kGlueSidechainOffHz;
    if (scOn && params.sidechainHz != sidechainSetHz)
    {
        const auto c = design::butterworth (true, std::clamp (params.sidechainHz, 20.0, 500.0), sampleRate);
        for (auto& ch : sidechain) for (auto& f : ch) design::apply (f, c);
        sidechainSetHz = params.sidechainHz;
    }
    if (scOn && ! sidechainOn)
        for (auto& ch : sidechain) for (auto& f : ch) f.reset();   // switched back on: no stale state from before
    sidechainOn = scOn;
    mixGoal = std::clamp (params.mix, 0.0, 1.0);
    gainGoalDb = std::clamp (params.gainDb, -24.0, 24.0);

    const bool wanted = ! isNeutral (params);
    if (wanted && ! running)
    {
        // Starting (again): from a clean detector, faded in, the mix and gain already where they are set.
        running = true;
        meanSquare = {};
        for (auto& ch : sidechain) for (auto& f : ch) f.reset();
        grDb = grAverage = 0.0;
        weight = 0.0;
        mix = mixGoal;
        gainDb = gainGoalDb;
    }
    weightGoal = wanted ? 1.0 : 0.0;
    if (! wanted && running && weight <= 0.0)
    {
        running = false;   // faded out: an exact pass-through from here on
        grDb = grAverage = 0.0;
    }
}

} // namespace polisher

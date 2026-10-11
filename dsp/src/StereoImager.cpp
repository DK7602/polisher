#include "polisher/StereoImager.h"
#include "polisher/FilterDesign.h"

namespace polisher {

namespace {
    constexpr double kLowSplitHz = 250.0, kHighSplitHz = 5000.0;

    double widthToGain (double percent) { return std::clamp (percent, 0.0, 200.0) / 100.0; }
    bool isNeutral (const ImagerParams& p) noexcept
    {
        return ! p.enabled || (p.lowWidth == 100.0 && p.midWidth == 100.0 && p.highWidth == 100.0);
    }
}

void StereoImager::prepare (double sr)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    glide = 1.0 - std::exp (-static_cast<double> (kUpdateInterval) / (0.02 * sampleRate));
    fadeStep = 1.0 / (ParkGlide::kFadeSeconds * sampleRate);
    corrCoeff = 1.0 - std::exp (-1.0 / (0.3 * sampleRate));
    reset();
}

void StereoImager::setCrossovers() noexcept
{
    // In place: the exact 250 Hz / 5 kHz designs. Parked or gliding: wherever they are now.
    const double low = logLow == std::log (kLowSplitHz) ? kLowSplitHz : std::exp (logLow);
    const double high = logHigh == std::log (kHighSplitHz) ? kHighSplitHz : std::exp (logHigh);
    for (auto* g : { &lp250, &mLp250 }) for (auto& f : *g) design::apply (f, ParkGlide::butterworth (false, low, sampleRate));
    for (auto* g : { &hp250, &mHp250 }) for (auto& f : *g) design::apply (f, ParkGlide::butterworth (true, low, sampleRate));
    for (auto* g : { &lp5k, &apLp, &mLp5k }) for (auto& f : *g) design::apply (f, ParkGlide::butterworth (false, high, sampleRate));
    for (auto* g : { &hp5k, &apHp, &mHp5k }) for (auto& f : *g) design::apply (f, ParkGlide::butterworth (true, high, sampleRate));
}

void StereoImager::reset() noexcept
{
    for (auto* g : { &lp250, &hp250, &lp5k, &hp5k, &apLp, &apHp, &mLp250, &mHp250, &mLp5k, &mHp5k })
        for (auto& f : *g) f.reset();
    const bool neutral = isNeutral (params);
    gLow = params.enabled ? widthToGain (params.lowWidth) : 1.0;
    gMid = params.enabled ? widthToGain (params.midWidth) : 1.0;
    gHigh = params.enabled ? widthToGain (params.highWidth) : 1.0;
    mix = fade = fadeGoal = neutral ? 0.0 : 1.0;   // start fully in the set state
    running = engaged = ! neutral;
    logLow = std::log (kLowSplitHz);
    logHigh = std::log (kHighSplitHz);
    lowSegment.stop();
    highSegment.stop();
    setCrossovers();
    countdown = 0;
    lr = ll = rr = 0.0;
}

void StereoImager::update() noexcept
{
    const bool wanted = ! isNeutral (params);
    const double park = ParkGlide::parkLog (sampleRate);
    if (wanted && ! running)
    {
        // Start clean and parked: the blend can't cut a hole while the crossovers sit just under Nyquist.
        for (auto* g : { &lp250, &hp250, &lp5k, &hp5k, &apLp, &apHp, &mLp250, &mHp250, &mLp5k, &mHp5k })
            for (auto& f : *g) f.reset();
        logLow = logHigh = park;
        lowSegment.stop();
        highSegment.stop();
        setCrossovers();
        running = true;
        engaged = false;
    }
    if (! running)
        return;
    if (! wanted)
        engaged = false;

    // The widths move only while the crossovers are in place; otherwise they go back to 100 %.
    auto step = [this] (double& v, double t) { v = std::abs (t - v) < 1.0e-4 ? t : v + (t - v) * glide; };
    step (gLow, engaged ? widthToGain (params.lowWidth) : 1.0);
    step (gMid, engaged ? widthToGain (params.midWidth) : 1.0);
    step (gHigh, engaged ? widthToGain (params.highWidth) : 1.0);
    const bool unity = gLow == 1.0 && gMid == 1.0 && gHigh == 1.0;

    // On: blend in at the park, then glide down. Off: widths back to 100 %, glide up, then blend out.
    const bool down = wanted && fade >= 1.0;
    if (wanted || unity)
    {
        const double seconds = kUpdateInterval / sampleRate;
        const double lowGoal = down ? std::log (kLowSplitHz) : park, highGoal = down ? std::log (kHighSplitHz) : park;
        const int moving = (logLow != lowGoal ? 1 : 0) + (logHigh != highGoal ? 1 : 0);
        if (logLow != lowGoal)
            lowSegment.start (logLow, ParkGlide::step (logLow, lowGoal, seconds, sampleRate, moving), kUpdateInterval);
        if (logHigh != highGoal)
            highSegment.start (logHigh, ParkGlide::step (logHigh, highGoal, seconds, sampleRate, moving), kUpdateInterval);
    }
    const bool parked = logLow == park && logHigh == park;
    if (down && logLow == std::log (kLowSplitHz) && logHigh == std::log (kHighSplitHz))
        engaged = true;
    fadeGoal = wanted || ! parked || ! unity ? 1.0 : 0.0;
    if (! wanted && fade <= 0.0 && parked && unity)
    {
        running = false;   // blended out: exact pass-through again
        mix = 0.0;
    }
}

} // namespace polisher

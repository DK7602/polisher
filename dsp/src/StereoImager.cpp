#include "polisher/StereoImager.h"
#include "polisher/FilterDesign.h"

namespace polisher {

namespace {
    double widthToGain (double percent) { return std::clamp (percent, 0.0, 200.0) / 100.0; }
}

void StereoImager::prepare (double sr)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    glide = 1.0 - std::exp (-static_cast<double> (kUpdateInterval) / (0.02 * sampleRate));
    mixCoeff = 1.0 - std::exp (-1.0 / (0.02 * sampleRate));
    corrCoeff = 1.0 - std::exp (-1.0 / (0.3 * sampleRate));
    for (auto* g : { &lp250, &mLp250 }) for (auto& f : *g) design::apply (f, design::butterworth (false, 250.0, sampleRate));
    for (auto* g : { &hp250, &mHp250 }) for (auto& f : *g) design::apply (f, design::butterworth (true, 250.0, sampleRate));
    for (auto* g : { &lp5k, &apLp, &mLp5k }) for (auto& f : *g) design::apply (f, design::butterworth (false, 5000.0, sampleRate));
    for (auto* g : { &hp5k, &apHp, &mHp5k }) for (auto& f : *g) design::apply (f, design::butterworth (true, 5000.0, sampleRate));
    reset();
}

void StereoImager::reset() noexcept
{
    for (auto* g : { &lp250, &hp250, &lp5k, &hp5k, &apLp, &apHp, &mLp250, &mHp250, &mLp5k, &mHp5k })
        for (auto& f : *g) f.reset();
    const bool neutral = ! params.enabled || (params.lowWidth == 100.0 && params.midWidth == 100.0 && params.highWidth == 100.0);
    gLow = params.enabled ? widthToGain (params.lowWidth) : 1.0;
    gMid = params.enabled ? widthToGain (params.midWidth) : 1.0;
    gHigh = params.enabled ? widthToGain (params.highWidth) : 1.0;
    mix = mixTarget = neutral ? 0.0 : 1.0;
    running = ! neutral;
    countdown = 0;
    lr = ll = rr = 0.0;
}

void StereoImager::update() noexcept
{
    const bool neutral = ! params.enabled || (params.lowWidth == 100.0 && params.midWidth == 100.0 && params.highWidth == 100.0);
    if (! neutral)
    {
        auto step = [this] (double& v, double t) { v = std::abs (t - v) < 1.0e-4 ? t : v + (t - v) * glide; };
        step (gLow, widthToGain (params.lowWidth));
        step (gMid, widthToGain (params.midWidth));
        step (gHigh, widthToGain (params.highWidth));
        mixTarget = 1.0;
        if (! running)
        {
            for (auto* g : { &lp250, &hp250, &lp5k, &hp5k, &apLp, &apHp, &mLp250, &mHp250, &mLp5k, &mHp5k })
                for (auto& f : *g) f.reset();
            running = true;
        }
    }
    else
    {
        mixTarget = 0.0;
        if (running && mix < 1.0e-5)   // faded out: exact pass-through again
        {
            running = false;
            mix = 0.0;
        }
    }
}

} // namespace polisher

#include "polisher/Monitoring.h"
#include "polisher/FilterDesign.h"

#include <complex>
#include <numbers>

namespace polisher {

// --- ListenMix --------------------------------------------------------------------------------

void ListenMix::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    fadeStep = 1.0 / (kFadeSeconds * sr);
    loudCoeff = 1.0 - std::exp (-1.0 / (kLoudnessSeconds * sr));
    peakRelease = std::exp (-1.0 / (kPeakReleaseSeconds * sr));
    levelDown = 1.0 - std::exp (-1.0 / (0.02 * sr));
    levelUp = 1.0 - std::exp (-1.0 / (0.3 * sr));
    measureSamples = static_cast<int> (std::lround (kMeasureSeconds * sr));
    for (auto& k : kwMaster) k.prepare (sr);
    for (auto& k : kwSignal) k.prepare (sr);
    reset();
}

void ListenMix::reset() noexcept
{
    wanted = computing = listen::off;
    wantedArm = armLeft = 0;
    justStarted = false;
    masterGain = 1.0;
    signalGain = 0.0;
    level = levelTarget = 1.0;
    for (auto& k : kwMaster) k.reset();
    for (auto& k : kwSignal) k.reset();
    masterMs = signalMs = peak = 0.0;
    tracked = 0;
    countdown = 0;
}

int ListenMix::beginBlock() noexcept
{
    justStarted = false;
    if (computing == listen::off && wanted != listen::off)
    {
        // Start the new signal: its delay lines fill and its level is measured before it is heard.
        computing = wanted;
        armLeft = wantedArm + measureSamples;
        justStarted = true;
        signalGain = 0.0;
        level = levelTarget = 1.0;
        for (auto& k : kwSignal) k.reset();
        masterMs = signalMs = peak = 0.0;
        tracked = 0;
        countdown = 0;
    }
    return computing;
}

void ListenMix::trackMaster (double l, double r, int nch) noexcept
{
    double e = 0.0;
    const double a = kwMaster[0].process (l);
    e += a * a;
    if (nch > 1) { const double b = kwMaster[1].process (r); e += b * b; }
    // Averaged only while the monitored signal is complete, so the two averages cover the same audio.
    if (computing != listen::off && armLeft <= measureSamples)
        masterMs += (e - masterMs) * std::max (1.0 / static_cast<double> (tracked + 1), loudCoeff);
}

void ListenMix::trackSignal (double l, double r, int nch, double ceilingGain) noexcept
{
    double e = 0.0;
    const double a = kwSignal[0].process (l);
    e += a * a;
    if (nch > 1) { const double b = kwSignal[1].process (r); e += b * b; }
    ceilingNow = ceilingGain > 0.0 ? ceilingGain : 1.0;
    if (armLeft > measureSamples)
        return;   // still filling its delay lines: not the real signal yet
    // A plain average at first (no bias toward silence), then a ~1 s moving one.
    ++tracked;
    signalMs += (e - signalMs) * std::max (1.0 / static_cast<double> (tracked), loudCoeff);
    if (signalMs < 1.0e-30) signalMs = 0.0;
    peak = std::max (std::max (std::abs (l), std::abs (r)), peak * peakRelease);
}

double ListenMix::targetLevel() const noexcept
{
    if (! autoLevel)
        return 1.0;
    double db = 0.0;
    if (listen::isDelta (computing))
    {
        if (signalMs <= 1.0e-14 || masterMs <= 1.0e-12)
            return levelTarget;   // nothing to measure (silence, or A): keep the gain it has
        db = std::clamp (10.0 * std::log10 (masterMs / signalMs) - kTargetUnderMasterDb, 0.0, kMaxBoostDb);
    }
    if (peak > 1.0e-9)
        db = std::min (db, 20.0 * std::log10 (ceilingNow / peak));
    return std::pow (10.0, db / 20.0);
}

void ListenMix::step() noexcept
{
    if (computing != listen::off && computing == wanted)
    {
        if (armLeft > 0)
        {
            // Filling and measuring: the master (or the silence between two modes) keeps playing.
            if (--armLeft == 0)
            {
                levelTarget = level = targetLevel();   // fade in at the measured level
                countdown = 0;
            }
            return;
        }
        signalGain = std::min (1.0, signalGain + fadeStep);
        masterGain = std::max (0.0, masterGain - fadeStep);
    }
    else if (computing != listen::off)
    {
        // Another mode (or the master) is wanted: fade this one out first.
        signalGain = std::max (0.0, signalGain - fadeStep);
        if (wanted == listen::off)
            masterGain = std::min (1.0, masterGain + fadeStep);
        if (signalGain <= 0.0 && (wanted != listen::off || masterGain >= 1.0))
            computing = listen::off;   // the next beginBlock() starts the wanted one
    }
    else if (wanted == listen::off)
        masterGain = std::min (1.0, masterGain + fadeStep);

    if (computing != listen::off && armLeft == 0)
    {
        if (--countdown < 0)
        {
            levelTarget = targetLevel();
            countdown = 31;
        }
        level += (levelTarget - level) * (levelTarget < level ? levelDown : levelUp);
    }
}

// --- MonitorOutput ----------------------------------------------------------------------------

void MonitorOutput::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    fadeStep = 1.0 / (kFadeSeconds * sr);
    safetyRelease = std::exp (-1.0 / (kSafetyReleaseSeconds * sr));
    design::apply (phoneHp, design::butterworth (true, kPhoneLowHz, sr));
    design::apply (phoneLp, design::butterworth (false, kPhoneHighHz, sr));
    design::apply (phoneBump, design::bell (kPhoneBumpHz, kPhoneBumpDb, kPhoneBumpQ, sr));
    phoneMakeup = std::pow (10.0, kPhoneMakeupDb / 20.0);
    reset();
}

void MonitorOutput::reset() noexcept
{
    monoWanted = phoneWanted = false;
    monoMix = phoneMix = 0.0;
    safetyGain = 1.0;
    for (auto* f : { &phoneHp, &phoneLp, &phoneBump }) f->reset();
}

double MonitorOutput::phoneResponseDb (double f) const noexcept
{
    const double w = 2.0 * std::numbers::pi * f / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    double mag = phoneMakeup;
    for (const auto* b : { &phoneHp, &phoneLp, &phoneBump })
        mag *= std::abs ((b->b0 + b->b1 * z1 + b->b2 * z2) / (1.0 + b->a1 * z1 + b->a2 * z2));
    return 20.0 * std::log10 (std::max (mag, 1.0e-12));
}

} // namespace polisher

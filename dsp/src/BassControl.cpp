#include "polisher/BassControl.h"
#include "polisher/FilterDesign.h"

#include <numbers>

namespace polisher {

namespace {
    using namespace design;

    double perSample (double seconds, double sr) noexcept { return 1.0 - std::exp (-1.0 / (seconds * sr)); }
    double perUpdate (double seconds, double sr) noexcept
    {
        return 1.0 - std::exp (-static_cast<double> (BassControl::kUpdateInterval) / (seconds * sr));
    }

    constexpr double kTameShelfHz = 120.0;
    constexpr double kPunchHz = 80.0, kPunchQ = 0.9;
    constexpr double kClickLowHz = 1000.0, kClickHighHz = 5000.0;   // where a kick's click lives
    constexpr double kAirHz = 7000.0;                                // where hi-hats live
    constexpr double kClickJudgeSeconds = 0.006;                     // the low band has shown its size by then
    constexpr int kCountCap = 1 << 24;                                // update counters stop here (no overflow)

    constexpr double kWarmUpSeconds = 0.75;    // Sub Tame listens this long before it judges
    constexpr double kForgetSeconds = 2.0;     // low end gone this long: start listening afresh
    constexpr double kNewSectionDb = 12.0;     // a step this far above the average is a drop, not a note

    void flushTiny (Biquad& f) noexcept
    {
        if (std::abs (f.z1) < 1.0e-30) f.z1 = 0.0;
        if (std::abs (f.z2) < 1.0e-30) f.z2 = 0.0;
    }

    template <size_t N>
    bool hasRungOut (const std::array<Biquad, N>& filters, int count) noexcept
    {
        double energy = 0.0;
        for (int c = 0; c < count; ++c)
            energy += std::abs (filters[static_cast<size_t> (c)].z1) + std::abs (filters[static_cast<size_t> (c)].z2);
        return energy < 1.0e-12;
    }
}

void BassControl::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    channelCount = std::clamp (numChannels, 1, kMaxChannels);
    const double sr = sampleRate;

    fadeStep = 1.0 / (ParkGlide::kFadeSeconds * sr);
    glideCoeff = perUpdate (0.02, sr);
    tameAttack = perSample (0.010, sr);
    tameRelease = perSample (0.080, sr);
    longCoeff = perUpdate (2.0, sr);
    catchUpCoeff = perUpdate (0.25, sr);
    punchFastAttack = perSample (0.0005, sr);
    punchFastRelease = perSample (0.030, sr);
    punchSlowAttack = perSample (0.030, sr);
    punchSlowRelease = perSample (0.200, sr);
    clickFastRelease = perSample (0.010, sr);
    clickHoldDecay = 1.0 - perUpdate (0.050, sr);
    widthCoeff = perSample (1.0, sr);
    gainAttack = perUpdate (0.010, sr);
    gainRelease = perUpdate (0.150, sr);
    punchGainAttack = perUpdate (0.001, sr);
    punchGainRelease = perUpdate (0.060, sr);

    const auto lp = butterworth (false, kBassDetectHz, sr);
    for (auto& f : detectLp) apply (f, lp);
    const auto wlp = butterworth (false, kBassWidthHz, sr);
    for (auto& f : widthLpM) apply (f, wlp);
    for (auto& f : widthLpS) apply (f, wlp);
    const auto chp = butterworth (true, kClickLowHz, sr);
    for (auto& f : clickHp) apply (f, chp);
    const auto clp = butterworth (false, kClickHighHz, sr);
    for (auto& f : clickLp) apply (f, clp);
    const auto ahp = butterworth (true, kAirHz, sr);
    for (auto& f : airHp) apply (f, ahp);

    reset();
}

void BassControl::reset() noexcept
{
    for (auto* group : { &detectLp, &widthLpM, &widthLpS, &monoHp, &monoLp, &clickHp, &clickLp, &airHp })
        for (auto& f : *group)
            f.reset();
    for (auto& f : tameFilter) f.reset();
    for (auto& f : punchFilter) f.reset();

    fastEnergy = longEnergy = warmEnergy = 0.0;
    warmUpdates = silentUpdates = 0;
    fastPeak = slowPeak = 0.0;
    clickFast = clickSlow = clickHold = clickLevel = airFast = airLevel = 0.0;
    clickAge = 0;
    midEnergy = sideEnergy = 0.0;
    widthPercent = 0.0;
    tameGainDb = punchGainDb = 0.0;
    tameRunning = punchRunning = false;
    monoMix = monoFade = monoFadeGoal = monoCut = monoCutGoal = monoTarget;   // jump straight to the current Mono Below setting
    monoCutWeight = monoCut;
    monoLogFreq = monoTargetLogFreq;
    monoRunning = monoEngaged = monoMix > 0.0;
    monoSegment.stop();
    monoAp.reset();
    monoApSide.reset();
    setMonoFilters();
    maxTame = maxPunch = 0.0;
    updateCountdown = 0;
}

void BassControl::setParams (const BassParams& p) noexcept
{
    params = p;
    const bool monoOn = p.enabled && p.monoHz >= kBassMonoOffHz && channelCount >= 2;
    monoTarget = monoOn ? 1.0 : 0.0;
    if (monoOn)
        monoTargetLogFreq = std::log (std::min (p.monoHz, kBassMonoMaxHz));
}

void BassControl::setMonoFilters() noexcept
{
    const double f = std::exp (monoLogFreq);
    const auto hp = ParkGlide::butterworth (true, f, sampleRate), lp = ParkGlide::butterworth (false, f, sampleRate);
    apply (monoHp[0], hp);
    apply (monoHp[1], hp);
    apply (monoLp[0], lp);
    apply (monoLp[1], lp);
    const auto ap = ParkGlide::allpass (f, sampleRate);
    apply (monoAp, ap);
    apply (monoApSide, ap);
}

void BassControl::update() noexcept
{
    const double sr = sampleRate;

    // --- Mono Below: park and glide (see ParkGlide) -----------------------------------------------
    // On: blend in with the all-pass parked just under Nyquist (mid and side alike), glide down to the
    // set frequency, then take the side's bass out. Off: the other way round. Once there, a frequency
    // move glides as before (20 ms).
    const bool monoWanted = monoTarget > 0.0;
    if (monoWanted || monoRunning)
    {
        const double park = ParkGlide::parkLog (sr);
        if (! monoRunning)
        {
            for (auto* group : { &monoHp, &monoLp })
                for (auto& f : *group) f.reset();
            monoAp.reset();
            monoApSide.reset();
            monoSegment.stop();
            monoLogFreq = park;
            monoRunning = true;
            monoEngaged = false;
            setMonoFilters();
        }
        if (! monoWanted)
            monoEngaged = false;
        const bool down = monoWanted && monoFade >= 1.0;
        const bool holding = ! monoWanted && monoCut > 0.0;   // the side's bass comes back first
        const double goal = down ? monoTargetLogFreq : park;
        const double d = goal - monoLogFreq;
        if (! holding && d != 0.0)
        {
            if (monoEngaged)
            {
                monoLogFreq = std::abs (d) < 1.0e-5 ? goal : monoLogFreq + d * glideCoeff;
                setMonoFilters();
            }
            else
                monoSegment.start (monoLogFreq, ParkGlide::step (monoLogFreq, goal, kUpdateInterval / sr, sr), kUpdateInterval);
        }
        if (down && monoLogFreq == monoTargetLogFreq)
            monoEngaged = true;
        monoCutGoal = monoEngaged ? 1.0 : 0.0;
        monoFadeGoal = monoWanted || monoCut > 0.0 || monoLogFreq != park ? 1.0 : 0.0;
        if (! monoWanted && monoFade <= 0.0 && monoCut <= 0.0)
        {
            monoRunning = false;   // blended out: exact pass-through again
            monoMix = 0.0;
        }
    }

    // Keep the detector filters out of denormals in silence.
    for (auto* group : { &detectLp, &widthLpM, &widthLpS, &clickHp, &clickLp, &airHp })
        for (auto& f : *group)
            flushTiny (f);

    // --- Sub Tame -------------------------------------------------------------------------------
    // The average needs time: judging the first note against its own first 10 ms would cut it far
    // harder than the rest. So the low end plays untouched for kWarmUpSeconds, and the average
    // starts from what was heard. After kForgetSeconds without low end (an intro, a breakdown) it
    // starts over, so a drop isn't judged against the quiet part before it.
    const double fastDb = 10.0 * std::log10 (fastEnergy + 1.0e-20);
    const bool lowPresent = fastDb > -60.0;
    const double updatesPerSecond = sr / kUpdateInterval;
    bool warm = longEnergy > 0.0;
    if (lowPresent)
    {
        silentUpdates = 0;
        if (! warm)
        {
            ++warmUpdates;
            warmEnergy += (fastEnergy - warmEnergy) / warmUpdates;
            if (warmUpdates >= static_cast<int> (kWarmUpSeconds * updatesPerSecond))
            {
                longEnergy = warmEnergy;
                warm = true;
            }
        }
        else
        {
            // A step far above the average (a drop after a quiet-bass intro) is a new section:
            // catch up quickly instead of ducking it for seconds.
            const bool newSection = fastEnergy > longEnergy * std::pow (10.0, kNewSectionDb / 10.0);
            longEnergy += (fastEnergy - longEnergy) * (newSection ? catchUpCoeff : longCoeff);
        }
    }
    else if ((silentUpdates = std::min (silentUpdates + 1, kCountCap)) > static_cast<int> (kForgetSeconds * updatesPerSecond))
    {
        longEnergy = warmEnergy = 0.0;
        warmUpdates = 0;
        warm = false;
    }
    double tameTarget = 0.0;
    if (params.enabled && params.tame > 0.0 && lowPresent && warm)
    {
        const double amount = std::clamp (params.tame, 0.0, 1.0);
        const double threshold = 6.0 - 3.0 * amount;            // dB above the running average
        const double ratio = 1.0 + 3.0 * amount;
        const double excess = fastDb - 10.0 * std::log10 (longEnergy + 1.0e-20) - threshold;
        if (excess > 0.0)
            tameTarget = -std::min (kMaxTameDb, excess * (1.0 - 1.0 / ratio));
    }
    {
        const double d = tameTarget - tameGainDb;
        // Snap only onto 0 dB (so it can rest as an exact pass-through). Snapping onto any target within
        // 0.01 dB made the release faster the more updates a second there are: at 96 kHz a leaping 808
        // note was let go ~0.5 dB sooner than at 48 kHz.
        tameGainDb = tameTarget == 0.0 && std::abs (d) < 0.01 ? 0.0 : tameGainDb + d * (d < 0.0 ? gainAttack : gainRelease);
        if (tameGainDb != 0.0)
        {
            tameRunning = true;
            const auto c = lowShelf (kTameShelfHz, tameGainDb, sr);
            for (auto& f : tameFilter) apply (f, c);
        }
        else if (tameRunning)
        {
            const auto c = lowShelf (kTameShelfHz, 0.0, sr);
            for (auto& f : tameFilter) apply (f, c);
            if (hasRungOut (tameFilter, channelCount))
            {
                tameRunning = false;
                for (auto& f : tameFilter) f.reset();
            }
        }
        maxTame = std::min (maxTame, tameGainDb);
    }

    // --- Punch -----------------------------------------------------------------------------------
    // A kick is a jump in the low band together with a click in the 1-5 kHz band. An 808 note is a
    // jump in the low band alone, so it isn't boosted (the sub stays where it was).
    // - The click must jump (fast vs slow envelope of the click band),
    // - be loud enough next to the low end: even a clean 808 start puts a trace into the click
    //   band (~40 dB below the note). The low band needs a few ms to show how big the note is, so
    //   this is judged kClickJudgeSeconds after the click starts,
    // - and not be outweighed by the band above 7 kHz, so a hi-hat landing with an 808 note
    //   isn't taken for a kick.
    // The click is over in a few ms but the thump builds for tens of ms, so it is held for ~50 ms.
    double clickNow = 0.0;
    if (clickFast > 1.0e-4 && clickSlow > 1.0e-9)
        clickNow = std::clamp ((20.0 * std::log10 (clickFast / clickSlow) - 2.0) / 4.0, 0.0, 1.0);
    if (clickNow > 0.0 && clickHold < 0.1)
        clickAge = 0;                                      // a new click
    clickHold = std::max (clickNow, clickHold * clickHoldDecay);
    clickLevel = std::max (clickFast, clickLevel * clickHoldDecay);
    airLevel = std::max (airFast, airLevel * clickHoldDecay);
    if (clickHold < 1.0e-6) clickHold = clickLevel = 0.0;
    if (airLevel < 1.0e-30) airLevel = 0.0;
    clickAge = std::min (clickAge + 1, kCountCap);

    double punchTarget = 0.0;
    if (params.enabled && params.punch > 0.0 && fastPeak > 1.0e-4 && slowPeak > 1.0e-9 && clickHold > 0.0
        && clickAge >= static_cast<int> (kClickJudgeSeconds * updatesPerSecond))
    {
        const double clickVsLowDb = 20.0 * std::log10 (clickLevel / fastPeak + 1.0e-20);
        const double clickVsAirDb = 20.0 * std::log10 ((clickLevel + 1.0e-20) / (airLevel + 1.0e-20));
        const double clickGate = clickHold * std::clamp ((clickVsLowDb + 34.0) / 10.0, 0.0, 1.0)
                                           * std::clamp ((clickVsAirDb + 4.0) / 6.0, 0.0, 1.0);
        const double transient = std::clamp (20.0 * std::log10 (fastPeak / slowPeak) / 6.0, 0.0, 1.0);
        punchTarget = std::clamp (params.punch, 0.0, 1.0) * kMaxPunchDb * transient * clickGate;
    }
    {
        const double d = punchTarget - punchGainDb;
        punchGainDb = std::abs (d) < 0.01 ? punchTarget : punchGainDb + d * (d > 0.0 ? punchGainAttack : punchGainRelease);
        if (punchGainDb != 0.0)
        {
            punchRunning = true;
            const auto c = bell (kPunchHz, punchGainDb, kPunchQ, sr);
            for (auto& f : punchFilter) apply (f, c);
        }
        else if (punchRunning)
        {
            const auto c = bell (kPunchHz, 0.0, kPunchQ, sr);
            for (auto& f : punchFilter) apply (f, c);
            if (hasRungOut (punchFilter, channelCount))
            {
                punchRunning = false;
                for (auto& f : punchFilter) f.reset();
            }
        }
        maxPunch = std::max (maxPunch, punchGainDb);
    }

    // --- Bass width -------------------------------------------------------------------------------
    const double total = midEnergy + sideEnergy;
    if (total > 1.0e-10)
        widthPercent = std::min (100.0, 200.0 * sideEnergy / total);
}

} // namespace polisher

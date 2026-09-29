#include "polisher/BassControl.h"

#include <numbers>

namespace polisher {

namespace {
    struct Coeffs { double b0, b1, b2, a1, a2; };

    void apply (Biquad& f, const Coeffs& c) noexcept
    {
        f.b0 = c.b0; f.b1 = c.b1; f.b2 = c.b2; f.a1 = c.a1; f.a2 = c.a2;
    }

    // RBJ cookbook designs, normalised (a0 == 1).
    Coeffs butterworth (bool highPass, double f, double sr) noexcept
    {
        const double w0 = 2.0 * std::numbers::pi * std::min (f, 0.45 * sr) / sr;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * std::numbers::sqrt2 / 2.0);
        const double a0 = 1.0 + alpha;
        const double b1 = highPass ? -(1.0 + cw) : (1.0 - cw);
        const double b0 = highPass ? (1.0 + cw) / 2.0 : (1.0 - cw) / 2.0;
        return { b0 / a0, b1 / a0, b0 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }

    Coeffs lowShelf (double f, double gainDb, double sr) noexcept
    {
        const double w0 = 2.0 * std::numbers::pi * f / sr, cw = std::cos (w0);
        const double A = std::pow (10.0, gainDb / 40.0), alpha = std::sin (w0) / (2.0 * 0.707);
        const double k = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1.0) + (A - 1.0) * cw + k;
        return { A * ((A + 1.0) - (A - 1.0) * cw + k) / a0, 2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0,
                 A * ((A + 1.0) - (A - 1.0) * cw - k) / a0, -2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0,
                 ((A + 1.0) + (A - 1.0) * cw - k) / a0 };
    }

    Coeffs bell (double f, double gainDb, double q, double sr) noexcept
    {
        const double w0 = 2.0 * std::numbers::pi * f / sr, cw = std::cos (w0);
        const double A = std::pow (10.0, gainDb / 40.0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha / A;
        return { (1.0 + alpha * A) / a0, -2.0 * cw / a0, (1.0 - alpha * A) / a0, -2.0 * cw / a0, (1.0 - alpha / A) / a0 };
    }

    double perSample (double seconds, double sr) noexcept { return 1.0 - std::exp (-1.0 / (seconds * sr)); }
    double perUpdate (double seconds, double sr) noexcept
    {
        return 1.0 - std::exp (-static_cast<double> (BassControl::kUpdateInterval) / (seconds * sr));
    }

    constexpr double kTameShelfHz = 120.0;
    constexpr double kPunchHz = 80.0, kPunchQ = 0.9;

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

    mixCoeff = perSample (0.02, sr);
    glideCoeff = perUpdate (0.02, sr);
    tameAttack = perSample (0.010, sr);
    tameRelease = perSample (0.080, sr);
    longCoeff = perUpdate (2.0, sr);
    punchFastAttack = perSample (0.0005, sr);
    punchFastRelease = perSample (0.030, sr);
    punchSlowAttack = perSample (0.030, sr);
    punchSlowRelease = perSample (0.200, sr);
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

    reset();
}

void BassControl::reset() noexcept
{
    for (auto* group : { &detectLp, &widthLpM, &widthLpS, &monoHp })
        for (auto& f : *group)
            f.reset();
    for (auto& f : tameFilter) f.reset();
    for (auto& f : punchFilter) f.reset();

    fastEnergy = longEnergy = 0.0;
    fastPeak = slowPeak = 0.0;
    midEnergy = sideEnergy = 0.0;
    widthPercent = 0.0;
    tameGainDb = punchGainDb = 0.0;
    tameRunning = punchRunning = false;
    monoMix = monoTarget;          // jump straight to the current Mono Below setting
    monoLogFreq = monoTargetLogFreq;
    monoRunning = monoMix > 0.0;
    apply (monoHp[0], butterworth (true, std::exp (monoLogFreq), sampleRate));
    apply (monoHp[1], butterworth (true, std::exp (monoLogFreq), sampleRate));
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

void BassControl::update() noexcept
{
    const double sr = sampleRate;

    // --- Mono Below: frequency glide (jumps while fully blended out) ---------------------------
    if (monoTarget > 0.0 || monoMix > 0.0)
    {
        if (! monoRunning)
        {
            for (auto& f : monoHp) f.reset();
            monoLogFreq = monoTargetLogFreq;
            monoRunning = true;
            const auto c = butterworth (true, std::exp (monoLogFreq), sr);
            apply (monoHp[0], c); apply (monoHp[1], c);
        }
        const double d = monoTargetLogFreq - monoLogFreq;
        if (d != 0.0)
        {
            monoLogFreq = std::abs (d) < 1.0e-5 ? monoTargetLogFreq : monoLogFreq + d * glideCoeff;
            const auto c = butterworth (true, std::exp (monoLogFreq), sr);
            apply (monoHp[0], c); apply (monoHp[1], c);
        }
    }
    else
    {
        monoRunning = false;
    }

    // --- Sub Tame -------------------------------------------------------------------------------
    const double fastDb = 10.0 * std::log10 (fastEnergy + 1.0e-20);
    const bool lowPresent = fastDb > -60.0;
    if (lowPresent)
    {
        if (longEnergy <= 0.0) longEnergy = fastEnergy;
        else                   longEnergy += (fastEnergy - longEnergy) * longCoeff;
    }
    double tameTarget = 0.0;
    if (params.enabled && params.tame > 0.0 && lowPresent)
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
        tameGainDb = std::abs (d) < 0.01 ? tameTarget : tameGainDb + d * (d < 0.0 ? gainAttack : gainRelease);
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
    double punchTarget = 0.0;
    if (params.enabled && params.punch > 0.0 && fastPeak > 1.0e-4 && slowPeak > 1.0e-9)
    {
        const double transient = std::clamp (20.0 * std::log10 (fastPeak / slowPeak) / 9.0, 0.0, 1.0);
        punchTarget = std::clamp (params.punch, 0.0, 1.0) * kMaxPunchDb * transient;
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

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
    if (p.focus == SaturationFocus::highs || p.focus == SaturationFocus::bassHighs)
    {
        // The same filters as the plug-in, each band at its own bend.
        const auto hpHi = design::butterworth (true, std::min (kHighResidualHz, 0.4 * sampleRate), sampleRate);
        const auto lpLow = design::butterworth (false, kBassSplitHz, sampleRate);
        const auto hpLow = design::butterworth (true, kBassResidualHz, sampleRate);
        const double sHi = bendFor (p.driveDb, SaturationFocus::highs), sLow = bendFor (p.driveDb, SaturationFocus::bass);
        const bool withBass = p.focus == SaturationFocus::bassHighs;
        double res = 0.0, sig = 0.0;
        for (const auto& ch : channels)
        {
            Biquad hpSplit[2], post, lsplit[2], lpost;
            double fxy = 0.0, fxx = 0.0;
            const double fc = 1.0 - std::exp (-1.0 / (0.003 * sampleRate));
            for (auto& f : hpSplit) design::apply (f, design::butterworth (true, std::min (kHighSplitHz, 0.4 * sampleRate), sampleRate));
            design::apply (post, hpHi);
            for (auto& f : lsplit) design::apply (f, lpLow);
            design::apply (lpost, hpLow);
            for (float v : ch)
            {
                const double x = v, hi = hpSplit[1].process (hpSplit[0].process (x));
                const double y = shape (hi, p.mode, sHi);
                fxy += (y * hi - fxy) * fc;
                fxx += (hi * hi - fxx) * fc;
                const double g = fxx > 1.0e-20 ? std::clamp (fxy / fxx, 0.0, 1.0) : 1.0;
                double r = post.process (y - g * hi);
                if (withBass)
                {
                    const double low = lsplit[1].process (lsplit[0].process (x));
                    r += lpost.process (shape (low, p.mode, sLow) - low);
                }
                res += r * r;
                sig += x * x;
            }
        }
        const double ratio = sig > 0.0 ? m * m * res / sig : 0.0;
        return ratio > 1.0e-10 ? 10.0 * std::log10 (ratio) : -100.0;
    }
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

void Saturation::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    channelCount = std::clamp (numChannels, 1, kMaxChannels);
    channelState.assign (static_cast<size_t> (channelCount), Channel {});
    const auto lp = design::butterworth (false, kBassSplitHz, sampleRate);
    const auto hp = design::butterworth (true, kBassResidualHz, sampleRate);
    for (auto& ch : channelState)
    {
        ch.dry.assign (kLatency + 1, 0.0);
        for (auto& st : ch.slot)
        {
            for (auto& f : st.split) design::apply (f, lp);
            design::apply (st.bassHp, hp);
            for (auto& f : st.hiSplit) design::apply (f, design::butterworth (true, std::min (kHighSplitHz, 0.4 * sampleRate), sampleRate));
            design::apply (st.hiHp, design::butterworth (true, std::min (kHighResidualHz, 0.4 * sampleRate), sampleRate));
        }
    }
    glideCoeff = 1.0 - std::exp (-1.0 / (0.030 * sampleRate));
    mixCoeff = 1.0 - std::exp (-4.0 / (0.020 * sampleRate));   // settles within ~20 ms
    hpCoeff = std::exp (-2.0 * std::numbers::pi * 1.0 / sampleRate);
    dcGlideCoeff = 1.0 - std::exp (-1.0 / (kDcGlideSeconds * sampleRate));
    dcWindow = static_cast<int> (0.05 * sampleRate);   // two cycles of the lowest 808 note
    bassBendScale = std::pow (10.0, -kBassDriveDb / 20.0);
    highBendScale = std::pow (10.0, -kHighDriveDb / 20.0);
    fitCoeff = 1.0 - std::exp (-1.0 / (0.003 * 4.0 * sampleRate));   // ~3 ms at the 4x rate
    blendStep = 1.0 / (kFocusFadeSeconds * sampleRate);
    reset();
}

void Saturation::startSlot (int index, SaturationFocus focus, SaturationMode mode, SaturationChannels channels, int warmSamples) noexcept
{
    stopSlot (index);   // from a clean state
    auto& slot = slots[static_cast<size_t> (index)];
    slot.active = true;
    slot.warm = warmSamples;
    slot.focus = focus;
    slot.mode = mode;
    slot.channels = channels;
}

void Saturation::stopSlot (int index) noexcept
{
    auto& slot = slots[static_cast<size_t> (index)];
    slot.active = false;
    slot.warm = 0;
    slot.dcCount = 0;
    slot.dcHz = 1.0;
    for (auto& ch : channelState)   // start clean next time
    {
        auto& st = ch.slot[static_cast<size_t> (index)];
        for (auto& stage : st.stages) stage.reset();
        for (auto& f : st.split) f.reset();
        st.bassHp.reset();
        for (auto& stage : st.hiStages) stage.reset();
        for (auto& f : st.hiSplit) f.reset();
        st.hiHp.reset();
        st.fitXY = st.fitXX = 0.0;
        st.hpX = st.hpY = st.dcSum = 0.0;
    }
}

void Saturation::reset() noexcept
{
    for (auto& ch : channelState)
    {
        std::fill (ch.dry.begin(), ch.dry.end(), 0.0);
        ch.dryPos = 0;
    }
    stopSlot (0);
    stopSlot (1);
    heard = 0;
    blend = 0.0;
    bendTarget = std::pow (10.0, (kHeadroomDb - std::clamp (params.driveDb, 0.0, kMaxDriveDb)) / 20.0);
    bend = bendTarget;
    // Start fully in the requested state (offline renders and a fresh prepare): no fade-in, no warm-up.
    // Nothing has been heard, so the tube's DC can't be measured: its filter starts fast and glides down.
    running = ! isNeutral (params);
    mix = effectiveMix (params);
    if (running)
    {
        startSlot (0, params.focus, params.mode, params.channels, 0);
        slots[0].dcHz = kDcStartHz;
    }
    resEnergy = sigEnergy = 0.0;
}

double Saturation::lowWet (SaturationMode mode, SlotState& st, double x) const noexcept
{
    // The curve hears the low end alone; the high-pass keeps the harmonics above the 808's fundamental
    // (and removes the tube curve's DC within a few ms).
    double a, b, u[4];
    const double low = st.split[1].process (st.split[0].process (x));
    const double bassBend = bend * bassBendScale;
    st.stages[0].upsample (low, a, b);
    st.stages[1].upsample (a, u[0], u[1]);
    st.stages[1].upsample (b, u[2], u[3]);
    for (double& v : u) v = shape (v, mode, bassBend) - v;
    const double ra = st.stages[1].downsample (u[0], u[1]);
    const double rb = st.stages[1].downsample (u[2], u[3]);
    return st.bassHp.process (st.stages[0].downsample (ra, rb));
}

double Saturation::highWet (SaturationMode mode, SlotState& st, double x) const noexcept
{
    // The curve hears the top alone. Only its new harmonics are kept: the part of its output that follows
    // the top itself (g x, g from a running least-squares fit) is taken out, so the top keeps its level.
    // The high-pass then keeps only what is added up there (no difference tones in the vocal's range, no DC).
    double a, b, u[4];
    const double high = st.hiSplit[1].process (st.hiSplit[0].process (x));
    const double highBend = bend * highBendScale;
    st.hiStages[0].upsample (high, a, b);
    st.hiStages[1].upsample (a, u[0], u[1]);
    st.hiStages[1].upsample (b, u[2], u[3]);
    for (double& v : u)
    {
        const double y = shape (v, mode, highBend);
        st.fitXY += (y * v - st.fitXY) * fitCoeff;
        st.fitXX += (v * v - st.fitXX) * fitCoeff;
        const double g = st.fitXX > 1.0e-20 ? std::clamp (st.fitXY / st.fitXX, 0.0, 1.0) : 1.0;
        v = y - g * v;
    }
    const double ra = st.hiStages[1].downsample (u[0], u[1]);
    const double rb = st.hiStages[1].downsample (u[2], u[3]);
    return st.hiHp.process (st.hiStages[0].downsample (ra, rb));
}

double Saturation::slotWet (const Slot& slot, SlotState& st, double x, double dcWeight, bool measuringDc, bool seedDc, double dcCoeff) const noexcept
{
    double a, b, u[4];
    if (slot.focus == SaturationFocus::bass)
        return lowWet (slot.mode, st, x);
    if (slot.focus == SaturationFocus::highs)
        return highWet (slot.mode, st, x);
    if (slot.focus == SaturationFocus::bassHighs)
        return lowWet (slot.mode, st, x) + highWet (slot.mode, st, x);

    // Full: up 4x, keep only what the curve adds (the clean signal never goes through the filters),
    // down 4x, remove DC.
    st.stages[0].upsample (x, a, b);
    st.stages[1].upsample (a, u[0], u[1]);
    st.stages[1].upsample (b, u[2], u[3]);
    for (double& v : u) v = shape (v, slot.mode, bend) - v;
    const double ra = st.stages[1].downsample (u[0], u[1]);
    const double rb = st.stages[1].downsample (u[2], u[3]);
    const double res = st.stages[0].downsample (ra, rb);
    // Only the tube curve is asymmetric and makes DC; a 1 Hz high-pass removes it without shifting the
    // bass part of the residual against the dry signal.
    if (measuringDc) st.dcSum += dcWeight * res;
    if (seedDc)
    {
        st.hpX = st.dcSum / (0.5 * slot.dcCount);   // the Hann weights sum to half the window
        st.hpY = 0.0;
        st.dcSum = 0.0;
    }
    if (slot.mode == SaturationMode::tube)
        st.hpY = dcCoeff * (st.hpY + res - st.hpX);
    else
        st.hpY = res;
    st.hpX = res;
    return st.hpY;
}

void Saturation::slotWetFrame (int index, const double* x, int nch, double* wet, double dcWeight, bool measuringDc, bool seedDc, double dcCoeff) noexcept
{
    const auto& slot = slots[static_cast<size_t> (index)];
    const auto k = static_cast<size_t> (index);
    if (slot.channels == SaturationChannels::stereo || nch < 2)
    {
        if (slot.channels == SaturationChannels::side && nch < 2)
        {
            for (int c = 0; c < nch; ++c) wet[c] = 0.0;   // mono: no side
            return;
        }
        for (int c = 0; c < nch; ++c)
            wet[c] = slotWet (slot, channelState[static_cast<size_t> (c)].slot[k], x[c], dcWeight, measuringDc, seedDc, dcCoeff);
        return;
    }
    // Mid or side: the curve hears that part alone (channel 0's filters); what it adds goes back onto it.
    const bool mid = slot.channels == SaturationChannels::mid;
    const double part = mid ? 0.5 * (x[0] + x[1]) : 0.5 * (x[0] - x[1]);
    const double w = slotWet (slot, channelState[0].slot[k], part, dcWeight, measuringDc, seedDc, dcCoeff);
    wet[0] = w;
    wet[1] = mid ? w : -w;
    for (int c = 2; c < nch; ++c) wet[c] = 0.0;
}

void Saturation::processFrame (double* frame, int numChannels) noexcept
{
    const int nch = std::min (numChannels, channelCount);
    const double mixTarget = effectiveMix (params);
    bendTarget = std::pow (10.0, (kHeadroomDb - std::clamp (params.driveDb, 0.0, kMaxDriveDb)) / 20.0);
    auto& cur = slots[static_cast<size_t> (heard)];
    auto& other = slots[static_cast<size_t> (1 - heard)];

    // Let the filters fill, then (Full, tube) measure the curve's DC for dcWindow, before a slot is heard.
    const int warmSamples = 2 * kLatency + dcWindow;
    const bool idle = mix < 1.0e-6 && mixTarget == 0.0 && cur.warm == 0 && other.warm == 0;
    if (idle)
    {
        if (mix != 0.0 || running)
        {
            mix = 0.0;
            running = false;
            stopSlot (0);
            stopSlot (1);
            blend = 0.0;   // nothing is heard: the next start begins in the set Focus and Mode
        }
    }
    else
    {
        const bool curWanted = wanted (cur), otherWanted = wanted (other);
        if (! running)
        {
            running = true;
            startSlot (heard, params.focus, params.mode, params.channels, warmSamples);
            bend = bendTarget;
        }
        else if (! curWanted && ! otherWanted && blend == 0.0)
            startSlot (1 - heard, params.focus, params.mode, params.channels, warmSamples);   // Focus, Mode or Channels changed: warm the new one up next to this one
        // (A change during a cross-fade lets it finish; the next one starts from there.)

        // Mix waits while a slot that is heard warms up (only at the start); the cross-fade waits for the
        // slot it goes to.
        const bool heardWarming = (blend < 1.0 && cur.warm > 0) || (blend > 0.0 && other.warm > 0);
        for (auto& slot : slots)
            if (slot.warm > 0)
                --slot.warm;
        if (! heardWarming)
            mix += (mixTarget - mix) * mixCoeff;
        if (other.active && other.warm == 0)
        {
            const double goal = wanted (cur) ? 0.0 : 1.0;
            blend = goal > blend ? std::min (goal, blend + blendStep) : std::max (goal, blend - blendStep);
        }
        if (blend >= 1.0)
        {
            stopSlot (heard);   // faded over: the other one is the heard one now
            heard = 1 - heard;
            blend = 0.0;
        }
        else if (blend <= 0.0 && other.active && wanted (cur))
            stopSlot (1 - heard);   // faded back (or switched back before it was heard)
        bend += (bendTarget - bend) * glideCoeff;
    }

    // The 1 Hz DC filter would need ~0.5 s to settle from cold and let the DC out as a slow thump;
    // instead it starts from the DC measured during the warm-up, as if it had been running all along.
    // Started by reset() there is no warm-up: it starts at kDcStartHz and glides down to 1 Hz.
    const Slot& b = slots[static_cast<size_t> (1 - heard)];
    std::array<bool, 2> measuringDc {}, seedDc {};
    std::array<double, 2> dcWeight {}, dcCoeff {};
    for (size_t k = 0; k < 2; ++k)
    {
        auto& slot = slots[k];
        if (! slot.active || idle)
            continue;
        measuringDc[k] = slot.focus == SaturationFocus::full && slot.warm > 0 && slot.warm <= dcWindow;
        seedDc[k] = slot.focus == SaturationFocus::full && slot.warm == 0 && slot.dcCount > 0;
        // A Hann-weighted mean, so the bass itself (whatever its period) hardly leaks into the DC estimate.
        dcWeight[k] = measuringDc[k] ? std::pow (std::sin (std::numbers::pi * (slot.dcCount + 0.5) / dcWindow), 2.0) : 0.0;
        if (slot.dcHz > 1.0)
        {
            slot.dcHz += (1.0 - slot.dcHz) * dcGlideCoeff;
            if (slot.dcHz < 1.0001)
                slot.dcHz = 1.0;
        }
        dcCoeff[k] = slot.dcHz > 1.0 ? std::exp (-2.0 * std::numbers::pi * slot.dcHz / sampleRate) : hpCoeff;
    }
    // The cross-fade follows a raised cosine: it starts and ends gently, so the waveform never kinks.
    const double weight = blend > 0.0 && blend < 1.0 ? 0.5 - 0.5 * std::cos (std::numbers::pi * blend) : blend;
    const auto h = static_cast<size_t> (heard), o = static_cast<size_t> (1 - heard);

    // What each slot adds, per channel (a mid / side slot works on that part and puts it back on L and R).
    std::array<double, kMaxChannels> in {}, wetA {}, wetB {};
    for (int c = 0; c < nch; ++c) in[static_cast<size_t> (c)] = frame[c];
    if (! idle)
    {
        slotWetFrame (static_cast<int> (h), in.data(), nch, wetA.data(), dcWeight[h], measuringDc[h], seedDc[h], dcCoeff[h]);
        if (b.active)
            slotWetFrame (static_cast<int> (o), in.data(), nch, wetB.data(), dcWeight[o], measuringDc[o], seedDc[o], dcCoeff[o]);
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

        double wet = wetA[static_cast<size_t> (c)];
        if (b.active && weight > 0.0)
        {
            const double next = wetB[static_cast<size_t> (c)];
            wet = weight >= 1.0 ? next : (1.0 - weight) * wet + weight * next;
        }

        resEnergy += mix * mix * wet * wet;   // what is actually added at this Mix
        sigEnergy += dry * dry;
        frame[c] = dry + mix * wet;
    }
    for (size_t k = 0; k < 2; ++k)
        slots[k].dcCount = measuringDc[k] ? slots[k].dcCount + 1 : 0;
}

} // namespace polisher

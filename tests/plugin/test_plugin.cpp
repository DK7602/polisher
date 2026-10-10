// Plug-in-level checks: the real PolisherAudioProcessor (all eleven modules, parameters, MATCH,
// latency reporting, state) driven the way a DAW drives it. The engine tests check each module on
// its own; these check what only shows up in the assembled plug-in: block sizes, offline export,
// host delay compensation, sample-rate changes, allocation on the audio thread, saved state.

#include "PluginProcessor.h"
#include "../Signals.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <functional>
#include <numbers>
#include <new>
#include <random>

// --- Allocation counter: every operator new while `countAllocations` is set is counted. ----------
namespace {
std::atomic<bool> countAllocations { false };
std::atomic<int> allocations { 0 };
}

void* operator new (std::size_t n)
{
    if (countAllocations.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace {

using polisher::AudioClip;

void set (PolisherAudioProcessor& p, const juce::String& id, float plain)
{
    auto* param = p.parameters.getParameter (id);
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

/** Every module doing real work, as after an Auto-Master run on a loud trap mix. */
void busySettings (PolisherAudioProcessor& p)
{
    set (p, "ulAmount", 75.0f);
    set (p, "eqGain1", 2.0f);  set (p, "eqGain4", -1.5f); set (p, "eqGain6", 1.0f);
    set (p, "stemVoc", 3.0f);  set (p, "stemDrums", -2.0f);
    set (p, "tsAmount", 40.0f); set (p, "rtAmount", 50.0f);
    set (p, "bassMono", 120.0f); set (p, "bassTame", 30.0f); set (p, "bassPunch", 30.0f);
    for (int b = 1; b <= 4; ++b) { set (p, "dynRatio" + juce::String (b), 2.0f); set (p, "dynThr" + juce::String (b), -30.0f); }
    set (p, "imgHigh", 130.0f);
    set (p, "satDrive", 6.0f); set (p, "satMix", 50.0f);
    set (p, "drive", 6.0f);
    set (p, "clipOn", 1.0f); set (p, "clipLevel", 0.5f);
    set (p, "ceiling", -1.0f);
}

std::unique_ptr<PolisherAudioProcessor> makeProcessor (double sr, int maxBlock, bool busy)
{
    auto p = std::make_unique<PolisherAudioProcessor>();
    set (*p, "autoreset", 0.0f);
    if (busy) busySettings (*p);
    p->setRateAndBufferSizeDetails (sr, maxBlock);
    p->prepareToPlay (sr, maxBlock);
    return p;
}

/** Runs the clip through the processor in blocks whose sizes come from nextBlock(). */
template <typename NextBlock>
AudioClip render (PolisherAudioProcessor& p, const AudioClip& in, NextBlock nextBlock)
{
    AudioClip out = in;
    juce::MidiBuffer midi;
    const int total = in.numSamples();
    juce::AudioBuffer<float> buf (2, 8192);
    for (int pos = 0; pos < total;)
    {
        const int n = std::min (nextBlock(), total - pos);
        buf.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c)
            buf.copyFrom (c, 0, in.channels[static_cast<size_t> (c)].data() + pos, n);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + n, out.channels[static_cast<size_t> (c)].begin() + pos);
        pos += n;
    }
    return out;
}

AudioClip renderFixed (PolisherAudioProcessor& p, const AudioClip& in, int block)
{
    return render (p, in, [block] { return block; });
}

double maxAbsDiff (const AudioClip& a, const AudioClip& b, int from = 0)
{
    double d = 0.0;
    for (size_t c = 0; c < a.channels.size(); ++c)
        for (size_t i = static_cast<size_t> (from); i < a.channels[c].size(); ++i)
            d = std::max (d, std::abs ((double) a.channels[c][i] - b.channels[c][i]));
    return d;
}

} // namespace

TEST_CASE ("Plug-in: every block size gives the same master", "[plugin]")
{
    const double sr = 48000.0;
    const auto mix = testsig::musicLike (sr, 6.0, -6.0);
    auto ref = makeProcessor (sr, 4096, true);
    const auto reference = renderFixed (*ref, mix, 512);

    for (int block : { 1, 32, 100, 441, 480, 1024, 4096 })
    {
        auto p = makeProcessor (sr, 4096, true);
        const auto clip = block == 1 ? testsig::musicLike (sr, 2.0, -6.0) : mix;
        const auto out = renderFixed (*p, clip, block);
        const auto refOut = block == 1 ? renderFixed (*makeProcessor (sr, 4096, true), clip, 512) : reference;
        INFO ("block size " << block << ": largest difference " << maxAbsDiff (out, refOut));
        CHECK (maxAbsDiff (out, refOut) < 1.0e-5);
    }

    // A host that changes the block size every call (FL Studio, some offline renders).
    std::mt19937 rng (3);
    std::uniform_int_distribution<int> size (1, 4096);
    auto p = makeProcessor (sr, 4096, true);
    const auto out = render (*p, mix, [&] { return size (rng); });
    INFO ("random block sizes: largest difference " << maxAbsDiff (out, reference));
    CHECK (maxAbsDiff (out, reference) < 1.0e-5);
}

TEST_CASE ("Plug-in: offline export matches real time, and MATCH never reaches the export", "[plugin]")
{
    const double sr = 44100.0;
    const auto mix = testsig::musicLike (sr, 8.0, -6.0);

    auto live = makeProcessor (sr, 512, true);
    const auto liveOut = renderFixed (*live, mix, 512);

    auto offline = makeProcessor (sr, 512, true);
    offline->setNonRealtime (true);
    const auto offlineOut = renderFixed (*offline, mix, 512);
    CHECK (maxAbsDiff (liveOut, offlineOut) < 1.0e-6);

    // MATCH on: the live master is turned down to the mix's loudness, the export is not.
    auto matchedLive = makeProcessor (sr, 512, true);
    set (*matchedLive, "levelMatch", 1.0f);
    const auto matchedOut = renderFixed (*matchedLive, mix, 512);
    auto matchedOffline = makeProcessor (sr, 512, true);
    set (*matchedOffline, "levelMatch", 1.0f);
    matchedOffline->setNonRealtime (true);
    const auto exportOut = renderFixed (*matchedOffline, mix, 512);

    CHECK (maxAbsDiff (exportOut, liveOut) < 1.0e-6);
    const auto tail = static_cast<int> (4.0 * sr);   // MATCH has learned the gap by now
    CHECK (polisher::measure (matchedOut).integratedLufs < polisher::measure (liveOut).integratedLufs - 1.0);
    CHECK (maxAbsDiff (matchedOut, liveOut, tail) > 0.01);
}

TEST_CASE ("Plug-in: the latency it reports is the real delay, at every sample rate", "[plugin]")
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        // Every module on at its neutral settings, Drive 0, quiet audio: each module is an exact
        // pass-through there, so the master must be the mix delayed by exactly the reported latency.
        auto p = makeProcessor (sr, 512, false);
        set (*p, "ceiling", 0.0f);
        const int latency = p->getLatencySamples();
        const auto mix = testsig::musicLike (sr, 2.0, -24.0);
        const auto out = renderFixed (*p, mix, 512);

        double err = 0.0, ref = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = latency; i < mix.numSamples(); ++i)
            {
                const double d = (double) out.channels[(size_t) c][(size_t) i] - mix.channels[(size_t) c][(size_t) (i - latency)];
                err += d * d;
                ref += (double) mix.channels[(size_t) c][(size_t) i] * mix.channels[(size_t) c][(size_t) i];
            }
        INFO ("sample rate " << sr << ": latency " << latency << " samples, error " << 10.0 * std::log10 (err / ref + 1e-30) << " dB");
        CHECK (latency > 0);
        CHECK (err / ref < 1.0e-10);    // -100 dB

        // The global bypass (A) plays the mix with exactly the same delay, so A/B never jumps in time.
        set (*p, "bypass", 1.0f);
        const auto bypassed = renderFixed (*p, mix, 512);
        double errA = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = latency; i < mix.numSamples(); ++i)
                errA = std::max (errA, std::abs ((double) bypassed.channels[(size_t) c][(size_t) i] - mix.channels[(size_t) c][(size_t) (i - latency)]));
        CHECK (errA < 1.0e-6);
    }
}

TEST_CASE ("Plug-in: the host's bypass button is Polisher's latency-matched A, and the tail covers the delay", "[plugin]")
{
    auto p = makeProcessor (48000.0, 512, false);
    auto* hostBypass = p->getBypassParameter();
    REQUIRE (hostBypass != nullptr);
    CHECK (hostBypass == p->parameters.getParameter ("bypass"));
    // A host that stops calling silent plug-ins must wait for the delayed audio and the release.
    CHECK (p->getTailLengthSeconds() > p->getLatencySamples() / 48000.0 + 0.1);
}

TEST_CASE ("Plug-in: changing sample rate starts clean (same as a fresh plug-in)", "[plugin]")
{
    const auto mix44 = testsig::musicLike (44100.0, 4.0, -6.0, 11);
    auto p = makeProcessor (96000.0, 512, true);
    renderFixed (*p, testsig::musicLike (96000.0, 3.0, 0.0, 5), 512);   // leave state behind at 96k
    p->setRateAndBufferSizeDetails (44100.0, 512);
    p->prepareToPlay (44100.0, 512);
    const auto reused = renderFixed (*p, mix44, 512);

    auto fresh = makeProcessor (44100.0, 512, true);
    const auto clean = renderFixed (*fresh, mix44, 512);
    INFO ("largest difference " << maxAbsDiff (reused, clean));
    CHECK (maxAbsDiff (reused, clean) < 1.0e-5);
}

TEST_CASE ("Plug-in: processing never allocates memory on the audio thread", "[plugin]")
{
    const double sr = 48000.0;
    auto p = makeProcessor (sr, 1024, true);
    const auto mix = testsig::musicLike (sr, 3.0, -6.0);
    renderFixed (*p, testsig::musicLike (sr, 1.0, -6.0), 1024);     // warm up

    juce::AudioBuffer<float> buf (2, 1024);
    juce::MidiBuffer midi;
    std::mt19937 rng (9);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    const auto& params = p->getParameters();
    int worst = 0;
    for (int pos = 0; pos + 1024 <= mix.numSamples(); pos += 1024)
    {
        // Automation between blocks (as a host delivers it, A/B and MATCH flips included), then count
        // what the block allocates. Counts operator new only: malloc-based buffers are not seen here.
        for (int k = 0; k < 3; ++k)
            params[(int) (u (rng) * (float) params.size()) % params.size()]->setValueNotifyingHost (u (rng));
        for (int c = 0; c < 2; ++c)
            buf.copyFrom (c, 0, mix.channels[(size_t) c].data() + pos, 1024);
        allocations = 0;
        countAllocations = true;
        p->processBlock (buf, midi);
        countAllocations = false;
        worst = std::max (worst, allocations.load());
    }
    CHECK (worst == 0);
}

TEST_CASE ("Plug-in: saved state brings back every setting", "[plugin]")
{
    auto a = makeProcessor (48000.0, 512, false);
    std::mt19937 rng (21);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (auto* param : a->getParameters())
        param->setValueNotifyingHost (u (rng));

    juce::MemoryBlock state;
    a->getStateInformation (state);
    auto b = makeProcessor (48000.0, 512, false);
    b->setStateInformation (state.getData(), (int) state.getSize());

    const auto& pa = a->getParameters();
    const auto& pb = b->getParameters();
    REQUIRE (pa.size() == pb.size());
    for (int i = 0; i < pa.size(); ++i)
    {
        // Compare what each control means (a switch set to 0.42 by a host saves as "on"... or "off").
        auto* ra = dynamic_cast<juce::RangedAudioParameter*> (pa[i]);
        auto* rb = dynamic_cast<juce::RangedAudioParameter*> (pb[i]);
        REQUIRE ((ra != nullptr && rb != nullptr));
        INFO (ra->paramID << ": " << ra->getCurrentValueAsText() << " vs " << rb->getCurrentValueAsText());
        CHECK (ra->paramID == rb->paramID);
        CHECK (std::abs (ra->convertFrom0to1 (ra->getValue()) - rb->convertFrom0to1 (rb->getValue()))
               <= 1.0e-4f * std::max (1.0f, std::abs (ra->convertFrom0to1 (ra->getValue()))));
    }
}

TEST_CASE ("Plug-in: every control jumping at random never produces NaN or goes over the ceiling", "[plugin]")
{
    const double sr = 44100.0;
    auto p = makeProcessor (sr, 512, true);
    const auto mix = testsig::musicLike (sr, 20.0, 0.0);
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    const auto& params = p->getParameters();

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    bool finite = true;
    const int settle = (int) (0.5 * sr);
    AudioClip settled = testsig::silence (sr, 2, 0.0);
    for (int pos = 0; pos + 512 <= mix.numSamples(); pos += 512)
    {
        for (auto* param : params)
        {
            const auto id = dynamic_cast<juce::AudioProcessorParameterWithID*> (param)->paramID;
            if (id == "bypass" || id == "levelMatch" || id == "ceiling") continue;   // A and MATCH skip the limiter / turn down
            if (u (rng) < 0.3f) param->setValueNotifyingHost (u (rng));
        }
        for (int c = 0; c < 2; ++c)
            buf.copyFrom (c, 0, mix.channels[(size_t) c].data() + pos, 512);
        p->processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
            {
                const float v = buf.getSample (c, i);
                finite = finite && std::isfinite (v);
                if (pos >= settle) settled.channels[(size_t) c].push_back (v);
            }
    }
    CHECK (finite);
    // The limiter also clamps every sample to the ceiling, so check the true peak (between samples).
    const double tp = testsig::truePeakOf (settled);
    INFO ("true peak " << tp << " dBTP");
    CHECK (tp <= -0.95);
}

TEST_CASE ("Plug-in: double-precision hosts get the same master as single precision", "[plugin]")
{
    const double sr = 48000.0;
    const auto mix = testsig::musicLike (sr, 4.0, -6.0);
    auto pf = makeProcessor (sr, 512, true);
    const auto single = renderFixed (*pf, mix, 512);

    auto pd = makeProcessor (sr, 512, true);
    AudioClip dbl = mix;
    juce::AudioBuffer<double> buf (2, 512);
    juce::MidiBuffer midi;
    for (int pos = 0; pos + 512 <= mix.numSamples(); pos += 512)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                buf.setSample (c, i, mix.channels[(size_t) c][(size_t) (pos + i)]);
        pd->processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                dbl.channels[(size_t) c][(size_t) (pos + i)] = (float) buf.getSample (c, i);
    }
    INFO ("largest difference " << maxAbsDiff (single, dbl));
    CHECK (maxAbsDiff (single, dbl) < 1.0e-4);
}


TEST_CASE ("Plug-in: Input Trim turns the mix down before the chain, and A ignores it", "[plugin]")
{
    const double sr = 48000.0;
    auto p = makeProcessor (sr, 512, false);
    set (*p, "ceiling", 0.0f);
    set (*p, "inTrim", -6.0f);
    p->prepareToPlay (sr, 512);                          // starts at -6 dB (no ramp from 0)
    const int latency = p->getLatencySamples();
    const auto mix = testsig::musicLike (sr, 2.0, -24.0);
    const auto out = renderFixed (*p, mix, 512);
    const double g = std::pow (10.0, -6.0 / 20.0);
    double err = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int i = latency; i < mix.numSamples(); ++i)
            err = std::max (err, std::abs ((double) out.channels[(size_t) c][(size_t) i] - g * mix.channels[(size_t) c][(size_t) (i - latency)]));
    CHECK (err < 1.0e-6);

    // A (global bypass) plays the original mix: no trim.
    set (*p, "bypass", 1.0f);
    p->prepareToPlay (sr, 512);
    const auto a = renderFixed (*p, mix, 512);
    double errA = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int i = latency; i < mix.numSamples(); ++i)
            errA = std::max (errA, std::abs ((double) a.channels[(size_t) c][(size_t) i] - mix.channels[(size_t) c][(size_t) (i - latency)]));
    CHECK (errA < 1.0e-6);
}

TEST_CASE ("Plug-in: the Unlimiter lifts the first bar of an export like the rest of the song", "[plugin]")
{
    // A brickwalled beat, turned down so the lifted hits stay under the Maximizer's ceiling.
    const double sr = 48000.0;
    polisher::LimiterParams lp; lp.driveDb = 14.0; lp.ceilingDbtp = -0.3; lp.releaseMs = 30.0; lp.adaptiveRelease = false;
    const auto beat = polisher::withGain (polisher::renderLimited (testsig::musicLike (sr, 4.0, -6.0, 4), lp), -12.0);
    // Level of the second hit (0.5 s, inside the first second, while a fresh meter is still listening).
    auto secondHitDb = [&] (const AudioClip& out, int latency)
    {
        double e = 0.0;
        for (int i = static_cast<int> (0.5 * sr) + latency; i < static_cast<int> (0.53 * sr) + latency; ++i)
            e += (double) out.channels[0][(size_t) i] * out.channels[0][(size_t) i];
        return 10.0 * std::log10 (e + 1.0e-30);
    };
    auto exportAfter = [&] (float squashSet, bool playedBefore)
    {
        auto p = makeProcessor (sr, 512, false);
        set (*p, "ulAmount", 100.0f);
        set (*p, "ulSquashSet", squashSet);
        if (playedBefore) renderFixed (*p, beat, 512);
        p->prepareToPlay (sr, 512);    // the export starts here
        return secondHitDb (renderFixed (*p, beat, 512), p->getLatencySamples());
    };
    auto off = makeProcessor (sr, 512, false);
    const double untouched = secondHitDb (renderFixed (*off, beat, 512), off->getLatencySamples());
    const double fresh = exportAfter (0.0f, false);
    const double latched = exportAfter ((float) polisher::Unlimiter::measureSquashDb (beat.channels, sr), false);
    const double replayed = exportAfter (0.0f, true);
    INFO ("second hit: untouched " << untouched << " dB, fresh meter " << fresh << ", Auto-Master's Squash " << latched << ", after playback " << replayed);
    CHECK (fresh - untouched < 0.5);        // a meter that has heard nothing yet can't know (it used to be this way after every prepare)
    CHECK (latched - untouched > 1.5);      // Auto-Master's reading lifts it at once...
    CHECK (replayed - untouched > 1.5);     // ...and so does the last reading from playback
    CHECK (std::abs (latched - replayed) < 1.0);
    CHECK (! off->parameters.getParameter ("ulSquashSet")->isAutomatable());   // set by Auto-Master, not a control
}

TEST_CASE ("Plug-in: a state saved before a control existed puts that control back to its default", "[plugin]")
{
    // A v0.13 preset has no Input Trim. Loaded into a running Polisher whose trim Auto-Master set to
    // -6.6 dB, the trim must go back to 0 dB, not stay (the master would play 6.6 dB quieter). JUCE's
    // parameter tree does this (a control missing from the state gets its default); this pins it.
    auto old = makeProcessor (48000.0, 512, false);
    juce::MemoryBlock state;
    old->getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    REQUIRE (xml != nullptr);
    for (auto* child : xml->getChildIterator())
        if (child->getStringAttribute ("id") == "inTrim") { xml->removeChildElement (child, true); break; }
    juce::MemoryBlock v013;
    juce::AudioProcessor::copyXmlToBinary (*xml, v013);

    auto p = makeProcessor (48000.0, 512, false);
    set (*p, "inTrim", -6.6f);
    set (*p, "drive", 3.0f);
    p->setStateInformation (v013.getData(), (int) v013.getSize());
    CHECK (p->parameters.getRawParameterValue ("inTrim")->load() == 0.0f);
    CHECK (p->parameters.getRawParameterValue ("drive")->load() == 0.0f);    // from the preset
}

TEST_CASE ("Plug-in: Stem EQ Keep loudness is on for a new instance, off for a song saved before it existed", "[plugin]")
{
    auto keepOf = [] (PolisherAudioProcessor& p) { return p.parameters.getRawParameterValue ("stemKeep")->load(); };
    auto fresh = makeProcessor (48000.0, 512, false);
    CHECK (keepOf (*fresh) == 1.0f);

    // Round trip keeps whatever was saved (on, or switched off).
    for (float v : { 1.0f, 0.0f })
    {
        set (*fresh, "stemKeep", v);
        juce::MemoryBlock state;
        fresh->getStateInformation (state);
        auto p = makeProcessor (48000.0, 512, false);
        set (*p, "stemKeep", 1.0f - v);
        p->setStateInformation (state.getData(), (int) state.getSize());
        CHECK (keepOf (*p) == v);
    }

    // A v0.15.0 song (no stemKeep in its state) with Bass +6 loads with Keep loudness off: same sound as before.
    set (*fresh, "stemBass", 6.0f);
    juce::MemoryBlock state;
    fresh->getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    REQUIRE (xml != nullptr);
    for (auto* child : xml->getChildIterator())
        if (child->getStringAttribute ("id") == "stemKeep") { xml->removeChildElement (child, true); break; }
    juce::MemoryBlock v0150;
    juce::AudioProcessor::copyXmlToBinary (*xml, v0150);
    auto old = makeProcessor (48000.0, 512, false);
    old->setStateInformation (v0150.getData(), (int) v0150.getSize());
    CHECK (keepOf (*old) == 0.0f);
    CHECK (old->parameters.getRawParameterValue ("stemBass")->load() == 6.0f);
}

TEST_CASE ("Plug-in: Saturation Focus is Full for a new instance and for a song saved before it existed", "[plugin][saturation]")
{
    const double sr = 48000.0;
    auto focusOf = [] (PolisherAudioProcessor& p) { return p.parameters.getRawParameterValue ("satFocus")->load(); };
    auto fresh = makeProcessor (sr, 512, false);
    CHECK (focusOf (*fresh) == 0.0f);

    // Bass only survives a save and load.
    set (*fresh, "satMode", 1.0f);
    set (*fresh, "satDrive", 12.0f);
    set (*fresh, "satMix", 50.0f);
    set (*fresh, "satFocus", 1.0f);
    juce::MemoryBlock state;
    fresh->getStateInformation (state);
    auto again = makeProcessor (sr, 512, false);
    again->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (focusOf (*again) == 1.0f);

    // A v0.15.0 song (no satFocus in its state, Tube +12) loads as Full in a Polisher set to Bass only,
    // and plays exactly like the old version did (Full is the old algorithm, bit for bit).
    set (*fresh, "satFocus", 0.0f);
    fresh->getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    REQUIRE (xml != nullptr);
    for (auto* child : xml->getChildIterator())
        if (child->getStringAttribute ("id") == "satFocus") { xml->removeChildElement (child, true); break; }
    juce::MemoryBlock v0150;
    juce::AudioProcessor::copyXmlToBinary (*xml, v0150);
    auto old = makeProcessor (sr, 512, false);
    set (*old, "satFocus", 1.0f);
    old->setStateInformation (v0150.getData(), (int) v0150.getSize());
    CHECK (focusOf (*old) == 0.0f);
    CHECK (old->parameters.getRawParameterValue ("satDrive")->load() == 12.0f);   // from the song

    const auto beat = testsig::trapLike (sr, 3.0);
    fresh->prepareToPlay (sr, 512);
    old->prepareToPlay (sr, 512);
    CHECK (maxAbsDiff (renderFixed (*fresh, beat, 512), renderFixed (*old, beat, 512)) == 0.0);
}

TEST_CASE ("Plug-in: Saturation Bass only reaches the engine (a vocal tone passes clean, Full distorts it)", "[plugin][saturation]")
{
    const double sr = 48000.0;
    const auto voice = testsig::sine (sr, 2, 2000.0, -6.0, 1.0);
    auto renderWith = [&] (float drive, float focus)
    {
        auto p = makeProcessor (sr, 512, false);
        set (*p, "satMode", 1.0f);
        set (*p, "satDrive", drive);
        set (*p, "satFocus", focus);
        p->prepareToPlay (sr, 512);
        return renderFixed (*p, voice, 512);
    };
    const auto clean = renderWith (0.0f, 0.0f), bass = renderWith (18.0f, 1.0f), full = renderWith (18.0f, 0.0f);
    const int settled = 4800;   // after the tone's first 0.1 s (its abrupt start has a little low end of its own)
    INFO ("Bass only vs untouched " << maxAbsDiff (bass, clean, settled) << ", Full vs untouched " << maxAbsDiff (full, clean, settled));
    CHECK (maxAbsDiff (bass, clean, settled) < 1.0e-5);
    CHECK (maxAbsDiff (full, clean, settled) > 1.0e-2);
}

TEST_CASE ("Plug-in: Stem EQ Keep loudness holds the level of a Bass move and shows how much", "[plugin]")
{
    const double sr = 48000.0;
    const auto mix = testsig::musicLike (sr, 6.0, -12.0);
    auto loudness = [&] (bool keep, double& keepDb)
    {
        auto p = makeProcessor (sr, 512, false);
        set (*p, "ceiling", 0.0f);
        set (*p, "stemBass", 6.0f);
        set (*p, "stemKeep", keep ? 1.0f : 0.0f);
        p->prepareToPlay (sr, 512);
        const auto out = renderFixed (*p, mix, 512);
        keepDb = p->meters.stemKeep.load();
        return polisher::measure (out).integratedLufs;
    };
    double dbOn = 0.0, dbOff = 0.0;
    const double ref = polisher::measure (mix).integratedLufs, on = loudness (true, dbOn), off = loudness (false, dbOff);
    INFO ("mix " << ref << " LUFS; bass +6: keep on " << on << " (keeping " << dbOn << " dB), off " << off);
    CHECK (std::abs (on - ref) < 0.5);
    CHECK (off - ref > 1.0);
    CHECK (dbOn < -1.0);
    CHECK (dbOff == 0.0f);
}

TEST_CASE ("Plug-in: the Match ref reference is saved with the project and comes back", "[plugin][reference]")
{
    // Only the measurements and the name are kept: a song with a non-ASCII name, measured once.
    auto profile = polisher::analyseReference (testsig::musicLike (48000.0, 35.0, -12.0, 5));
    REQUIRE (profile.valid);
    profile.name = juce::String::fromUTF8 ("Beyonc\xC3\xA9 \xE2\x80\x93 Ref").toStdString();

    auto a = makeProcessor (48000.0, 512, false);
    a->reference.setProfile (profile);
    CHECK (a->reference.getState() == ReferenceTrack::loaded);
    juce::MemoryBlock state;
    a->getStateInformation (state);
    CHECK (state.getSize() < 64 * 1024);   // a summary, not audio

    auto b = makeProcessor (48000.0, 512, false);
    const int before = b->reference.getVersion();
    b->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b->reference.getVersion() > before);   // the editor sees the change
    const auto got = b->reference.getProfile();
    REQUIRE (got.valid);
    // Numbers go through JSON text (15 significant digits): equal to far below anything audible.
    auto same = [] (double x, double y) { return std::abs (x - y) <= 1.0e-9 * std::max (1.0, std::abs (y)); };
    CHECK (got.name == profile.name);
    CHECK (same (got.lufs, profile.lufs));
    CHECK (same (got.trackLufs, profile.trackLufs));
    for (size_t k = 0; k < profile.bandDb.size(); ++k)
        CHECK (same (got.bandDb[k], profile.bandDb[k]));
    REQUIRE (got.displayDb.size() == profile.displayDb.size());
    for (size_t k = 0; k < profile.displayDb.size(); ++k)
        CHECK (same (got.displayDb[k], profile.displayDb[k]));
    CHECK (same (got.bassWidthPercent, profile.bassWidthPercent));
    CHECK (same (got.midSideDb, profile.midSideDb));
    CHECK (same (got.highSideDb, profile.highSideDb));
    CHECK (same (got.subVsBassDb, profile.subVsBassDb));
    CHECK (same (got.startSeconds, profile.startSeconds));

    // A project or preset without a reference (also every state from before Match ref) clears it:
    // Auto-Master goes back to the genre targets, and every control still loads.
    auto none = makeProcessor (48000.0, 512, false);
    set (*none, "drive", 4.0f);
    juce::MemoryBlock plain;
    none->getStateInformation (plain);
    auto xml = juce::AudioProcessor::getXmlFromBinary (plain.getData(), (int) plain.getSize());
    REQUIRE (xml != nullptr);
    CHECK_FALSE (xml->hasAttribute ("amReference"));
    b->setStateInformation (plain.getData(), (int) plain.getSize());
    CHECK_FALSE (b->reference.getProfile().valid);
    CHECK (b->reference.getState() == ReferenceTrack::empty);
    CHECK (b->parameters.getRawParameterValue ("drive")->load() == 4.0f);

    // A damaged or future-format reference is ignored rather than half-read.
    xml->setAttribute ("amReference", "{\"format\": 9, \"name\": \"x\"}");
    juce::MemoryBlock odd;
    juce::AudioProcessor::copyXmlToBinary (*xml, odd);
    b->setStateInformation (odd.getData(), (int) odd.getSize());
    CHECK_FALSE (b->reference.getProfile().valid);
}

TEST_CASE ("Plug-in: a reference file is read in the background and its loudest part measured", "[plugin][reference]")
{
    // MP3 support is compiled in (JUCE_USE_MP3AUDIOFORMAT), next to WAV, AIFF and FLAC.
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        for (const char* ext : { ".wav", ".aiff", ".flac", ".mp3" })
        {
            INFO (ext);
            CHECK (formats.findFormatForFileExtension (ext) != nullptr);
        }
    }

    // A 50 s song: quiet 15 s intro, loud 35 s, written as a 44.1 kHz 24-bit WAV.
    const double sr = 44100.0;
    auto song = testsig::musicLike (sr, 15.0, -34.0, 1);
    const auto chorus = testsig::musicLike (sr, 35.0, -14.0, 2);
    for (size_t c = 0; c < 2; ++c)
        song.channels[c].insert (song.channels[c].end(), chorus.channels[c].begin(), chorus.channels[c].end());
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PolisherRefTest");
    dir.createDirectory();
    const auto file = dir.getChildFile ("My Reference.wav");
    file.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
        REQUIRE (out != nullptr);
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        const float* ptrs[] = { song.channels[0].data(), song.channels[1].data() };
        REQUIRE (writer->writeFromFloatArrays (ptrs, 2, song.numSamples()));
    }

    juce::String error;
    const auto direct = ReferenceTrack::analyseFile (file, error);
    INFO (error);
    REQUIRE (direct.valid);
    CHECK (direct.name == "My Reference");
    CHECK (std::abs (direct.trackSeconds - 50.0) < 0.05);
    CHECK (direct.startSeconds >= 15.0 - 0.1);                 // inside the loud part, not the intro
    CHECK (direct.startSeconds <= 20.0 + 0.1);
    CHECK (std::abs (direct.partSeconds - polisher::kReferencePartSeconds) < 0.01);
    CHECK (std::abs (direct.lufs - polisher::measure (chorus).integratedLufs) < 0.5);

    // The real thing: load() returns at once, the reading happens on a background thread.
    ReferenceTrack ref;
    ref.load (file);
    for (int i = 0; i < 600 && ref.getState() == ReferenceTrack::loading; ++i)
        juce::Thread::sleep (50);
    REQUIRE (ref.getState() == ReferenceTrack::loaded);
    CHECK (ref.getProfile().lufs == direct.lufs);              // the same result, every time
    CHECK (ref.getLastFolder() == dir);

    // A file it can't read leaves the loaded reference in place and says why in plain words.
    const auto junk = dir.getChildFile ("notes.wav");
    junk.replaceWithText ("not audio");
    ref.load (junk);
    for (int i = 0; i < 600 && ref.getState() == ReferenceTrack::loading; ++i)
        juce::Thread::sleep (50);
    CHECK (ref.getState() == ReferenceTrack::loaded);
    CHECK (ref.getProfile().name == "My Reference");
    CHECK (ref.getMessage().contains ("can't read"));

    ref.clear();
    CHECK (ref.getState() == ReferenceTrack::empty);
    CHECK_FALSE (ref.getProfile().valid);
    dir.deleteRecursively();
}

// --- Monitoring: Listen / Solo, MONO and PHONE ------------------------------------------------------

namespace {

using Setup = std::function<void (PolisherAudioProcessor&)>;

/** Every module at its neutral default except what `setup` sets; Drive 0, ceiling 0 dBTP. */
std::unique_ptr<PolisherAudioProcessor> plainProcessor (double sr, const Setup& setup)
{
    auto p = std::make_unique<PolisherAudioProcessor>();
    set (*p, "autoreset", 0.0f);
    set (*p, "ceiling", 0.0f);
    if (setup) setup (*p);
    p->setRateAndBufferSizeDetails (sr, 512);
    p->prepareToPlay (sr, 512);
    return p;
}

/** What comes out with a listen mode on, at its level in the master (no "hear it" gain). */
AudioClip heard (const AudioClip& mix, int mode, const Setup& setup)
{
    auto p = plainProcessor (mix.sampleRate, setup);
    p->setListenAutoLevel (false);
    p->setListenMode (mode);
    return renderFixed (*p, mix, 512);
}

AudioClip masterOf (const AudioClip& mix, const Setup& setup)
{
    auto p = plainProcessor (mix.sampleRate, setup);
    return renderFixed (*p, mix, 512);
}

/** Largest |a + sa * b - expected| from `from` on, with expected(c, i). */
template <typename Expected>
double worstError (const AudioClip& a, double sa, const AudioClip& b, int from, Expected expected)
{
    double e = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int i = from; i < a.numSamples(); ++i)
            e = std::max (e, std::abs ((double) a.channels[(size_t) c][(size_t) i] + sa * b.channels[(size_t) c][(size_t) i] - expected (c, i)));
    return e;
}

double peakFrom (const AudioClip& a, int from)
{
    double p = 0.0;
    for (const auto& ch : a.channels)
        for (size_t i = (size_t) from; i < ch.size(); ++i) p = std::max (p, (double) std::abs (ch[i]));
    return p;
}

double maxStepOf (const AudioClip& a, int from)
{
    double s = 0.0;
    for (const auto& ch : a.channels)
        for (size_t i = (size_t) std::max (from, 1); i < ch.size(); ++i) s = std::max (s, std::abs ((double) ch[i] - ch[i - 1]));
    return s;
}

AudioClip ringing (double sr, double seconds)
{
    auto in = testsig::musicLike (sr, seconds, -18.0, 5);
    for (auto& ch : in.channels)
        for (size_t i = 0; i < ch.size(); ++i)
            ch[i] += static_cast<float> (0.05 * std::sin (2.0 * std::numbers::pi * 2400.0 * static_cast<double> (i) / sr));
    return in;
}

} // namespace

TEST_CASE ("Listen: each delta lines up with the master to the sample, and is silent when its module rests", "[plugin][monitoring]")
{
    namespace L = polisher::listen;
    const double sr = 48000.0;
    const int settle = (int) (0.4 * sr);   // armed, measured and faded in by then

    SECTION ("Saturation: added = output minus aligned input")
    {
        const auto mix = testsig::musicLike (sr, 2.0, -6.0);
        const Setup on = [] (auto& p) { set (p, "satDrive", 12.0f); };
        const auto m1 = masterOf (mix, on), m0 = masterOf (mix, nullptr), l1 = heard (mix, L::satAdded, on);
        CHECK (peakFrom (m1, 0) < 0.95);   // the Maximizer doesn't touch it, so the master is sat output, delayed
        const double err = worstError (l1, -1.0, m1, settle, [&] (int c, int i) { return -(double) m0.channels[(size_t) c][(size_t) i]; });
        INFO ("listen - (master - dry master): " << err << ", delta peak " << peakFrom (l1, settle));
        CHECK (err < 1.0e-5);
        CHECK (peakFrom (l1, settle) > 0.01);
        CHECK (peakFrom (heard (mix, L::satAdded, nullptr), settle) == 0.0);   // Drive 0: nothing added, exactly
    }
    SECTION ("Soft Clipper: removed = aligned input x makeup minus output")
    {
        const auto tone = testsig::sine (sr, 2, 1000.0, -11.0, 1.5);
        const Setup off = [] (auto& p) { set (p, "clipLevel", -9.0f); set (p, "clipSoftness", 100.0f); };
        const Setup on = [] (auto& p) { set (p, "clipLevel", -9.0f); set (p, "clipSoftness", 100.0f); set (p, "clipOn", 1.0f); };
        const auto m1 = masterOf (tone, on), m0 = masterOf (tone, off), l1 = heard (tone, L::clipRemoved, on);
        const double makeup = std::pow (10.0, 9.0 / 20.0);
        CHECK (peakFrom (m1, 0) < 0.95);
        const double err = worstError (l1, 1.0, m1, settle, [&] (int c, int i) { return makeup * m0.channels[(size_t) c][(size_t) i]; });
        INFO ("listen + master - makeup x dry master: " << err << ", shaved peak " << peakFrom (l1, settle));
        CHECK (err < 1.0e-5);
        CHECK (peakFrom (l1, settle) > 0.01);
        CHECK (peakFrom (heard (tone, L::clipRemoved, off), settle) == 0.0);   // clipper off: nothing shaved
    }
    SECTION ("Resonance Tamer: removed = what its cuts took out of the spectral stage")
    {
        const auto mix = ringing (sr, 2.5);
        const Setup on = [] (auto& p) { set (p, "rtAmount", 100.0f); };
        auto probe = plainProcessor (sr, on);
        const int latency = probe->getLatencySamples();
        const auto m1 = masterOf (mix, on), l1 = heard (mix, L::tamerRemoved, on);
        // Only the tamer works: master + what it cut = the mix, delayed.
        const double err = worstError (l1, 1.0, m1, settle, [&] (int c, int i) { return i >= latency ? (double) mix.channels[(size_t) c][(size_t) (i - latency)] : 0.0; });
        INFO ("listen + master - delayed mix: " << err << ", cut peak " << peakFrom (l1, settle));
        CHECK (err < 1.0e-5);
        CHECK (peakFrom (l1, settle) > 0.01);
        CHECK (peakFrom (heard (mix, L::tamerRemoved, nullptr), settle) == 0.0);   // Amount 0: nothing cut
    }
    SECTION ("Maximizer: removed = aligned limiter input minus output")
    {
        const auto mix = testsig::musicLike (sr, 2.0, -6.0);
        // (Drive 6 keeps what is removed under 0 dBFS, where the monitoring's safety stage never acts.)
        const Setup on = [] (auto& p) { set (p, "drive", 6.0f); };
        auto probe = plainProcessor (sr, on);
        const int latency = probe->getLatencySamples();
        const double g = std::pow (10.0, 6.0 / 20.0);
        const auto m1 = masterOf (mix, on), l1 = heard (mix, L::maxRemoved, on);
        const double err = worstError (l1, 1.0, m1, settle, [&] (int c, int i) { return i >= latency ? g * mix.channels[(size_t) c][(size_t) (i - latency)] : 0.0; });
        INFO ("listen + master - driven mix: " << err << ", removed peak " << peakFrom (l1, settle));
        CHECK (err < 2.0e-5);
        CHECK (peakFrom (l1, settle) > 0.05);
        CHECK (peakFrom (l1, settle) < 0.99);
        CHECK (peakFrom (heard (mix, L::maxRemoved, nullptr), settle) == 0.0);   // not limiting: nothing removed
    }
    SECTION ("Dynamics: the four band solos add up to the master; removed + master = the uncompressed split")
    {
        const auto mix = testsig::musicLike (sr, 2.0, -6.0);
        const Setup on = [] (auto& p) { set (p, "dynRatio1", 4.0f); set (p, "dynThr1", -30.0f); set (p, "dynRatio3", 2.0f); set (p, "dynThr3", -40.0f); set (p, "dynGain3", 2.0f); };
        const Setup idle = [] (auto& p) { set (p, "dynRatio1", 1.01f); set (p, "dynThr1", 0.0f); };   // running, not compressing
        const auto m1 = masterOf (mix, on), split = masterOf (mix, idle);
        AudioClip sum = testsig::silence (sr, 2, 2.0);
        for (int k = 0; k < 4; ++k)
        {
            const auto s = heard (mix, L::dynSolo + k, on);
            CHECK (peakFrom (s, settle) > 0.001);
            for (size_t c = 0; c < 2; ++c)
                for (size_t i = 0; i < s.channels[c].size(); ++i) sum.channels[c][i] += s.channels[c][i];
        }
        const double solos = worstError (sum, -1.0, m1, settle, [] (int, int) { return 0.0; });
        INFO ("sum of the four solos - master: " << solos);
        CHECK (solos < 1.0e-5);
        // Removed is what each band's compression took off, before the band's gain: the master plus the
        // low band's removed part plus the high-mid's (at its +2 dB) is the same split uncompressed.
        const auto removed = heard (mix, L::dynRemoved + 0, on), removed3 = heard (mix, L::dynRemoved + 2, on);
        const Setup gainOnly = [] (auto& p) { set (p, "dynRatio1", 1.01f); set (p, "dynThr1", 0.0f); set (p, "dynGain3", 2.0f); };
        const auto uncompressed = masterOf (mix, gainOnly);
        const double g3 = std::pow (10.0, 2.0 / 20.0);
        AudioClip restored = m1;
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < restored.channels[c].size(); ++i)
                restored.channels[c][i] += removed.channels[c][i] + (float) (g3 * removed3.channels[c][i]);
        const double err = worstError (restored, -1.0, uncompressed, settle, [] (int, int) { return 0.0; });
        INFO ("master + both removed parts - uncompressed split: " << err);
        CHECK (err < 1.0e-5);
        CHECK (peakFrom (removed, settle) > 0.01);
        CHECK (peakFrom (heard (mix, L::dynRemoved + 0, idle), settle) == 0.0);   // not compressing: nothing removed
        CHECK (peakFrom (split, settle) > 0.1);
    }
}

TEST_CASE ("Listen, MONO and PHONE switch without clicks", "[plugin][monitoring]")
{
    namespace L = polisher::listen;
    const double sr = 48000.0;
    const auto tone = testsig::sine (sr, 2, 200.0, -6.0, 3.6);
    const Setup busy = [] (auto& p) { set (p, "satDrive", 12.0f); set (p, "drive", 6.0f); set (p, "dynRatio1", 3.0f); set (p, "dynThr1", -30.0f); set (p, "rtAmount", 60.0f); };
    auto master = masterOf (tone, busy);
    auto p = plainProcessor (sr, busy);
    AudioClip out = tone;
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    static constexpr int modes[] = { L::satAdded, L::off, L::maxRemoved, L::dynSolo, L::dynRemoved, L::tamerRemoved, L::clipRemoved, L::satAdded, L::off };
    for (int pos = 0, k = 0; pos + 512 <= tone.numSamples(); pos += 512, ++k)
    {
        const int step = k / 37;   // ~0.4 s each
        p->setListenMode (modes[std::min (step, 8)]);
        p->setMonoCheck (step % 3 == 1);
        p->setPhoneCheck (step % 4 == 2);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, tone.channels[(size_t) c].data() + pos, 512);
        p->processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + 512, out.channels[(size_t) c].begin() + pos);
    }
    const int from = p->getLatencySamples() + 512;
    for (auto* clip : { &out, &master })
        for (auto& ch : clip->channels) ch.resize ((size_t) (tone.numSamples() / 512 * 512));   // the blocks rendered
    const double steady = maxStepOf (master, from), worst = maxStepOf (out, from);
    size_t at = 0;
    for (size_t i = (size_t) from; i < out.channels[0].size(); ++i)
        for (const auto& ch : out.channels)
            if (std::abs ((double) ch[i] - ch[i - 1]) >= worst) at = i;
    INFO ("largest step while switching " << worst << " at " << at / sr << " s (step " << at / 512 / 37 << "), the master's own " << steady);
    CHECK (worst < 1.5 * steady);
    CHECK (peakFrom (out, 0) <= 1.0);
}

TEST_CASE ("An offline export never contains Listen, MONO or PHONE; closing the window puts the master back", "[plugin][monitoring]")
{
    namespace L = polisher::listen;
    const double sr = 44100.0;
    const auto mix = testsig::musicLike (sr, 4.0, -6.0);
    auto reference = makeProcessor (sr, 512, true);
    const auto master = renderFixed (*reference, mix, 512);

    // Everything on, then the host renders offline: the export is the master, bit for bit.
    for (int mode : { (int) L::satAdded, (int) L::tamerRemoved, (int) L::dynSolo + 1, (int) L::maxRemoved })
    {
        auto p = makeProcessor (sr, 512, true);
        p->setListenMode (mode);
        p->setMonoCheck (true);
        p->setPhoneCheck (true);
        p->setNonRealtime (true);
        const auto exported = renderFixed (*p, mix, 512);
        INFO ("listen mode " << mode);
        CHECK (maxAbsDiff (exported, master) == 0.0);
    }
    // The same live is what you hear, not the master.
    auto live = makeProcessor (sr, 512, true);
    live->setListenMode (L::satAdded);
    CHECK (maxAbsDiff (renderFixed (*live, mix, 512), master) > 0.01);

    // Listening live, then the export starts mid-song: from that block on it is the master.
    auto p = makeProcessor (sr, 512, true);
    p->setListenMode (L::maxRemoved);
    p->setPhoneCheck (true);
    AudioClip out = mix;
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    const int switchAt = 512 * 100;
    for (int pos = 0; pos + 512 <= mix.numSamples(); pos += 512)
    {
        if (pos == switchAt) p->setNonRealtime (true);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, mix.channels[(size_t) c].data() + pos, 512);
        p->processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + 512, out.channels[(size_t) c].begin() + pos);
    }
    double before = 0.0, after = 0.0;
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < (size_t) (mix.numSamples() / 512 * 512); ++i)
        {
            const double d = std::abs ((double) out.channels[c][i] - master.channels[c][i]);
            (i < (size_t) switchAt ? before : after) = std::max (i < (size_t) switchAt ? before : after, d);
        }
    CHECK (before > 0.01);
    CHECK (after == 0.0);

    // The window closing (resetMonitoring) fades back to the master and forgets every check.
    auto q = makeProcessor (sr, 512, true);
    q->setListenMode (L::dynSolo);
    q->setMonoCheck (true);
    renderFixed (*q, testsig::musicLike (sr, 1.0, -6.0, 3), 512);
    q->resetMonitoring();
    CHECK (q->getListenMode() == L::off);
    CHECK_FALSE (q->getMonoCheck());
    CHECK_FALSE (q->getPhoneCheck());
    auto r = makeProcessor (sr, 512, true);
    renderFixed (*r, testsig::musicLike (sr, 1.0, -6.0, 3), 512);
    const auto qOut = renderFixed (*q, mix, 512), rOut = renderFixed (*r, mix, 512);
    CHECK (maxAbsDiff (qOut, rOut, (int) (0.05 * sr)) == 0.0);   // the master after a 15 ms fade
    CHECK (maxAbsDiff (qOut, rOut) > 0.0);                       // (it did fade, rather than jump)
}

TEST_CASE ("MONO plays (L + R) / 2 of the master; PHONE is mono; MATCH stays fair", "[plugin][monitoring]")
{
    const double sr = 48000.0;
    const auto mix = testsig::musicLike (sr, 4.0, -8.0, 9);
    auto ref = makeProcessor (sr, 512, true);
    set (*ref, "levelMatch", 1.0f);
    const auto master = renderFixed (*ref, mix, 512);

    auto pm = makeProcessor (sr, 512, true);
    set (*pm, "levelMatch", 1.0f);
    pm->setMonoCheck (true);
    const auto mono = renderFixed (*pm, mix, 512);
    double err = 0.0;
    for (size_t i = (size_t) (0.05 * sr); i < master.channels[0].size(); ++i)
    {
        const double expect = 0.5 * ((double) master.channels[0][i] + master.channels[1][i]);
        err = std::max ({ err, std::abs (mono.channels[0][i] - expect), std::abs (mono.channels[1][i] - expect) });
    }
    CHECK (err < 1.0e-6);
    // MATCH learns from the meters before any check, so it turns the master down the same way.
    CHECK (pm->meters.matchDb.load() == ref->meters.matchDb.load());

    auto pp = makeProcessor (sr, 512, true);
    pp->setPhoneCheck (true);
    const auto phone = renderFixed (*pp, mix, 512);
    double lr = 0.0;
    for (size_t i = 0; i < phone.channels[0].size(); ++i) lr = std::max (lr, std::abs ((double) phone.channels[0][i] - phone.channels[1][i]));
    CHECK (lr == 0.0);
    CHECK (peakFrom (phone, 0) <= 1.0);
}

TEST_CASE ("Listening never allocates on the audio thread", "[plugin][monitoring]")
{
    namespace L = polisher::listen;
    const double sr = 48000.0;
    auto p = makeProcessor (sr, 1024, true);
    const auto mix = testsig::musicLike (sr, 4.0, -6.0);
    juce::AudioBuffer<float> buf (2, 1024);
    juce::MidiBuffer midi;
    int worst = 0;
    for (int pos = 0, k = 0; pos + 1024 <= mix.numSamples(); pos += 1024, ++k)
    {
        p->setListenMode ((k / 6) % L::count);
        p->setMonoCheck (k % 5 < 2);
        p->setPhoneCheck (k % 7 < 3);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, mix.channels[(size_t) c].data() + pos, 1024);
        allocations = 0;
        countAllocations = true;
        p->processBlock (buf, midi);
        countAllocations = false;
        worst = std::max (worst, allocations.load());
    }
    CHECK (worst == 0);
}

// --- Auto-Master's whole-song listen and Tone Steady's stored target ---------------------------------

namespace {

/** The DAW's playhead: playing, at `seconds` on the timeline (moved along by play()). */
struct FakePlayHead final : juce::AudioPlayHead
{
    double seconds = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setTimeInSeconds (seconds);
        return info;
    }
};

/** Plays a clip through the processor with the playhead moving along, ticking Auto-Master after every
    block as its timer would. Returns the most operator-new calls any one block made. */
int play (PolisherAudioProcessor& p, FakePlayHead& head, const AudioClip& clip, int block = 512)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    int worst = 0;
    for (int pos = 0; pos < clip.numSamples(); pos += block)
    {
        const int n = std::min (block, clip.numSamples() - pos);
        buf.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, clip.channels[(size_t) c].data() + pos, n);
        allocations = 0;
        countAllocations = true;
        p.processBlock (buf, midi);
        countAllocations = false;
        worst = std::max (worst, allocations.load());
        head.seconds += n / clip.sampleRate;
        p.autoMaster.tick();
    }
    return worst;
}

/** Waits (ticking, like the timer) until Auto-Master's analysis is applied. */
bool waitForAutoMaster (PolisherAudioProcessor& p)
{
    for (int i = 0; i < 15000; ++i)   // up to 5 minutes
    {
        p.autoMaster.tick();
        if (p.autoMaster.getState() == AutoMasterController::idle)
            return true;
        juce::Thread::sleep (20);
    }
    return false;
}

AudioClip songOf (std::initializer_list<AudioClip> parts)
{
    AudioClip song;
    for (const auto& part : parts)
    {
        if (song.channels.empty()) { song = part; continue; }
        for (size_t c = 0; c < 2; ++c)
            song.channels[c].insert (song.channels[c].end(), part.channels[c].begin(), part.channels[c].end());
    }
    return song;
}

juce::var reportOf (PolisherAudioProcessor& p) { return juce::JSON::parse (p.autoMaster.getReportJson()); }

} // namespace

TEST_CASE ("Auto-Master listens to the whole song, finishes by itself when it ends, and decides from the loudest part", "[plugin][automaster]")
{
    const double sr = 48000.0;
    auto p = makeProcessor (sr, 512, false);
    FakePlayHead head;
    head.seconds = 5.0;                       // playback started at 0:05
    p->setPlayHead (&head);
    REQUIRE (p->autoMaster.start());
    const auto chorus = testsig::musicLike (sr, 14.0, -12.0, 3);

    play (*p, head, testsig::musicLike (sr, 10.0, -30.0, 1));   // intro
    play (*p, head, testsig::musicLike (sr, 20.0, -20.0, 2));   // verse
    CHECK (p->autoMaster.getState() == AutoMasterController::listening);   // past 12 s: still listening
    CHECK (p->autoMaster.canFinish());
    const auto memory = p->autoMaster.getListenMemoryBytes();
    CHECK (memory > 0);
    CHECK (memory < 16u * 1024u * 1024u);
    play (*p, head, chorus);
    play (*p, head, testsig::musicLike (sr, 10.0, -20.0, 4));   // verse
    CHECK (p->autoMaster.getState() == AutoMasterController::listening);
    CHECK (p->autoMaster.getListenMemoryBytes() == memory);    // fixed, however long the song
    CHECK (std::abs (p->autoMaster.getHeardSeconds() - 54.0) < 0.05);
    const double loudAt = p->autoMaster.getLoudestStartSeconds();
    INFO ("loudest part found at " << loudAt << " s on the timeline");
    CHECK (loudAt >= 5.0 + 30.0 - 0.05);                        // the chorus: 0:35 - 0:49
    CHECK (loudAt <= 5.0 + 32.0 + 0.05);

    // The song ends: a few seconds of silence while the transport keeps running.
    play (*p, head, testsig::silence (sr, 2, 4.5));
    CHECK (p->autoMaster.getState() == AutoMasterController::analysing);
    REQUIRE (waitForAutoMaster (*p));
    CHECK (p->autoMaster.finishedByItself());

    const auto r = reportOf (*p);
    REQUIRE (static_cast<bool> (r["ok"]));
    const auto listen = r["listen"];
    CHECK (std::abs ((double) listen["heard"] - 54.0) < 0.05);
    CHECK (static_cast<bool> (listen["fromHost"]));
    CHECK (static_cast<bool> (listen["byItself"]));
    CHECK (std::abs ((double) listen["loudStart"] - loudAt) < 1.0e-6);
    CHECK (std::abs ((double) listen["loudEnd"] - loudAt - 12.0) < 1.0e-6);
    CHECK ((double) listen["songLufs"] < (double) listen["loudLufs"] - 2.0);   // the whole song is quieter than its chorus
    const auto text = r["listened"].toString();
    INFO (text);
    const auto start = (int) std::lround (loudAt);
    CHECK (text == "Listened to 0:54 of your song; the loudest part (0:" + juce::String (start) + juce::String::fromUTF8 ("\xE2\x80\x93") + "0:"
                   + juce::String (start + 12) + ", usually the hook or drop) set the levels.");
    // It decided from the chorus: what it heard measures like the chorus.
    CHECK (std::abs ((double) r["capturedLufs"] - polisher::measure (chorus).integratedLufs) < 0.5);

    // Tone Steady got the song's own tone (learned from all 54 s), and Undo puts the old reference back.
    REQUIRE (p->toneTarget.isSet());
    CHECK (std::abs (p->toneTarget.get().seconds - 54.0) < 0.05);
    CHECK (std::abs ((double) r["toneTarget"] - 54.0) < 0.05);
    REQUIRE (p->autoMaster.undo());
    CHECK_FALSE (p->toneTarget.isSet());
}

TEST_CASE ("Auto-Master: Finish uses what it heard from 12 s of music on, not before; a pause doesn't end it", "[plugin][automaster]")
{
    const double sr = 44100.0;
    auto p = makeProcessor (sr, 512, false);
    FakePlayHead head;
    p->setPlayHead (&head);
    REQUIRE (p->autoMaster.start());
    play (*p, head, testsig::musicLike (sr, 8.0, -14.0, 5));
    CHECK_FALSE (p->autoMaster.canFinish());
    CHECK_FALSE (p->autoMaster.finish());
    CHECK (p->autoMaster.getState() == AutoMasterController::listening);
    CHECK (std::abs (p->autoMaster.getProgress() - 8.0f / 12.0f) < 0.01f);

    // Playback stopped for a while (no audio at all): under 30 s of music, it keeps waiting.
    juce::Thread::sleep (4200);
    p->autoMaster.tick();
    CHECK (p->autoMaster.getState() == AutoMasterController::listening);
    CHECK_FALSE (p->autoMaster.isHearingAudio());

    play (*p, head, testsig::musicLike (sr, 5.0, -14.0, 6));
    REQUIRE (p->autoMaster.canFinish());
    REQUIRE (p->autoMaster.finish());
    CHECK (p->autoMaster.getState() == AutoMasterController::analysing);
    REQUIRE (waitForAutoMaster (*p));
    CHECK_FALSE (p->autoMaster.finishedByItself());
    const auto r = reportOf (*p);
    REQUIRE (static_cast<bool> (r["ok"]));
    CHECK (std::abs ((double) r["listen"]["heard"] - 13.0) < 0.05);
    CHECK_FALSE (static_cast<bool> (r["listen"]["byItself"]));
    CHECK (r["listened"].toString() == "Listened to 0:13 of your song and set the levels from it.");
}

TEST_CASE ("Auto-Master's listening never allocates on the audio thread", "[plugin][automaster]")
{
    const double sr = 48000.0;
    auto p = makeProcessor (sr, 1024, true);
    renderFixed (*p, testsig::musicLike (sr, 1.0, -6.0), 1024);   // warm up
    FakePlayHead head;
    p->setPlayHead (&head);
    REQUIRE (p->autoMaster.start());
    // Getting louder all the time, so the loudest part moves on with every 100 ms once 12 s are in.
    auto mix = testsig::musicLike (sr, 16.0, -8.0, 7);
    for (auto& ch : mix.channels)
        for (size_t i = 0; i < ch.size(); ++i) ch[i] *= (float) (0.3 + 0.7 * (double) i / (double) ch.size());
    const int worst = play (*p, head, mix, 1024);
    CHECK (worst == 0);
    CHECK (p->autoMaster.getState() == AutoMasterController::listening);
    CHECK (p->autoMaster.getLoudestStartSeconds() > 3.5);           // the newest (loudest) 12 s
    p->autoMaster.cancel();
}

TEST_CASE ("Tone Steady: Learn listens to the song for its tone only; the target is saved with the song", "[plugin][automaster]")
{
    const double sr = 48000.0;
    auto setUp = [] (PolisherAudioProcessor& p) { set (p, "tsAmount", 60.0f); set (p, "tsRange", 4.0f); };
    auto song = testsig::trapLike (sr, 34.0);

    // Learn: a tone-only listen. The song ends (no audio arrives for a few seconds) and it's done,
    // with nothing else changed.
    auto a = makeProcessor (sr, 512, false);
    setUp (*a);
    FakePlayHead head;
    a->setPlayHead (&head);
    const int reportBefore = a->autoMaster.getReportVersion();
    REQUIRE (a->autoMaster.start (AutoMasterController::toneOnly));
    play (*a, head, song);
    CHECK (a->autoMaster.getState() == AutoMasterController::listening);
    juce::Thread::sleep (4200);
    a->autoMaster.tick();
    CHECK (a->autoMaster.getState() == AutoMasterController::idle);
    REQUIRE (a->toneTarget.isSet());
    const auto learned = a->toneTarget.get();
    CHECK (std::abs (learned.seconds - 34.0) < 0.05);
    CHECK (a->autoMaster.getReportVersion() == reportBefore);
    CHECK (a->parameters.getRawParameterValue ("tsAmount")->load() == 60.0f);
    CHECK (a->parameters.getRawParameterValue ("drive")->load() == 0.0f);

    // Saved with the song, back exactly as it was.
    juce::MemoryBlock state;
    a->getStateInformation (state);
    auto b = makeProcessor (sr, 512, false);
    b->setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (b->toneTarget.isSet());
    CHECK (b->toneTarget.get().bandDb == learned.bandDb);
    CHECK (std::abs (b->toneTarget.get().seconds - learned.seconds) < 1.0e-9);

    // The restored song plays exactly like one given the same target, and not like following the music.
    auto direct = makeProcessor (sr, 512, false);
    setUp (*direct);
    direct->toneTarget.set (learned);
    auto follow = makeProcessor (sr, 512, false);
    setUp (*follow);
    const auto clip = testsig::trapLike (sr, 8.0, 3);
    const auto restoredOut = renderFixed (*b, clip, 512);
    CHECK (maxAbsDiff (restoredOut, renderFixed (*direct, clip, 512)) == 0.0);
    CHECK (maxAbsDiff (restoredOut, renderFixed (*follow, clip, 512)) > 1.0e-4);

    // A song saved before stored targets existed follows the music, exactly as it did.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    REQUIRE (xml != nullptr);
    CHECK (xml->hasAttribute ("toneTarget"));
    xml->removeAttribute ("toneTarget");
    juce::MemoryBlock older;
    juce::AudioProcessor::copyXmlToBinary (*xml, older);
    auto old = makeProcessor (sr, 512, false);
    old->toneTarget.set (learned);                        // whatever it had is replaced by the song's state
    old->setStateInformation (older.getData(), (int) older.getSize());
    CHECK_FALSE (old->toneTarget.isSet());
    auto fresh = makeProcessor (sr, 512, false);
    setUp (*fresh);
    CHECK (maxAbsDiff (renderFixed (*old, clip, 512), renderFixed (*fresh, clip, 512)) == 0.0);

    // A damaged or future-format target is ignored rather than half-read.
    xml->setAttribute ("toneTarget", "{\"format\": 9, \"bands\": [1, 2]}");
    juce::MemoryBlock odd;
    juce::AudioProcessor::copyXmlToBinary (*xml, odd);
    b->setStateInformation (odd.getData(), (int) odd.getSize());
    CHECK_FALSE (b->toneTarget.isSet());

    // Learn on too little music at the song's level says so and keeps what it had.
    b->toneTarget.set (learned);
    REQUIRE (b->autoMaster.start (AutoMasterController::toneOnly));
    FakePlayHead head2;
    b->setPlayHead (&head2);
    play (*b, head2, testsig::musicLike (sr, 6.0, -14.0, 2));
    CHECK_FALSE (b->autoMaster.finish());                  // under 12 s: keeps listening
    b->autoMaster.cancel();
    CHECK (b->toneTarget.get().bandDb == learned.bandDb);
}

// JUCE (its message manager, for the parameter tree's timers) lives exactly as long as the test run:
// a function-local static would be torn down after JUCE's own shutdown objects and crash on exit.
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    return Catch::Session().run (argc, argv);
}

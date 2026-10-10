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

// JUCE (its message manager, for the parameter tree's timers) lives exactly as long as the test run:
// a function-local static would be torn down after JUCE's own shutdown objects and crash on exit.
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    return Catch::Session().run (argc, argv);
}

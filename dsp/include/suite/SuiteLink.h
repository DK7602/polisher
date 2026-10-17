#pragma once

// Suite link: Voxology, Mixology and Masterology tell each other what they do.
//
// THIS FILE AND SuiteLink.cpp ARE THE SAME IN ALL THREE REPOSITORIES (Voxology, Mixology, Masterology).
// Change them in one, copy them to the other two, and bump kLayout when the memory layout changes (an older and a
// newer build then never read each other's memory: kLayout is part of the block's name).
//
// The plug-ins are separate libraries, so they meet in a named block of shared memory, one per host process:
//   - vocals:  each Voxology publishes the track it sits on and what it does there (compressing, de-essing, its
//              reverb / delay, its style), so Mixology never stacks a second compressor or reverb on that vocal.
//              Mixology answers each one: whether it found that track and treats it as a vocal (and if not, why),
//              and what it hears there, so Voxology only says "MIXOLOGY ✓" when it is true.
//   - mix:     Mixology on the Stereo Out publishes the hand-off for mastering (the mix style, peaks, loudness,
//              its tone against the style, a mono mix), so Masterology can start from the right place.
//   - masters: each Masterology publishes its goal (loudness target, true-peak ceiling, genre), so Mixology's
//              hand-off check uses Masterology's real numbers.
//   - beat key: the song's key as a Voxology on the beat hears it, for Honey Tune (another plug-in file).
//   - choices: the song's style (Mixology's list of 75, "genre.style") and the DAW you work in ("cubase", "studioone"...),
//              the same in all three: pick one in any of them and the others follow. The newest choice wins (each saves
//              the time you picked it with the project).
// A plug-in takes part only while the host runs it, from prepareToPlay to releaseResources (Presence), so projects that
// are open in the same host but not active don't reach each other.
// Framework-free C++17, lock-free (atomics and sequence counters), no allocation on the audio thread (the
// publishers call it from their message thread anyway). Zero-filled memory is a valid, empty block.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace suite
{
    constexpr std::uint32_t kLayout = 4;   // 2: the shared song style; 3: the shared DAW, Voxology's recording warnings;
                                           // 4: Mixology answers each vocal
    constexpr int kMaxVocals = 64;
    constexpr int kMaxMasters = 4;
    constexpr std::size_t kTextBytes = 64;
    constexpr std::uint64_t kAliveMs = 2000;   // a publisher silent this long is gone

    /** Track names are matched by this hash (FNV-1a over the UTF-8 bytes, 28 bits; Mixology's Cubase bridge uses the same). */
    std::uint32_t nameHash (const std::string& utf8);
    /** Milliseconds on a clock all libraries in the process share (steady; never 0). */
    std::uint64_t nowMs();
    /** The wall clock in milliseconds since 1970: when you picked a style (kept with the project, so it outlives
        the process; nowMs() doesn't). */
    std::uint64_t wallMs();
    /** major.minor.patch packed as major << 16 | minor << 8 | patch. */
    constexpr std::uint32_t packVersion (int major, int minor, int patch)
    {
        return (static_cast<std::uint32_t> (major) << 16) | (static_cast<std::uint32_t> (minor & 0xFF) << 8) | static_cast<std::uint32_t> (patch & 0xFF);
    }

    // ---- what is published -----------------------------------------------------------------------------

    /** What Voxology does on one vocal track. */
    struct VocalInfo
    {
        enum Flag : std::uint32_t
        {
            processing = 1u << 0,   // the chain is on (not bypassed)
            compressor = 1u << 1,   // its compressor / leveler is working on the vocal
            deEsser    = 1u << 2,
            reverb     = 1u << 3,   // it adds its own reverb
            delay      = 1u << 4,   // it adds its own delay
            doubler    = 1u << 5,
            pitch      = 1u << 6,   // pitch correction
            eq         = 1u << 7,   // its tone EQ shapes the vocal
            saturation = 1u << 8,
            gate       = 1u << 9,
            beat       = 1u << 10,  // Voxology in Beat mode: this track is the beat (Voxology only listens to its key)
            // What Voxology's last Auto-Edit heard in the recording itself (before any plug-in): fixed at the source.
            recordedHot     = 1u << 11,   // peaks over the top of the meter (0 dBFS) coming in
            recordedClipped = 1u << 12,   // flat tops: it clipped while it was recorded
        };
        /** Voxology's Auto-Edit style (its aeStyle choice + 1). */
        enum class Style : std::uint32_t { unknown = 0, trapLead, rap, melodic, robot, rnb, pop, folk, naturalSinger };
        static constexpr std::uint32_t kStyles = 8;

        std::string trackName;        // as the host reported it ("" = unknown)
        std::uint32_t nameHash = 0;   // of trackName (0 = unknown)
        std::uint32_t flags = 0;
        Style style = Style::unknown;
        float compressionDb = 0.0f;   // how much its compressor / leveler takes off lately (dB, >= 0)
        float reverbMix = 0.0f;       // 0..1
        float delayMix = 0.0f;        // 0..1
        float recordedPeakDb = -100.0f;   // the recording's loudest peak coming in (dBFS), from the last Auto-Edit
        std::uint32_t clippedSpots = 0;   // how many clipped spots that Auto-Edit found
        std::uint32_t version = 0;    // packVersion of the Voxology that wrote it (0: readers skip the slot)
        // Filled in by the reader:
        int slot = -1;
        std::uint64_t instanceId = 0;
        std::uint32_t generation = 0;   // which claim of the slot (an answer is only for the claim it was given to)
    };

    /** Mixology's answer to one Voxology. */
    struct VocalAck
    {
        enum class State : std::uint32_t
        {
            none = 0,
            matched,      // Mixology found the track and treats it as a vocal
            noLink,       // no Mixology Link on that track: Mixology can't hear it
            noName,       // the host told Voxology no track name: Mixology can't match it
            sharedName,   // another track has the same name: Mixology can't tell them apart
            notVocal,     // you gave the track another role in Mixology
            beatTrack,    // Voxology is in Beat mode there: Mixology knows it is the beat
        };
        enum Bit : std::uint32_t
        {
            balancesLevel = 1u << 0,   // Mixology moves this track's fader (its DAW bridge reads it): it sets the vocal level
            heard         = 1u << 1,   // Analyze Mix heard this vocal: vocalDb / targetDb are real
        };
        State state = State::none;
        std::uint32_t bits = 0;
        float vocalDb = 0.0f;    // the vocal against the rest of the mix while it sings (dB, after the fader)
        float targetDb = 0.0f;   // where the song's style (and your setting) wants it
    };

    /** Mixology's hand-off to mastering (the Mixology on the Stereo Out). */
    struct MixInfo
    {
        enum Flag : std::uint32_t
        {
            analysed = 1u << 0,   // the tone fields come from a finished Analyze Mix
            monoMix  = 1u << 1,   // left and right are the same at the Stereo Out
            limited  = 1u << 2,   // Mixology's own limiter is on the full mix
        };
        std::string styleKey;         // "hiphop.modern" (genre.style)
        std::string styleName;        // "Hip Hop - Modern Clean"
        std::uint32_t flags = 0;
        float peakDbtp = -100.0f;     // the mix's highest true peak so far
        float integratedLufs = -100.0f;
        float shortTermLufs = -100.0f;
        // The mix against the style's tonal target (dB, + = more than the style), from the last Analyze Mix:
        float lowDb = 0.0f, lowMidDb = 0.0f, presenceDb = 0.0f, airDb = 0.0f;
        std::uint32_t version = 0;    // packVersion of the Mixology that wrote it
        std::uint64_t whenMs = 0;     // when it was written (reader: 0 = never)
    };

    /** What a Masterology is aiming for. */
    struct MasterInfo
    {
        std::string goalName;         // e.g. "Spotify / Apple Music" or "Club (loud)"
        std::string genreName;        // its genre / style, "" = not chosen
        float targetLufs = -14.0f;
        float ceilingDbtp = -1.0f;
        float inputLufs = -100.0f;    // what reaches it (integrated), and its true peak
        float inputPeakDbtp = -100.0f;
        std::uint32_t version = 0;    // packVersion of the Masterology that wrote it (0: readers skip the slot)
        // Filled in by the reader:
        int slot = -1;
        std::uint64_t instanceId = 0;
    };

    /** What the three share and you pick in any of them: the song's style, and the DAW you work in. */
    enum class Choice : std::uint32_t { style = 0, daw = 1 };
    constexpr int kChoices = 2;

    /** A shared choice (the song's style, or the DAW), shared by Voxology, Mixology and Masterology. */
    struct StyleInfo
    {
        enum class From : std::uint32_t { none = 0, voxology, mixology, masterology };
        std::string key;              // "hiphop.modern" / "cubase" ("" = none)
        std::string name;             // "Hip Hop - Modern Clean" / "Cubase"
        std::uint64_t pickedAt = 0;   // when you picked it (wallMs; 0 = never picked: follows the others)
        From from = From::none;       // the plug-in you picked it in
        bool operator== (const StyleInfo& o) const { return key == o.key && pickedAt == o.pickedAt; }
        bool operator!= (const StyleInfo& o) const { return ! (*this == o); }
    };

    // ---- the shared memory ------------------------------------------------------------------------------

    template <std::size_t N> using Text = std::array<std::atomic<char>, N>;

    struct alignas (64) VocalSlot
    {
        std::atomic<std::uint32_t> state;        // 0 free, 1 claimed
        std::atomic<std::uint32_t> generation;
        std::atomic<std::uint64_t> instanceId;
        std::atomic<std::uint64_t> heartbeatMs;
        std::atomic<std::uint32_t> seq;          // odd while the fields below are written
        std::atomic<std::uint32_t> nameHash, flags, style, version;
        std::atomic<float> compressionDb, reverbMix, delayMix, recordedPeakDb;
        std::atomic<std::uint32_t> clippedSpots;
        Text<128> trackName;
        // Mixology's answer (written by Mixology, read by the Voxology that owns the slot):
        std::atomic<std::uint32_t> ackSeq;
        std::atomic<std::uint32_t> ackGeneration, ackHash, ackState, ackBits;
        std::atomic<float> ackVocalDb, ackTargetDb;
        std::atomic<std::uint64_t> ackMs;
    };

    struct alignas (64) MixSlot
    {
        std::atomic<std::uint32_t> seq;
        std::atomic<std::uint64_t> whenMs;
        std::atomic<std::uint32_t> flags, version;
        std::atomic<float> peakDbtp, integratedLufs, shortTermLufs, lowDb, lowMidDb, presenceDb, airDb;
        Text<kTextBytes> styleKey, styleName;
    };

    struct alignas (64) MasterSlot
    {
        std::atomic<std::uint32_t> state;
        std::atomic<std::uint32_t> generation;
        std::atomic<std::uint64_t> instanceId;
        std::atomic<std::uint64_t> heartbeatMs;
        std::atomic<std::uint32_t> seq;
        std::atomic<std::uint32_t> version;
        std::atomic<float> targetLufs, ceilingDbtp, inputLufs, inputPeakDbtp;
        Text<kTextBytes> goalName, genreName;
    };

    struct alignas (64) ChoiceSlot
    {
        std::atomic<std::uint32_t> seq;          // odd while written; writers take it with a compare-exchange
        std::atomic<std::uint64_t> heartbeatMs;  // refreshed by the plug-ins that hold this choice (0 = none)
        std::atomic<std::uint64_t> holder;       // the instance that refreshed it last
        std::atomic<std::uint64_t> pickedAt;
        std::atomic<std::uint32_t> from;
        Text<kTextBytes> key, name;
    };

    struct alignas (64) KeySlot
    {
        std::atomic<std::uint32_t> seq;          // odd while written; writers take it with a compare-exchange
        std::atomic<std::uint64_t> whenMs;       // 0 = never written
        std::atomic<std::int32_t> key, scale;
        std::atomic<float> confidence;
    };

    struct Block
    {
        std::atomic<std::uint32_t> layout;
        MixSlot mix;
        std::array<ChoiceSlot, kChoices> choices;   // [Choice::style], [Choice::daw]
        KeySlot beatKey;
        std::array<VocalSlot, kMaxVocals> vocals;
        std::array<MasterSlot, kMaxMasters> masters;
    };

    /** The suite's block. Construct over memory that holds sizeof (Block) bytes (zero-filled or already in use),
        or use Link::process() for this host process's block. Readers and writers may be in different libraries. */
    class Link
    {
    public:
        explicit Link (void* memory);
        static constexpr std::size_t bytes() { return sizeof (Block); }

        /** This library's handle on the host process's block (opened on first use). */
        static Link& process();
        /** False when the OS refused shared memory (the plug-ins then can't see each other). */
        static bool processShared();

        // ---- Voxology --------------------------------------------------------------------------------
        int claimVocal (std::uint64_t instanceId, std::uint64_t nowMs);   // -1 when full
        void releaseVocal (int slot, std::uint64_t instanceId);
        /** Message thread, a few times a second (also the heartbeat). False when the slot is no longer this
            instance's (it was silent longer than kAliveMs and another took it over): claim again. */
        bool publishVocal (int slot, std::uint64_t instanceId, const VocalInfo&, std::uint64_t nowMs);

        /** Mixology's answer for this instance's slot and track (nameHash as published), if a Mixology gave one within
            kAliveMs for this claim of the slot and this name. */
        bool vocalAck (int slot, std::uint64_t instanceId, std::uint32_t nameHash, VocalAck&, std::uint64_t nowMs) const;
        /** The song's key as a Voxology on the beat hears it (key 0 = C .. 11, Voxology's scale index, 0..1 sure), with
            the time it was heard; Honey Tune reads it (and judges how old is too old). */
        void publishBeatKey (int key, int scale, float confidence, std::uint64_t whenMs);
        bool readBeatKey (int& key, int& scale, float& confidence, std::uint64_t& whenMs) const;   // false: never written

        // ---- Mixology ---------------------------------------------------------------------------------
        /** Every live Voxology. */
        std::vector<VocalInfo> vocals (std::uint64_t nowMs) const;
        /** Mixology answers one vocal (as vocals() gave it), a few times a second. Nothing happens when the slot was
            claimed again since. */
        void ackVocal (const VocalInfo&, const VocalAck&, std::uint64_t nowMs);
        void publishMix (const MixInfo&, std::uint64_t nowMs);

        // ---- Masterology ------------------------------------------------------------------------------
        int claimMaster (std::uint64_t instanceId, std::uint64_t nowMs);
        void releaseMaster (int slot, std::uint64_t instanceId);
        bool publishMaster (int slot, std::uint64_t instanceId, const MasterInfo&, std::uint64_t nowMs);   // as publishVocal
        /** Mixology's hand-off, if a Mixology wrote one within kAliveMs. */
        bool readMix (MixInfo&, std::uint64_t nowMs) const;

        // ---- Mixology reads the masters -----------------------------------------------------------------
        std::vector<MasterInfo> masters (std::uint64_t nowMs) const;

        // ---- all three: the shared choices (the song's style, the DAW) ------------------------------------
        /** Offer this instance's choice, a few times a second (message thread). It goes in when it is newer than the
            one there, or when no live plug-in holds the one there; holding the same one refreshes it. Returns the
            choice everyone should use now (yours, or a newer one: adopt it). Never-picked choices (pickedAt 0) are
            never offered: they just get the shared one, if any (else they come back as they are). */
        StyleInfo shareChoice (Choice, const StyleInfo& mine, std::uint64_t instanceId, std::uint64_t nowMs);
        /** The shared choice, if a live plug-in holds one. */
        bool readChoice (Choice, StyleInfo&, std::uint64_t nowMs) const;
        /** Closing: if this instance refreshed the shared choice last, it lapses straight away (so a project opened
            next in the same host doesn't take the closed project's choice). */
        void leaveChoice (Choice, std::uint64_t instanceId);

        StyleInfo shareStyle (const StyleInfo& mine, std::uint64_t id, std::uint64_t now) { return shareChoice (Choice::style, mine, id, now); }
        bool readStyle (StyleInfo& out, std::uint64_t now) const { return readChoice (Choice::style, out, now); }
        void leaveStyle (std::uint64_t id) { leaveChoice (Choice::style, id); }

    private:
        Block& block;
    };

    /** One plug-in's side of a shared choice (message thread): what it saves with the project and what it uses.
        SongStyle (the default) is the song's style; SharedChoice (from, Choice::daw) the DAW. */
    class SharedChoice
    {
    public:
        explicit SharedChoice (StyleInfo::From plugIn, Choice c = Choice::style) : who (plugIn), choice (c) {}

        /** You picked a style in this plug-in: the others follow at their next sync. */
        void pick (const std::string& key, const std::string& name)
        {
            const auto after = mine.pickedAt + 1;   // always newer than the style it replaces (clocks can step back)
            mine.key = key;
            mine.name = name;
            mine.pickedAt = std::max (wallMs(), after);
            mine.from = who;
        }
        /** The style this plug-in had before the suite style existed (pickedAt 0: the others' choice wins). */
        void setUnpicked (const std::string& key, const std::string& name) { mine = StyleInfo {}; mine.key = key; mine.name = name; }

        /** A few times a second. True when another plug-in's newer choice replaced this one's: use style() now. */
        bool sync (Link& link, std::uint64_t instanceId, std::uint64_t nowMs)
        {
            const auto shared = link.shareChoice (choice, mine, instanceId, nowMs);
            if (shared.key.empty() || (shared == mine && shared.name == mine.name)) return false;
            mine = shared;
            return true;
        }
        void leave (Link& link, std::uint64_t instanceId) { link.leaveChoice (choice, instanceId); }

        const StyleInfo& style() const { return mine; }

        /** Saved with the project: "key|name|pickedAt|from". */
        std::string save() const
        {
            return mine.key + "|" + mine.name + "|" + std::to_string (mine.pickedAt) + "|" + std::to_string (static_cast<std::uint32_t> (mine.from));
        }
        /** False (nothing changed) when the text isn't a saved style (an older project). */
        bool restore (const std::string& text)
        {
            std::vector<std::string> f;
            std::size_t start = 0;
            for (std::size_t i = 0; i <= text.size(); ++i)
                if (i == text.size() || text[i] == '|') { f.push_back (text.substr (start, i - start)); start = i + 1; }
            if (f.size() != 4 || f[0].empty()) return false;
            StyleInfo s;
            s.key = f[0];
            s.name = f[1];
            for (char c : f[2]) { if (c < '0' || c > '9') return false; s.pickedAt = s.pickedAt * 10 + static_cast<std::uint64_t> (c - '0'); }
            s.from = static_cast<StyleInfo::From> (f[3] == "1" ? 1 : f[3] == "2" ? 2 : f[3] == "3" ? 3 : 0);
            mine = s;
            return true;
        }

    private:
        StyleInfo::From who;
        Choice choice;
        StyleInfo mine;
    };
    using SongStyle = SharedChoice;

    /** Whether a plug-in takes part in the link: only while the host runs it, from prepareToPlay to releaseResources.
        The block is one per host process, and hosts keep projects that aren't active loaded (Cubase, Logic, Studio One;
        REAPER's background tabs) with their plug-ins' timers running: one song's style must not reach another, and a
        plug-in that isn't playing must not look present. While away a plug-in neither publishes nor reads anything, and
        neither offers nor follows the shared choices. The same in all three plug-ins:
          prepareToPlay:     presence.start();     releaseResources: presence.stop();     (any thread)
          the timer (message thread), before anything else with the link:
            switch (presence.update (nowMs)) { leave: release slots, leave() the choices, forget what was read; away: stop;
                                               join / here: go on, and sync the choices only once settled (nowMs). } */
    class Presence
    {
    public:
        enum class Step { away, leave, join, here };

        void start() noexcept { running.store (true, std::memory_order_release); }
        void stop() noexcept  { running.store (false, std::memory_order_release); }

        /** Message thread, from the plug-in's timer. leave / join once, when the host stopped / started the plug-in. */
        Step update (std::uint64_t nowMs) noexcept
        {
            const bool run = running.load (std::memory_order_acquire);
            if (run == taking) return run ? Step::here : Step::away;
            taking = run;
            joinedMs = nowMs;
            return run ? Step::join : Step::leave;
        }
        bool takingPart() const noexcept { return taking; }
        /** Taking part for kSettleMs: sync the shared choices from now on. A project just switched to waits a moment,
            so the plug-ins of the one switched from have stopped (and left the choices) first. */
        bool settled (std::uint64_t nowMs) const noexcept { return taking && nowMs - std::min (nowMs, joinedMs) >= kSettleMs; }

        static constexpr std::uint64_t kSettleMs = 500;

    private:
        std::atomic<bool> running { false };
        bool taking = false;          // message thread
        std::uint64_t joinedMs = 0;   // message thread
    };

    // ---- the DAW (keys and names: plugin/ui/daws.js has the same list) ---------------------------------------
    /** The DAW a host program is ("cubase", "studioone", "fl", "ableton", "reaper", "bitwig", "logic", "cakewalk", "other"),
        from its file name or path (JUCE's PluginHostType::getHostPath()), or from a host's name (JUCE's
        PluginHostType().getHostDescription(): "Fender Studio Pro", "Apple Logic"...). Nuendo counts as Cubase, and Fender
        Studio Pro (Studio One's successor) as Studio One. */
    inline std::string dawFromHostPath (const std::string& path)
    {
        std::string p;
        for (char c : path) p.push_back (static_cast<char> (c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
        const auto slash = p.find_last_of ("/\\");
        const auto file = slash == std::string::npos ? p : p.substr (slash + 1);
        auto has = [&] (const char* w) { return p.find (w) != std::string::npos; };
        auto fileHas = [&] (const char* w) { return file.find (w) != std::string::npos; };
        if (has ("cubase") || has ("nuendo")) return "cubase";
        if (has ("studio one") || has ("studioone")) return "studioone";
        if (has ("fl studio") || file.rfind ("fl64", 0) == 0 || file == "fl.exe" || file.rfind ("fl ", 0) == 0
            || fileHas ("ilbridge")) return "fl";   // FL Studio, and its plug-in bridge
        if (fileHas ("studio pro") || fileHas ("studiopro")) return "studioone";   // Fender Studio Pro (the file name only:
                                                                                  // "FL Studio Producer Edition" is FL)
        if (has ("ableton")) return "ableton";
        if (has ("reaper")) return "reaper";
        if (has ("bitwig")) return "bitwig";
        if (has ("logic")) return "logic";
        if (has ("cakewalk") || file.rfind ("sonar", 0) == 0) return "cakewalk";   // Cakewalk by BandLab, Sonar
        return "other";
    }

    /** The DAW from the host program's path, else from the host's name as JUCE's PluginHostType().getHostDescription() gives
        it: JUCE also knows Logic running plug-ins out of process (AUHostingService, whose path says nothing) and Fender
        Studio Pro. "other" only when neither tells. */
    inline std::string dawFromHost (const std::string& path, const std::string& hostName)
    {
        const auto fromPath = dawFromHostPath (path);
        return fromPath != "other" ? fromPath : dawFromHostPath (hostName);
    }

    inline const char* dawName (const std::string& key)
    {
        if (key == "cubase") return "Cubase";
        if (key == "studioone") return "Studio One";
        if (key == "fl") return "FL Studio";
        if (key == "ableton") return "Ableton Live";
        if (key == "reaper") return "REAPER";
        if (key == "bitwig") return "Bitwig Studio";
        if (key == "logic") return "Logic Pro";
        if (key == "cakewalk") return "Cakewalk";
        return "Other DAW";
    }
}

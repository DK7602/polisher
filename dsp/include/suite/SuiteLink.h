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
//   - mix:     Mixology on the Stereo Out publishes the hand-off for mastering (the mix style, peaks, loudness,
//              its tone against the style, a mono mix), so Masterology can start from the right place.
//   - masters: each Masterology publishes its goal (loudness target, true-peak ceiling, genre), so Mixology's
//              hand-off check uses Masterology's real numbers.
// Framework-free C++17, lock-free (atomics and sequence counters), no allocation on the audio thread (the
// publishers call it from their message thread anyway). Zero-filled memory is a valid, empty block.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace suite
{
    constexpr std::uint32_t kLayout = 1;
    constexpr int kMaxVocals = 64;
    constexpr int kMaxMasters = 4;
    constexpr std::size_t kTextBytes = 64;
    constexpr std::uint64_t kAliveMs = 2000;   // a publisher silent this long is gone

    /** Track names are matched by this hash (FNV-1a over the UTF-8 bytes, 28 bits; Mixology's Cubase bridge uses the same). */
    std::uint32_t nameHash (const std::string& utf8);
    /** Milliseconds on a clock all libraries in the process share (steady; never 0). */
    std::uint64_t nowMs();
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
        std::uint32_t version = 0;    // packVersion of the Voxology that wrote it (0: readers skip the slot)
        // Filled in by the reader:
        int slot = -1;
        std::uint64_t instanceId = 0;
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
        std::atomic<float> compressionDb, reverbMix, delayMix;
        Text<128> trackName;
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

    struct Block
    {
        std::atomic<std::uint32_t> layout;
        MixSlot mix;
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

        // ---- Mixology ---------------------------------------------------------------------------------
        /** Every live Voxology. */
        std::vector<VocalInfo> vocals (std::uint64_t nowMs) const;
        void publishMix (const MixInfo&, std::uint64_t nowMs);

        // ---- Masterology ------------------------------------------------------------------------------
        int claimMaster (std::uint64_t instanceId, std::uint64_t nowMs);
        void releaseMaster (int slot, std::uint64_t instanceId);
        bool publishMaster (int slot, std::uint64_t instanceId, const MasterInfo&, std::uint64_t nowMs);   // as publishVocal
        /** Mixology's hand-off, if a Mixology wrote one within kAliveMs. */
        bool readMix (MixInfo&, std::uint64_t nowMs) const;

        // ---- Mixology reads the masters -----------------------------------------------------------------
        std::vector<MasterInfo> masters (std::uint64_t nowMs) const;

    private:
        Block& block;
    };
}

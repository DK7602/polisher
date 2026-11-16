#pragma once

// Sonicology Rack link: every Sonicology plug-in tells the Rack view what its modules do.
//
// THIS FILE AND RackLink.cpp ARE THE SAME IN ALL THREE REPOSITORIES (Voxology, Mixology, Masterology).
// Change them in Mixology (the master copy), copy them to the other two, and bump kLayout when the memory layout
// changes (kLayout is part of the block's name, so an older and a newer build never read each other's memory).
//
// The Rack (plugin/ui/rack.js, opened from any of the three) shows the whole suite in one view: each track's
// Sonicology plug-ins and their modules in signal order, what each is set to, and how the plug-ins work together.
// It only shows (stage 1 of docs/rack/RACK-PLAN.md in Mixology's repo); the plug-ins do the processing.
//
//   - Each plug-in instance (Voxology, Honey Tune, Mixology, each Mixology Link, Masterology) claims a slot in a named
//     block of shared memory, one per host process, and publishes a Unit there from its message thread: which plug-in,
//     the track, its role, its modules (state, a one-line summary, up to 8 values, EQ bands), its levels, a spectrum,
//     and the lines it knows itself ("Pitch rests: Honey Tune tunes this track").
//   - An open Rack view refreshes a viewer heartbeat; the plug-ins publish their modules' details only while one is
//     fresh (watched()), else just who and where they are, about once a second. No cost while no Rack is open.
//   - The view's plug-in reads every live unit (units()), works out what they do together (together()) and hands the
//     view one JSON text (toJson()).
// Framework-free C++17, lock-free (atomics and a sequence counter per slot), nothing allocated in shared memory.
// Zero-filled memory is a valid, empty block. Same rules as the suite link (SuiteLink.h): a plug-in takes part only
// while the host runs it (suite::Presence), a slot silent for kAliveMs is gone and may be taken over.

#include "suite/SuiteLink.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rack
{
    constexpr std::uint32_t kLayout = 1;
    constexpr int kMaxSlots = 256;          // every Mixology Link, Voxology, Honey Tune, Mixology and Masterology
    constexpr int kMaxModules = 16;         // a Link has 15 (the Assist EQ and its processing modules)
    constexpr int kMaxValues = 8;           // the knobs the view draws
    constexpr int kMaxBands = 8;            // an EQ's bands
    constexpr int kMaxLines = 4;            // a plug-in's own "together" lines
    constexpr int kSpectrumBins = 128;      // log-spaced 20 Hz - 20 kHz: bin i at 20 * 1000^(i / 127) Hz
    // Generous: a DAW's window thread (where the plug-ins' timers run) can stall for seconds on a busy computer, and a
    // stall must not make the plug-ins vanish from the Rack or drop their details.
    constexpr std::uint64_t kAliveMs = 6000;    // a publisher silent this long is gone
    constexpr std::uint64_t kViewerMs = 5000;   // a Rack view that hasn't looked for this long is closed

    constexpr std::size_t kTrackBytes = 128, kKeyBytes = 24, kSummaryBytes = 96, kByBytes = 40, kValueNameBytes = 24,
                          kValueTextBytes = 24, kBandNameBytes = 16, kLineBytes = 160;

    // ---- what a plug-in publishes ---------------------------------------------------------------------------------

    enum class Product : std::uint32_t { none = 0, voxology, honeytune, link, mixology, masterology };
    enum class Role : std::uint32_t { track = 0, vocal, beat, fullmix, master };
    enum class State : std::uint32_t { off = 0, on, idle, rest };   // idle: on, nothing to do now; rest: stands aside
    enum class MeterKind : std::uint32_t { none = 0, gr, level };   // gr: gain reduction (dB, <= 0); level: dBFS
    enum class BandType : std::uint32_t { bell = 0, lowShelf, highShelf, lowCut, highCut };
    enum class Level : std::uint32_t { ok = 0, info, warn };

    /** One knob as the view shows it: its name, its value as text ("3.2 kHz"), where it sits (0..1) and whether it is
        bipolar (its arc grows from the middle). */
    struct Value
    {
        std::string name, text;
        float norm = 0.5f;
        bool bi = false;
    };

    struct Band
    {
        BandType type = BandType::bell;
        float hz = 1000.0f, db = 0.0f, q = 0.71f;
        std::string name;               // "Mud", "Presence" ("" = the view names it by its type)
        bool on = true;
    };

    /** Mixology's Space effects (its layers), as bits of Module::layers. */
    enum SpaceLayer : std::uint32_t
    {
        reverb = 1u << 0, delay = 1u << 1, chorus = 1u << 2, phaser = 1u << 3, hall = 1u << 4, room = 1u << 5, special = 1u << 6,
    };

    struct Module
    {
        std::string key;                // the view's catalogue: "vox.pitch", "mix.eq", "mas.max", "ht.tune" ...
        State state = State::off;
        std::string summary;            // one line, written by the plug-in itself ("Natural · 50 ms · F minor")
        std::string by;                 // who set it, when it wasn't you ("Mixology's Clean Up", "AUTO") or ""
        MeterKind meterKind = MeterKind::none;
        float meter = 0.0f;
        std::vector<Value> values;      // up to kMaxValues
        std::vector<Band> bands;        // up to kMaxBands (EQs)
        std::uint32_t layers = 0;       // SpaceLayer bits (Mixology's Space)
    };

    struct Line
    {
        Level level = Level::ok;
        std::string text;
    };

    /** One plug-in instance. */
    struct Unit
    {
        Product product = Product::none;
        Role role = Role::track;
        std::string track;              // as the host names it ("" = unknown; Mixology / Masterology on the master: "")
        std::uint32_t hash = 0;         // suite::nameHash (track), as the suite link and Mixology's bridge use
        bool on = true;                 // not bypassed
        std::uint32_t version = 0;      // suite::packVersion of the plug-in (0: readers skip the slot)
        float inDb = -100.0f, outDb = -100.0f;   // its input and output, peak dBFS lately
        float latencyMs = 0.0f;
        bool detailed = false;          // modules, values and the spectrum are filled (a Rack view is watching)
        std::vector<Module> modules;    // in signal order, up to kMaxModules
        std::vector<Line> lines;        // up to kMaxLines
        std::vector<float> spectrum;    // kSpectrumBins dB values (what it hears, tilted as its own display), or empty
        // Filled in by the reader:
        int slot = -1;
        std::uint64_t instanceId = 0;
    };

    /** A line about how plug-ins work together, from together(): on a track (hash), the Full Mix, the Master, or the
        whole project. */
    struct ProjectLine
    {
        enum class Scope : std::uint32_t { track = 0, fullmix, master, project };
        Scope scope = Scope::project;
        std::uint32_t hash = 0;         // Scope::track
        Level level = Level::ok;
        std::string text;
    };

    /** What the plug-ins in the project do together: two reverbs or two compressors on one vocal, two tuners, a vocal
        Mixology can't hear, tracks that share a name, two Masterologys, Mixology's Limiter before Masterology ... */
    std::vector<ProjectLine> together (const std::vector<Unit>&);

    /** The overview the view's footer shows (from the suite link: Mixology's hand-off, Masterology's goal). */
    struct Overview
    {
        bool mixKnown = false;
        float lufsI = -100.0f, lufsS = -100.0f, truePeak = -100.0f;
        std::string source;             // "the mix", "after Masterology"
        bool masterKnown = false;
        std::string goal;
        float targetLufs = -14.0f, ceilingDbtp = -1.0f;
    };

    /** The JSON text rack.js reads (getRack()): host is "voxology" | "mixology" | "masterology", hostHash the track the
        view's plug-in sits on. Locale-independent, every number finite. */
    std::string toJson (const std::string& host, std::uint32_t hostHash, const std::vector<Unit>&, const std::vector<ProjectLine>&,
                        const Overview&);

    const char* productKey (Product);   // "voxology", "honeytune", "link", "mixology", "masterology"

    // ---- the shared memory ----------------------------------------------------------------------------------------

    /** Text in 8-byte words (a byte at a time would be slow for a whole module). */
    template <std::size_t N>
    struct Text
    {
        std::array<std::atomic<std::uint64_t>, (N + 7) / 8> words;
    };

    struct ValueSlot
    {
        Text<kValueNameBytes> name;
        Text<kValueTextBytes> text;
        std::atomic<float> norm;
        std::atomic<std::uint32_t> bi;
    };

    struct BandSlot
    {
        std::atomic<std::uint32_t> type, on;
        std::atomic<float> hz, db, q;
        Text<kBandNameBytes> name;
    };

    struct ModuleSlot
    {
        Text<kKeyBytes> key;
        Text<kSummaryBytes> summary;
        Text<kByBytes> by;
        std::atomic<std::uint32_t> state, meterKind, layers, valueCount, bandCount;
        std::atomic<float> meter;
        std::array<ValueSlot, kMaxValues> values;
        std::array<BandSlot, kMaxBands> bands;
    };

    struct LineSlot
    {
        std::atomic<std::uint32_t> level;
        Text<kLineBytes> text;
    };

    struct alignas (64) UnitSlot
    {
        std::atomic<std::uint32_t> state;          // 0 free, 1 claimed
        std::atomic<std::uint32_t> generation;
        std::atomic<std::uint64_t> instanceId;
        std::atomic<std::uint64_t> heartbeatMs;
        std::atomic<std::uint32_t> seq;            // odd while the fields below are written
        std::atomic<std::uint32_t> product, role, hash, on, version, detailed, moduleCount, lineCount, spectrumBins;
        std::atomic<float> inDb, outDb, latencyMs;
        Text<kTrackBytes> track;
        std::array<LineSlot, kMaxLines> lines;
        std::array<std::atomic<float>, kSpectrumBins> spectrum;
        std::array<ModuleSlot, kMaxModules> modules;
    };

    struct Block
    {
        std::atomic<std::uint32_t> layout;
        std::atomic<std::uint64_t> viewerMs;       // an open Rack view's heartbeat (0 = none)
        std::array<UnitSlot, kMaxSlots> slots;
    };

    /** The Rack's block. Construct over memory that holds sizeof (Block) bytes (zero-filled or already in use), or use
        Link::process() for this host process's block. Readers and writers may be in different libraries. */
    class Link
    {
    public:
        explicit Link (void* memory);
        static constexpr std::size_t bytes() { return sizeof (Block); }

        /** This library's handle on the host process's block (opened on first use). */
        static Link& process();
        /** False when the OS refused shared memory (the plug-ins then can't see each other). */
        static bool processShared();

        // ---- every plug-in (message thread) ----
        int claim (std::uint64_t instanceId, std::uint64_t nowMs);   // -1 when full
        void release (int slot, std::uint64_t instanceId);
        /** A few times a second while watched(), else about once a second (also the heartbeat). False when the slot is
            no longer this instance's (it was silent longer than kAliveMs and another took it over): claim again. Values
            past the limits are dropped, texts cut (at a UTF-8 boundary), numbers that aren't finite stored as 0. */
        bool publish (int slot, std::uint64_t instanceId, const Unit&, std::uint64_t nowMs);
        /** A Rack view is open somewhere in the project: publish the modules' details. */
        bool watched (std::uint64_t nowMs) const;

        // ---- the Rack view's plug-in (message thread) ----
        /** The view looked (a few times a second while it is open). */
        void view (std::uint64_t nowMs);
        /** Every live unit (version != 0), by slot. */
        std::vector<Unit> units (std::uint64_t nowMs) const;

    private:
        Block& block;
    };

    /** One plug-in's side of the link (message thread, except start / stop): its slot, the suite link's rule that only
        the project that plays takes part, and how often to publish. In the plug-in:
          prepareToPlay: rack.start();      releaseResources: rack.stop();      (any thread)
          a timer (any rate): if (auto due = rack.due (now); due != rack::Publisher::Due::none)
                                  rack.publish (unitFor (due == rack::Publisher::Due::full), now);
        Full (modules, values, bands, the spectrum) about kFullMs while a Rack view is open; else brief (who and where,
        the modules' keys and states) about every kBriefMs, which is also the heartbeat. */
    class Publisher
    {
    public:
        enum class Due { none, brief, full };
        static constexpr std::uint64_t kFullMs = 200, kBriefMs = 800;

        explicit Publisher (Link& = Link::process());
        ~Publisher();
        Publisher (const Publisher&) = delete;
        Publisher& operator= (const Publisher&) = delete;

        void start() noexcept { presence.start(); }
        void stop() noexcept { presence.stop(); }

        Due due (std::uint64_t nowMs);
        /** Publishes (u.detailed set from the last due()). */
        void publish (Unit u, std::uint64_t nowMs);

        bool takingPart() const noexcept { return presence.takingPart(); }
        int slot() const noexcept { return slotIndex; }
        bool watched (std::uint64_t nowMs) const { return link.watched (nowMs); }

    private:
        Link& link;
        std::uint64_t id;
        suite::Presence presence;
        int slotIndex = -1;
        std::uint64_t lastMs = 0;
        bool lastWasFull = false;
        Due pending = Due::none;
    };

    // ---- the Rack view's plug-in ----------------------------------------------------------------------------------

    /** The mix's loudness (Mixology's hand-off, else what reaches Masterology) and Masterology's goal, from the suite
        link. */
    Overview overview (const suite::Link&, std::uint64_t nowMs);

    /** Everything an open Rack view shows (its getRack()), for the plug-in it is open in: refreshes the viewer heartbeat
        (so the plug-ins publish their details), reads every live unit, works out what they do together, adds the
        overview. Message thread, a few times a second while the view is open. Details (values, bands, the spectrum)
        only for the track the view shows: rack.js's track key "t<hash>", "fullmix" or "master" ("" = every track's). */
    std::string viewJson (Link&, const suite::Link&, const std::string& host, std::uint32_t hostHash, std::uint64_t nowMs,
                          const std::string& detailTrack = {});
    inline std::string viewJson (const std::string& host, std::uint32_t hostHash, std::uint64_t nowMs, const std::string& detailTrack = {})
    {
        return viewJson (Link::process(), suite::Link::process(), host, hostHash, nowMs, detailTrack);
    }
}

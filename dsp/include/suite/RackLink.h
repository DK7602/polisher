#pragma once

// Sonicology Rack link: every Sonicology plug-in tells the Rack view what its modules do.
//
// THIS FILE AND RackLink.cpp ARE THE SAME IN ALL THREE REPOSITORIES (Voxology, Mixology, Masterology).
// Change them in Mixology (the master copy), copy them to the other two, and bump kLayout when the memory layout
// changes (kLayout is part of the block's name, so an older and a newer build never read each other's memory).
//
// The Rack (plugin/ui/rack.js, opened from any of the three) shows the whole suite in one view: each track's
// Sonicology plug-ins and their modules in signal order, what each is set to, and how the plug-ins work together.
// Stage 1 of docs/rack/RACK-PLAN.md (in Mixology's repo) shows; stage 2 changes things through the plug-ins that own them
// (requests, below). The plug-ins do the processing.
//
//   - Each plug-in instance (Voxology, Honey Tune, Mixology, each Mixology Link, Masterology) claims a slot in a named
//     block of shared memory, one per host process, and publishes a Unit there from its message thread: which plug-in,
//     the track, its role, its modules (state, a one-line summary, up to 8 values, EQ bands), its levels, a spectrum,
//     and the lines it knows itself ("Pitch rests: Honey Tune tunes this track").
//   - An open Rack view refreshes a viewer heartbeat; the plug-ins publish their modules' details only while one is
//     fresh (watched()), else just who and where they are, about once a second. No cost while no Rack is open.
//   - The view's plug-in reads every live unit (units()), works out what they do together (together()) and hands the
//     view one JSON text (toJson()).
//   - Stage 2: a click in the view becomes a Request (turn a module on / off, move a knob) that the view's plug-in puts
//     in the queue of the unit that owns the module (request()). The owner takes it on its message thread
//     (Publisher::serve()) and applies it like a move of your own mouse, so the host's undo and automation see it; a
//     unit that is locked ("Auto Mix is running") refuses, and the view reads the answer (answer()).
// Framework-free C++17, lock-free (atomics and a sequence counter per slot), nothing allocated in shared memory.
// Zero-filled memory is a valid, empty block. Same rules as the suite link (SuiteLink.h): a plug-in takes part only
// while the host runs it (suite::Presence), a slot silent for kAliveMs is gone and may be taken over.

#include "suite/SuiteLink.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rack
{
    constexpr std::uint32_t kLayout = 4;    // 2: + requests (stage 2), the lock; 3: + the tracks the views show; 4: + the Coach's report
    constexpr int kMaxSlots = 256;          // every Mixology Link, Voxology, Honey Tune, Mixology and Masterology
    constexpr int kMaxModules = 16;         // a Link has 15 (the Assist EQ and its processing modules)
    constexpr int kMaxValues = 8;           // the knobs the view draws
    constexpr int kMaxSteps = 64;           // a choice knob's options at most
    constexpr int kMaxBands = 8;            // an EQ's bands
    constexpr int kMaxLines = 4;            // a plug-in's own "together" lines
    constexpr int kSpectrumBins = 128;      // log-spaced 20 Hz - 20 kHz: bin i at 20 * 1000^(i / 127) Hz
    // Generous: a DAW's window thread (where the plug-ins' timers run) can stall for seconds on a busy computer, and a
    // stall must not make the plug-ins vanish from the Rack or drop their details.
    constexpr std::uint64_t kAliveMs = 6000;    // a publisher silent this long is gone
    constexpr std::uint64_t kViewerMs = 5000;   // a Rack view that hasn't looked for this long is closed
    constexpr int kMaxRequests = 32;            // a unit's queue: requests its owner hasn't taken yet
    constexpr int kMaxRefusals = 4;             // the latest refusals a view can still read
    constexpr std::uint64_t kGestureMs = 2000;  // a knob drag that stopped moving this long has ended
    constexpr std::uint64_t kStuckMs = 1000;    // a request handed out but never written (its sender stopped) is skipped

    constexpr std::size_t kTrackBytes = 128, kKeyBytes = 24, kSummaryBytes = 96, kByBytes = 40, kValueNameBytes = 24,
                          kValueTextBytes = 24, kBandNameBytes = 16, kLineBytes = 160, kControlBytes = 24, kWhyBytes = 128;
    constexpr std::size_t kReportBytes = 8192;   // a plug-in's Coach report (JSON text, the owner's own)

    // ---- what a plug-in publishes ---------------------------------------------------------------------------------

    enum class Product : std::uint32_t { none = 0, voxology, honeytune, link, mixology, masterology };
    enum class Role : std::uint32_t { track = 0, vocal, beat, fullmix, master };
    enum class State : std::uint32_t { off = 0, on, idle, rest };   // idle: on, nothing to do now; rest: stands aside
    enum class MeterKind : std::uint32_t { none = 0, gr, level };   // gr: gain reduction (dB, <= 0); level: dBFS
    enum class BandType : std::uint32_t { bell = 0, lowShelf, highShelf, lowCut, highCut };
    enum class Level : std::uint32_t { ok = 0, info, warn };

    /** One knob as the view shows it: its name, its value as text ("3.2 kHz"), where it sits (0..1) and whether it is
        bipolar (its arc grows from the middle). Stage 2: `key` names the control the Rack can turn ("" = shown only, as
        a key Pitch follows or what the Beat Listener hears); the owner turns a request's 0..1 back into the value with
        its own curve (the same one that gave `norm`). `steps`: a choice of that many (Mode, Speed: the Rack clicks
        through them, step i at i / (steps - 1)), 0 = continuous. */
    struct Value
    {
        std::string name, text;
        float norm = 0.5f;
        bool bi = false;
        std::string key;
        int steps = 0;
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
        bool switchable = false;        // the Rack can turn it on / off (it has its own on switch)
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
        std::string lock;               // why the Rack can't change it now ("Auto Mix is running"), "" = it can
        /** Stage 3, the Coach: what this plug-in's own automatic setup heard and would do on this track (a JSON object
            as text, the owner's format: rack.js reads it), "" = nothing to say. Up to kReportBytes; a longer one is
            dropped whole (half a JSON text is no use). Sent with the details only (the track a Rack shows). */
        std::string report;
        // Filled in by the reader:
        int slot = -1;
        std::uint32_t generation = 0;   // the slot's owner: requests name it, so a slot taken over never gets another's
        std::uint64_t instanceId = 0;
    };

    // ---- stage 2: requests from a Rack view to the plug-in that owns a module ------------------------------------

    /** value: a knob to `value`; state: a module on / off; reset: a knob back to its default (the owner's own); power:
        the plug-in itself on (1) / off (0), its own bypass (the Rack's BYPASS; module "power"); save: keep the plug-in's
        whole setup where it keeps its own (Voxology's My Chains: the Rack's SAVE CHAIN; module "save"); coach: the
        Coach's step in the owner's own automatic setup, named by `control` ("plan": listen and work out a plan without
        changing anything, "apply": apply it, "undo": take it back; module "coach"). */
    enum class Change : std::uint32_t { value = 0, state, reset, power, save, coach };
    /** A knob drag: begin, moves, end (the owner holds the host's gesture open between them, so automation records it
        as one touch); set: a single change (a click). The owner always gets them balanced (Publisher::serve()). */
    enum class Gesture : std::uint32_t { set = 0, begin, move, end };

    struct Request
    {
        Change change = Change::value;
        std::string module;             // the module's key ("link.comp")
        std::string control;            // Change::value: the control's key in that module; Change::state: ""
        float value = 0.0f;             // Change::value: where the knob goes (0..1); Change::state: 1 on, 0 off; reset: unused
        Gesture gesture = Gesture::set; // Change::state: always set
        std::uint64_t ticket = 0;       // from request() (0: made up by serve() to close a gesture the view left open)
    };

    enum class Outcome { gone = 0, pending, applied, refused };
    struct Answer
    {
        Outcome outcome = Outcome::gone;
        std::string why;                // refused: the owner's reason, shown in the Rack
    };

    /** A line about how plug-ins work together, from together(): on a track (hash), the Full Mix, the Master, or the
        whole project. */
    /** A one-click fix for a warning: switch a module off (or on) in the unit that owns it ("Turn the Link's Space
        off"). The Rack sends it as a Change::state request. */
    struct Fix
    {
        int slot = -1;                  // the unit (as units() gave it)
        std::uint32_t generation = 0;
        std::string module;             // its key ("mix.space")
        bool on = false;                // what to switch it to
        std::string label;              // the button ("Turn the Link's Space off")
    };

    struct ProjectLine
    {
        enum class Scope : std::uint32_t { track = 0, fullmix, master, project };
        Scope scope = Scope::project;
        std::uint32_t hash = 0;         // Scope::track
        Level level = Level::ok;
        std::string text;
        std::vector<Fix> fixes;         // up to two (units read from the block only: a unit with no slot gets none)
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
                        const Overview&, const std::vector<std::uint32_t>& otherLayouts = {});

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
        std::atomic<std::uint32_t> bi, steps;
        Text<kControlBytes> key;
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
        std::atomic<std::uint32_t> state, meterKind, layers, valueCount, bandCount, switchable;
        std::atomic<float> meter;
        std::array<ValueSlot, kMaxValues> values;
        std::array<BandSlot, kMaxBands> bands;
    };

    struct LineSlot
    {
        std::atomic<std::uint32_t> level;
        Text<kLineBytes> text;
    };

    /** One request in a unit's queue. stamp: 2t - 1 while ticket t is written, 2t once it is (0: never used). */
    struct RequestSlot
    {
        std::atomic<std::uint64_t> stamp;
        std::atomic<std::uint32_t> generation, change, gesture;
        std::atomic<float> value;
        Text<kKeyBytes> module;
        Text<kControlBytes> control;
    };

    struct RefusalSlot
    {
        std::atomic<std::uint32_t> seq;            // odd while written
        std::atomic<std::uint64_t> ticket;
        Text<kWhyBytes> why;
    };

    /** A unit's requests: many senders (any Rack view), one taker (the owner). Tickets count from 1. */
    struct Queue
    {
        std::atomic<std::uint64_t> issued;         // the last ticket handed out (senders, compare-and-swap)
        std::atomic<std::uint64_t> taken;          // the owner has taken every ticket up to here
        std::atomic<std::uint64_t> answered;       // ... and answered them (refusals written first)
        std::array<RequestSlot, kMaxRequests> ring;            // ticket t at t % kMaxRequests
        std::array<RefusalSlot, kMaxRefusals> refusals;        // ticket t at t % kMaxRefusals
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
        Text<kWhyBytes> lock;
        std::atomic<std::uint32_t> reportBytes;    // the Coach's report: its length (0 = none) ...
        Text<kReportBytes> report;                 // ... (only those bytes are written and read)
        std::array<LineSlot, kMaxLines> lines;
        std::array<std::atomic<float>, kSpectrumBins> spectrum;
        std::array<ModuleSlot, kMaxModules> modules;
        Queue queue;
    };

    /** What an open Rack view shows: every track's details (kind 0), a track (1, by its hash), the Full Mix (2) or the
        Master (3). The plug-ins elsewhere send the short version (their details aren't drawn). */
    struct ViewerSlot
    {
        std::atomic<std::uint64_t> ms;             // its last look (0 = never)
        std::atomic<std::uint32_t> kind, hash;
    };
    constexpr int kMaxViewers = 4;

    struct Block
    {
        std::atomic<std::uint32_t> layout;
        std::atomic<std::uint64_t> viewerMs;       // an open Rack view's heartbeat (0 = none)
        std::array<ViewerSlot, kMaxViewers> viewers;
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
        /** The layouts of other Rack blocks alive in this process: Sonicology plug-ins of an older (or, on Windows, a
            newer) beta, which can't join this Rack. Empty when all match. A few OS calls: once a second is plenty. */
        static std::vector<std::uint32_t> otherLayouts (std::uint64_t nowMs);

        // ---- every plug-in (message thread) ----
        int claim (std::uint64_t instanceId, std::uint64_t nowMs);   // -1 when full
        void release (int slot, std::uint64_t instanceId);
        /** A few times a second while watched(), else about once a second (also the heartbeat). False when the slot is
            no longer this instance's (it was silent longer than kAliveMs and another took it over): claim again. Values
            past the limits are dropped, texts cut (at a UTF-8 boundary), numbers that aren't finite stored as 0. */
        bool publish (int slot, std::uint64_t instanceId, const Unit&, std::uint64_t nowMs);
        /** A Rack view is open somewhere in the project. */
        bool watched (std::uint64_t nowMs) const;
        /** A Rack view open somewhere shows this unit's track (role fullmix / master: the Full Mix / the Master; else the
            track's hash): publish its details, serve its requests quickly. */
        bool shows (std::uint64_t nowMs, std::uint32_t hash, Role) const;
        /** The tracks (their hashes) open Rack views show now: Mixology publishes their faders for them. */
        std::vector<std::uint32_t> shownTracks (std::uint64_t nowMs) const;

        // ---- the Rack view's plug-in (message thread) ----
        /** The view looked (a few times a second while it is open), showing `track`: rack.js's track key ("t<hash>",
            "fullmix", "master"; "" = every track's details). */
        void view (std::uint64_t nowMs, const std::string& track = {});
        /** Every live unit (version != 0), by slot. */
        std::vector<Unit> units (std::uint64_t nowMs) const;

        /** Puts a request in the queue of the unit in `slot` (as units() gave it: slot and generation). Its ticket, or 0
            with `why` (shown in the Rack) when it can't go: the unit is gone, locked (its lock), or its queue is full.
            Any thread, any number of views at once; texts cut, the value kept to 0..1. */
        std::uint64_t request (int slot, std::uint32_t generation, const Request&, std::uint64_t nowMs, std::string& why);
        /** What became of a ticket: pending until the owner took it; refused (with why) or applied; gone when the unit
            went (its requests with it). A refusal stays readable until kMaxRefusals later tickets of the same unit. */
        Answer answer (int slot, std::uint32_t generation, std::uint64_t ticket) const;

        // ---- the owner of a slot (its message thread; Publisher::serve() does all this) ----
        /** Up to `max` requests in ticket order. A ticket handed out but not written within kStuckMs (its sender
            stopped half-way) is skipped; `waitingSinceMs` is the owner's own memo of that (start at 0). */
        int take (int slot, std::uint64_t instanceId, Request* out, int max, std::uint64_t nowMs, std::uint64_t& waitingSinceMs);
        /** Refuses a taken ticket (before answered() passes it). */
        void refuse (int slot, std::uint64_t instanceId, std::uint64_t ticket, const std::string& why);
        /** Every taken ticket up to `ticket` is answered (applied unless refused). */
        void answered (int slot, std::uint64_t instanceId, std::uint64_t ticket);

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

        /** Full when a Rack view open somewhere shows this unit's track (hash, role: as Link::shows()), else brief. Right
            after serve() applied a request, due at once (the Rack sees the new value without waiting). */
        Due due (std::uint64_t nowMs, std::uint32_t hash, Role role);
        /** As above, for a unit any open Rack view counts as showing. */
        Due due (std::uint64_t nowMs);
        /** Publishes (u.detailed set from the last due()). */
        void publish (Unit u, std::uint64_t nowMs);

        /** Applies one request like a move of your own mouse (Change::value: the control to `value` 0..1, with the
            host's gesture: begin -> beginChangeGesture + set, move -> set, end -> set + endChangeGesture, set -> all
            three; Change::state: the module on / off). Returns "" when done, else why not (shown in the Rack: "Pitch has
            no Speed knob"). A Gesture::end with ticket 0 closes a drag the view left open: always end the gesture. */
        using Apply = std::function<std::string (const Request&)>;
        /** The owner's side of the queue, from the same timer as due() (as often as you like; 30 a second keeps a drag
            smooth). Takes the requests sent since the last call and applies them, in order, through `apply`:
              - `lock` not "" (Auto Mix is running ...): every request refused with it; open drags are closed first.
              - Gestures balanced: a move without a begin begins, a begin while open moves, a set while open ends, an
                end without a begin sets. A drag that stopped kGestureMs ago is closed (Gesture::end, ticket 0).
              - A move followed by another change of the same control in the same batch is skipped (counts as applied).
            Returns how many requests it applied. */
        int serve (std::uint64_t nowMs, const std::string& lock, const Apply& apply);

        bool takingPart() const noexcept { return presence.takingPart(); }
        int slot() const noexcept { return slotIndex; }
        bool watched (std::uint64_t nowMs) const { return link.watched (nowMs); }
        bool shows (std::uint64_t nowMs, std::uint32_t hash, Role role) const { return link.shows (nowMs, hash, role); }
        std::vector<std::uint32_t> shownTracks (std::uint64_t nowMs) const { return link.shownTracks (nowMs); }

    private:
        Due dueIf (std::uint64_t nowMs, bool full);
        Link& link;
        std::uint64_t id;
        suite::Presence presence;
        int slotIndex = -1;
        std::uint64_t lastMs = 0;
        bool lastWasFull = false;
        Due pending = Due::none;
        bool refresh = false;                      // serve() applied a request: publish at once

        struct Open { std::string module, control; float value = 0.0f; std::uint64_t lastMs = 0; };
        std::vector<Open> open;                    // drags in progress (the host's gesture is open)
        std::uint64_t waitingSinceMs = 0;          // take()'s memo
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

    /** Stage 2, for the view's plug-in (its rackRequest() / rackAnswer() native functions): a click in the view as rack.js
        sends it. unit: the JSON's "u<slot>", gen: its "gen"; change "value" | "state" | "reset" | "power" | "save" | "coach"; gesture "set" | "begin" | "move" |
        "end". Returns {"ticket":N,"why":""} (ticket 0 and why when it can't go). */
    std::string requestJson (Link&, const std::string& unit, std::uint32_t generation, const std::string& change, const std::string& module,
                             const std::string& control, float value, const std::string& gesture, std::uint64_t nowMs);
    /** {"outcome":"pending" | "applied" | "refused" | "gone","why":"..."} for a ticket requestJson() gave. */
    std::string answerJson (const Link&, const std::string& unit, std::uint32_t generation, std::uint64_t ticket);
}

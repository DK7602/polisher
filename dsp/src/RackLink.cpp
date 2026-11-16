// Sonicology Rack link: see RackLink.h. THIS FILE IS THE SAME IN ALL THREE REPOSITORIES (Voxology, Mixology, Masterology).

#include "suite/RackLink.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <new>

#if defined(_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace rack
{
    namespace
    {
        constexpr auto kRelaxed = std::memory_order_relaxed;
        constexpr auto kAcquire = std::memory_order_acquire;
        constexpr auto kRelease = std::memory_order_release;

        static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
        static_assert (std::atomic<std::uint32_t>::is_always_lock_free);
        static_assert (std::atomic<float>::is_always_lock_free);

        float finite (float v, float fallback = 0.0f) { return std::isfinite (v) ? v : fallback; }

        // ---- text in 8-byte words ----
        std::size_t utf8Cut (const std::string& s, std::size_t max)
        {
            std::size_t n = std::min (s.size(), max);
            while (n > 0 && n < s.size() && (static_cast<unsigned char> (s[n]) & 0xC0) == 0x80) --n;   // UTF-8 boundary
            return n;
        }

        template <std::size_t N>
        void storeText (Text<N>& to, const std::string& s)
        {
            const std::size_t n = utf8Cut (s, N - 1);
            for (std::size_t w = 0; w < to.words.size(); ++w)
            {
                char bytes[8] = {};
                for (std::size_t b = 0; b < 8; ++b)
                {
                    const std::size_t i = w * 8 + b;
                    bytes[b] = i < n ? s[i] : '\0';
                }
                std::uint64_t word = 0;
                std::memcpy (&word, bytes, 8);
                to.words[w].store (word, kRelaxed);
            }
        }

        template <std::size_t N>
        std::string loadText (const Text<N>& from)
        {
            std::string s;
            for (std::size_t w = 0; w < from.words.size(); ++w)
            {
                const std::uint64_t word = from.words[w].load (kRelaxed);
                char bytes[8];
                std::memcpy (bytes, &word, 8);
                for (char c : bytes)
                {
                    if (c == '\0' || s.size() >= N - 1) return s;
                    s.push_back (c);
                }
            }
            return s;
        }

        // ---- seqlock (as the suite link) ----
        void beginWrite (std::atomic<std::uint32_t>& seq)
        {
            seq.fetch_add (1, std::memory_order_acq_rel);
            std::atomic_thread_fence (kRelease);
        }
        void endWrite (std::atomic<std::uint32_t>& seq) { seq.fetch_add (1, kRelease); }

        template <typename ReadFn>
        bool readConsistent (const std::atomic<std::uint32_t>& seq, ReadFn&& read)
        {
            for (int attempt = 0; attempt < 64; ++attempt)
            {
                const auto before = seq.load (kAcquire);
                if (before & 1u) continue;
                read();
                std::atomic_thread_fence (kAcquire);
                if (seq.load (kRelaxed) == before) return true;
            }
            return false;
        }

        bool alive (std::uint64_t beat, std::uint64_t now, std::uint64_t windowMs = kAliveMs)
        {
            return beat != 0 && now - std::min (now, beat) <= windowMs;
        }

        // ---- this process's block (one per library, all on the same memory) ----
        class SharedMemory
        {
        public:
            SharedMemory (const std::string& name, std::size_t bytes) : total (bytes)
            {
#if defined(_WIN32)
                const std::string osName = "Local\\" + name;
                const auto size = static_cast<unsigned long long> (total);
                handle = CreateFileMappingA (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD> (size >> 32),
                                             static_cast<DWORD> (size & 0xFFFFFFFFull), osName.c_str());
                if (handle != nullptr)
                {
                    mapping = MapViewOfFile (handle, FILE_MAP_ALL_ACCESS, 0, 0, total);
                    if (mapping == nullptr) { CloseHandle (handle); handle = nullptr; }
                }
#else
                // Never unlinked while the process lives; the name carries the process ID.
                const std::string osName = "/" + name;
                const int fd = shm_open (osName.c_str(), O_RDWR | O_CREAT, 0600);
                if (fd >= 0)
                {
                    struct stat st {};
                    bool ok = fstat (fd, &st) == 0;
                    if (ok && static_cast<std::size_t> (st.st_size) < total) ok = ftruncate (fd, static_cast<off_t> (total)) == 0;
                    if (ok)
                    {
                        void* p = mmap (nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                        if (p != MAP_FAILED) mapping = p;
                    }
                    close (fd);
                }
#endif
                shared = mapping != nullptr;
                if (! shared)
                {
                    mapping = ::operator new (total, std::align_val_t { 64 });
                    std::memset (mapping, 0, total);
                }
            }
            ~SharedMemory()
            {
                if (! shared) { ::operator delete (mapping, std::align_val_t { 64 }); return; }
#if defined(_WIN32)
                UnmapViewOfFile (mapping);
                CloseHandle (handle);
#else
                munmap (mapping, total);
#endif
            }
            SharedMemory (const SharedMemory&) = delete;
            SharedMemory& operator= (const SharedMemory&) = delete;

            void* data() const { return mapping; }
            bool isShared() const { return shared; }

        private:
            void* mapping = nullptr;
            std::size_t total = 0;
            bool shared = false;
#if defined(_WIN32)
            HANDLE handle = nullptr;
#endif
        };

        std::string processBlockName()
        {
#if defined(_WIN32)
            const auto pid = static_cast<unsigned long> (GetCurrentProcessId());
#else
            const auto pid = static_cast<unsigned long> (getpid());
#endif
            return "SonicologyRack.v" + std::to_string (kLayout) + "." + std::to_string (pid);
        }

        struct ProcessHandle
        {
            SharedMemory memory { processBlockName(), Link::bytes() };
            Link link { memory.data() };
        };

        ProcessHandle& handle()
        {
            static ProcessHandle h;
            return h;
        }
    }

    // ---- the link ----------------------------------------------------------------------------------------------

    Link::Link (void* memory) : block (*static_cast<Block*> (memory))
    {
        block.layout.store (kLayout, kRelaxed);
    }

    Link& Link::process() { return handle().link; }
    bool Link::processShared() { return handle().memory.isShared(); }

    int Link::claim (std::uint64_t instanceId, std::uint64_t now)
    {
        for (std::size_t i = 0; i < block.slots.size(); ++i)
        {
            auto& s = block.slots[i];
            std::uint32_t expected = 0;
            if (s.state.compare_exchange_strong (expected, 1u, std::memory_order_acq_rel))
                s.heartbeatMs.store (now, kRelaxed);
            else
            {
                // A claimed slot whose owner vanished without releasing it (a crashed plug-in): take it over. Only one
                // claimer wins the swap of the stale heartbeat.
                auto beat = s.heartbeatMs.load (kAcquire);
                if (beat == 0 || alive (beat, now)) continue;
                if (! s.heartbeatMs.compare_exchange_strong (beat, now, std::memory_order_acq_rel)) continue;
            }
            s.instanceId.store (instanceId, kRelaxed);
            s.generation.fetch_add (1, kRelease);
            const int slot = static_cast<int> (i);
            publish (slot, instanceId, Unit {}, now);   // nothing of an earlier owner shows
            return slot;
        }
        return -1;
    }

    void Link::release (int slot, std::uint64_t instanceId)
    {
        if (slot < 0 || slot >= kMaxSlots) return;
        auto& s = block.slots[static_cast<std::size_t> (slot)];
        if (s.instanceId.load (kRelaxed) != instanceId) return;
        beginWrite (s.seq);
        s.version.store (0, kRelaxed);
        endWrite (s.seq);
        s.heartbeatMs.store (0, kRelaxed);
        s.instanceId.store (0, kRelaxed);
        s.state.store (0, kRelease);
    }

    bool Link::publish (int slot, std::uint64_t instanceId, const Unit& u, std::uint64_t now)
    {
        if (slot < 0 || slot >= kMaxSlots) return false;
        auto& s = block.slots[static_cast<std::size_t> (slot)];
        if (s.instanceId.load (kRelaxed) != instanceId) return false;   // not ours any more
        beginWrite (s.seq);
        s.product.store (static_cast<std::uint32_t> (u.product), kRelaxed);
        s.role.store (static_cast<std::uint32_t> (u.role), kRelaxed);
        s.hash.store (u.track.empty() ? u.hash : suite::nameHash (u.track), kRelaxed);
        s.on.store (u.on ? 1u : 0u, kRelaxed);
        s.version.store (u.version, kRelaxed);
        s.detailed.store (u.detailed ? 1u : 0u, kRelaxed);
        s.inDb.store (finite (u.inDb, -100.0f), kRelaxed);
        s.outDb.store (finite (u.outDb, -100.0f), kRelaxed);
        s.latencyMs.store (std::max (0.0f, finite (u.latencyMs)), kRelaxed);
        storeText (s.track, u.track);

        const auto lines = std::min<std::size_t> (u.lines.size(), kMaxLines);
        for (std::size_t i = 0; i < lines; ++i)
        {
            s.lines[i].level.store (static_cast<std::uint32_t> (u.lines[i].level), kRelaxed);
            storeText (s.lines[i].text, u.lines[i].text);
        }
        s.lineCount.store (static_cast<std::uint32_t> (lines), kRelaxed);

        const bool spectrum = u.spectrum.size() == static_cast<std::size_t> (kSpectrumBins);
        if (spectrum)
            for (std::size_t i = 0; i < static_cast<std::size_t> (kSpectrumBins); ++i)
                s.spectrum[i].store (finite (u.spectrum[i], -100.0f), kRelaxed);
        s.spectrumBins.store (spectrum ? static_cast<std::uint32_t> (kSpectrumBins) : 0u, kRelaxed);

        const auto mods = std::min<std::size_t> (u.modules.size(), kMaxModules);
        for (std::size_t m = 0; m < mods; ++m)
        {
            const auto& from = u.modules[m];
            auto& to = s.modules[m];
            storeText (to.key, from.key);
            storeText (to.summary, from.summary);
            storeText (to.by, from.by);
            to.state.store (static_cast<std::uint32_t> (from.state), kRelaxed);
            to.meterKind.store (static_cast<std::uint32_t> (from.meterKind), kRelaxed);
            to.meter.store (finite (from.meter), kRelaxed);
            to.layers.store (from.layers, kRelaxed);
            const auto values = std::min<std::size_t> (from.values.size(), kMaxValues);
            for (std::size_t v = 0; v < values; ++v)
            {
                storeText (to.values[v].name, from.values[v].name);
                storeText (to.values[v].text, from.values[v].text);
                to.values[v].norm.store (std::clamp (finite (from.values[v].norm, 0.5f), 0.0f, 1.0f), kRelaxed);
                to.values[v].bi.store (from.values[v].bi ? 1u : 0u, kRelaxed);
            }
            to.valueCount.store (static_cast<std::uint32_t> (values), kRelaxed);
            const auto bands = std::min<std::size_t> (from.bands.size(), kMaxBands);
            for (std::size_t b = 0; b < bands; ++b)
            {
                const auto& band = from.bands[b];
                to.bands[b].type.store (static_cast<std::uint32_t> (band.type), kRelaxed);
                to.bands[b].on.store (band.on ? 1u : 0u, kRelaxed);
                to.bands[b].hz.store (std::clamp (finite (band.hz, 1000.0f), 10.0f, 24000.0f), kRelaxed);
                to.bands[b].db.store (std::clamp (finite (band.db), -48.0f, 48.0f), kRelaxed);
                to.bands[b].q.store (std::clamp (finite (band.q, 0.71f), 0.1f, 20.0f), kRelaxed);
                storeText (to.bands[b].name, band.name);
            }
            to.bandCount.store (static_cast<std::uint32_t> (bands), kRelaxed);
        }
        s.moduleCount.store (static_cast<std::uint32_t> (mods), kRelaxed);
        s.heartbeatMs.store (now, kRelaxed);
        endWrite (s.seq);
        return true;
    }

    bool Link::watched (std::uint64_t now) const { return alive (block.viewerMs.load (kAcquire), now, kViewerMs); }

    void Link::view (std::uint64_t now) { block.viewerMs.store (now, kRelease); }

    std::vector<Unit> Link::units (std::uint64_t now) const
    {
        std::vector<Unit> out;
        for (std::size_t i = 0; i < block.slots.size(); ++i)
        {
            const auto& s = block.slots[i];
            if (s.state.load (kAcquire) != 1u || ! alive (s.heartbeatMs.load (kRelaxed), now)) continue;
            Unit u;
            const bool ok = readConsistent (s.seq, [&] {
                u = Unit {};
                u.version = s.version.load (kRelaxed);
                if (u.version == 0) return;
                u.product = static_cast<Product> (s.product.load (kRelaxed));
                u.role = static_cast<Role> (s.role.load (kRelaxed));
                u.hash = s.hash.load (kRelaxed);
                u.on = s.on.load (kRelaxed) != 0;
                u.detailed = s.detailed.load (kRelaxed) != 0;
                u.inDb = s.inDb.load (kRelaxed);
                u.outDb = s.outDb.load (kRelaxed);
                u.latencyMs = s.latencyMs.load (kRelaxed);
                u.track = loadText (s.track);
                const auto lines = std::min<std::uint32_t> (s.lineCount.load (kRelaxed), kMaxLines);
                for (std::uint32_t l = 0; l < lines; ++l)
                    u.lines.push_back ({ static_cast<Level> (s.lines[l].level.load (kRelaxed)), loadText (s.lines[l].text) });
                if (s.spectrumBins.load (kRelaxed) == static_cast<std::uint32_t> (kSpectrumBins))
                {
                    u.spectrum.resize (kSpectrumBins);
                    for (std::size_t b = 0; b < static_cast<std::size_t> (kSpectrumBins); ++b) u.spectrum[b] = s.spectrum[b].load (kRelaxed);
                }
                const auto mods = std::min<std::uint32_t> (s.moduleCount.load (kRelaxed), kMaxModules);
                for (std::uint32_t m = 0; m < mods; ++m)
                {
                    const auto& from = s.modules[m];
                    Module mod;
                    mod.key = loadText (from.key);
                    mod.summary = loadText (from.summary);
                    mod.by = loadText (from.by);
                    mod.state = static_cast<State> (from.state.load (kRelaxed));
                    mod.meterKind = static_cast<MeterKind> (from.meterKind.load (kRelaxed));
                    mod.meter = from.meter.load (kRelaxed);
                    mod.layers = from.layers.load (kRelaxed);
                    const auto values = std::min<std::uint32_t> (from.valueCount.load (kRelaxed), kMaxValues);
                    for (std::uint32_t v = 0; v < values; ++v)
                        mod.values.push_back ({ loadText (from.values[v].name), loadText (from.values[v].text),
                                                from.values[v].norm.load (kRelaxed), from.values[v].bi.load (kRelaxed) != 0 });
                    const auto bands = std::min<std::uint32_t> (from.bandCount.load (kRelaxed), kMaxBands);
                    for (std::uint32_t b = 0; b < bands; ++b)
                    {
                        Band band;
                        band.type = static_cast<BandType> (from.bands[b].type.load (kRelaxed));
                        band.on = from.bands[b].on.load (kRelaxed) != 0;
                        band.hz = from.bands[b].hz.load (kRelaxed);
                        band.db = from.bands[b].db.load (kRelaxed);
                        band.q = from.bands[b].q.load (kRelaxed);
                        band.name = loadText (from.bands[b].name);
                        mod.bands.push_back (std::move (band));
                    }
                    u.modules.push_back (std::move (mod));
                }
            });
            if (! ok || u.version == 0 || u.product == Product::none) continue;
            u.slot = static_cast<int> (i);
            u.instanceId = s.instanceId.load (kRelaxed);
            out.push_back (std::move (u));
        }
        return out;
    }

    // ---- one plug-in's publisher -------------------------------------------------------------------------------

    namespace
    {
        std::uint64_t newInstanceId (const void* self)
        {
            static std::atomic<std::uint64_t> counter { 0 };
            const auto mixed = (static_cast<std::uint64_t> (reinterpret_cast<std::uintptr_t> (self)) << 16)
                               ^ (suite::wallMs() << 20) ^ (counter.fetch_add (1) + 1) * 0x9E3779B97F4A7C15ull;
            return mixed != 0 ? mixed : 1;
        }
    }

    Publisher::Publisher (Link& l) : link (l), id (newInstanceId (this)) {}

    Publisher::~Publisher()
    {
        if (slotIndex >= 0) link.release (slotIndex, id);
    }

    Publisher::Due Publisher::due (std::uint64_t now)
    {
        pending = Due::none;
        switch (presence.update (now))
        {
            case suite::Presence::Step::away:
                return Due::none;
            case suite::Presence::Step::leave:
                if (slotIndex >= 0) link.release (slotIndex, id);
                slotIndex = -1;
                return Due::none;
            case suite::Presence::Step::join:
            case suite::Presence::Step::here:
                break;
        }
        const bool claimed = slotIndex < 0;
        if (claimed)
        {
            slotIndex = link.claim (id, now);
            if (slotIndex < 0) return Due::none;   // full: try again next time
        }
        const bool full = link.watched (now);
        const std::uint64_t every = full ? kFullMs : kBriefMs;
        if (! claimed && ! (full && ! lastWasFull) && now - std::min (now, lastMs) < every) return Due::none;
        pending = full ? Due::full : Due::brief;
        return pending;
    }

    void Publisher::publish (Unit u, std::uint64_t now)
    {
        if (slotIndex < 0 || pending == Due::none) return;
        u.detailed = pending == Due::full;
        if (! link.publish (slotIndex, id, u, now))
        {
            slotIndex = -1;   // taken over (we were silent too long): claim again next time
            return;
        }
        lastMs = now;
        lastWasFull = u.detailed;
        pending = Due::none;
    }

    // ---- what they do together ---------------------------------------------------------------------------------

    const char* productKey (Product p)
    {
        switch (p)
        {
            case Product::voxology:    return "voxology";
            case Product::honeytune:   return "honeytune";
            case Product::link:        return "link";
            case Product::mixology:    return "mixology";
            case Product::masterology: return "masterology";
            case Product::none:        break;
        }
        return "none";
    }

    namespace
    {
        const char* productName (Product p)
        {
            switch (p)
            {
                case Product::voxology:    return "Voxology";
                case Product::honeytune:   return "Honey Tune";
                case Product::link:        return "Mixology Link";
                case Product::mixology:    return "Mixology";
                case Product::masterology: return "Masterology";
                case Product::none:        break;
            }
            return "plug-in";
        }

        const Module* findModule (const Unit* u, const char* key)
        {
            if (u == nullptr) return nullptr;
            for (const auto& m : u->modules)
                if (m.key == key) return &m;
            return nullptr;
        }

        bool isOn (const Module* m) { return m != nullptr && (m->state == State::on || m->state == State::idle); }

        std::string layerNames (std::uint32_t layers)
        {
            static const char* names[] = { "Reverb", "Delay", "Chorus", "Phaser", "Hall", "Room", "Special FX" };
            std::string s;
            for (int i = 0; i < 7; ++i)
                if (layers & (1u << i)) s += (s.empty() ? "" : " + ") + std::string (names[i]);
            return s;
        }

        constexpr std::uint32_t kReverbLayers = SpaceLayer::reverb | SpaceLayer::hall | SpaceLayer::room | SpaceLayer::special;
    }

    std::vector<ProjectLine> together (const std::vector<Unit>& units)
    {
        std::vector<ProjectLine> out;
        auto add = [&] (ProjectLine::Scope scope, std::uint32_t hash, Level level, std::string text) {
            out.push_back ({ scope, hash, level, std::move (text) });
        };
        const auto T = ProjectLine::Scope::track;

        bool anyMixology = false, anyLink = false, fullMixMixology = false;
        int masterologys = 0, fullMixes = 0;
        for (const auto& u : units)
        {
            if (u.product == Product::link) anyLink = true;
            if (u.product == Product::mixology) anyMixology = true;
            if (u.product == Product::mixology && u.role == Role::fullmix) { fullMixMixology = true; ++fullMixes; }
            if (u.product == Product::masterology) ++masterologys;
        }

        // ---- per track ----
        std::map<std::uint32_t, std::vector<const Unit*>> byTrack;
        for (const auto& u : units)
            if (u.role != Role::fullmix && u.role != Role::master && u.hash != 0) byTrack[u.hash].push_back (&u);

        for (const auto& [hash, list] : byTrack)
        {
            const std::string name = "'" + list.front()->track + "'";
            std::map<Product, int> count;
            const Unit *vox = nullptr, *link = nullptr, *ht = nullptr;
            for (const auto* u : list)
            {
                ++count[u->product];
                if (u->product == Product::voxology && vox == nullptr) vox = u;
                if (u->product == Product::link && link == nullptr) link = u;
                if (u->product == Product::honeytune && ht == nullptr) ht = u;
            }
            bool shared = false;
            for (const auto& [product, n] : count)
                if (n > 1 && product != Product::honeytune)   // Honey Tune: one per clip is fine
                {
                    shared = true;
                    add (T, hash, Level::warn, std::to_string (n) + " tracks are named " + name + " (each with " + productName (product)
                         + "): the Rack and Mixology can't tell them apart. Give each track its own name.");
                }
            if (shared) continue;   // what follows could mix up two tracks

            if (vox != nullptr && vox->role == Role::beat)
                add (T, hash, Level::ok, "Voxology in Beat mode: it listens for the song's key and shares it with your vocals and Honey Tune.");

            if (vox != nullptr && vox->role == Role::vocal && vox->on)
            {
                if (link == nullptr && (anyLink || anyMixology))
                    add (T, hash, Level::warn, "Mixology can't hear " + name + ": there is no Mixology Link on it. Insert Mixology Link after Voxology.");
                if (link != nullptr && link->on)
                {
                    const auto* vComp = findModule (vox, "vox.comp");
                    const auto* lComp = findModule (link, "mix.comp");
                    if (isOn (vComp) && isOn (lComp))
                        add (T, hash, Level::warn, "Two compressors on this vocal: Voxology's and the Link's. Mixology adds none where Voxology compresses; switch one off unless you want both.");
                    else if (isOn (vComp))
                        add (T, hash, Level::ok, "Voxology compresses this vocal, so Mixology's Link adds no second compressor.");

                    const auto* space = findModule (link, "mix.space");
                    const std::uint32_t layers = isOn (space) ? space->layers : 0u;
                    const bool vRev = isOn (findModule (vox, "vox.reverb")), vDly = isOn (findModule (vox, "vox.delay"));
                    if (vRev && (layers & kReverbLayers) != 0)
                        add (T, hash, Level::warn, "Two reverbs on this vocal: Voxology's Reverb and the Link's Space (" + layerNames (layers & kReverbLayers)
                             + "). Stacked spaces can wash it out: turn one down or off.");
                    if (vDly && (layers & SpaceLayer::delay) != 0)
                        add (T, hash, Level::warn, "Two delays on this vocal: Voxology's Delay and the Link's Space. Their echoes can clash: keep one.");
                    if ((vRev || vDly) && layers == 0)
                        add (T, hash, Level::ok, std::string ("Voxology adds the ") + (vRev && vDly ? "reverb and delay" : vRev ? "reverb" : "delay")
                             + " here; Mixology adds none on top.");
                }
            }

            if (ht != nullptr && ht->on && vox != nullptr && vox->on)
            {
                const auto* pitch = findModule (vox, "vox.pitch");
                if (pitch != nullptr && pitch->state == State::rest)
                    add (T, hash, Level::ok, "Voxology's Pitch rests while Honey Tune tunes this track: one tuner at a time.");
                else if (isOn (pitch))
                    add (T, hash, Level::warn, "Two tuners on this track: Honey Tune and Voxology's Pitch (set to tune here anyway). Notes outside Voxology's key get pulled away from where Honey Tune put them.");
            }
        }

        // ---- the Full Mix and the Master ----
        if (fullMixes > 1)
            add (ProjectLine::Scope::fullmix, 0, Level::warn, std::to_string (fullMixes) + " Mixologys sit on the master channel: one is enough (its tools and Make Room work on the first).");
        if (masterologys > 1)
            add (ProjectLine::Scope::master, 0, Level::warn, std::to_string (masterologys) + " Masterologys are in this project: one on the master channel is enough (two limit twice).");
        if (masterologys > 0)
            for (const auto& u : units)
                if (u.product == Product::mixology && u.role == Role::fullmix && isOn (findModule (&u, "mix.limit")))
                {
                    add (ProjectLine::Scope::fullmix, 0, Level::warn, "Mixology's Limiter is on the Full Mix: leave the final limiting to Masterology (it measures true peaks the way streaming does).");
                    break;
                }
        if (masterologys > 0 && ! fullMixMixology)
            add (ProjectLine::Scope::master, 0, Level::info, "No Mixology on the master channel: Masterology starts from its own listening (Mixology's hand-off would tell it the style, peaks and tone).");
        if (anyLink && ! anyMixology)
            add (ProjectLine::Scope::project, 0, Level::info, "Mixology Links are in the project but no Mixology: insert Mixology on the master channel to use its tools.");
        return out;
    }

    // ---- JSON ----------------------------------------------------------------------------------------------------

    namespace
    {
        void str (std::string& o, const std::string& s)
        {
            o.push_back ('"');
            for (unsigned char c : s)
            {
                if (c == '"') o += "\\\"";
                else if (c == '\\') o += "\\\\";
                else if (c == '\n') o += "\\n";
                else if (c == '\r') o += "\\r";
                else if (c == '\t') o += "\\t";
                else if (c < 0x20)
                {
                    static const char* hex = "0123456789abcdef";
                    o += "\\u00";
                    o.push_back (hex[c >> 4]);
                    o.push_back (hex[c & 15]);
                }
                else o.push_back (static_cast<char> (c));
            }
            o.push_back ('"');
        }

        // Fixed decimals without printf (whose decimal point follows the host's locale).
        void num (std::string& o, double v, int decimals = 2)
        {
            if (! std::isfinite (v)) v = 0.0;
            v = std::clamp (v, -1.0e9, 1.0e9);
            long long scale = 1;
            for (int i = 0; i < decimals; ++i) scale *= 10;
            const long long n = std::llround (v * static_cast<double> (scale));
            if (n < 0) o.push_back ('-');
            const unsigned long long a = static_cast<unsigned long long> (n < 0 ? -n : n);
            o += std::to_string (a / static_cast<unsigned long long> (scale));
            if (decimals > 0)
            {
                std::string frac = std::to_string (a % static_cast<unsigned long long> (scale));
                o.push_back ('.');
                o += std::string (static_cast<std::size_t> (decimals) - std::min (frac.size(), static_cast<std::size_t> (decimals)), '0') + frac;
            }
        }

        void key (std::string& o, const char* k, bool& first)
        {
            if (! first) o.push_back (',');
            first = false;
            str (o, k);
            o.push_back (':');
        }

        const char* stateKey (State s)
        {
            switch (s)
            {
                case State::on:   return "on";
                case State::idle: return "idle";
                case State::rest: return "rest";
                case State::off:  break;
            }
            return "off";
        }
        const char* roleKey (Role r)
        {
            switch (r)
            {
                case Role::vocal:   return "vocal";
                case Role::beat:    return "beat";
                case Role::fullmix: return "fullmix";
                case Role::master:  return "master";
                case Role::track:   break;
            }
            return "track";
        }
        const char* levelKey (Level l) { return l == Level::warn ? "warn" : l == Level::info ? "info" : "ok"; }
        const char* bandKey (BandType t)
        {
            switch (t)
            {
                case BandType::lowShelf:  return "lowShelf";
                case BandType::highShelf: return "highShelf";
                case BandType::lowCut:    return "lowCut";
                case BandType::highCut:   return "highCut";
                case BandType::bell:      break;
            }
            return "bell";
        }

        std::string versionText (std::uint32_t v)
        {
            return std::to_string (v >> 16) + "." + std::to_string ((v >> 8) & 0xFF) + "." + std::to_string (v & 0xFF);
        }

        void lines (std::string& o, const std::vector<Line>& ls)
        {
            o.push_back ('[');
            for (std::size_t i = 0; i < ls.size(); ++i)
            {
                if (i) o.push_back (',');
                o += "{\"level\":";
                str (o, levelKey (ls[i].level));
                o += ",\"text\":";
                str (o, ls[i].text);
                o.push_back ('}');
            }
            o.push_back (']');
        }
    }

    std::string toJson (const std::string& host, std::uint32_t hostHash, const std::vector<Unit>& units,
                        const std::vector<ProjectLine>& projectLines, const Overview& ov)
    {
        std::string o;
        o.reserve (8192 + units.size() * 4096);
        o += "{\"host\":";
        str (o, host);
        o += ",\"hostHash\":" + std::to_string (hostHash);
        o += ",\"units\":[";
        for (std::size_t i = 0; i < units.size(); ++i)
        {
            const auto& u = units[i];
            if (i) o.push_back (',');
            bool f = true;
            o.push_back ('{');
            key (o, "id", f); str (o, "u" + std::to_string (u.slot >= 0 ? u.slot : static_cast<int> (i)));
            key (o, "product", f); str (o, productKey (u.product));
            key (o, "version", f); str (o, versionText (u.version));
            key (o, "track", f); str (o, u.track);
            key (o, "hash", f); o += std::to_string (u.hash);
            key (o, "role", f); str (o, roleKey (u.role));
            key (o, "on", f); o += u.on ? "true" : "false";
            key (o, "detailed", f); o += u.detailed ? "true" : "false";
            key (o, "inDb", f); num (o, u.inDb, 1);
            key (o, "outDb", f); num (o, u.outDb, 1);
            key (o, "latencyMs", f); num (o, u.latencyMs, 1);

            // The spectrum, as the EQ graph wants it: its loudest bin at +3 dB on the graph's scale.
            std::string spectrum;
            if (u.spectrum.size() == static_cast<std::size_t> (kSpectrumBins))
            {
                float top = -1000.0f;
                for (float v : u.spectrum) if (std::isfinite (v)) top = std::max (top, v);
                spectrum.push_back ('[');
                for (std::size_t b = 0; b < u.spectrum.size(); ++b)
                {
                    if (b) spectrum.push_back (',');
                    num (spectrum, std::isfinite (u.spectrum[b]) ? std::max (-60.0f, u.spectrum[b] - top + 3.0f) : -60.0f, 1);
                }
                spectrum.push_back (']');
            }

            key (o, "modules", f);
            o.push_back ('[');
            for (std::size_t m = 0; m < u.modules.size(); ++m)
            {
                const auto& mod = u.modules[m];
                if (m) o.push_back (',');
                bool g = true;
                o.push_back ('{');
                key (o, "key", g); str (o, mod.key);
                key (o, "state", g); str (o, stateKey (mod.state));
                key (o, "summary", g); str (o, mod.summary);
                key (o, "by", g); str (o, mod.by);
                key (o, "meterKind", g); str (o, mod.meterKind == MeterKind::gr ? "gr" : mod.meterKind == MeterKind::level ? "level" : "");
                key (o, "meter", g); num (o, mod.meter, 1);
                key (o, "values", g);
                o.push_back ('[');
                for (std::size_t v = 0; v < mod.values.size(); ++v)
                {
                    const auto& val = mod.values[v];
                    if (v) o.push_back (',');
                    o += "{\"name\":"; str (o, val.name);
                    o += ",\"text\":"; str (o, val.text);
                    o += ",\"norm\":"; num (o, std::clamp (val.norm, 0.0f, 1.0f), 3);
                    o += ",\"bi\":"; o += val.bi ? "true" : "false";
                    o.push_back ('}');
                }
                o.push_back (']');
                key (o, "bands", g);
                o.push_back ('[');
                for (std::size_t b = 0; b < mod.bands.size(); ++b)
                {
                    const auto& band = mod.bands[b];
                    if (b) o.push_back (',');
                    o += "{\"type\":"; str (o, bandKey (band.type));
                    o += ",\"hz\":"; num (o, band.hz, 1);
                    o += ",\"db\":"; num (o, band.db, 2);
                    o += ",\"q\":"; num (o, band.q, 3);
                    o += ",\"name\":"; str (o, band.name);
                    o += ",\"on\":"; o += band.on ? "true" : "false";
                    o.push_back ('}');
                }
                o.push_back (']');
                key (o, "layers", g);
                {
                    static const char* names[] = { "reverb", "delay", "chorus", "phaser", "hall", "room", "special" };
                    o.push_back ('[');
                    bool first = true;
                    for (int l = 0; l < 7; ++l)
                        if (mod.layers & (1u << l)) { if (! first) o.push_back (','); first = false; str (o, names[l]); }
                    o.push_back (']');
                }
                if (! mod.bands.empty() && ! spectrum.empty()) { key (o, "spectrum", g); o += spectrum; }
                o.push_back ('}');
            }
            o.push_back (']');
            key (o, "together", f); lines (o, u.lines);
            o.push_back ('}');
        }
        o += "],\"together\":[";
        for (std::size_t i = 0; i < projectLines.size(); ++i)
        {
            const auto& l = projectLines[i];
            if (i) o.push_back (',');
            o.push_back ('{');
            bool f = true;
            if (l.scope == ProjectLine::Scope::track) { key (o, "hash", f); o += std::to_string (l.hash); }
            else if (l.scope == ProjectLine::Scope::fullmix) { key (o, "hash", f); str (o, "fullmix"); }
            else if (l.scope == ProjectLine::Scope::master) { key (o, "hash", f); str (o, "master"); }
            key (o, "level", f); str (o, levelKey (l.level));
            key (o, "text", f); str (o, l.text);
            o.push_back ('}');
        }
        o += "],\"mix\":{";
        if (ov.mixKnown)
        {
            o += "\"lufsI\":"; num (o, ov.lufsI, 1);
            o += ",\"lufsS\":"; num (o, ov.lufsS, 1);
            o += ",\"tp\":"; num (o, ov.truePeak, 1);
            o += ",\"source\":"; str (o, ov.source);
        }
        o += "},\"master\":{";
        if (ov.masterKnown)
        {
            o += "\"goal\":"; str (o, ov.goal);
            o += ",\"lufs\":"; num (o, ov.targetLufs, 1);
            o += ",\"ceiling\":"; num (o, ov.ceilingDbtp, 1);
        }
        o += "}}";
        return o;
    }

    // ---- the Rack view's plug-in ----------------------------------------------------------------------------------

    Overview overview (const suite::Link& suiteLink, std::uint64_t now)
    {
        Overview ov;
        const auto masters = suiteLink.masters (now);
        suite::MixInfo mix;
        if (suiteLink.readMix (mix, now) && (mix.integratedLufs > -99.0f || mix.shortTermLufs > -99.0f))
        {
            ov.mixKnown = true;
            ov.lufsI = mix.integratedLufs;
            ov.lufsS = mix.shortTermLufs;
            ov.truePeak = mix.peakDbtp;
            ov.source = "the mix";
        }
        else if (! masters.empty() && masters.front().inputLufs > -99.0f)
        {
            ov.mixKnown = true;
            ov.lufsI = masters.front().inputLufs;
            ov.truePeak = masters.front().inputPeakDbtp;
            ov.source = "into Masterology";
        }
        if (! masters.empty())
        {
            ov.masterKnown = true;
            ov.goal = masters.front().goalName;
            ov.targetLufs = masters.front().targetLufs;
            ov.ceilingDbtp = masters.front().ceilingDbtp;
        }
        return ov;
    }

    std::string viewJson (Link& rackLink, const suite::Link& suiteLink, const std::string& host, std::uint32_t hostHash, std::uint64_t now,
                          const std::string& detailTrack)
    {
        rackLink.view (now);
        auto units = rackLink.units (now);
        const auto lines = together (units);
        if (! detailTrack.empty())
        {
            // The other tracks' units go brief (keys, states, summaries): a tenth of the text for a big project.
            std::uint32_t hash = 0;
            const bool isHash = detailTrack.size() > 1 && detailTrack[0] == 't';
            if (isHash)
                for (std::size_t i = 1; i < detailTrack.size() && detailTrack[i] >= '0' && detailTrack[i] <= '9'; ++i)
                    hash = hash * 10u + static_cast<std::uint32_t> (detailTrack[i] - '0');
            for (auto& u : units)
            {
                const bool shown = detailTrack == "fullmix" ? u.role == Role::fullmix
                                 : detailTrack == "master"  ? u.role == Role::master
                                 : isHash && u.role != Role::fullmix && u.role != Role::master && u.hash == hash;
                if (shown || ! u.detailed) continue;
                u.detailed = false;
                u.spectrum.clear();
                for (auto& m : u.modules)
                {
                    m.values.clear();
                    m.bands.clear();
                }
            }
        }
        return toJson (host, hostHash, units, lines, overview (suiteLink, now));
    }
}

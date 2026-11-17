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

        std::string blockName (std::uint32_t layout)
        {
#if defined(_WIN32)
            const auto pid = static_cast<unsigned long> (GetCurrentProcessId());
#else
            const auto pid = static_cast<unsigned long> (getpid());
#endif
            return "SonicologyRack.v" + std::to_string (layout) + "." + std::to_string (pid);
        }

        std::string processBlockName() { return blockName (kLayout); }

        // Earlier layouts: where their slots start and how long one is (state at +0, heartbeat at +16 in each), so a
        // block left from a plug-in that has gone (macOS / Linux keep it until the next restart) isn't taken for one
        // that is here. Add this layout's when bumping kLayout: v3 slots at 128, 23936 bytes each.
        struct EarlierLayout { std::uint32_t layout; std::size_t slotsAt, slotBytes; };
        constexpr EarlierLayout kEarlier[] = { { 1, 64, 16640 }, { 2, 64, 23936 } };

        bool layoutAlive (std::uint32_t layout, std::uint64_t now)
        {
#if defined(_WIN32)
            // Windows drops a block with the last plug-in that had it open: there is one only while one is here.
            (void) now;
            const std::string osName = "Local\\" + blockName (layout);
            HANDLE h = OpenFileMappingA (FILE_MAP_READ, FALSE, osName.c_str());
            if (h == nullptr) return false;
            CloseHandle (h);
            return true;
#else
            const EarlierLayout* known = nullptr;
            for (const auto& e : kEarlier)
                if (e.layout == layout) known = &e;
            if (known == nullptr) return false;
            const std::string osName = "/" + blockName (layout);
            const int fd = shm_open (osName.c_str(), O_RDONLY, 0);
            if (fd < 0) return false;
            const std::size_t bytes = known->slotsAt + known->slotBytes * static_cast<std::size_t> (kMaxSlots);
            struct stat st {};
            void* p = (fstat (fd, &st) == 0 && static_cast<std::size_t> (st.st_size) >= bytes)
                          ? mmap (nullptr, bytes, PROT_READ, MAP_SHARED, fd, 0) : MAP_FAILED;
            close (fd);
            if (p == MAP_FAILED) return false;
            bool any = false;
            for (int i = 0; i < kMaxSlots && ! any; ++i)
            {
                const auto* slot = static_cast<const unsigned char*> (p) + known->slotsAt + known->slotBytes * static_cast<std::size_t> (i);
                std::uint32_t state = 0;
                std::uint64_t beat = 0;
                std::memcpy (&state, slot, sizeof state);
                std::memcpy (&beat, slot + 16, sizeof beat);
                any = state == 1u && alive (beat, now);
            }
            munmap (p, bytes);
            return any;
#endif
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

    std::vector<std::uint32_t> Link::otherLayouts (std::uint64_t now)
    {
        static std::uint64_t lastMs = 0;
        static std::vector<std::uint32_t> found;
        if (lastMs != 0 && now - std::min (now, lastMs) < 1000) return found;
        lastMs = now;
        found.clear();
        for (std::uint32_t layout = 1; layout <= kLayout + 5; ++layout)
            if (layout != kLayout && layoutAlive (layout, now)) found.push_back (layout);
        return found;
    }

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
            // Requests for the slot's earlier owner aren't ours: start after the last one handed out (before the new
            // generation shows, so a sender that sees it finds the queue ready).
            const auto issued = s.queue.issued.load (kAcquire);
            s.queue.taken.store (issued, kRelaxed);
            s.queue.answered.store (issued, kRelaxed);
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
        storeText (s.lock, u.lock);

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
            to.switchable.store (from.switchable ? 1u : 0u, kRelaxed);
            const auto values = std::min<std::size_t> (from.values.size(), kMaxValues);
            for (std::size_t v = 0; v < values; ++v)
            {
                storeText (to.values[v].name, from.values[v].name);
                storeText (to.values[v].text, from.values[v].text);
                to.values[v].norm.store (std::clamp (finite (from.values[v].norm, 0.5f), 0.0f, 1.0f), kRelaxed);
                to.values[v].bi.store (from.values[v].bi ? 1u : 0u, kRelaxed);
                to.values[v].steps.store (static_cast<std::uint32_t> (from.values[v].steps >= 2 ? std::min (from.values[v].steps, kMaxSteps) : 0), kRelaxed);
                storeText (to.values[v].key, from.values[v].key);
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

    void Link::view (std::uint64_t now, const std::string& track)
    {
        block.viewerMs.store (now, kRelease);
        std::uint32_t kind = 0, hash = 0;
        if (track == "fullmix") kind = 2;
        else if (track == "master") kind = 3;
        else if (track.size() > 1 && track[0] == 't')
        {
            kind = 1;
            for (std::size_t i = 1; i < track.size() && track[i] >= '0' && track[i] <= '9'; ++i)
                hash = hash * 10u + static_cast<std::uint32_t> (track[i] - '0');
        }
        // Its own place (same track), else the one looked at longest ago. Two views writing one place at once (rare)
        // can only make a track look shown or not for a moment.
        auto* place = &block.viewers[0];
        for (auto& v : block.viewers)
        {
            if (v.ms.load (kAcquire) != 0 && v.kind.load (kRelaxed) == kind && v.hash.load (kRelaxed) == hash) { place = &v; break; }
            if (v.ms.load (kRelaxed) < place->ms.load (kRelaxed)) place = &v;
        }
        place->kind.store (kind, kRelaxed);
        place->hash.store (hash, kRelaxed);
        place->ms.store (now, kRelease);
    }

    bool Link::shows (std::uint64_t now, std::uint32_t hash, Role role) const
    {
        for (const auto& v : block.viewers)
        {
            if (! alive (v.ms.load (kAcquire), now, kViewerMs)) continue;
            const auto kind = v.kind.load (kRelaxed);
            if (kind == 0) return true;
            if (role == Role::fullmix) { if (kind == 2) return true; continue; }
            if (role == Role::master) { if (kind == 3) return true; continue; }
            if (kind == 1 && hash != 0 && v.hash.load (kRelaxed) == hash) return true;
        }
        return false;
    }

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
                u.lock = loadText (s.lock);
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
                    mod.switchable = from.switchable.load (kRelaxed) != 0;
                    const auto values = std::min<std::uint32_t> (from.valueCount.load (kRelaxed), kMaxValues);
                    for (std::uint32_t v = 0; v < values; ++v)
                        mod.values.push_back ({ loadText (from.values[v].name), loadText (from.values[v].text),
                                                from.values[v].norm.load (kRelaxed), from.values[v].bi.load (kRelaxed) != 0,
                                                loadText (from.values[v].key),
                                                static_cast<int> (std::min<std::uint32_t> (from.values[v].steps.load (kRelaxed), kMaxSteps)) });
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
            u.generation = s.generation.load (kAcquire);
            u.instanceId = s.instanceId.load (kRelaxed);
            out.push_back (std::move (u));
        }
        return out;
    }

    // ---- requests (stage 2) ------------------------------------------------------------------------------------

    namespace
    {
        const char* const kGoneWhy = "That plug-in has closed or moved. The Rack catches up in a moment.";
        const char* const kBusyWhy = "That plug-in is busy. Try again in a moment.";

        float requestValue (float v) { return std::clamp (finite (v), 0.0f, 1.0f); }
    }

    std::uint64_t Link::request (int slot, std::uint32_t generation, const Request& r, std::uint64_t now, std::string& why)
    {
        why.clear();
        if (slot < 0 || slot >= kMaxSlots) { why = kGoneWhy; return 0; }
        auto& s = block.slots[static_cast<std::size_t> (slot)];
        if (s.state.load (kAcquire) != 1u || s.generation.load (kAcquire) != generation || ! alive (s.heartbeatMs.load (kRelaxed), now))
        {
            why = kGoneWhy;
            return 0;
        }
        std::uint32_t version = 0;
        std::string lock;
        if (! readConsistent (s.seq, [&] { version = s.version.load (kRelaxed); lock = loadText (s.lock); })) { why = kBusyWhy; return 0; }
        if (version == 0) { why = kGoneWhy; return 0; }
        if (! lock.empty()) { why = lock; return 0; }

        // A ticket: only while the owner has room for it (it hasn't taken the request kMaxRequests before it).
        auto& q = s.queue;
        std::uint64_t ticket = 0;
        for (auto last = q.issued.load (kAcquire);;)
        {
            if (last + 1 - std::min (last + 1, q.taken.load (kAcquire)) > static_cast<std::uint64_t> (kMaxRequests)) { why = kBusyWhy; return 0; }
            if (q.issued.compare_exchange_weak (last, last + 1, std::memory_order_acq_rel)) { ticket = last + 1; break; }
        }

        // Its place in the ring, held by one sender at a time: odd while written. Still odd from an earlier sender that
        // stalled half-way (the owner skipped it): this one gives up, nothing is mixed. The owner skips the ticket.
        auto& e = q.ring[static_cast<std::size_t> (ticket % static_cast<std::uint64_t> (kMaxRequests))];
        const std::uint64_t writing = 2 * ticket - 1, written = 2 * ticket;
        for (auto old = e.stamp.load (kAcquire);;)
        {
            if ((old & 1u) != 0 || old >= writing) { why = kBusyWhy; return 0; }
            if (e.stamp.compare_exchange_weak (old, writing, std::memory_order_acq_rel)) break;
        }
        std::atomic_thread_fence (kRelease);
        e.generation.store (generation, kRelaxed);
        e.change.store (static_cast<std::uint32_t> (r.change), kRelaxed);
        e.gesture.store (static_cast<std::uint32_t> (r.gesture), kRelaxed);
        e.value.store (requestValue (r.value), kRelaxed);
        storeText (e.module, r.module);
        storeText (e.control, r.control);
        e.stamp.store (written, kRelease);
        return ticket;
    }

    Answer Link::answer (int slot, std::uint32_t generation, std::uint64_t ticket) const
    {
        if (slot < 0 || slot >= kMaxSlots || ticket == 0) return {};
        const auto& s = block.slots[static_cast<std::size_t> (slot)];
        if (s.state.load (kAcquire) != 1u || s.generation.load (kAcquire) != generation) return {};
        const auto& q = s.queue;
        if (ticket > q.answered.load (kAcquire)) return { ticket <= q.issued.load (kAcquire) ? Outcome::pending : Outcome::gone, {} };
        const auto& f = q.refusals[static_cast<std::size_t> (ticket % static_cast<std::uint64_t> (kMaxRefusals))];
        std::uint64_t refused = 0;
        std::string why;
        if (readConsistent (f.seq, [&] { refused = f.ticket.load (kRelaxed); why = loadText (f.why); }) && refused == ticket)
            return { Outcome::refused, why };
        return { Outcome::applied, {} };
    }

    int Link::take (int slot, std::uint64_t instanceId, Request* out, int max, std::uint64_t now, std::uint64_t& waitingSince)
    {
        if (slot < 0 || slot >= kMaxSlots || out == nullptr || max <= 0) return 0;
        auto& s = block.slots[static_cast<std::size_t> (slot)];
        if (s.instanceId.load (kRelaxed) != instanceId) return 0;
        auto& q = s.queue;
        const auto generation = s.generation.load (kAcquire);
        const auto issued = q.issued.load (kAcquire);
        auto taken = q.taken.load (kRelaxed);   // only the owner writes it
        int n = 0;
        while (taken < issued && n < max)
        {
            const auto ticket = taken + 1;
            const auto& e = q.ring[static_cast<std::size_t> (ticket % static_cast<std::uint64_t> (kMaxRequests))];
            Request r;
            std::uint32_t forGeneration = 0;
            bool ready = e.stamp.load (kAcquire) == 2 * ticket;
            if (ready)
            {
                forGeneration = e.generation.load (kRelaxed);
                r.change = static_cast<Change> (e.change.load (kRelaxed));
                r.gesture = static_cast<Gesture> (e.gesture.load (kRelaxed));
                r.value = requestValue (e.value.load (kRelaxed));
                r.module = loadText (e.module);
                r.control = loadText (e.control);
                std::atomic_thread_fence (kAcquire);
                ready = e.stamp.load (kRelaxed) == 2 * ticket;
            }
            if (! ready)
            {
                // Not written yet: its sender is between getting the ticket and writing it (microseconds), or stopped.
                if (waitingSince == 0) waitingSince = now + 1;
                if (now + 1 - std::min (now + 1, waitingSince) < kStuckMs) break;
                forGeneration = generation + 1;   // skip it
            }
            waitingSince = 0;
            taken = ticket;
            q.taken.store (taken, kRelease);
            if (forGeneration != generation) continue;   // for the slot's earlier owner, or skipped
            if (r.change != Change::value && r.change != Change::state && r.change != Change::reset && r.change != Change::power) r.change = Change::value;
            if (static_cast<std::uint32_t> (r.gesture) > static_cast<std::uint32_t> (Gesture::end)) r.gesture = Gesture::set;
            r.ticket = ticket;
            out[n++] = std::move (r);
        }
        return n;
    }

    void Link::refuse (int slot, std::uint64_t instanceId, std::uint64_t ticket, const std::string& why)
    {
        if (slot < 0 || slot >= kMaxSlots || ticket == 0) return;
        auto& s = block.slots[static_cast<std::size_t> (slot)];
        if (s.instanceId.load (kRelaxed) != instanceId) return;
        auto& f = s.queue.refusals[static_cast<std::size_t> (ticket % static_cast<std::uint64_t> (kMaxRefusals))];
        beginWrite (f.seq);
        f.ticket.store (ticket, kRelaxed);
        storeText (f.why, why.empty() ? std::string ("Not done.") : why);
        endWrite (f.seq);
    }

    void Link::answered (int slot, std::uint64_t instanceId, std::uint64_t ticket)
    {
        if (slot < 0 || slot >= kMaxSlots) return;
        auto& s = block.slots[static_cast<std::size_t> (slot)];
        if (s.instanceId.load (kRelaxed) != instanceId) return;
        auto& q = s.queue;
        const auto upTo = std::min (ticket, q.taken.load (kRelaxed));
        if (upTo > q.answered.load (kRelaxed)) q.answered.store (upTo, kRelease);
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

    Publisher::Due Publisher::due (std::uint64_t now) { return dueIf (now, link.watched (now)); }

    Publisher::Due Publisher::due (std::uint64_t now, std::uint32_t hash, Role role) { return dueIf (now, link.shows (now, hash, role)); }

    Publisher::Due Publisher::dueIf (std::uint64_t now, bool full)
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
        const std::uint64_t every = full ? kFullMs : kBriefMs;
        if (! claimed && ! refresh && ! (full && ! lastWasFull) && now - std::min (now, lastMs) < every) return Due::none;
        refresh = false;
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

    int Publisher::serve (std::uint64_t now, const std::string& lock, const Apply& apply)
    {
        if (! apply) return 0;
        const auto close = [&] (const Open& o) {
            Request r;
            r.module = o.module;
            r.control = o.control;
            r.value = o.value;
            r.gesture = Gesture::end;
            apply (r);   // ticket 0: the plug-in ends the host's gesture
        };
        const auto find = [this] (const Request& r) {
            return std::find_if (open.begin(), open.end(), [&] (const Open& o) { return o.module == r.module && o.control == r.control; });
        };

        // Drags that stopped (the view closed or stalled mid-drag), or all of them while locked.
        for (auto it = open.begin(); it != open.end();)
        {
            if (! lock.empty() || now - std::min (now, it->lastMs) >= kGestureMs) { close (*it); it = open.erase (it); }
            else ++it;
        }
        if (slotIndex < 0) return 0;

        std::array<Request, kMaxRequests> batch;
        const int n = link.take (slotIndex, id, batch.data(), kMaxRequests, now, waitingSinceMs);
        int applied = 0;
        for (int i = 0; i < n; ++i)
        {
            auto r = batch[static_cast<std::size_t> (i)];
            if (! lock.empty()) { link.refuse (slotIndex, id, r.ticket, lock); continue; }
            if (r.change == Change::state || r.change == Change::power)
            {
                r.control.clear();
                r.gesture = Gesture::set;
                r.value = r.value >= 0.5f ? 1.0f : 0.0f;
            }
            else if (r.change == Change::reset)
            {
                r.gesture = Gesture::set;
                r.value = 0.0f;
                // a drag of the same knob still open: closed first, so the reset is a gesture of its own
                for (auto it = open.begin(); it != open.end(); ++it)
                    if (it->module == r.module && it->control == r.control) { close (*it); open.erase (it); break; }
            }
            else
            {
                if (r.gesture == Gesture::move)
                {
                    bool later = false;
                    for (int j = i + 1; j < n && ! later; ++j)
                    {
                        const auto& o = batch[static_cast<std::size_t> (j)];
                        later = o.change == Change::value && o.module == r.module && o.control == r.control;
                    }
                    if (later) { ++applied; continue; }   // a newer value of the same knob follows at once
                }
                const bool isOpen = find (r) != open.end();
                if (r.gesture == Gesture::move && ! isOpen) r.gesture = Gesture::begin;
                else if (r.gesture == Gesture::begin && isOpen) r.gesture = Gesture::move;
                else if (r.gesture == Gesture::set && isOpen) r.gesture = Gesture::end;
                else if (r.gesture == Gesture::end && ! isOpen) r.gesture = Gesture::set;
            }

            const auto why = apply (r);
            if (r.change == Change::value)
            {
                auto it = find (r);
                if (r.gesture == Gesture::end) { if (it != open.end()) open.erase (it); }
                else if (why.empty() && r.gesture == Gesture::begin) open.push_back ({ r.module, r.control, r.value, now });
                else if (why.empty() && r.gesture == Gesture::move && it != open.end()) { it->value = r.value; it->lastMs = now; }
            }
            if (! why.empty()) { link.refuse (slotIndex, id, r.ticket, why); continue; }
            ++applied;
        }
        link.answered (slotIndex, id, ~std::uint64_t { 0 });   // every taken ticket, skipped ones too (refusals written)
        if (applied > 0) refresh = true;                       // the Rack sees the new value at the next due()
        return applied;
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
        auto add = [&] (ProjectLine::Scope scope, std::uint32_t hash, Level level, std::string text, std::vector<Fix> fixes = {}) {
            ProjectLine l { scope, hash, level, std::move (text), {} };
            for (auto& f : fixes)
                if (f.slot >= 0 && l.fixes.size() < 2) l.fixes.push_back (std::move (f));
            out.push_back (std::move (l));
        };
        auto off = [] (const Unit* u, const char* module, std::string label) {
            return Fix { u != nullptr ? u->slot : -1, u != nullptr ? u->generation : 0u, module, false, std::move (label) };
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
                        add (T, hash, Level::warn, "Two compressors on this vocal: Voxology's and the Link's. Mixology adds none where Voxology compresses; switch one off unless you want both.",
                             { off (link, "mix.comp", "Turn the Link's Compressor off"), off (vox, "vox.comp", "Turn Voxology's Compressor off") });
                    else if (isOn (vComp))
                        add (T, hash, Level::ok, "Voxology compresses this vocal, so Mixology's Link adds no second compressor.");

                    const auto* space = findModule (link, "mix.space");
                    const std::uint32_t layers = isOn (space) ? space->layers : 0u;
                    const bool vRev = isOn (findModule (vox, "vox.reverb")), vDly = isOn (findModule (vox, "vox.delay"));
                    // Space switches all its effects at once: the button says so when it plays more than the one in question.
                    const auto spaceOff = [&] (std::uint32_t these) {
                        return off (link, "mix.space", (layers & ~these) != 0 ? "Turn the Link's Space off (" + layerNames (layers) + ")"
                                                                              : std::string ("Turn the Link's Space off"));
                    };
                    if (vRev && (layers & kReverbLayers) != 0)
                        add (T, hash, Level::warn, "Two reverbs on this vocal: Voxology's Reverb and the Link's Space (" + layerNames (layers & kReverbLayers)
                             + "). Stacked spaces can wash it out: turn one down or off.",
                             { spaceOff (kReverbLayers), off (vox, "vox.reverb", "Turn Voxology's Reverb off") });
                    if (vDly && (layers & SpaceLayer::delay) != 0)
                        add (T, hash, Level::warn, "Two delays on this vocal: Voxology's Delay and the Link's Space. Their echoes can clash: keep one.",
                             { spaceOff (SpaceLayer::delay), off (vox, "vox.delay", "Turn Voxology's Delay off") });
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
                    add (T, hash, Level::warn, "Two tuners on this track: Honey Tune and Voxology's Pitch (set to tune here anyway). Notes outside Voxology's key get pulled away from where Honey Tune put them.",
                         { off (vox, "vox.pitch", "Turn Voxology's Pitch off") });
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
                    add (ProjectLine::Scope::fullmix, 0, Level::warn, "Mixology's Limiter is on the Full Mix: leave the final limiting to Masterology (it measures true peaks the way streaming does).",
                         { off (&u, "mix.limit", "Turn Mixology's Limiter off") });
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
                        const std::vector<ProjectLine>& projectLines, const Overview& ov, const std::vector<std::uint32_t>& otherLayouts)
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
            key (o, "gen", f); o += std::to_string (u.generation);
            key (o, "lock", f); str (o, u.lock);
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
                key (o, "switchable", g); o += mod.switchable ? "true" : "false";
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
                    o += ",\"key\":"; str (o, val.key);
                    o += ",\"steps\":"; o += std::to_string (val.steps >= 2 ? std::min (val.steps, kMaxSteps) : 0);
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
            if (! l.fixes.empty())
            {
                key (o, "fixes", f);
                o.push_back ('[');
                for (std::size_t k = 0; k < l.fixes.size(); ++k)
                {
                    const auto& x = l.fixes[k];
                    if (k) o.push_back (',');
                    o += "{\"unit\":"; str (o, "u" + std::to_string (x.slot));
                    o += ",\"gen\":" + std::to_string (x.generation);
                    o += ",\"module\":"; str (o, x.module);
                    o += ",\"on\":"; o += x.on ? "true" : "false";
                    o += ",\"label\":"; str (o, x.label);
                    o.push_back ('}');
                }
                o.push_back (']');
            }
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
        o += "},\"otherLayouts\":[";
        for (std::size_t i = 0; i < otherLayouts.size(); ++i) o += (i ? "," : "") + std::to_string (otherLayouts[i]);
        o += "]}";
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
        rackLink.view (now, detailTrack);
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
        return toJson (host, hostHash, units, lines, overview (suiteLink, now), Link::otherLayouts (now));
    }

    // ---- the view's requests (stage 2) ---------------------------------------------------------------------------

    namespace
    {
        int unitSlot (const std::string& unit)
        {
            if (unit.size() < 2 || unit.size() > 5 || unit[0] != 'u') return -1;
            int slot = 0;
            for (std::size_t i = 1; i < unit.size(); ++i)
            {
                if (unit[i] < '0' || unit[i] > '9') return -1;
                slot = slot * 10 + (unit[i] - '0');
            }
            return slot < kMaxSlots ? slot : -1;
        }
    }

    std::string requestJson (Link& link, const std::string& unit, std::uint32_t generation, const std::string& change, const std::string& module,
                             const std::string& control, float value, const std::string& gesture, std::uint64_t now)
    {
        Request r;
        r.change = change == "state" ? Change::state : change == "reset" ? Change::reset : change == "power" ? Change::power : Change::value;
        r.module = r.change == Change::power ? std::string ("power") : module;
        r.control = control;
        r.value = value;
        r.gesture = gesture == "begin" ? Gesture::begin : gesture == "move" ? Gesture::move : gesture == "end" ? Gesture::end : Gesture::set;
        std::string why;
        std::uint64_t ticket = 0;
        if (r.module.empty() || (r.change != Change::state && r.change != Change::power && control.empty()))
            why = "Nothing to change.";
        else
            ticket = link.request (unitSlot (unit), generation, r, now, why);
        std::string o = "{\"ticket\":" + std::to_string (ticket) + ",\"why\":";
        str (o, why);
        o.push_back ('}');
        return o;
    }

    std::string answerJson (const Link& link, const std::string& unit, std::uint32_t generation, std::uint64_t ticket)
    {
        const auto a = link.answer (unitSlot (unit), generation, ticket);
        const char* outcome = a.outcome == Outcome::pending ? "pending" : a.outcome == Outcome::applied ? "applied"
                            : a.outcome == Outcome::refused ? "refused" : "gone";
        std::string o = std::string ("{\"outcome\":\"") + outcome + "\",\"why\":";
        str (o, a.why);
        o.push_back ('}');
        return o;
    }
}

// Suite link: see SuiteLink.h. THIS FILE IS THE SAME IN ALL THREE REPOSITORIES (Voxology, Mixology, Masterology).

#include "suite/SuiteLink.h"

#include <algorithm>
#include <chrono>
#include <cstring>
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

namespace suite
{
    namespace
    {
        constexpr auto kRelaxed = std::memory_order_relaxed;
        constexpr auto kAcquire = std::memory_order_acquire;
        constexpr auto kRelease = std::memory_order_release;

        static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
        static_assert (std::atomic<float>::is_always_lock_free);
        static_assert (std::atomic<char>::is_always_lock_free);

        template <std::size_t N>
        void storeText (Text<N>& to, const std::string& s)
        {
            std::size_t n = std::min (s.size(), N - 1);
            while (n > 0 && n < s.size() && (static_cast<unsigned char> (s[n]) & 0xC0) == 0x80) --n;   // UTF-8 boundary
            for (std::size_t i = 0; i < N; ++i) to[i].store (i < n ? s[i] : '\0', kRelaxed);
        }

        template <std::size_t N>
        std::string loadText (const Text<N>& from)
        {
            std::string s;
            for (std::size_t i = 0; i < N; ++i)
            {
                const char c = from[i].load (kRelaxed);
                if (c == '\0') break;
                s.push_back (c);
            }
            return s;
        }

        // Seqlock: the writer makes the counter odd, writes, makes it even again; a reader that sees it change retries.
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

        bool alive (std::uint64_t beat, std::uint64_t now) { return beat != 0 && now - std::min (now, beat) <= kAliveMs; }

        template <typename Slots>
        int claim (Slots& slots, std::uint64_t instanceId, std::uint64_t now)
        {
            for (std::size_t i = 0; i < slots.size(); ++i)
            {
                auto& s = slots[i];
                std::uint32_t expected = 0;
                if (s.state.compare_exchange_strong (expected, 1u, std::memory_order_acq_rel))
                    s.heartbeatMs.store (now, kRelaxed);
                else
                {
                    // A claimed slot whose owner vanished without releasing it (a crashed plug-in): take it over.
                    // Only one claimer wins the swap of the stale heartbeat.
                    auto beat = s.heartbeatMs.load (kAcquire);
                    if (beat == 0 || alive (beat, now)) continue;
                    if (! s.heartbeatMs.compare_exchange_strong (beat, now, std::memory_order_acq_rel)) continue;
                }
                s.instanceId.store (instanceId, kRelaxed);
                s.generation.fetch_add (1, kRelease);
                return static_cast<int> (i);
            }
            return -1;
        }

        template <typename Slot>
        void release (Slot& s, std::uint64_t instanceId)
        {
            if (s.instanceId.load (kRelaxed) != instanceId) return;
            s.heartbeatMs.store (0, kRelaxed);
            s.instanceId.store (0, kRelaxed);
            s.state.store (0, kRelease);
        }

        // ---- this process's block (one per library, all on the same memory) ----------------------------
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
                // Never unlinked while the process lives (the block is tiny); the name carries the process ID.
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
            return "VoxMixMasterSuite.v" + std::to_string (kLayout) + "." + std::to_string (pid);
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

    std::uint32_t nameHash (const std::string& utf8)
    {
        std::uint32_t h = 2166136261u;
        for (unsigned char c : utf8)
        {
            h ^= c;
            h *= 16777619u;
        }
        return h & 0x0FFFFFFFu;
    }

    std::uint64_t nowMs()
    {
        using namespace std::chrono;
        return static_cast<std::uint64_t> (duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count()) + 1;
    }

    std::uint64_t wallMs()
    {
        using namespace std::chrono;
        return static_cast<std::uint64_t> (duration_cast<milliseconds> (system_clock::now().time_since_epoch()).count());
    }

    Link::Link (void* memory) : block (*static_cast<Block*> (memory))
    {
        block.layout.store (kLayout, kRelaxed);
    }

    Link& Link::process() { return handle().link; }
    bool Link::processShared() { return handle().memory.isShared(); }

    // ---- Voxology --------------------------------------------------------------------------------------

    int Link::claimVocal (std::uint64_t instanceId, std::uint64_t now)
    {
        const int slot = claim (block.vocals, instanceId, now);
        if (slot >= 0) publishVocal (slot, instanceId, VocalInfo {}, now);   // nothing of an earlier owner shows
        return slot;
    }

    void Link::releaseVocal (int slot, std::uint64_t instanceId)
    {
        if (slot >= 0 && slot < kMaxVocals) release (block.vocals[static_cast<std::size_t> (slot)], instanceId);
    }

    bool Link::publishVocal (int slot, std::uint64_t instanceId, const VocalInfo& v, std::uint64_t now)
    {
        if (slot < 0 || slot >= kMaxVocals) return false;
        auto& s = block.vocals[static_cast<std::size_t> (slot)];
        if (s.instanceId.load (kRelaxed) != instanceId) return false;   // not ours any more
        beginWrite (s.seq);
        s.nameHash.store (v.trackName.empty() ? 0u : nameHash (v.trackName), kRelaxed);
        s.flags.store (v.flags, kRelaxed);
        s.style.store (static_cast<std::uint32_t> (v.style), kRelaxed);
        s.version.store (v.version, kRelaxed);
        s.compressionDb.store (std::max (0.0f, v.compressionDb), kRelaxed);
        s.reverbMix.store (std::clamp (v.reverbMix, 0.0f, 1.0f), kRelaxed);
        s.delayMix.store (std::clamp (v.delayMix, 0.0f, 1.0f), kRelaxed);
        s.recordedPeakDb.store (v.recordedPeakDb, kRelaxed);
        s.clippedSpots.store (v.clippedSpots, kRelaxed);
        storeText (s.trackName, v.trackName);
        endWrite (s.seq);
        s.heartbeatMs.store (now, kRelease);
        return true;
    }

    std::vector<VocalInfo> Link::vocals (std::uint64_t now) const
    {
        std::vector<VocalInfo> out;
        for (int i = 0; i < kMaxVocals; ++i)
        {
            const auto& s = block.vocals[static_cast<std::size_t> (i)];
            if (s.state.load (kAcquire) != 1u || ! alive (s.heartbeatMs.load (kAcquire), now)) continue;
            if (s.version.load (kAcquire) == 0) continue;   // claimed, not published yet
            VocalInfo v;
            const bool ok = readConsistent (s.seq, [&] {
                v.nameHash = s.nameHash.load (kRelaxed);
                v.flags = s.flags.load (kRelaxed);
                v.style = static_cast<VocalInfo::Style> (s.style.load (kRelaxed));
                v.version = s.version.load (kRelaxed);
                v.compressionDb = s.compressionDb.load (kRelaxed);
                v.reverbMix = s.reverbMix.load (kRelaxed);
                v.delayMix = s.delayMix.load (kRelaxed);
                v.recordedPeakDb = s.recordedPeakDb.load (kRelaxed);
                v.clippedSpots = s.clippedSpots.load (kRelaxed);
                v.trackName = loadText (s.trackName);
            });
            if (! ok || v.version == 0) continue;
            v.slot = i;
            v.instanceId = s.instanceId.load (kRelaxed);
            v.generation = s.generation.load (kAcquire);
            out.push_back (std::move (v));
        }
        return out;
    }

    void Link::ackVocal (const VocalInfo& v, const VocalAck& a, std::uint64_t now)
    {
        if (v.slot < 0 || v.slot >= kMaxVocals) return;
        auto& s = block.vocals[static_cast<std::size_t> (v.slot)];
        if (s.generation.load (kAcquire) != v.generation) return;   // claimed again since: the answer isn't for it
        beginWrite (s.ackSeq);
        s.ackGeneration.store (v.generation, kRelaxed);
        s.ackHash.store (v.nameHash, kRelaxed);
        s.ackState.store (static_cast<std::uint32_t> (a.state), kRelaxed);
        s.ackBits.store (a.bits, kRelaxed);
        s.ackVocalDb.store (a.vocalDb, kRelaxed);
        s.ackTargetDb.store (a.targetDb, kRelaxed);
        endWrite (s.ackSeq);
        s.ackMs.store (now, kRelease);
    }

    bool Link::vocalAck (int slot, std::uint64_t instanceId, std::uint32_t hash, VocalAck& out, std::uint64_t now) const
    {
        if (slot < 0 || slot >= kMaxVocals) return false;
        const auto& s = block.vocals[static_cast<std::size_t> (slot)];
        if (s.instanceId.load (kRelaxed) != instanceId) return false;
        VocalAck a;
        std::uint32_t generation = 0, ackHash = 0;
        const bool ok = readConsistent (s.ackSeq, [&] {
            generation = s.ackGeneration.load (kRelaxed);
            ackHash = s.ackHash.load (kRelaxed);
            a.state = static_cast<VocalAck::State> (s.ackState.load (kRelaxed));
            a.bits = s.ackBits.load (kRelaxed);
            a.vocalDb = s.ackVocalDb.load (kRelaxed);
            a.targetDb = s.ackTargetDb.load (kRelaxed);
        });
        // For this claim of the slot and this name (a renamed track gets a fresh answer), and recent.
        if (! ok || generation != s.generation.load (kAcquire) || ackHash != hash || ! alive (s.ackMs.load (kAcquire), now)) return false;
        out = a;
        return true;
    }

    // ---- Mixology --------------------------------------------------------------------------------------

    void Link::publishMix (const MixInfo& m, std::uint64_t now)
    {
        auto& s = block.mix;
        beginWrite (s.seq);
        s.flags.store (m.flags, kRelaxed);
        s.version.store (m.version, kRelaxed);
        s.peakDbtp.store (m.peakDbtp, kRelaxed);
        s.integratedLufs.store (m.integratedLufs, kRelaxed);
        s.shortTermLufs.store (m.shortTermLufs, kRelaxed);
        s.lowDb.store (m.lowDb, kRelaxed);
        s.lowMidDb.store (m.lowMidDb, kRelaxed);
        s.presenceDb.store (m.presenceDb, kRelaxed);
        s.airDb.store (m.airDb, kRelaxed);
        storeText (s.styleKey, m.styleKey);
        storeText (s.styleName, m.styleName);
        s.whenMs.store (now, kRelaxed);
        endWrite (s.seq);
    }

    bool Link::readMix (MixInfo& m, std::uint64_t now) const
    {
        const auto& s = block.mix;
        MixInfo r;
        const bool ok = readConsistent (s.seq, [&] {
            r.flags = s.flags.load (kRelaxed);
            r.version = s.version.load (kRelaxed);
            r.peakDbtp = s.peakDbtp.load (kRelaxed);
            r.integratedLufs = s.integratedLufs.load (kRelaxed);
            r.shortTermLufs = s.shortTermLufs.load (kRelaxed);
            r.lowDb = s.lowDb.load (kRelaxed);
            r.lowMidDb = s.lowMidDb.load (kRelaxed);
            r.presenceDb = s.presenceDb.load (kRelaxed);
            r.airDb = s.airDb.load (kRelaxed);
            r.styleKey = loadText (s.styleKey);
            r.styleName = loadText (s.styleName);
            r.whenMs = s.whenMs.load (kRelaxed);
        });
        if (! ok || ! alive (r.whenMs, now)) return false;
        m = r;
        return true;
    }

    // ---- Masterology -----------------------------------------------------------------------------------

    int Link::claimMaster (std::uint64_t instanceId, std::uint64_t now)
    {
        const int slot = claim (block.masters, instanceId, now);
        if (slot >= 0) publishMaster (slot, instanceId, MasterInfo {}, now);
        return slot;
    }

    void Link::releaseMaster (int slot, std::uint64_t instanceId)
    {
        if (slot >= 0 && slot < kMaxMasters) release (block.masters[static_cast<std::size_t> (slot)], instanceId);
    }

    bool Link::publishMaster (int slot, std::uint64_t instanceId, const MasterInfo& m, std::uint64_t now)
    {
        if (slot < 0 || slot >= kMaxMasters) return false;
        auto& s = block.masters[static_cast<std::size_t> (slot)];
        if (s.instanceId.load (kRelaxed) != instanceId) return false;
        beginWrite (s.seq);
        s.version.store (m.version, kRelaxed);
        s.targetLufs.store (m.targetLufs, kRelaxed);
        s.ceilingDbtp.store (m.ceilingDbtp, kRelaxed);
        s.inputLufs.store (m.inputLufs, kRelaxed);
        s.inputPeakDbtp.store (m.inputPeakDbtp, kRelaxed);
        storeText (s.goalName, m.goalName);
        storeText (s.genreName, m.genreName);
        endWrite (s.seq);
        s.heartbeatMs.store (now, kRelease);
        return true;
    }

    std::vector<MasterInfo> Link::masters (std::uint64_t now) const
    {
        std::vector<MasterInfo> out;
        for (int i = 0; i < kMaxMasters; ++i)
        {
            const auto& s = block.masters[static_cast<std::size_t> (i)];
            if (s.state.load (kAcquire) != 1u || ! alive (s.heartbeatMs.load (kAcquire), now)) continue;
            if (s.version.load (kAcquire) == 0) continue;
            MasterInfo m;
            const bool ok = readConsistent (s.seq, [&] {
                m.version = s.version.load (kRelaxed);
                m.targetLufs = s.targetLufs.load (kRelaxed);
                m.ceilingDbtp = s.ceilingDbtp.load (kRelaxed);
                m.inputLufs = s.inputLufs.load (kRelaxed);
                m.inputPeakDbtp = s.inputPeakDbtp.load (kRelaxed);
                m.goalName = loadText (s.goalName);
                m.genreName = loadText (s.genreName);
            });
            if (! ok || m.version == 0) continue;
            m.slot = i;
            m.instanceId = s.instanceId.load (kRelaxed);
            out.push_back (std::move (m));
        }
        return out;
    }

    // ---- the shared choices (the song's style, the DAW) -------------------------------------------------

    namespace
    {
        bool lockStyle (std::atomic<std::uint32_t>& seq)
        {
            for (int attempt = 0; attempt < 1000; ++attempt)
            {
                auto v = seq.load (kRelaxed);
                if ((v & 1u) == 0 && seq.compare_exchange_weak (v, v + 1, std::memory_order_acq_rel))
                {
                    std::atomic_thread_fence (kRelease);   // the odd count is seen before anything written after it
                    return true;
                }
            }
            return false;   // another writer stuck mid-write (it crashed): try again next time
        }
    }

    StyleInfo Link::shareChoice (Choice c, const StyleInfo& mine, std::uint64_t instanceId, std::uint64_t now)
    {
        auto& s = block.choices[static_cast<std::size_t> (c) % static_cast<std::size_t> (kChoices)];
        if (! lockStyle (s.seq)) return mine;
        std::atomic_thread_fence (kAcquire);
        StyleInfo there;
        there.key = loadText (s.key);
        there.name = loadText (s.name);
        there.pickedAt = s.pickedAt.load (kRelaxed);
        there.from = static_cast<StyleInfo::From> (s.from.load (kRelaxed));
        const bool live = ! there.key.empty() && alive (s.heartbeatMs.load (kRelaxed), now);

        StyleInfo result = live ? there : mine;
        const bool offer = mine.pickedAt != 0 && ! mine.key.empty();
        if (offer && (! live || mine.pickedAt > there.pickedAt))
        {
            storeText (s.key, mine.key);
            storeText (s.name, mine.name);
            s.pickedAt.store (mine.pickedAt, kRelaxed);
            s.from.store (static_cast<std::uint32_t> (mine.from), kRelaxed);
            result = mine;
        }
        if (offer && result == mine)
        {
            s.heartbeatMs.store (now, kRelaxed);
            s.holder.store (instanceId, kRelaxed);
        }
        s.seq.fetch_add (1, kRelease);
        return result;
    }

    bool Link::readChoice (Choice c, StyleInfo& out, std::uint64_t now) const
    {
        const auto& s = block.choices[static_cast<std::size_t> (c) % static_cast<std::size_t> (kChoices)];
        StyleInfo r;
        std::uint64_t beat = 0;
        const bool ok = readConsistent (s.seq, [&] {
            r.key = loadText (s.key);
            r.name = loadText (s.name);
            r.pickedAt = s.pickedAt.load (kRelaxed);
            r.from = static_cast<StyleInfo::From> (s.from.load (kRelaxed));
            beat = s.heartbeatMs.load (kRelaxed);
        });
        if (! ok || r.key.empty() || ! alive (beat, now)) return false;
        out = r;
        return true;
    }

    void Link::leaveChoice (Choice c, std::uint64_t instanceId)
    {
        auto& s = block.choices[static_cast<std::size_t> (c) % static_cast<std::size_t> (kChoices)];
        if (! lockStyle (s.seq)) return;
        if (s.holder.load (kRelaxed) == instanceId) s.heartbeatMs.store (0, kRelaxed);
        s.seq.fetch_add (1, kRelease);
    }

    // ---- the beat key (Voxology on the beat -> Honey Tune) ------------------------------------------------

    void Link::publishBeatKey (int key, int scale, float confidence, std::uint64_t whenMs)
    {
        auto& s = block.beatKey;
        if (! lockStyle (s.seq)) return;   // (several Voxologys on beats may write: one at a time)
        s.key.store (key, kRelaxed);
        s.scale.store (scale, kRelaxed);
        s.confidence.store (confidence, kRelaxed);
        s.whenMs.store (whenMs, kRelaxed);
        s.seq.fetch_add (1, kRelease);
    }

    bool Link::readBeatKey (int& key, int& scale, float& confidence, std::uint64_t& whenMs) const
    {
        const auto& s = block.beatKey;
        int k = 0, sc = 0;
        float c = 0.0f;
        std::uint64_t when = 0;
        const bool ok = readConsistent (s.seq, [&] {
            k = s.key.load (kRelaxed);
            sc = s.scale.load (kRelaxed);
            c = s.confidence.load (kRelaxed);
            when = s.whenMs.load (kRelaxed);
        });
        if (! ok || when == 0) return false;
        key = k;
        scale = sc;
        confidence = c;
        whenMs = when;
        return true;
    }
}

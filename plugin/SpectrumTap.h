#pragma once

#include <array>
#include <atomic>
#include <cstdint>

/** Lock-free ring buffer that keeps the most recent audio (mono sum) for the spectrum display.
    The audio thread writes, the UI thread reads a snapshot. A read can overlap a write at the
    very edge of the buffer; that is harmless for a visual analyser and keeps the audio thread wait-free. */
class SpectrumTap
{
public:
    static constexpr int kSize = 8192;   // power of two

    template <typename Sample>
    void push (const Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        if (numChannels <= 0)
            return;

        const float scale = 1.0f / static_cast<float> (numChannels);
        auto w = writeIndex.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            float v = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                v += static_cast<float> (channels[c][i]);
            ring[w & (kSize - 1)] = v * scale;
            ++w;
        }
        writeIndex.store (w, std::memory_order_release);
    }

    /** Copies the newest n samples (n <= kSize), oldest first. */
    void copyLatest (float* dest, int n) const noexcept
    {
        const auto w = writeIndex.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            dest[i] = ring[(w - static_cast<std::uint32_t> (n) + static_cast<std::uint32_t> (i)) & (kSize - 1)];
    }

private:
    std::array<float, kSize> ring {};
    std::atomic<std::uint32_t> writeIndex { 0 };
};

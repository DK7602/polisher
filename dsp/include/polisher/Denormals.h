#pragma once

#include <cstdint>

#if defined (__SSE__) || defined (_M_X64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 1)
 #include <xmmintrin.h>
 #define POLISHER_FTZ_SSE 1
#elif defined (__aarch64__) && (defined (__GNUC__) || defined (__clang__))
 #define POLISHER_FTZ_ARM64 1
#endif

namespace polisher {

/** Flushes denormals to zero on the calling thread while in scope, and restores the previous mode
    afterwards (so nesting is fine). For offline renders and analysis, which run on whatever thread
    calls them; the plug-in's audio callback uses juce::ScopedNoDenormals. Without it, filter tails
    decaying into the denormal range can make a render many times slower. A no-op on platforms
    without a known control register. */
class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept
    {
       #if POLISHER_FTZ_SSE
        saved = _mm_getcsr();
        _mm_setcsr (static_cast<unsigned int> (saved | 0x8040u));   // FTZ | DAZ
       #elif POLISHER_FTZ_ARM64
        std::uint64_t fpcr;
        asm volatile ("mrs %0, fpcr" : "=r" (fpcr));
        saved = fpcr;
        asm volatile ("msr fpcr, %0" : : "r" (fpcr | (std::uint64_t { 1 } << 24)));   // FZ
       #endif
    }

    ~ScopedFlushDenormals()
    {
       #if POLISHER_FTZ_SSE
        _mm_setcsr (static_cast<unsigned int> (saved));
       #elif POLISHER_FTZ_ARM64
        asm volatile ("msr fpcr, %0" : : "r" (saved));
       #endif
    }

    ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

private:
    [[maybe_unused]] std::uint64_t saved = 0;
};

} // namespace polisher

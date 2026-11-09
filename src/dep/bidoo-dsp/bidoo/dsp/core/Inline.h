#pragma once

// BIDOO_DSP_INLINE: asks the compiler to inline a small per-sample function (as JUCE's
// forcedinline did), so a call through the library costs nothing in an audio loop.

#if defined(_MSC_VER)
 #define BIDOO_DSP_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
 #define BIDOO_DSP_INLINE inline __attribute__ ((always_inline))
#else
 #define BIDOO_DSP_INLINE inline
#endif

// BIDOO_DSP_NOINLINE: keeps a rare path (a recovery from NaN) out of the audio loop it is called
// from, so the loop's code and registers stay as they were.
#if defined(_MSC_VER)
 #define BIDOO_DSP_NOINLINE __declspec (noinline)
#elif defined(__GNUC__) || defined(__clang__)
 #define BIDOO_DSP_NOINLINE __attribute__ ((noinline))
#else
 #define BIDOO_DSP_NOINLINE
#endif

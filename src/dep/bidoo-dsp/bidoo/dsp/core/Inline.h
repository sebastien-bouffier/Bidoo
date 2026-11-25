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

// BIDOO_DSP_RESTRICT: a pointer that nothing else in the loop aliases (MSVC vectorises little without it).
#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
 #define BIDOO_DSP_RESTRICT __restrict
#else
 #define BIDOO_DSP_RESTRICT
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

// Finite-math-only (-ffast-math, -ffinite-math-only, /fp:fast on clang-cl) lets the compiler delete every NaN and
// infinity check (finiteOrZero, zeroNonFinite, the self-recovery of filters and reverbs): one bad sample would then
// stay in a state for good. Rack's -funsafe-math-optimizations keeps them (__FINITE_MATH_ONLY__ stays 0).
#if defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__
 #error "bidoo-dsp needs NaN and infinity checks: build without -ffast-math / -ffinite-math-only (DSP-TOOLCHAIN-PLAN.md)"
#endif

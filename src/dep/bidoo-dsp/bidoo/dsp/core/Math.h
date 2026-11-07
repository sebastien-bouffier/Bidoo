#pragma once

// Constants and small conversions used everywhere, in place of juce::MathConstants, jlimit,
// jmap and juce::Decibels.

#include "Inline.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace bidoo::dsp
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kHalfPi = 0.5 * kPi;
constexpr double kSqrt2 = 1.41421356237309504880;

// The level juce::Decibels treats as silence by default.
constexpr double kMinusInfinityDb = -100.0;

template <typename T>
BIDOO_DSP_INLINE T clamp (T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

template <typename T>
BIDOO_DSP_INLINE T lerp (T a, T b, T t) noexcept { return a + (b - a) * t; }

// NaN and +-inf become 0, finite values pass unchanged (bit for bit). For the input of a
// detector whose state would otherwise hold a NaN forever (Ballistics, SlidingMax, an IIR):
// one bad side-chain sample must not leave a gate or compressor stuck.
template <typename T>
BIDOO_DSP_INLINE T finiteOrZero (T x) noexcept { return std::isfinite (x) ? x : (T) 0; }

// x from [inLo, inHi] to [outLo, outHi], like juce::jmap.
template <typename T>
BIDOO_DSP_INLINE T map (T x, T inLo, T inHi, T outLo, T outHi) noexcept
{
    return outLo + (outHi - outLo) * (x - inLo) / (inHi - inLo);
}

// Same results as juce::Decibels: gain 0 below minusInfinityDb, and the reverse.
template <typename T>
inline T dbToGain (T db, T minusInfinityDb = (T) kMinusInfinityDb) noexcept
{
    return db > minusInfinityDb ? std::pow ((T) 10, db * (T) 0.05) : (T) 0;
}

template <typename T>
inline T gainToDb (T gain, T minusInfinityDb = (T) kMinusInfinityDb) noexcept
{
    return gain > (T) 0 ? std::max (minusInfinityDb, (T) std::log10 (gain) * (T) 20) : minusInfinityDb;
}

// Fast log2 / 2^x for float level maths (detectors in dB, gain from dB, at audio rate), in place
// of std::log2 / std::exp2 / std::pow. Branch-free bit tricks plus a degree-5 polynomial on the
// mantissa (log2) or the fraction (2^x); both are exact at the octave ends, so there is no step
// between octaves and log2Approx (2^k) == k, pow2Approx (k) == 2^k exactly for whole k.
//   log2Approx (x), x > 0 normal: absolute error < 2e-5 (about 1e-4 dB once scaled by 6.02).
//     The same polynomial as CharacterCompressor's and Portau's. Denormals, 0, inf and NaN are
//     not handled: floor the input first (std::max (x, 1e-5f)).
//   pow2Approx (p): relative error < 2e-5 (2e-4 dB), p clamped to [-126, 126], so 2^-126 is the
//     smallest result (never a denormal) and no overflow. Taylor coefficients of 2^f with the
//     last one (0.00150407, from Fab's Portau) chosen so the sum at f = 1 is exactly 2.
//     CharacterCompressor keeps its own pow2Approx (plain Taylor coefficient 0.00133336, error
//     < 2e-4, not end-exact): its output must stay bit for bit.
// Speed: in a scalar loop MSVC's float std::exp / std::exp2 (SVML-backed) is already about as
// fast as this polynomial; the gain comes where the call is not vectorised and in log2 (bidoo-dsp
// bench rows "log2Approx" / "pow2Approx"). Measure before replacing std:: calls for speed alone.
BIDOO_DSP_INLINE float log2Approx (float x) noexcept
{
    std::uint32_t i;
    std::memcpy (&i, &x, sizeof i);
    const float e = static_cast<float> (static_cast<int> (i >> 23) - 127);
    i = (i & 0x007FFFFFu) | 0x3F800000u;
    float m;
    std::memcpy (&m, &i, sizeof m); // mantissa in [1, 2)
    const float t = m - 1.0f;
    return e + t * (1.44191693f + t * (-0.709095557f + t * (0.415603761f + t * (-0.193573322f + t * 0.0451481878f))));
}

BIDOO_DSP_INLINE float pow2Approx (float p) noexcept
{
    p = clamp (p, -126.0f, 126.0f);
    int i = static_cast<int> (p);
    i -= static_cast<float> (i) > p ? 1 : 0; // floor
    const float f = p - static_cast<float> (i);
    const float m = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00150407f))));
    const std::uint32_t bits = static_cast<std::uint32_t> (i + 127) << 23;
    float scale;
    std::memcpy (&scale, &bits, sizeof scale);
    return m * scale;
}

// 20 log10 (g) and 10^(db / 20) on the two above (g > 0 normal).
BIDOO_DSP_INLINE float gainToDbApprox (float g) noexcept { return 6.02059991f * log2Approx (g); }
BIDOO_DSP_INLINE float dbToGainApprox (float db) noexcept { return pow2Approx (db * 0.166096405f); }

// Smallest power of two >= n (n >= 1).
constexpr int nextPowerOfTwo (int n) noexcept
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

} // namespace bidoo::dsp

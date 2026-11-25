#pragma once

// sinF / cosF: std::sin / std::cos of a float, except where the libm is slow. MinGW's sinf (the libm of
// VCV Rack's Windows builds) costs about 30 ns per call against 3 for UCRT's (MSVC, clang-cl) and the
// other platforms': there they evaluate a polynomial instead (Cody-Waite reduction by pi / 2 in double,
// Taylor to 3e-14 on [-pi/4, pi/4], one rounding to float), the correctly rounded result (MinGW's own
// sinf rounds correctly too, so the polynomial matches it nearly always), within 1 ulp at worst
// on |x| <= 1e5, and on |x| <= 8 under Rack's -funsafe-math-optimizations (which merges the two-part
// reduction: beyond 8 they call std::sin there). Elsewhere they are std::sin / std::cos, so a block switching
// to them keeps its output bit for bit on MSVC, clang-cl and Apple Clang and moves it by about -140 dBFS on
// MinGW. Infinity, NaN and |x| above those bounds always go to std::sin / std::cos.

#include "Inline.h"

#include <cmath>

#if defined(__MINGW32__)
 #define BIDOO_DSP_TRIG_POLY 1
#else
 #define BIDOO_DSP_TRIG_POLY 0
#endif
#if defined(__ASSOCIATIVE_MATH__)
 #define BIDOO_DSP_TRIG_POLY_MAX 8.0f // the merged reduction stays within 1 ulp up to here
#else
 #define BIDOO_DSP_TRIG_POLY_MAX 1.0e5f
#endif

namespace bidoo::dsp
{
namespace trig_detail
{
BIDOO_DSP_INLINE double sinPoly (double r) noexcept // |r| <= pi / 4, error < 3e-14: correct rounding to float
{
    const double z = r * r;
    return r
           + r * z
                 * (-1.0 / 6.0
                    + z * (1.0 / 120.0 + z * (-1.0 / 5040.0 + z * (1.0 / 362880.0 + z * (-1.0 / 39916800.0 + z * (1.0 / 6227020800.0))))));
}

BIDOO_DSP_INLINE double cosPoly (double r) noexcept // |r| <= pi / 4, error < 2e-15
{
    const double z = r * r;
    return 1.0
           + z
                 * (-0.5
                    + z * (1.0 / 24.0
                           + z * (-1.0 / 720.0
                                  + z * (1.0 / 40320.0 + z * (-1.0 / 3628800.0 + z * (1.0 / 479001600.0 + z * (-1.0 / 87178291200.0)))))));
}

// x = q pi / 2 + r, |r| <= pi / 4 (pi / 2 split in two so that q * kHi is exact for |q| < 2^17); q by a
// conversion, not std::floor (a library call in MSVC's SSE2 builds)
BIDOO_DSP_INLINE double reduce (double x, int& quadrant) noexcept
{
    constexpr double kTwoOverPi = 0.63661977236758134308;
    constexpr double kHi = 1.5707963267341256, kLo = 6.077100506506192e-11;
    const double t = x * kTwoOverPi;
    const int qi = (int) (t + (t >= 0.0 ? 0.5 : -0.5));
    quadrant = qi & 3;
    const double q = (double) qi;
    return (x - q * kHi) - q * kLo;
}
} // namespace trig_detail

BIDOO_DSP_INLINE float sinF (float x) noexcept
{
    if (! BIDOO_DSP_TRIG_POLY || ! (std::fabs (x) <= BIDOO_DSP_TRIG_POLY_MAX) || x == 0.0f) // NaN, infinity, huge; and +-0 keeps its sign
        return std::sin (x);
    int n;
    const double r = trig_detail::reduce ((double) x, n);
    const double v = (n & 1) ? trig_detail::cosPoly (r) : trig_detail::sinPoly (r);
    return (float) ((n & 2) ? -v : v);
}

BIDOO_DSP_INLINE float cosF (float x) noexcept
{
    if (! BIDOO_DSP_TRIG_POLY || ! (std::fabs (x) <= BIDOO_DSP_TRIG_POLY_MAX))
        return std::cos (x);
    int n;
    const double r = trig_detail::reduce ((double) x, n);
    const double v = (n & 1) ? trig_detail::sinPoly (r) : trig_detail::cosPoly (r);
    return (float) (((n + 1) & 2) ? -v : v);
}
} // namespace bidoo::dsp

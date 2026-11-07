#pragma once

// Ready-made curves for adaa() (shaping/Adaa.h), each with its first and second antiderivatives
// in closed form, plus wrappers that build a plugin's curve from them without writing the
// antiderivatives again:
//
//   AdaaSine            sin x (folders)
//   AdaaSqrtSigmoid     x / sqrt(1 + x^2) (soft, transformer-like)
//   AdaaRationalSigmoid x / (1 + |x|) (soft, slow approach to the ceiling: tape-like)
//   AdaaSmoothAbs       smoothAbs (shaping/Shapers.h): a rectifier with a rounded corner
//   AdaaTriFold         triFold (shaping/Shapers.h): the triangle folder, period 4
//   fastTanhIntegral, fastTanhIntegral2: fastTanh's antiderivatives (shaping/Shapers.h); the
//   table-driven AdaaFastTanh, with no logarithm per sample, is in shaping/AdaaTable.h.
//
//   AdaaScaled<C>       a * C(k x): drive inside the curve with the level kept (tanh (k x) / k)
//   AdaaBiased<C>       C(x + b) - C(b): a biased operating point (even harmonics), still 0 at 0
//   AdaaAsymmetric<P,N> P for x >= 0, N below: different positive and negative halves
//
// Conventions shared by every curve here and in Adaa.h: f(0) = 0 and F1(0) = F2(0) = 0, so the
// wrappers can splice and shift them. All in double (see Adaa.h for why).
//
// A wrapper whose parameters move while it runs changes the curve under the ADAA memory: clear
// the state's `primed` flag when that happens (AdaaState keeps F2 of the previous input, and may
// keep F1 too).

#include "Adaa.h"
#include "../core/Inline.h"
#include <cmath>

namespace bidoo::dsp
{
// sin x, with 1 - cos x and x - sin x.
struct AdaaSine
{
    BIDOO_DSP_INLINE static double f (double x) { return std::sin (x); }
    BIDOO_DSP_INLINE static double F1 (double x) { return 1.0 - std::cos (x); }
    static double F2 (double x) { return x - std::sin (x); }
};

// x / sqrt(1 + x^2): slope 1 at 0, reaches its ceiling like 1 - 1 / (2 x^2).
struct AdaaSqrtSigmoid
{
    BIDOO_DSP_INLINE static double f (double x) { return x / std::sqrt (1.0 + x * x); }
    // (the cancellation near 0 costs relative precision only: ADAA divides differences of F1,
    // whose absolute error stays at the rounding of 1)
    BIDOO_DSP_INLINE static double F1 (double x) { return std::sqrt (1.0 + x * x) - 1.0; }
    static double F2 (double x)
    {
        const double h = std::sqrt (1.0 + x * x);
        return 0.5 * (x * h + std::asinh (x)) - x;
    }
};

// x / (1 + |x|): slope 1 at 0, reaches its ceiling slowly (like 1 - 1 / x).
struct AdaaRationalSigmoid
{
    BIDOO_DSP_INLINE static double f (double x) { return x / (1.0 + std::fabs (x)); }
    BIDOO_DSP_INLINE static double F1 (double x)
    {
        const double a = std::fabs (x);
        return a - std::log1p (a);
    }
    static double F2 (double x)
    {
        const double a = std::fabs (x);
        const double v = 0.5 * a * a + a - (1.0 + a) * std::log1p (a);
        return x < 0.0 ? -v : v;
    }
};

// smoothAbs (shaping/Shapers.h): sqrt(x^2 + e^2) - e with e = 0.05.
struct AdaaSmoothAbs
{
    static constexpr double e = 0.05;
    BIDOO_DSP_INLINE static double f (double x) { return std::sqrt (x * x + e * e) - e; }
    BIDOO_DSP_INLINE static double F1 (double x)
    {
        const double h = std::sqrt (x * x + e * e);
        return 0.5 * (x * h + e * e * std::asinh (x / e)) - e * x;
    }
    static double F2 (double x)
    {
        const double h = std::sqrt (x * x + e * e);
        return h * h * h / 6.0 + 0.5 * e * e * (x * std::asinh (x / e) - h) - 0.5 * e * x * x + e * e * e / 3.0;
    }
};

// triFold (shaping/Shapers.h): the triangle fold, period 4, peaks at +-1. F1 is periodic; F2
// grows like x / 2 (F1's mean) plus a periodic part.
struct AdaaTriFold
{
    // the position in the period, m in [0, 4), with the triangle 1 - |m - 2|
    BIDOO_DSP_INLINE static double phase (double x)
    {
        const double u = x + 1.0;
        return u - 4.0 * std::floor (u * 0.25);
    }
    BIDOO_DSP_INLINE static double f (double x) { return 1.0 - std::fabs (phase (x) - 2.0); }
    BIDOO_DSP_INLINE static double F1 (double x)
    {
        const double m = phase (x);
        return (m <= 2.0 ? 0.5 * m * m - m : 3.0 * m - 0.5 * m * m - 4.0) + 0.5;
    }
    static double F2 (double x)
    {
        const double m = phase (x);
        const double g = m <= 2.0 ? m * m * m / 6.0 - 0.5 * m * m : 1.5 * m * m - m * m * m / 6.0 - 4.0 * m + 8.0 / 3.0;
        return g + 1.0 / 3.0 + 0.5 * x;
    }
};

// fastTanh's antiderivatives (the rational x (27 + x^2) / (27 + 9 x^2), exactly +-1 beyond +-3):
// F1 = x^2 / 18 + 4/3 ln((x^2 + 3) / 3) up to 3, then linear.
inline double fastTanhIntegral (double x)
{
    const double a = std::fabs (x);
    if (a > 3.0)
        return a - 3.0 + 2.3483924814931874; // F1(3) = 1/2 + 4/3 ln 4
    const double x2 = x * x;
    return x2 / 18.0 + (4.0 / 3.0) * std::log ((x2 + 3.0) / 3.0);
}

// F2 = x^3 / 54 + 4/3 (x ln((x^2 + 3) / 3) - 2 x + 2 sqrt(3) atan(x / sqrt(3))) up to 3, then
// quadratic. Odd.
inline double fastTanhIntegral2 (double x)
{
    constexpr double sqrt3 = 1.7320508075688772935;
    auto inner = [] (double a)
    {
        return a * a * a / 54.0 + (4.0 / 3.0) * (a * std::log ((a * a + 3.0) / 3.0) - 2.0 * a + 2.0 * sqrt3 * std::atan (a / sqrt3));
    };
    const double a = std::fabs (x);
    double v;
    if (a > 3.0)
    {
        const double d = a - 3.0;
        v = inner (3.0) + 2.3483924814931874 * d + 0.5 * d * d;
    }
    else
        v = inner (a);
    return x < 0.0 ? -v : v;
}

// a * C(k x), k > 0. a = 1 / k keeps the slope at 0 (tanh (k x) / k: more drive, same level).
template <typename C>
struct AdaaScaled
{
    AdaaScaled (double scale, double drive, C curve = {}) : AdaaScaled (scale, drive, 1.0 / drive, curve) {}
    // the same with 1 / drive given (a curve built every sample: one division less)
    AdaaScaled (double scale, double drive, double invDrive, C curve) : a (scale), k (drive), a1 (scale * invDrive), a2 (scale * invDrive * invDrive), c (curve) {}

    BIDOO_DSP_INLINE double f (double x) const { return a * c.f (k * x); }
    BIDOO_DSP_INLINE double F1 (double x) const { return a1 * c.F1 (k * x); }
    double F2 (double x) const { return a2 * c.F2 (k * x); }

    double a, k, a1, a2;
    C c;
};

// C(x + b) - C(b): the operating point moved to b (the asymmetry gives even harmonics), shifted
// back so the curve still passes through 0. F1 and F2 subtract the matching linear terms.
template <typename C>
struct AdaaBiased
{
    explicit AdaaBiased (double bias, C curve = {}) : b (bias), c (curve), fb (c.f (b)), F1b (c.F1 (b)), F2b (c.F2 (b)) {}

    BIDOO_DSP_INLINE double f (double x) const { return c.f (x + b) - fb; }
    BIDOO_DSP_INLINE double F1 (double x) const { return c.F1 (x + b) - F1b - x * fb; }
    double F2 (double x) const { return c.F2 (x + b) - F2b - x * (F1b + 0.5 * x * fb); }

    double b;
    C c;
    double fb, F1b, F2b;
};

// Positive half from P, negative half from N. Both pass through 0 with F1(0) = F2(0) = 0, so the
// joined antiderivatives are continuous.
template <typename P, typename N>
struct AdaaAsymmetric
{
    AdaaAsymmetric (P positive = {}, N negative = {}) : pos (positive), neg (negative) {}

    BIDOO_DSP_INLINE double f (double x) const { return x >= 0.0 ? pos.f (x) : neg.f (x); }
    BIDOO_DSP_INLINE double F1 (double x) const { return x >= 0.0 ? pos.F1 (x) : neg.F1 (x); }
    double F2 (double x) const { return x >= 0.0 ? pos.F2 (x) : neg.F2 (x); }

    P pos;
    N neg;
};
} // namespace bidoo::dsp

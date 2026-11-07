#pragma once

// Antiderivative anti-aliasing (ADAA) for static curves (Parker, Zavalishin, Le Bivic 2016;
// Bilbao, Esqueda, Parker, Valimaki 2017), from Chourmo's shapers. A curve is any object with
// three members f, F1 and F2: the curve and its first and second antiderivatives (any constant
// or linear offset in F2 cancels out):
//
//   1st order: y[n] = (F1(x[n]) - F1(x[n-1])) / (x[n] - x[n-1])
//              half a sample late, -12 dB/oct on what folds back;
//   2nd order: y[n] = 2 / (x[n] - x[n-2]) * (D[n] - D[n-1]),
//              D[n] = (F2(x[n]) - F2(x[n-1])) / (x[n] - x[n-1]),
//              one sample late, -18 dB/oct, a little duller at the top.
//
// Where the differences get too small to divide by, both fall back to the curve at the
// midpoint. The tolerance grows with the signal so the rounding error of large F2 values never
// shows. Everything runs in double: F1 and F2 differences of nearby inputs cancel most of their
// digits.
//
// Ready-made curves: AdaaTanh (with logCosh and logCoshIntegral, its antiderivatives) and
// AdaaHardClip. A plugin with its own curve writes a struct with f, F1, F2 (static or not; bind
// the curve's parameters in it) and passes it to adaa(). ADAA combines well with oversampling
// (oversampling/): the curve runs inside the oversampled loop.

#include "../core/Inline.h"
#include <algorithm>
#include <cmath>

namespace bidoo::dsp
{
// One channel's memory. Plain doubles, no padding: a plugin may compare or copy it bitwise.
// Clear it (= AdaaState{}) when the order changes or a voice restarts.
struct AdaaState
{
    double x1 { 0.0 }, x2 { 0.0 }; // the last two inputs
    double d1 { 0.0 };             // D[n-1] (2nd order)
    double fx1 { 0.0 };            // F1(x1) (1st order) or F2(x1) (2nd order), so it runs once a sample
    double primed { 0.0 };         // 1 once fx1 is set (a double: no padding); clear it when the curve changes

    void reset() { *this = AdaaState {}; }

    // Clears the memory as if the input had been x0 for ever (no step from 0 into a biased curve).
    void startAt (double x0)
    {
        *this = AdaaState {};
        x1 = x2 = x0;
    }
};

// Below this difference between inputs, the divided differences fall back to the midpoint.
inline double adaaTolerance (double x) { return 1e-5 * (1.0 + std::fabs (x)); }

// One sample through `curve` with anti-aliasing of the given order: 0 off (the bare curve, no
// delay), 1 first order, 2 second order. The order may come from a plugin's choice; clear the
// state when it changes.
template <typename Curve>
BIDOO_DSP_INLINE double adaa (AdaaState& s, double x, int order, const Curve& curve)
{
    double y;
    if (order <= 0)
        y = curve.f (x);
    else if (order == 1)
    {
        if (s.primed == 0.0)
        {
            s.fx1 = curve.F1 (s.x1);
            s.primed = 1.0;
        }
        const double f1 = curve.F1 (x);
        const double dx = x - s.x1;
        y = std::fabs (dx) > adaaTolerance (x) ? (f1 - s.fx1) / dx : curve.f (0.5 * (x + s.x1));
        s.fx1 = f1;
    }
    else
    {
        if (s.primed == 0.0)
        {
            s.fx1 = curve.F2 (s.x1);
            s.primed = 1.0;
        }
        const double f2 = curve.F2 (x);
        const double dx1 = x - s.x1;
        const double d = std::fabs (dx1) > adaaTolerance (x) ? (f2 - s.fx1) / dx1 : curve.F1 (0.5 * (x + s.x1));
        const double dx = x - s.x2;
        if (std::fabs (dx) > adaaTolerance (x))
            y = 2.0 * (d - s.d1) / dx;
        else
        {
            // x[n] ~ x[n-2]: expand around their mean.
            const double xBar = 0.5 * (x + s.x2);
            const double delta = xBar - s.x1;
            y = std::fabs (delta) > adaaTolerance (xBar)
                    ? (2.0 / delta) * (curve.F1 (xBar) + (s.fx1 - curve.F2 (xBar)) / delta)
                    : curve.f (0.5 * (xBar + s.x1));
        }
        s.d1 = d;
        s.fx1 = f2;
    }
    s.x2 = s.x1;
    s.x1 = x;
    return y;
}

// The same with the order fixed at compile time, for a plugin that never switches it.
template <int order, typename Curve>
BIDOO_DSP_INLINE double adaa (AdaaState& s, double x, const Curve& curve)
{
    static_assert (order >= 0 && order <= 2, "ADAA order 0, 1 or 2");
    return adaa (s, x, order, curve);
}

// Li2(-u) for 0 <= u <= 1, by Landen's identity and the Bernoulli series of Li2(w) in
// t = -log(1 - w), which converges fast for w <= 1/2.
inline double dilogNeg (double u)
{
    const double w = u / (1.0 + u);
    const double t = -std::log1p (-w);
    const double t2 = t * t;
    static const double c[] = { -2.7777777777777778e-4, 4.7241118669690098e-6,   -9.1857730746619641e-8,
                                1.8978869988970999e-9,  -4.0647616451442256e-11, 8.9216910204564526e-13,
                                -1.9939295860721074e-14 };
    double odd = 0.0, p = t2 * t2 * t; // t^5
    for (double k : c)
    {
        odd += k * p;
        p *= t2;
    }
    const double li2w = t - 0.25 * t2 + t2 * t / 36.0 + odd;
    const double l = std::log1p (u);
    return -0.5 * l * l - li2w;
}

// log(cosh(x)), the first antiderivative of tanh, written so it never overflows:
// |x| + log(1 + e^(-2|x|)) - log 2.
inline double logCosh (double x)
{
    const double ax = std::fabs (x);
    return ax + std::log1p (std::exp (-2.0 * ax)) - 0.69314718055994530942;
}

// The integral of log(cosh(x)) from 0 (the second antiderivative of tanh), odd:
// x^2/2 - x log 2 + (Li2(-e^(-2x)) + pi^2/12) / 2 for x >= 0. No overflow either: e^(-2|x|) only
// underflows to 0 for large |x|.
inline double logCoshIntegral (double x)
{
    const double ax = std::fabs (x);
    const double v =
        0.5 * ax * ax - 0.69314718055994530942 * ax + 0.5 * (dilogNeg (std::exp (-2.0 * ax)) + 0.82246703342411321824);
    return x < 0.0 ? -v : v;
}

// tanh with its antiderivatives, for adaa().
struct AdaaTanh
{
    static double f (double x) { return std::tanh (x); }
    static double F1 (double x) { return logCosh (x); }
    static double F2 (double x) { return logCoshIntegral (x); }
};

// The hard clip at +-1 with its antiderivatives, for adaa().
struct AdaaHardClip
{
    static double f (double x) { return std::clamp (x, -1.0, 1.0); }
    static double F1 (double x)
    {
        const double ax = std::fabs (x);
        return ax <= 1.0 ? 0.5 * x * x : ax - 0.5;
    }
    static double F2 (double x)
    {
        const double ax = std::fabs (x);
        const double v = ax <= 1.0 ? ax * ax * ax / 6.0 : 0.5 * ax * ax - 0.5 * ax + 1.0 / 6.0;
        return x < 0.0 ? -v : v;
    }
};
} // namespace bidoo::dsp

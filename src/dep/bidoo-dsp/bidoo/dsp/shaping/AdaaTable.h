#pragma once

// Tabulated antiderivatives for adaa() (shaping/Adaa.h), from Crame: for curves whose
// antiderivative has no closed form (compositions such as the rectifier curves, drawn curves),
// or whose closed form costs a logarithm per sample (fastTanh).
//
// F1 is known at nodes h apart, and so is its slope (the curve itself); between them it is the
// cubic Hermite through both, kept as polynomial coefficients per segment (Horner: 3 multiplies
// and 3 adds a lookup). The slope of that cubic, which is what first-order ADAA outputs, stays
// within about h^3 |f'''| / 100 of the curve (1e-9 for fastTanh at h = 1/256). F2 is the exact
// integral of the same cubic, accumulated at the nodes, so both orders see one consistent pair.
// Second order divides F2 differences twice: keep tables for first order unless the steps are
// large.
//
// F1 at the nodes comes from the caller (an exact formula) or from Simpson's rule on the curve,
// from 0 outwards (F1(0) = F2(0) = 0, the convention of every curve in Adaa.h and AdaaCurves.h).
// Beyond the range the curve is taken as constant (its value at the edge): F1 continues linearly
// and F2 quadratically, so choose a range where the curve has flattened out.
//
// A table is built off the audio thread (it allocates) and only read afterwards; a plugin
// usually keeps it as an `inline const` global, built once when the plug-in loads. AdaaFastTanh
// below is the ready-made fastTanh curve on such a table.

#include "AdaaCurves.h"
#include "../core/Inline.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bidoo::dsp
{
class AdaaTable
{
public:
    // none: the table covers [-range, range]. odd: the curve is odd (f(-x) = -f(x)), so F1 is
    // even and F2 odd, and the table covers [0, range] only.
    enum class Symmetry
    {
        none,
        odd
    };

    // From the curve alone (F1 by Simpson's rule). range * segmentsPerUnit must be a whole number.
    template <typename Fn>
    AdaaTable (Fn curve, double range, int segmentsPerUnit, Symmetry symmetry = Symmetry::none)
    {
        build (curve, [] (double) { return 0.0; }, false, range, segmentsPerUnit, symmetry);
    }

    // From the curve and its exact first antiderivative (with F1(0) = 0).
    template <typename Fn, typename AntiFn>
    AdaaTable (Fn curve, AntiFn exactF1, double range, int segmentsPerUnit, Symmetry symmetry = Symmetry::none)
    {
        build (curve, exactF1, true, range, segmentsPerUnit, symmetry);
    }

    BIDOO_DSP_INLINE double F1 (double x) const
    {
        const double t = odd ? std::fabs (x) : x;
        const double pos = (t - lo) * invH;
        if (pos >= (double) n)
            return F1hi + fhi * (t - hi);
        if (pos <= 0.0)
            return F1lo + flo * (t - lo);
        const int i = std::min ((int) pos, n - 1);
        const double u = pos - (double) i;
        const double* k = c[(size_t) i].data();
        return ((k[3] * u + k[2]) * u + k[1]) * u + k[0];
    }

    double F2 (double x) const
    {
        const double t = odd ? std::fabs (x) : x;
        const double pos = (t - lo) * invH;
        double v;
        if (pos >= (double) n)
        {
            const double d = t - hi;
            v = F2hi + d * (F1hi + 0.5 * d * fhi);
        }
        else if (pos <= 0.0)
        {
            const double d = t - lo;
            v = F2lo + d * (F1lo + 0.5 * d * flo);
        }
        else
        {
            const int i = std::min ((int) pos, n - 1);
            const double u = pos - (double) i;
            const double* k = c[(size_t) i].data();
            v = g[(size_t) i] + h * u * (k[0] + u * (0.5 * k[1] + u * (k[2] / 3.0 + u * 0.25 * k[3])));
        }
        return odd && x < 0.0 ? -v : v;
    }

    double rangeLow() const { return odd ? -hi : lo; }
    double rangeHigh() const { return hi; }

    // The cubic of segment i (F1 = c0 + c1 u + c2 u^2 + c3 u^3, u in [0, 1)), for a curve that
    // knows the table's layout at compile time and looks it up with constants (AdaaFastTanh).
    BIDOO_DSP_INLINE const double* segment (int i) const { return c[(size_t) i].data(); }

private:
    template <typename Fn, typename AntiFn>
    void build (Fn curve, AntiFn exactF1, bool exact, double range, int perUnit, Symmetry symmetry)
    {
        odd = symmetry == Symmetry::odd;
        h = 1.0 / (double) perUnit;
        invH = (double) perUnit;
        const int half = (int) std::lround (range * perUnit); // segments on each side of 0
        lo = odd ? 0.0 : -half * h;
        hi = half * h;
        n = odd ? half : 2 * half;
        const int zero = odd ? 0 : half; // the node at x = 0

        std::vector<double> F ((size_t) n + 1), f ((size_t) n + 1);
        auto at = [&] (int i) { return lo + (double) i * h; };
        for (int i = 0; i <= n; ++i)
            f[(size_t) i] = (double) curve (at (i));
        if (exact)
            for (int i = 0; i <= n; ++i)
                F[(size_t) i] = (double) exactF1 (at (i));
        else
        {
            // Simpson per segment, from 0 outwards
            F[(size_t) zero] = 0.0;
            for (int i = zero + 1; i <= n; ++i)
                F[(size_t) i] = F[(size_t) i - 1] + h / 6.0 * (f[(size_t) i - 1] + 4.0 * (double) curve (at (i) - 0.5 * h) + f[(size_t) i]);
            for (int i = zero - 1; i >= 0; --i)
                F[(size_t) i] = F[(size_t) i + 1] - h / 6.0 * (f[(size_t) i] + 4.0 * (double) curve (at (i) + 0.5 * h) + f[(size_t) i + 1]);
        }

        c.assign ((size_t) n, {});
        for (int i = 0; i < n; ++i)
        {
            const size_t s = (size_t) i;
            c[s][0] = F[s];
            c[s][1] = h * f[s];
            c[s][2] = 3.0 * (F[s + 1] - F[s]) - h * (2.0 * f[s] + f[s + 1]);
            c[s][3] = 2.0 * (F[s] - F[s + 1]) + h * (f[s] + f[s + 1]);
        }

        // F2 at the nodes: the cubic's exact integral over each segment, accumulated from 0
        g.assign ((size_t) n + 1, 0.0);
        auto segment = [&] (int i)
        {
            const auto& k = c[(size_t) i];
            return h * (k[0] + 0.5 * k[1] + k[2] / 3.0 + 0.25 * k[3]);
        };
        for (int i = zero + 1; i <= n; ++i)
            g[(size_t) i] = g[(size_t) i - 1] + segment (i - 1);
        for (int i = zero - 1; i >= 0; --i)
            g[(size_t) i] = g[(size_t) i + 1] - segment (i);

        flo = f[0];
        fhi = f[(size_t) n];
        F1lo = F[0];
        F1hi = F[(size_t) n];
        F2lo = g[0];
        F2hi = g[(size_t) n];
    }

    std::vector<std::array<double, 4>> c; // per segment: F1 = c0 + c1 u + c2 u^2 + c3 u^3, u in [0, 1)
    std::vector<double> g;                // F2 at the nodes
    double lo = 0.0, hi = 0.0, h = 1.0, invH = 1.0;
    double flo = 0.0, fhi = 0.0, F1lo = 0.0, F1hi = 0.0, F2lo = 0.0, F2hi = 0.0;
    int n = 0;
    bool odd = false;
};

// A curve for adaa() with F1 and F2 from a table and the curve itself from `fn` (used where
// consecutive inputs are too close to divide by). The table must outlive it.
template <typename Fn>
struct AdaaTableCurve
{
    const AdaaTable& table;
    Fn fn;

    BIDOO_DSP_INLINE double f (double x) const { return (double) fn (x); }
    BIDOO_DSP_INLINE double F1 (double x) const { return table.F1 (x); }
    double F2 (double x) const { return table.F2 (x); }
};
template <typename Fn>
AdaaTableCurve (const AdaaTable&, Fn) -> AdaaTableCurve<Fn>;

// fastTanh (shaping/Shapers.h) in double: x (27 + x^2) / (27 + 9 x^2), exactly +-1 beyond +-3.
BIDOO_DSP_INLINE double fastTanhD (double x)
{
    if (x > 3.0)
        return 1.0;
    if (x < -3.0)
        return -1.0;
    const double x2 = x * x;
    return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}

// fastTanh's F1 on [0, 3] at 256 segments per unit (768 segments, 24 kB): exact values and
// slopes at the nodes; beyond 3 the curve is exactly 1, so the linear continuation is exact.
inline const AdaaTable fastTanhAdaaTable { [] (double x) { return fastTanhD (x); }, [] (double x) { return fastTanhIntegral (x); }, 3.0, 256,
                                           AdaaTable::Symmetry::odd };

// fastTanh for adaa(): F1 from the table above (no logarithm per sample), F2 exact (second
// order needs more precision than a table holds).
struct AdaaFastTanh
{
    BIDOO_DSP_INLINE static double f (double x) { return fastTanhD (x); }
    // fastTanhAdaaTable.F1 with the table's layout as constants: this is the hot one
    BIDOO_DSP_INLINE static double F1 (double x)
    {
        const double a = std::fabs (x);
        if (a >= 3.0)
            return a - 3.0 + 2.3483924814931874; // F1(3) = 1/2 + 4/3 ln 4, then slope 1
        const double pos = a * 256.0;
        const int i = std::min ((int) pos, 767);
        const double u = pos - (double) i;
        const double* k = fastTanhAdaaTable.segment (i);
        return ((k[3] * u + k[2]) * u + k[1]) * u + k[0];
    }
    static double F2 (double x) { return fastTanhIntegral2 (x); }

    // F1 with no branch, for drives that often take |x| past 3 (a hard overdrive, FadaBox's OVER):
    // there the branch above is mispredicted all the time, and this one is about 27 % cheaper per
    // ADAA step (bench "hard drive" rows). Below 3 it costs about 7 % more than F1, whose branch is
    // then always predicted: keep F1 for drives that stay under 3. Bit for bit F1 for every input
    // but NaN (NaN out; F1 reads the table at index INT_MIN). From FadaBox 0.52.4.
    // How: |x| clamps to 3 and the lookup lands on segment 768 of hardSegments, F1(3) as a constant
    // past the table's end; max (|x| - 3, 0) adds the linear part (+0 below 3). The sum is F1's
    // (|x| - 3) + F1(3). The clamps compile to minsd / maxsd.
    BIDOO_DSP_INLINE static double F1Hard (double x)
    {
        const double a = std::fabs (x);
        const double pos = (a < 3.0 ? a : 3.0) * 256.0; // NaN -> 3
        const int i = (int) pos;                        // 0 .. 768
        const double u = pos - (double) i;
        const double* k = hardSegments[(size_t) i].data();
        const double d = a - 3.0;
        return (((k[3] * u + k[2]) * u + k[1]) * u + k[0]) + (d < 0.0 ? 0.0 : d); // NaN stays NaN
    }

    // fastTanhAdaaTable's 768 segments followed by F1(3) = 1/2 + 4/3 ln 4 as a constant (the
    // constant F1 continues from; the table's own last node, from std::log, is 1 ulp lower). A copy
    // (24.6 kB, filled when the plug-in loads) because the edge must sit right after the last
    // segment: choosing between the table and a separate edge costs a branch or a blend, both slower.
    static inline const std::array<std::array<double, 4>, 769> hardSegments = []
    {
        std::array<std::array<double, 4>, 769> s {};
        for (int i = 0; i < 768; ++i)
            for (int j = 0; j < 4; ++j)
                s[(size_t) i][(size_t) j] = fastTanhAdaaTable.segment (i)[j];
        s[768] = { 2.3483924814931874, 0.0, 0.0, 0.0 };
        return s;
    }();
};

// AdaaFastTanh with F1Hard, for adaa() on drives that often exceed |x| = 3 (see F1Hard).
struct AdaaFastTanhHard
{
    BIDOO_DSP_INLINE static double f (double x) { return fastTanhD (x); }
    BIDOO_DSP_INLINE static double F1 (double x) { return AdaaFastTanh::F1Hard (x); }
    static double F2 (double x) { return fastTanhIntegral2 (x); }
};
} // namespace bidoo::dsp

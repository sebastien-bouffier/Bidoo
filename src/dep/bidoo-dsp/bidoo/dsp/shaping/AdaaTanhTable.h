#pragma once

// Exact tanh and the rational sigmoid v / (1 + |v|) for first-order ADAA (shaping/Adaa.h), with
// their first antiderivatives tabulated (shaping/AdaaTable.h) instead of computed: log cosh costs an
// exp and a log1p per sample in double, |v| - log (1 + |v|) a log1p. From Quichar, Potion and Mangle,
// which each had a copy.
//
// The tables keep F1 as Hermite cubics between exact nodes (exact values and slopes), so the slope
// ADAA outputs stays within h^3 max|f'''| / 100 of the curve: about 1e-7 at 64 segments per unit,
// float resolution. Both curves are odd, so the tables cover [0, range] only; the hot path reads them
// with the layout as constants (AdaaTable::F1 would check the symmetry and both ends every sample).
// F2 is exact (only the AdaaBiased / AdaaScaled wrappers' constants and second order use it).
//
//   AdaaTanhTable<64>       tanh, table on [0, 10] at 64 segments per unit (640 segments, 20 kB):
//                           Quichar (soft, tube, fuzz), Potion (VoiceFx soft and tube)
//   AdaaTanhTable<128>      the same at 128 segments per unit (40 kB): Mangle (Drive)
//   AdaaRationalSigmoidTable  v / (1 + |v|), table on [0, 16] at 64 per unit (32 kB), exact F1
//                           beyond 16 (the curve never flattens): Quichar (tape)
// Beyond 10, tanh is 1 to within 4e-9 and F1 continues linearly from the edge node (the table's own
// continuation, so F1 stays smooth there).
//
// The tables are plain `inline const` globals (not templates) built once when the plug-in loads.
// That keeps their initialisation ordered before anything declared after this header in the same
// translation unit, so a curve constant built at load time (Potion's `static inline const
// AdaaBiased<AdaaTanhTable<>> tubeCurve`, whose constructor reads F1) sees a built table. The
// price: every plug-in including this header builds all three (92 kB, well under a millisecond).
// Read-only afterwards, safe from any thread.

#include "Adaa.h"
#include "AdaaCurves.h"
#include "AdaaTable.h"
#include "../core/Inline.h"
#include <algorithm>
#include <cmath>

namespace bidoo::dsp
{
namespace adaa_tables
{
constexpr double kTanhRange = 10.0;
constexpr double kRationalRange = 16.0;
constexpr int kRationalSegmentsPerUnit = 64;

inline const AdaaTable tanh64 { [] (double v) { return std::tanh (v); }, [] (double v) { return logCosh (v); }, kTanhRange, 64,
                                AdaaTable::Symmetry::odd };
inline const AdaaTable tanh128 { [] (double v) { return std::tanh (v); }, [] (double v) { return logCosh (v); }, kTanhRange, 128,
                                 AdaaTable::Symmetry::odd };
inline const double tanhEdgeF1 = logCosh (kTanhRange), tanhEdgeF = std::tanh (kTanhRange);

inline const AdaaTable rationalSigmoid { [] (double v) { return AdaaRationalSigmoid::f (v); },
                                         [] (double v) { return AdaaRationalSigmoid::F1 (v); }, kRationalRange,
                                         kRationalSegmentsPerUnit, AdaaTable::Symmetry::odd };

// F1 of an odd table at a = |v| inside its range: Horner on the segment, the layout as constants
template <int SegmentsPerUnit>
BIDOO_DSP_INLINE double oddTableF1 (const AdaaTable& t, double a, int lastSegment)
{
    const double pos = a * static_cast<double> (SegmentsPerUnit);
    const int i = std::min (static_cast<int> (pos), lastSegment);
    const double u = pos - static_cast<double> (i);
    const double* k = t.segment (i);
    return ((k[3] * u + k[2]) * u + k[1]) * u + k[0];
}
} // namespace adaa_tables

// Exact tanh for adaa(): f = std::tanh, F1 from the table, F2 = logCoshIntegral (exact).
template <int SegmentsPerUnit = 64>
struct AdaaTanhTable
{
    static_assert (SegmentsPerUnit == 64 || SegmentsPerUnit == 128, "tables exist at 64 and 128 segments per unit");

    static const AdaaTable& table()
    {
        if constexpr (SegmentsPerUnit == 64)
            return adaa_tables::tanh64;
        else
            return adaa_tables::tanh128;
    }

    BIDOO_DSP_INLINE static double f (double v) { return std::tanh (v); }
    BIDOO_DSP_INLINE static double F1 (double v)
    {
        constexpr int last = static_cast<int> (adaa_tables::kTanhRange) * SegmentsPerUnit - 1;
        const double a = std::fabs (v);
        if (! (a < adaa_tables::kTanhRange)) // (written so that a NaN takes this path and stays NaN: never a table index)
            return adaa_tables::tanhEdgeF1 + adaa_tables::tanhEdgeF * (a - adaa_tables::kTanhRange);
        return adaa_tables::oddTableF1<SegmentsPerUnit> (table(), a, last);
    }
    static double F2 (double v) { return logCoshIntegral (v); }
};

// v / (1 + |v|) for adaa(): F1 from the table inside [-16, 16], exact beyond; F2 exact.
struct AdaaRationalSigmoidTable
{
    BIDOO_DSP_INLINE static double f (double v) { return AdaaRationalSigmoid::f (v); }
    BIDOO_DSP_INLINE static double F1 (double v)
    {
        constexpr int last = static_cast<int> (adaa_tables::kRationalRange) * adaa_tables::kRationalSegmentsPerUnit - 1;
        const double a = std::fabs (v);
        if (! (a < adaa_tables::kRationalRange)) // (a NaN takes the exact path and stays NaN: never a table index)
            return a - std::log1p (a);
        return adaa_tables::oddTableF1<adaa_tables::kRationalSegmentsPerUnit> (adaa_tables::rationalSigmoid, a, last);
    }
    static double F2 (double v) { return AdaaRationalSigmoid::F2 (v); }
};
} // namespace bidoo::dsp

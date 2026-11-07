#pragma once

// FIR design with the Kaiser window (from Plafond): windowed-sinc low passes and half-band filters
// sized from a specification (transition width and stopband attenuation). Replaces
// juce::dsp::FilterDesign's equiripple half-band design: for the same specification a Kaiser
// design needs a few more taps (about 5 to 10 %) but has no design iteration and no JUCE.
// Design runs off the audio thread (it allocates), except halfBandOddTaps, which writes to a
// caller's array.

#include "../core/Math.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace bidoo::dsp::fir
{
inline double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        const double t = x / (2.0 * k);
        term *= t * t;
        sum += term;
        if (term < 1.0e-12 * sum)
            break;
    }
    return sum;
}

inline double kaiser (int i, int length, double beta)
{
    const double r = 2.0 * i / (double) (length - 1) - 1.0;
    return besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
}

// Kaiser's beta for a stopband attenuation in dB (positive).
inline double kaiserBeta (double attenuationDb)
{
    const double a = std::abs (attenuationDb);
    if (a > 50.0)
        return 0.1102 * (a - 8.7);
    if (a >= 21.0)
        return 0.5842 * std::pow (a - 21.0, 0.4) + 0.07886 * (a - 21.0);
    return 0.0;
}

// Kaiser's length estimate for a transition width relative to the sample rate.
inline int kaiserLength (double attenuationDb, double transitionWidth)
{
    const double a = std::abs (attenuationDb);
    return (int) std::ceil ((a - 7.95) / (14.36 * transitionWidth)) + 1;
}

// Windowed sinc low pass, cutoff as a fraction of the sample rate (0.25 = half band), unity gain
// at DC.
inline std::vector<double> windowedSinc (int length, double cutoff, double beta)
{
    std::vector<double> h ((size_t) length);
    const double centre = 0.5 * (length - 1);
    double sum = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const double t = i - centre;
        const double s = std::abs (t) < 1.0e-9 ? 2.0 * cutoff : std::sin (2.0 * kPi * cutoff * t) / (kPi * t);
        h[(size_t) i] = s * kaiser (i, length, beta);
        sum += h[(size_t) i];
    }
    for (auto& v : h)
        v /= sum;
    return h;
}

// A half-band low pass (cutoff at a quarter of its sample rate) for a transition width
// (relative to the filter's sample rate, centred on the quarter) and a stopband attenuation in
// dB, as juce::dsp::FilterDesign::designFIRLowpassHalfBandEquirippleMethod is specified.
// Length 4m + 3, so the centre index is odd and every tap at an even distance from it (other
// than the centre) is zero: the polyphase form the oversamplers use.
inline std::vector<double> halfBand (double transitionWidth, double attenuationDb)
{
    int length = std::max (7, kaiserLength (attenuationDb, transitionWidth));
    while ((length - 3) % 4 != 0)
        ++length;
    auto h = windowedSinc (length, 0.25, kaiserBeta (attenuationDb));
    const int c = (length - 1) / 2;
    for (int i = 0; i < length; ++i)
        if (i != c && ((i - c) % 2) == 0)
            h[(size_t) i] = 0.0; // exactly zero (sin(pi k) leaves rounding noise)
    return h;
}

// A half-band low pass given by its half length k and Kaiser beta (from Bando), for stages that
// keep only the taps at odd distances 1, 3, .. 2k - 1 from the centre (the centre is 0.5, the
// other even ones zero; length 4k - 1). Unlike halfBand above, the window reaches zero just past
// the outermost tap (at distance 2k, not on it) and beta is given directly, not derived from an
// attenuation. Writes the k odd taps (one side) to a[], scaled for unity gain at DC. The Bessel
// series runs a fixed 30 terms (no early stop), so the taps are bit-identical to Bando's own.
// Allocation-free.
inline void halfBandOddTaps (int k, double beta, double* a)
{
    auto i0 = [] (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int i = 1; i < 30; ++i)
        {
            term *= (x * 0.5 / i) * (x * 0.5 / i);
            sum += term;
        }
        return sum;
    };
    const double half = 2.0 * k;
    double sum = 0.0;
    for (int i = 0; i < k; ++i)
    {
        const double d = 2.0 * i + 1.0, t = d / half;
        const double w = i0 (beta * std::sqrt (std::max (0.0, 1.0 - t * t))) / i0 (beta);
        a[i] = std::sin (kPi * d * 0.5) / (kPi * d) * w;
        sum += 2.0 * a[i];
    }
    for (int i = 0; i < k; ++i)
        a[i] *= 0.5 / sum; // the centre tap (0.5) plus the odd taps (0.5)
}
} // namespace bidoo::dsp::fir

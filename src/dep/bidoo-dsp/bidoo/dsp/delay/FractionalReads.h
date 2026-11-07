#pragma once

// Fractional read policies for ModDelayLine-style buffers (anything with at (age), the sample
// `age` steps in the past, 0 = the one just pushed, and maxDelay()): ModDelayLine itself offers
// linear and Hermite; these are the better ones.
//  - readLagrange6 (buf, delay): 6-point Lagrange, from Grotte's reverb. Its high-frequency loss
//    barely changes with the fraction, so a modulated line doesn't flutter at the top of the
//    spectrum (a Hermite read dips more at some fractions than at others). Delay >= 2.
//  - readSinc (buf, delay): 16-tap windowed sinc, from Retour (SincTable in SincDelayLine.h):
//    flat to about 18 kHz at 44.1 kHz, so feedback loops don't darken. Delay >= 8. For the
//    fastest sinc reads on a big loop use SincDelayLine (SSE2, same samples).
// Both clamp the delay into the usable range. Weights are float, summed in tap order.

#include "SincDelayLine.h"
#include "../core/Inline.h"
#include <algorithm>

namespace bidoo::dsp
{
// Weights of the taps at offsets -2..3 from the integer part, for a fraction t in [0, 1).
template <typename T>
BIDOO_DSP_INLINE void lagrange6Weights (T t, T* h) noexcept
{
    const T a = t + 2, b = t + 1, c = t, d = t - 1, e = t - 2, f = t - 3;
    h[0] = -b * c * d * e * f / (T) 120;
    h[1] = a * c * d * e * f / (T) 24;
    h[2] = -a * b * d * e * f / (T) 12;
    h[3] = a * b * c * e * f / (T) 12;
    h[4] = -a * b * c * d * f / (T) 24;
    h[5] = a * b * c * d * e / (T) 120;
}

template <typename Buffer>
BIDOO_DSP_INLINE float readLagrange6 (const Buffer& buf, float delay) noexcept
{
    delay = std::clamp (delay, 2.0f, (float) (buf.maxDelay() - 1));
    const int i = (int) delay;
    float h[6];
    lagrange6Weights (delay - (float) i, h);
    float acc = 0.0f;
    for (int k = 0; k < 6; ++k)
        acc += h[k] * buf.at (i + k - 2);
    return acc;
}

template <typename Buffer>
BIDOO_DSP_INLINE float readSinc (const Buffer& buf, float delay) noexcept
{
    constexpr int n = SincTable::kTaps, half = SincTable::kHalf;
    delay = std::clamp (delay, (float) half, (float) (buf.maxDelay() - n));
    const int di = (int) delay;
    const float pf = (delay - (float) di) * (float) SincTable::kPhases;
    const int pi = std::min ((int) pf, SincTable::kPhases - 1);
    const float pt = pf - (float) pi;
    const float* h = sincTable.h[pi];
    const float* dh = sincTable.dh[pi];
    // as SincDelayLine::read (delay + 1): tap j is the sample written di + 1 - (half - 1) + j ago
    float acc = 0.0f;
    for (int j = 0; j < n; ++j)
        acc += (h[j] + pt * dh[j]) * buf.at (di + 1 - half + j);
    return acc;
}
} // namespace bidoo::dsp

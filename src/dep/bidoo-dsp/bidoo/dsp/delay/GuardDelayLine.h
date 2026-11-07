#pragma once

// GuardDelayLine: a circular delay line with a guard zone, from Grotte's reverb. The first 8
// samples are mirrored past the end of the buffer, so the taps of a read are contiguous (one
// wrapped index per read instead of one per tap). Reads:
//  - tap (d): d = 0 is the newest sample;
//  - read (d): 4-point Hermite, d >= 1 (smooth under modulation, no zipper);
//  - read6 (d): 6-point Lagrange, d >= 3. Its high-frequency loss barely changes with the
//    fraction, so a modulated line doesn't flutter at the top of the spectrum;
//  - window (i) with hermiteWeights / lagrange6: the same reads as weights of the raw samples,
//    when a still read becomes a fixed FIR or the weights of many lines are worked out together.
// Power-of-two size. Same indexing as ModDelayLine's at (age): tap (d) is the sample d steps in the
// past (0 = the one just pushed), read (d) == ModDelayLine::readHermite (d) for d >= 1.

#include "../core/Inline.h"
#include <algorithm>
#include <vector>

namespace bidoo::dsp
{
class GuardDelayLine
{
public:
    void prepare (int maxDelay)
    {
        int size = 16;
        while (size < maxDelay + 8)
            size <<= 1;
        // the first kGuard samples are mirrored past the end, so a read's taps are contiguous
        // (one wrapped index per read instead of one per tap)
        buf.assign ((size_t) (size + kGuard), 0.0f);
        mask = size - 1;
        w = 0;
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        w = 0;
    }

    inline void push (float x)
    {
        buf[(size_t) w] = x;
        if (w < kGuard)
            buf[(size_t) (w + mask + 1)] = x;
        w = (w + 1) & mask;
    }

    // d = 0: the newest sample.
    inline float tap (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }

    // Fractional delay (d >= 1), 4-point Hermite: smooth under modulation, no zipper.
    inline float read (float d) const
    {
        const int i = (int) d;
        const float t = d - (float) i;
        const float* p = buf.data() + ((w - 3 - i) & mask); // p[0] = tap (i + 2) .. p[3] = tap (i - 1)
        const float xm1 = p[3], x0 = p[2], x1 = p[1], x2 = p[0];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    // The 4 samples a Hermite read at integer part i uses, oldest first: (tap (i + 2) .. tap (i - 1)).
    inline const float* window (int i) const { return buf.data() + ((w - 3 - i) & mask); }

    // The Hermite read above as weights of window()'s 4 samples, for a fraction t.
    static inline void hermiteWeights (double t, double* h)
    {
        const double t2 = t * t, t3 = t2 * t;
        h[0] = 0.5 * (t3 - t2);                  // x2
        h[1] = 0.5 * t + 2.0 * t2 - 1.5 * t3;    // x1
        h[2] = 1.0 - 2.5 * t2 + 1.5 * t3;        // x0
        h[3] = -0.5 * t + t2 - 0.5 * t3;         // xm1
    }

    // Fractional delay (d >= 3), 6-point Lagrange: its high-frequency loss barely changes with
    // the fraction, so a modulated line doesn't flutter at the top of the spectrum.
    inline float read6 (float d) const
    {
        const int i = (int) d;
        const float t = d - (float) i;
        float h[6];
        lagrange6 (t, h);
        const float* p = buf.data() + ((w - 4 - i) & mask); // p[5 - k] = tap (i + k - 2)
        float acc = 0.0f;
        for (int k = 0; k < 6; ++k)
            acc += h[k] * p[5 - k];
        return acc;
    }

    // the same with the integer part and the weights (lagrange6) already worked out
    inline float read6 (int i, const float* h) const
    {
        const float* p = buf.data() + ((w - 4 - i) & mask);
        float acc = 0.0f;
        for (int k = 0; k < 6; ++k)
            acc += h[k] * p[5 - k];
        return acc;
    }

    // weights of the taps at offsets -2..3 from the integer part, for a fraction t
    template <typename T>
    static inline void lagrange6 (T t, T* h)
    {
        const T a = t + 2, b = t + 1, c = t, d = t - 1, e = t - 2, f = t - 3;
        h[0] = -b * c * d * e * f / (T) 120;
        h[1] = a * c * d * e * f / (T) 24;
        h[2] = -a * b * d * e * f / (T) 12;
        h[3] = a * b * c * e * f / (T) 12;
        h[4] = -a * b * c * d * f / (T) 24;
        h[5] = a * b * c * d * e / (T) 120;
    }

    inline float readLinear (float d) const
    {
        const int i = (int) d;
        const float t = d - (float) i;
        return tap (i) + t * (tap (i + 1) - tap (i));
    }

    int capacity() const { return mask - 4; }

private:
    static constexpr int kGuard = 8;
    std::vector<float> buf;
    int mask = 0, w = 0;
};

} // namespace bidoo::dsp

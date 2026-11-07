#pragma once

// ModDelayLine: a circular delay line for modulated and fractional reads (delays, choruses,
// flangers, pitch heads), from Bourriho's DelayBuffer. Power-of-two size, masked indices.
// Push one sample, then read at any delay in samples:
//  - at (age): the sample `age` steps in the past (0 = the one just pushed);
//  - readLinear (delay): linear interpolation;
//  - readHermite (delay): 4-point Hermite (Catmull-Rom), linear below one sample where there is no
//    newer sample to interpolate with.
// For fixed whole-sample delays (latency compensation) DelayLine in dynamics/Dynamics.h is simpler.

#include "../core/Inline.h"
#include <algorithm>
#include <vector>

namespace bidoo::dsp
{
class ModDelayLine
{
public:
    // Allocates: call from prepare. The longest usable delay is maxDelay().
    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 8)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    BIDOO_DSP_INLINE void push (float x) noexcept
    {
        writePos = (writePos + 1) & mask;
        buffer[(size_t) writePos] = x;
    }

    BIDOO_DSP_INLINE float at (int age) const noexcept { return buffer[(size_t) ((writePos - age) & mask)]; }

    BIDOO_DSP_INLINE float readLinear (float delay) const noexcept
    {
        delay = std::clamp (delay, 0.0f, (float) (mask - 4));
        const int i = (int) delay;
        const float f = delay - (float) i;
        return at (i) + f * (at (i + 1) - at (i));
    }

    BIDOO_DSP_INLINE float readHermite (float delay) const noexcept
    {
        delay = std::clamp (delay, 0.0f, (float) (mask - 4));
        const int i = (int) delay;
        const float f = delay - (float) i;
        if (i == 0) // under one sample: no newer sample to interpolate with, so stay linear
            return at (0) + f * (at (1) - at (0));

        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    int maxDelay() const noexcept { return mask - 4; }

private:
    std::vector<float> buffer { 0.0f };
    int mask = 0, writePos = 0;
};
} // namespace bidoo::dsp

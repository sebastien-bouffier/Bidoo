#pragma once

// Fractional reads with Resampler.h's polyphase sinc kernel, for audio that is not a whole sample
// with a mip chain: a buffer that is still being recorded (a live take), a ring that a time
// stretcher fills as it goes. Same interpolator as SampleResampler::read at its finest level
// (bit-identical to it there), so for a read step up to about one source sample per output sample
// it is as clean as the sample players (Quality Good: 32 taps, 90 dB, flat to 0.41 fs); a larger
// step (a pitch up) is not band-limited here, which needs the mips: filter the signal on the way
// in, or build a SampleResampler once the audio is final.
//
//  - kernelRead / kernelRead2: a contiguous buffer, silent outside [0, length) (the same as a
//    MipChain's level 0), one or two channels sharing the index and the phase work.
//  - kernelReadRing: a power-of-two ring buffer (mask = size - 1) whose taps are all valid: no bounds
//    checks, the 32 taps are read in place unless they wrap round the end.
//  - kernelReadAt: any storage, through at(i) -> float for an integer index (a ring buffer with
//    its own wrap and bounds); the 32 taps are gathered once, then the same dot products.
// The kernel comes from ResampleKernel::shared (q): call it once off the audio thread so its
// table is built before the first read.

#include "Resampler.h"
#include <cstdint>

namespace bidoo::dsp
{
namespace kernel_read_detail
{
// Index of the first tap, the two phases and the fraction between them, for a position.
struct Where
{
    int64_t first;
    const float *a, *b;
    float frac;
};

BIDOO_DSP_INLINE Where locate (const ResampleKernel& k, double position) noexcept
{
    const int taps = k.taps();
    const double fl = std::floor (position);
    const int64_t i = (int64_t) fl;
    const double fp = (position - fl) * k.phases();
    int p = (int) fp;
    if (p >= k.phases())
        p = k.phases() - 1;
    return { i - (taps / 2 - 1), k.phase (p), k.phase (p + 1), (float) (fp - (double) p) };
}
} // namespace kernel_read_detail

// x[0, length) read at a position (fractional, in samples); zero outside.
BIDOO_DSP_INLINE float kernelRead (const ResampleKernel& k, const float* x, int length, double position) noexcept
{
    const int taps = k.taps();
    if (x == nullptr || position < (double) (-taps / 2) || position >= (double) (length + taps / 2))
        return 0.0f;
    const auto w = kernel_read_detail::locate (k, position);
    float s0, s1;
    if (w.first >= 0 && w.first + taps <= length)
        resampler_detail::dot2 (w.a, w.b, x + w.first, taps, s0, s1);
    else
    {
        float tmp[64];
        for (int j = 0; j < taps; ++j)
        {
            const int64_t idx = w.first + j;
            tmp[j] = idx >= 0 && idx < (int64_t) length ? x[idx] : 0.0f;
        }
        resampler_detail::dot2 (w.a, w.b, tmp, taps, s0, s1);
    }
    return s0 + (s1 - s0) * w.frac;
}

// Two channels of the same length at the same position (the phase work is shared). x1 may be x0.
BIDOO_DSP_INLINE void kernelRead2 (const ResampleKernel& k, const float* x0, const float* x1, int length, double position,
                                   float& out0, float& out1) noexcept
{
    const int taps = k.taps();
    out0 = out1 = 0.0f;
    if (x0 == nullptr || position < (double) (-taps / 2) || position >= (double) (length + taps / 2))
        return;
    const auto w = kernel_read_detail::locate (k, position);
    float s0, s1;
    if (w.first >= 0 && w.first + taps <= length)
    {
        resampler_detail::dot2 (w.a, w.b, x0 + w.first, taps, s0, s1);
        out0 = s0 + (s1 - s0) * w.frac;
        if (x1 == x0 || x1 == nullptr)
        {
            out1 = out0;
            return;
        }
        resampler_detail::dot2 (w.a, w.b, x1 + w.first, taps, s0, s1);
        out1 = s0 + (s1 - s0) * w.frac;
        return;
    }
    float tmp[64];
    for (int c = 0; c < 2; ++c)
    {
        const float* x = c == 0 ? x0 : (x1 != nullptr ? x1 : x0);
        for (int j = 0; j < taps; ++j)
        {
            const int64_t idx = w.first + j;
            tmp[j] = idx >= 0 && idx < (int64_t) length ? x[idx] : 0.0f;
        }
        resampler_detail::dot2 (w.a, w.b, tmp, taps, s0, s1);
        (c == 0 ? out0 : out1) = s0 + (s1 - s0) * w.frac;
    }
}

// A ring of size mask + 1 (a power of two): the caller guarantees the taps around `position` hold
// valid audio. Read in place unless the window wraps round the end of the ring.
BIDOO_DSP_INLINE float kernelReadRing (const ResampleKernel& k, const float* ring, int64_t mask, double position) noexcept
{
    const auto w = kernel_read_detail::locate (k, position);
    const int taps = k.taps();
    const int64_t start = w.first & mask;
    float s0, s1;
    if (start + taps <= mask + 1)
        resampler_detail::dot2 (w.a, w.b, ring + start, taps, s0, s1);
    else
    {
        float tmp[64];
        for (int j = 0; j < taps; ++j)
            tmp[j] = ring[(w.first + j) & mask];
        resampler_detail::dot2 (w.a, w.b, tmp, taps, s0, s1);
    }
    return s0 + (s1 - s0) * w.frac;
}

// Any storage: at(i) returns the sample at integer index i (the caller's wrap, bounds and silence).
template <typename At>
BIDOO_DSP_INLINE float kernelReadAt (const ResampleKernel& k, double position, At&& at) noexcept
{
    const auto w = kernel_read_detail::locate (k, position);
    const int taps = k.taps();
    float tmp[64];
    for (int j = 0; j < taps; ++j)
        tmp[j] = at (w.first + j);
    float s0, s1;
    resampler_detail::dot2 (w.a, w.b, tmp, taps, s0, s1);
    return s0 + (s1 - s0) * w.frac;
}
} // namespace bidoo::dsp

#pragma once

// Band-limited kernels for a read head that moves up to two source samples per output sample, on
// audio that has no mip chain of its own (a live history: delay/HistoryTap.h's readFast). From
// Mangle 0.12.25 (bit-identical).
//
// Up to one sample per sample the read is Resampler.h's Good kernel (32 taps, 512 phases, Kaiser
// 90 dB). Faster, the kernel's interpolation images are not the only problem: whatever lies above
// sr / (2 step) folds back below the output's Nyquist. So above 1x the read uses the same design
// with 64 taps (a transition half as wide, 0.09 of the rate) and its sinc narrowed so that its
// stopband starts at 0.5 / step: what would fold is 90 dB down first. One kernel per semitone of
// step from 1 to 2 (12, each designed for the top of its semitone, so the stopband is never above
// 0.5 / step), plus one whose stopband starts at levelStop(): for reads from a decimated copy (the
// MipChain's Good half-band, which is clean only up to 0.41 of its rate), whatever the step.
//
// Past a step of 2 the next decimated copy (half the rate) gives the same bandwidth for a quarter
// of the work, so kTop is the largest step a kernel serves. All 13 tables share one build
// (StepKernels::shared(), about 1.7 MB, built once off the audio thread).
//
// step_kernels_detail::blendDot2 and dot2ch are the stereo dot products of the 64-tap reads and of
// the half-band decimators that write the copies (SSE2 / NEON when present).

#include "Resampler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bidoo::dsp
{
namespace step_kernels_detail
{
// a . x and a . y over n floats (n a multiple of 8): one filter on two channels, with four
// independent sums (one long chain of adds was the cost).
BIDOO_DSP_INLINE void dot2ch (const float* a, const float* x, const float* y, int n, float& sx, float& sy) noexcept
{
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    __m128 x0 = _mm_setzero_ps(), x1 = _mm_setzero_ps(), y0 = _mm_setzero_ps(), y1 = _mm_setzero_ps();
    for (int k = 0; k < n; k += 8)
    {
        const __m128 a0 = _mm_loadu_ps (a + k), a1 = _mm_loadu_ps (a + k + 4);
        x0 = _mm_add_ps (x0, _mm_mul_ps (a0, _mm_loadu_ps (x + k)));
        x1 = _mm_add_ps (x1, _mm_mul_ps (a1, _mm_loadu_ps (x + k + 4)));
        y0 = _mm_add_ps (y0, _mm_mul_ps (a0, _mm_loadu_ps (y + k)));
        y1 = _mm_add_ps (y1, _mm_mul_ps (a1, _mm_loadu_ps (y + k + 4)));
    }
    const __m128 t = _mm_add_ps (x0, x1), u = _mm_add_ps (y0, y1);
    __m128 sum = _mm_add_ps (_mm_unpacklo_ps (t, u), _mm_unpackhi_ps (t, u)); // t0+t2 u0+u2 t1+t3 u1+u3
    sum = _mm_add_ps (sum, _mm_movehl_ps (sum, sum));
    sx = _mm_cvtss_f32 (sum);
    sy = _mm_cvtss_f32 (_mm_shuffle_ps (sum, sum, 1));
#else
    float px[8] {}, py[8] {};
    for (int k = 0; k < n; k += 8)
        for (int l = 0; l < 8; ++l)
        {
            px[l] += a[k + l] * x[k + l];
            py[l] += a[k + l] * y[k + l];
        }
    sx = ((px[0] + px[4]) + (px[1] + px[5])) + ((px[2] + px[6]) + (px[3] + px[7]));
    sy = ((py[0] + py[4]) + (py[1] + py[5])) + ((py[2] + py[6]) + (py[3] + py[7]));
#endif
}

// The two channels at one kernel position: the kernel's two neighbouring phases a and b blended
// by frac once, then one dot product per channel (n a multiple of 8, at most 64). Cheaper than
// resampler_detail::dot2 per channel for long kernels (it rounds differently).
BIDOO_DSP_INLINE void blendDot2 (const float* a, const float* b, float frac, const float* x, const float* y, int n, float& sx,
                                 float& sy) noexcept
{
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    const __m128 f = _mm_set1_ps (frac);
    __m128 x0 = _mm_setzero_ps(), x1 = _mm_setzero_ps(), y0 = _mm_setzero_ps(), y1 = _mm_setzero_ps();
    for (int k = 0; k < n; k += 8)
    {
        const __m128 a0 = _mm_loadu_ps (a + k), a1 = _mm_loadu_ps (a + k + 4);
        const __m128 c0 = _mm_add_ps (a0, _mm_mul_ps (_mm_sub_ps (_mm_loadu_ps (b + k), a0), f));
        const __m128 c1 = _mm_add_ps (a1, _mm_mul_ps (_mm_sub_ps (_mm_loadu_ps (b + k + 4), a1), f));
        x0 = _mm_add_ps (x0, _mm_mul_ps (c0, _mm_loadu_ps (x + k)));
        x1 = _mm_add_ps (x1, _mm_mul_ps (c1, _mm_loadu_ps (x + k + 4)));
        y0 = _mm_add_ps (y0, _mm_mul_ps (c0, _mm_loadu_ps (y + k)));
        y1 = _mm_add_ps (y1, _mm_mul_ps (c1, _mm_loadu_ps (y + k + 4)));
    }
    const __m128 t = _mm_add_ps (x0, x1), u = _mm_add_ps (y0, y1);
    __m128 sum = _mm_add_ps (_mm_unpacklo_ps (t, u), _mm_unpackhi_ps (t, u));
    sum = _mm_add_ps (sum, _mm_movehl_ps (sum, sum));
    sx = _mm_cvtss_f32 (sum);
    sy = _mm_cvtss_f32 (_mm_shuffle_ps (sum, sum, 1));
#else
    float c[64];
    for (int k = 0; k < n; ++k)
        c[k] = a[k] + (b[k] - a[k]) * frac;
    dot2ch (c, x, y, n, sx, sy);
#endif
}
} // namespace step_kernels_detail

class StepKernels
{
public:
    static constexpr int kPhases = 512, kWideTaps = 64, kBands = 12;
    // past this step the next decimated copy is as wide
    static constexpr double kTop = 2.0;

    struct Kernel
    {
        const float* table; // kPhases + 1 phases of `taps` (ResampleKernel's layout)
        int taps;
    };

    // Built once, off the audio thread (HistoryBuffer::prepare calls it).
    static const StepKernels& shared()
    {
        static const StepKernels k;
        return k;
    }

    // The kernel for a step of at most kTop, on the source itself or on a decimated copy, whose
    // content is clean up to `levelStop` of its rate only.
    Kernel forStep (double step, bool decimated, double levelStop) const noexcept
    {
        if (step <= 1.0 && ! decimated)
            return { good, 32 };
        int j = 0;
        while (j + 1 < kBands && step > bandTop[j])
            ++j;
        if (decimated && 0.5 / bandTop[j] > levelStop)
            return { tables[(size_t) kBands].data(), kWideTaps };
        return { tables[(size_t) j].data(), kWideTaps };
    }

    // Where a decimated copy's half-band transition starts (0.41 of its rate): the stopband of the
    // reads from it starts there at the latest.
    static double levelStop()
    {
        const auto& g = ResampleKernel::shared (ResampleQuality::Good);
        return 1.0 - 2.0 * (0.25 + 0.5 * halfBandWidth (g));
    }
    // The half-band of Resampler.h's MipChain for the Good kernel: transition width (0.09).
    static double halfBandWidth (const ResampleKernel& g) { return std::max (0.01, 0.5 * (1.0 - 2.0 * g.passband())); }

private:
    StepKernels()
    {
        const auto& g = ResampleKernel::shared (ResampleQuality::Good);
        good = g.phase (0);
        const double att = g.stopbandDb();
        const double transition = (att - 7.95) / (14.36 * kWideTaps); // Kaiser's estimate, as ResampleKernel's
        const double beta = fir::kaiserBeta (att);
        const double i0Beta = fir::besselI0 (beta);
        for (int j = 0; j <= kBands; ++j)
        {
            const double step = std::pow (2.0, (j + 1) / 12.0);
            if (j < kBands)
                bandTop[j] = step;
            // the -6 dB point: stopband from 0.5 / step (the last: from levelStop)
            const double cutoff = (j < kBands ? 0.5 / step : levelStop()) - 0.5 * transition;
            auto& table = tables[(size_t) j];
            table.assign ((size_t) (kPhases + 1) * kWideTaps, 0.0f);
            for (int p = 0; p <= kPhases; ++p)
            {
                const double frac = (double) p / kPhases;
                double h[kWideTaps], sum = 0.0;
                for (int k = 0; k < kWideTaps; ++k)
                {
                    const double t = (double) (k - (kWideTaps / 2 - 1)) - frac;
                    const double u = 2.0 * cutoff * t;
                    const double s = std::abs (u) < 1.0e-12 ? 1.0 : std::sin (kPi * u) / (kPi * u);
                    const double r = t / (0.5 * kWideTaps);
                    const double w = std::abs (r) >= 1.0 ? 0.0 : fir::besselI0 (beta * std::sqrt (1.0 - r * r)) / i0Beta;
                    h[k] = s * w;
                    sum += h[k];
                }
                for (int k = 0; k < kWideTaps; ++k)
                    table[(size_t) p * kWideTaps + (size_t) k] = (float) (h[k] / sum); // unity at DC for every phase
            }
        }
    }

    const float* good = nullptr;
    double bandTop[kBands] {};
    std::array<std::vector<float>, kBands + 1> tables;
};
} // namespace bidoo::dsp

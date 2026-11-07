#pragma once

// Real-time variable-rate resampling for sample playback at any pitch, written from Laurent de
// Soras' paper "The Quest For The Perfect Resampler" (2003): the source is kept as a chain of
// half-band low-passed, decimated copies (mip levels, 2x apart), and each output sample is read
// from the level where the read step is at most about one sample, with a polyphase windowed-sinc
// interpolator (a table of fractional-delay FIR phases, linearly interpolated between two
// neighbouring phases). Reading never decimates inside the interpolator, so there is no
// per-pitch filter design and no aliasing: the only cost is picking a level, two dot products and
// one lerp. No code of his Resampler library (LGPL) was used or read; the kernels are designed
// here with fir:: (Kaiser window) and the levels with fir::halfBand.
//
//  - ResampleKernel: the interpolation table for a Quality. Shared (shared(q)) or owned; building
//    it allocates, do it off the audio thread (call shared() once from the main thread).
//  - MipChain: one channel of a sample, built on the main thread (build() allocates); read-only
//    afterwards. Level 0 is a padded copy of the source, level k+1 is level k half-band filtered
//    and decimated by 2 with the same time alignment, so switching level while the pitch moves
//    does not shift the sound. Outside the sample the signal is zero.
//  - Resampler::read(): the sample at a position (in source samples, fractional) when the read
//    moves `step` source samples per output sample (step > 1 plays higher, < 1 lower, and may
//    change at every call). Allocation-free, thread-safe on a shared chain.
//
// Quality sets the interpolator length, the number of phases and the stopband attenuation; the
// usable bandwidth follows (Fast 0.375, Good 0.41, Best 0.444 of the source rate, flat), and
// content above it is attenuated by the stopband figure, in sources pitched up or down alike.

#include "../oversampling/FirDesign.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
 #include <emmintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
 #include <arm_neon.h>
#endif

namespace bidoo::dsp
{
enum class ResampleQuality { Fast, Good, Best };

class ResampleKernel
{
public:
    explicit ResampleKernel (ResampleQuality q)
    {
        switch (q)
        {
            case ResampleQuality::Fast: taps_ = 16; phases_ = 256; attenuationDb_ = 66.0; break;
            case ResampleQuality::Good: taps_ = 32; phases_ = 512; attenuationDb_ = 90.0; break;
            case ResampleQuality::Best: taps_ = 64; phases_ = 1024; attenuationDb_ = 110.0; break;
        }
        // Kaiser's estimate of the transition width this length and attenuation give, centred on
        // half the source rate: the flat band ends at 0.5 - width / 2.
        transition_ = std::max (0.02, (attenuationDb_ - 7.95) / (14.36 * taps_));
        passband_ = 0.5 - 0.5 * transition_;
        const double beta = fir::kaiserBeta (attenuationDb_);
        const double i0Beta = fir::besselI0 (beta);

        table.assign ((size_t) (phases_ + 1) * (size_t) taps_, 0.0f);
        for (int p = 0; p <= phases_; ++p)
        {
            const double frac = (double) p / phases_;
            std::vector<double> h ((size_t) taps_);
            double sum = 0.0;
            for (int k = 0; k < taps_; ++k)
            {
                const double t = (double) (k - (taps_ / 2 - 1)) - frac; // sample offset from the read position
                const double s = std::abs (t) < 1.0e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
                const double r = t / (0.5 * taps_);
                const double w = std::abs (r) >= 1.0 ? 0.0 : fir::besselI0 (beta * std::sqrt (1.0 - r * r)) / i0Beta;
                h[(size_t) k] = s * w;
                sum += h[(size_t) k];
            }
            for (int k = 0; k < taps_; ++k)
                table[(size_t) p * (size_t) taps_ + (size_t) k] = (float) (h[(size_t) k] / sum); // unity gain at DC for every phase
        }
    }

    static const ResampleKernel& shared (ResampleQuality q)
    {
        static const ResampleKernel fast (ResampleQuality::Fast), good (ResampleQuality::Good), best (ResampleQuality::Best);
        return q == ResampleQuality::Fast ? fast : (q == ResampleQuality::Good ? good : best);
    }

    int taps() const noexcept { return taps_; }
    int phases() const noexcept { return phases_; }
    double stopbandDb() const noexcept { return attenuationDb_; }
    // Upper edge of the flat band, as a fraction of the source rate.
    double passband() const noexcept { return passband_; }
    // The largest read step a level serves without aliasing: 0.5 / passband.
    double maxStep() const noexcept { return 0.5 / passband_; }
    const float* phase (int p) const noexcept { return table.data() + (size_t) p * (size_t) taps_; }

private:
    int taps_ = 32, phases_ = 512;
    double attenuationDb_ = 90.0, transition_ = 0.2, passband_ = 0.4;
    std::vector<float> table;
};

class MipChain
{
public:
    static constexpr int kMaxLevels = 12; // reads up to 2^11 times the source rate

    // Copies `x` (n samples) as level 0 and builds the half-band levels while they stay longer
    // than the interpolator. Main thread.
    void build (const float* x, int n, const ResampleKernel& kernel)
    {
        taps = kernel.taps();
        pad = taps; // zeros each side: reads near the ends need no checks
        levelCount = 0;
        for (auto& l : levels)
            l = Level {};
        if (n <= 0)
            return;

        // The half-band that carries the kernel's flat band down one level: transition half as
        // wide (in the input rate), centred on a quarter.
        const double width = 0.5 * (1.0 - 2.0 * kernel.passband());
        const auto h = fir::halfBand (std::max (0.01, width), kernel.stopbandDb());
        const int c = ((int) h.size() - 1) / 2;
        std::vector<int> offsets;
        std::vector<float> coefs;
        for (int d = -c; d <= c; ++d)
            if (h[(size_t) (c + d)] != 0.0)
            {
                offsets.push_back (d);
                coefs.push_back ((float) h[(size_t) (c + d)]);
            }

        levels[0].length = n;
        levels[0].data.assign ((size_t) n + 2 * (size_t) pad, 0.0f);
        std::copy (x, x + n, levels[0].data.begin() + pad);
        levelCount = 1;

        while (levelCount < kMaxLevels && levels[(size_t) levelCount - 1].length / 2 >= 2 * taps)
        {
            const Level& src = levels[(size_t) levelCount - 1];
            Level& dst = levels[(size_t) levelCount];
            dst.length = src.length / 2;
            dst.data.assign ((size_t) dst.length + 2 * (size_t) pad, 0.0f);
            const float* in = src.data.data() + pad;
            float* out = dst.data.data() + pad;
            for (int o = 0; o < dst.length; ++o)
            {
                const int centre = 2 * o;
                float acc = 0.0f;
                if (centre - c >= 0 && centre + c < src.length)
                    for (size_t j = 0; j < offsets.size(); ++j)
                        acc += coefs[j] * in[centre + offsets[j]];
                else
                    for (size_t j = 0; j < offsets.size(); ++j)
                    {
                        const int idx = centre + offsets[j];
                        if (idx >= 0 && idx < src.length)
                            acc += coefs[j] * in[idx];
                    }
                out[o] = acc;
            }
            ++levelCount;
        }
    }

    int numLevels() const noexcept { return levelCount; }
    int length (int level = 0) const noexcept { return levels[(size_t) level].length; }
    // Level samples; indices -pad .. length + pad - 1 are valid (zeros outside the signal).
    const float* data (int level) const noexcept { return levels[(size_t) level].data.data() + pad; }
    int padding() const noexcept { return pad; }
    bool empty() const noexcept { return levelCount == 0; }

private:
    struct Level
    {
        std::vector<float> data;
        int length = 0;
    };
    std::array<Level, kMaxLevels> levels;
    int levelCount = 0, taps = 32, pad = 32;
};

namespace resampler_detail
{
// The two dot products of the interpolation (the two neighbouring phases against the same
// samples). taps is a multiple of 8. SSE2 (the x64 baseline) and NEON are used when present;
// the portable loop keeps eight independent partial sums so the compiler can vectorise it.
BIDOO_DSP_INLINE void dot2 (const float* a, const float* b, const float* x, int taps, float& s0, float& s1) noexcept
{
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    __m128 a0 = _mm_setzero_ps(), a1 = _mm_setzero_ps(), c0 = _mm_setzero_ps(), c1 = _mm_setzero_ps();
    for (int k = 0; k < taps; k += 8)
    {
        const __m128 x0 = _mm_loadu_ps (x + k), x1 = _mm_loadu_ps (x + k + 4);
        a0 = _mm_add_ps (a0, _mm_mul_ps (_mm_loadu_ps (a + k), x0));
        c0 = _mm_add_ps (c0, _mm_mul_ps (_mm_loadu_ps (b + k), x0));
        a1 = _mm_add_ps (a1, _mm_mul_ps (_mm_loadu_ps (a + k + 4), x1));
        c1 = _mm_add_ps (c1, _mm_mul_ps (_mm_loadu_ps (b + k + 4), x1));
    }
    __m128 t = _mm_add_ps (a0, a1), u = _mm_add_ps (c0, c1);
    // horizontal sums of t and u
    __m128 lo = _mm_unpacklo_ps (t, u), hi = _mm_unpackhi_ps (t, u); // t0 u0 t1 u1 | t2 u2 t3 u3
    __m128 sum = _mm_add_ps (lo, hi);                                   // t0+t2 u0+u2 t1+t3 u1+u3
    sum = _mm_add_ps (sum, _mm_movehl_ps (sum, sum));
    s0 = _mm_cvtss_f32 (sum);
    s1 = _mm_cvtss_f32 (_mm_shuffle_ps (sum, sum, 1));
#elif defined(__ARM_NEON) || defined(__aarch64__)
    float32x4_t a0 = vdupq_n_f32 (0.0f), a1 = vdupq_n_f32 (0.0f), c0 = vdupq_n_f32 (0.0f), c1 = vdupq_n_f32 (0.0f);
    for (int k = 0; k < taps; k += 8)
    {
        const float32x4_t x0 = vld1q_f32 (x + k), x1 = vld1q_f32 (x + k + 4);
        a0 = vmlaq_f32 (a0, vld1q_f32 (a + k), x0);
        c0 = vmlaq_f32 (c0, vld1q_f32 (b + k), x0);
        a1 = vmlaq_f32 (a1, vld1q_f32 (a + k + 4), x1);
        c1 = vmlaq_f32 (c1, vld1q_f32 (b + k + 4), x1);
    }
    float32x4_t t = vaddq_f32 (a0, a1), u = vaddq_f32 (c0, c1);
    float tt[4], uu[4];
    vst1q_f32 (tt, t);
    vst1q_f32 (uu, u);
    s0 = (tt[0] + tt[2]) + (tt[1] + tt[3]);
    s1 = (uu[0] + uu[2]) + (uu[1] + uu[3]);
#else
    float p0[8] {}, p1[8] {};
    for (int k = 0; k < taps; k += 8)
        for (int l = 0; l < 8; ++l)
        {
            p0[l] += a[k + l] * x[k + l];
            p1[l] += b[k + l] * x[k + l];
        }
    s0 = ((p0[0] + p0[4]) + (p0[1] + p0[5])) + ((p0[2] + p0[6]) + (p0[3] + p0[7]));
    s1 = ((p1[0] + p1[4]) + (p1[1] + p1[5])) + ((p1[2] + p1[6]) + (p1[3] + p1[7]));
#endif
}
} // namespace resampler_detail

namespace Resampler
{
// The level to read for a step (source samples per output sample).
inline int levelFor (const MipChain& chain, const ResampleKernel& kernel, double step) noexcept
{
    int level = 0;
    const double limit = kernel.maxStep();
    while (step > limit && level + 1 < chain.numLevels())
    {
        step *= 0.5;
        ++level;
    }
    return level;
}

// One output sample at `position` (source samples, may be fractional; outside the sample the
// signal is zero), for a read that advances `step` source samples per output sample.
inline float read (const MipChain& chain, const ResampleKernel& kernel, double position, double step) noexcept
{
    if (chain.empty())
        return 0.0f;
    const int level = levelFor (chain, kernel, step);
    const double pos = position * (1.0 / (double) (1 << level)); // exact: a power of two
    const int len = chain.length (level);
    const int taps = kernel.taps();
    if (pos < (double) (-taps / 2) || pos >= (double) (len + taps / 2))
        return 0.0f;
    const int i = pos >= 0.0 ? (int) pos : (int) pos - 1; // floor
    const double fp = (pos - (double) i) * kernel.phases();
    int p = (int) fp;
    if (p >= kernel.phases())
        p = kernel.phases() - 1;
    const float frac = (float) (fp - (double) p);
    const float* x = chain.data (level) + (i - (taps / 2 - 1));
    const float* a = kernel.phase (p);
    const float* b = kernel.phase (p + 1);
    float s0, s1;
    resampler_detail::dot2 (a, b, x, taps, s0, s1);
    return s0 + (s1 - s0) * frac;
}

// Renders n samples at a constant step from `position`, which is advanced.
inline void render (const MipChain& chain, const ResampleKernel& kernel, double& position, double step, float* out, int n) noexcept
{
    for (int j = 0; j < n; ++j)
    {
        out[j] = read (chain, kernel, position, step);
        position += step;
    }
}
} // namespace Resampler
} // namespace bidoo::dsp

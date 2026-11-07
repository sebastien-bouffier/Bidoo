#pragma once

// Per-sample 2x and 4x oversampling around a nonlinear stage (a shaper, a saturating filter), on
// polyphase IIR half-band filters: Laurent de Soras' HIIR structure (two chains of first-order
// all-passes, coefficients alternating between them) run one sample at a time, so the stage in the
// middle is a lambda called in time order and may keep state (ADAA, filters). Cheap, unity gain at
// DC, a few samples of delay. From FadaBox's Oversampler and Chourmo's (the same filters, bit for
// bit). For block processing at 2x..32x with latency reporting, IirOversampler.h is the one.
//
//  - IirHalfBandCoefs<n, transitionMilli>: the coefficients HIIR's PolyphaseIir2Designer gives for
//    n coefficients and that transition width (thousandths of the filter's rate), as float
//    constants (tests check them against the designer). 8 at 0.040: about 99 dB of image
//    rejection, about two samples of phase delay; 4 at 0.130: 79 dB; 4 at 0.250.
//  - IirHalfBand<n, T>: one direction's state (up: 1 in 2 out; down: 2 in 1 out), any n, float or
//    double, any coefficients (a plugin's own set works too).
//  - IirHalfBandBank<lanes, n>: the same on `lanes` independent signals at once (SSE2 / NEON),
//    each lane computing exactly what an IirHalfBand would.
//  - IirShaperOversampler<Stage1, Stage2>: factor 1, 2 or 4 around a lambda (the second stage
//    only clears the first's band); IirShaperOversamplerBank<lanes, Stage1, Stage2>: the filters of
//    `lanes` of them run together, shape (lane, v) per lane.

#include "../core/Inline.h"
#include <cstddef>
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
 #include <emmintrin.h>
 #define BIDOO_DSP_IIRHB_SSE2 1
#elif defined(__ARM_NEON) || defined(_M_ARM64)
 #include <arm_neon.h>
 #define BIDOO_DSP_IIRHB_NEON 1
#endif

namespace bidoo::dsp
{
template <int kCoefs, int kTransitionMilli>
struct IirHalfBandCoefs;

template <>
struct IirHalfBandCoefs<8, 40>
{
    static constexpr int size = 8;
    static constexpr float c[8] = { 0.0406334624f, 0.150505126f, 0.300757051f, 0.460774511f,
                                    0.60952431f,   0.738503814f, 0.849223793f, 0.949742794f };
};
template <>
struct IirHalfBandCoefs<4, 130>
{
    static constexpr int size = 4;
    static constexpr float c[4] = { 0.0670134872f, 0.246876761f, 0.499129117f, 0.809598446f };
};
template <>
struct IirHalfBandCoefs<4, 250>
{
    static constexpr int size = 4;
    static constexpr float c[4] = { 0.0424547084f, 0.170739844f, 0.393319905f, 0.745713592f };
};

// One direction of a half-band filter: the two all-pass chains' states. Each section is
// y[n] = c (x[n] - y[n-1]) + x[n-1] on its branch's (half-rate) samples; even coefficients on the
// first branch, odd ones on the second (an odd N gives the first branch one section more). T is
// float or double (Grotte's thick saturator runs its half-band in double).
template <int N, typename T = float>
struct IirHalfBand
{
    T x[N] {}, y[N] {};

    BIDOO_DSP_INLINE void branches (const T* c, T& a, T& b) noexcept
    {
        for (int i = 0; i + 1 < N; i += 2)
        {
            const T ta = (a - y[i]) * c[i] + x[i];
            x[i] = a;
            y[i] = ta;
            a = ta;
            const T tb = (b - y[i + 1]) * c[i + 1] + x[i + 1];
            x[i + 1] = b;
            y[i + 1] = tb;
            b = tb;
        }
        if constexpr (N % 2 != 0)
        {
            const T ta = (a - y[N - 1]) * c[N - 1] + x[N - 1];
            x[N - 1] = a;
            y[N - 1] = ta;
            a = ta;
        }
    }
    // One sample in, two out (earlier first).
    BIDOO_DSP_INLINE void up (const T* c, T in, T& out0, T& out1) noexcept
    {
        out0 = in;
        out1 = in;
        branches (c, out0, out1);
    }
    // Two samples in (earlier first), one out.
    BIDOO_DSP_INLINE T down (const T* c, T in0, T in1) noexcept
    {
        T a = in1, b = in0;
        branches (c, a, b);
        return (T) 0.5 * (a + b);
    }
    void reset() noexcept { *this = IirHalfBand {}; }
    // After a blow-up: back to silence when a state is not finite or beyond +-limit.
    void sanitize (T limit) noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            if (! (x[i] > -limit && x[i] < limit) || ! (y[i] > -limit && y[i] < limit))
            {
                reset();
                return;
            }
        }
    }
};

namespace iirhb_detail
{
// Four floats in one register (SSE2 on x86, NEON on ARM, plain floats elsewhere): only what the
// half-band filters need, each lane computing exactly what the scalar code would.
struct F4
{
#if defined(BIDOO_DSP_IIRHB_SSE2)
    __m128 v;
    static BIDOO_DSP_INLINE F4 load (const float* p) noexcept { return { _mm_load_ps (p) }; }
    static BIDOO_DSP_INLINE F4 set1 (float x) noexcept { return { _mm_set1_ps (x) }; }
    BIDOO_DSP_INLINE void store (float* p) const noexcept { _mm_store_ps (p, v); }
    friend BIDOO_DSP_INLINE F4 operator+ (F4 a, F4 b) noexcept { return { _mm_add_ps (a.v, b.v) }; }
    friend BIDOO_DSP_INLINE F4 operator- (F4 a, F4 b) noexcept { return { _mm_sub_ps (a.v, b.v) }; }
    friend BIDOO_DSP_INLINE F4 operator* (F4 a, F4 b) noexcept { return { _mm_mul_ps (a.v, b.v) }; }
#elif defined(BIDOO_DSP_IIRHB_NEON)
    float32x4_t v;
    static BIDOO_DSP_INLINE F4 load (const float* p) noexcept { return { vld1q_f32 (p) }; }
    static BIDOO_DSP_INLINE F4 set1 (float x) noexcept { return { vdupq_n_f32 (x) }; }
    BIDOO_DSP_INLINE void store (float* p) const noexcept { vst1q_f32 (p, v); }
    friend BIDOO_DSP_INLINE F4 operator+ (F4 a, F4 b) noexcept { return { vaddq_f32 (a.v, b.v) }; }
    friend BIDOO_DSP_INLINE F4 operator- (F4 a, F4 b) noexcept { return { vsubq_f32 (a.v, b.v) }; }
    friend BIDOO_DSP_INLINE F4 operator* (F4 a, F4 b) noexcept { return { vmulq_f32 (a.v, b.v) }; }
#else
    float v[4];
    static BIDOO_DSP_INLINE F4 load (const float* p) noexcept { return { { p[0], p[1], p[2], p[3] } }; }
    static BIDOO_DSP_INLINE F4 set1 (float x) noexcept { return { { x, x, x, x } }; }
    BIDOO_DSP_INLINE void store (float* p) const noexcept
    {
        for (int i = 0; i < 4; ++i)
            p[i] = v[i];
    }
    friend BIDOO_DSP_INLINE F4 operator+ (F4 a, F4 b) noexcept { return { { a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2], a.v[3] + b.v[3] } }; }
    friend BIDOO_DSP_INLINE F4 operator- (F4 a, F4 b) noexcept { return { { a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2], a.v[3] - b.v[3] } }; }
    friend BIDOO_DSP_INLINE F4 operator* (F4 a, F4 b) noexcept { return { { a.v[0] * b.v[0], a.v[1] * b.v[1], a.v[2] * b.v[2], a.v[3] * b.v[3] } }; }
#endif
};
} // namespace iirhb_detail

// IirHalfBand on kLanes independent signals (kLanes a multiple of 4): the same arithmetic, lane by
// lane, in a quarter of the instructions and without the chains of dependent operations.
template <int kLanes, int N>
struct IirHalfBandBank
{
    static_assert (kLanes % 4 == 0 && N % 2 == 0);

    BIDOO_DSP_INLINE void up (const float* c, const float* in, float* out0, float* out1) noexcept
    {
        using iirhb_detail::F4;
        for (int j = 0; j < kLanes; j += 4)
        {
            F4 s0 = F4::load (in + j), s1 = s0;
            run (c, j, s0, s1);
            s0.store (out0 + j);
            s1.store (out1 + j);
        }
    }
    BIDOO_DSP_INLINE void down (const float* c, const float* in0, const float* in1, float* out) noexcept
    {
        using iirhb_detail::F4;
        const F4 half = F4::set1 (0.5f);
        for (int j = 0; j < kLanes; j += 4)
        {
            F4 s0 = F4::load (in1 + j), s1 = F4::load (in0 + j);
            run (c, j, s0, s1);
            (half * (s0 + s1)).store (out + j);
        }
    }
    void reset() noexcept
    {
        for (int k = 0; k < N; ++k)
            for (int j = 0; j < kLanes; ++j)
                x[k][j] = y[k][j] = 0.0f;
    }

private:
    BIDOO_DSP_INLINE void run (const float* c, int j, iirhb_detail::F4& s0, iirhb_detail::F4& s1) noexcept
    {
        using iirhb_detail::F4;
        for (int k = 0; k < N; k += 2)
        {
            float* x0 = x[k] + j;
            float* y0 = y[k] + j;
            const F4 t0 = (s0 - F4::load (y0)) * F4::set1 (c[k]) + F4::load (x0);
            s0.store (x0);
            t0.store (y0);
            s0 = t0;
            float* x1 = x[k + 1] + j;
            float* y1 = y[k + 1] + j;
            const F4 t1 = (s1 - F4::load (y1)) * F4::set1 (c[k + 1]) + F4::load (x1);
            s1.store (x1);
            t1.store (y1);
            s1 = t1;
        }
    }
    alignas (16) float x[N][kLanes] {};
    alignas (16) float y[N][kLanes] {};
};

// One stream's up- and down-sampling for a factor of 1, 2 or 4: f (x) -> y runs `factor` times per
// call at `factor` times the base rate, in time order (a stateful f keeps its state across calls).
// Stage1 / Stage2 are IirHalfBandCoefs (the 2x filter, then the 4x one).
template <typename Stage1 = IirHalfBandCoefs<8, 40>, typename Stage2 = IirHalfBandCoefs<4, 250>>
class IirShaperOversampler
{
public:
    template <typename F>
    BIDOO_DSP_INLINE float process (float x, int factor, F&& f)
    {
        if (factor <= 1)
            return f (x);
        float a, b;
        up1.up (Stage1::c, x, a, b);
        if (factor == 2)
        {
            const float ya = f (a);
            const float yb = f (b);
            return down1.down (Stage1::c, ya, yb);
        }
        float a0, a1, b0, b1;
        up2.up (Stage2::c, a, a0, a1);
        up2.up (Stage2::c, b, b0, b1);
        const float ya0 = f (a0);
        const float ya1 = f (a1);
        const float ya = down2.down (Stage2::c, ya0, ya1);
        const float yb0 = f (b0);
        const float yb1 = f (b1);
        const float yb = down2.down (Stage2::c, yb0, yb1);
        return down1.down (Stage1::c, ya, yb);
    }

    void reset() noexcept
    {
        up1.reset();
        down1.reset();
        up2.reset();
        down2.reset();
    }
    // After a blow-up (NaN or a value beyond +-limit in a filter state): those filters start again.
    void sanitize (float limit) noexcept
    {
        up1.sanitize (limit);
        down1.sanitize (limit);
        up2.sanitize (limit);
        down2.sanitize (limit);
    }

private:
    IirHalfBand<Stage1::size> up1, down1;
    IirHalfBand<Stage2::size> up2, down2;
};

// The filters of kLanes IirShaperOversamplers run together: process (in, out, factor, shape) gives,
// lane by lane, what IirShaperOversampler::process (in[j], factor, s) would, with shape (j, v) as
// lane j's stage (called in time order within each lane).
template <int kLanes, typename Stage1 = IirHalfBandCoefs<8, 40>, typename Stage2 = IirHalfBandCoefs<4, 250>>
class IirShaperOversamplerBank
{
public:
    void reset() noexcept
    {
        up1.reset();
        down1.reset();
        up2.reset();
        down2.reset();
    }
    template <typename F>
    BIDOO_DSP_INLINE void process (const float* in, float* out, int factor, F&& shape)
    {
        if (factor <= 1)
        {
            for (int j = 0; j < kLanes; ++j)
                out[j] = shape (j, in[j]);
            return;
        }
        alignas (16) float u0[kLanes], u1[kLanes];
        up1.up (Stage1::c, in, u0, u1);
        if (factor == 2)
        {
            for (int j = 0; j < kLanes; ++j)
            {
                u0[j] = shape (j, u0[j]);
                u1[j] = shape (j, u1[j]);
            }
            down1.down (Stage1::c, u0, u1, out);
            return;
        }
        alignas (16) float w0[kLanes], w1[kLanes], w2[kLanes], w3[kLanes];
        up2.up (Stage2::c, u0, w0, w1);
        up2.up (Stage2::c, u1, w2, w3);
        for (int j = 0; j < kLanes; ++j)
        {
            w0[j] = shape (j, w0[j]);
            w1[j] = shape (j, w1[j]);
            w2[j] = shape (j, w2[j]);
            w3[j] = shape (j, w3[j]);
        }
        down2.down (Stage2::c, w0, w1, u0);
        down2.down (Stage2::c, w2, w3, u1);
        down1.down (Stage1::c, u0, u1, out);
    }

private:
    IirHalfBandBank<kLanes, Stage1::size> up1, down1;
    IirHalfBandBank<kLanes, Stage2::size> up2, down2;
};
} // namespace bidoo::dsp

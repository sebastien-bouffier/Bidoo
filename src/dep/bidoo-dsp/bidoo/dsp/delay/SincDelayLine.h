#pragma once

// SincDelayLine: a circular delay line with windowed-sinc fractional reads, from Retour. 16 taps,
// 512 phases (linearly interpolated between phases), Kaiser window, flat to about 18 kHz at 44.1 kHz,
// so repeats don't darken with every pass through a feedback loop and modulated times don't add
// grit (a 4-point cubic lost 3.5 dB at 15 kHz per pass). The shortest delay is kMinDelay (the
// sinc reaches that many samples either side); a delay of d is the sample written d samples ago
// (1 = the latest).
//
// The inner loops use SSE2 where the compiler has it (every x64 one) and the same loops scalar
// elsewhere. The vector code does the very operations of the scalar code in the same order (no
// reassociation, no fused multiply-add), so both give the same samples.
// Use locate() + interpolate() (or interpolate4() for four reads at once) when several reads share
// work, read() for one.

#include <algorithm>
#include <cmath>
#include <vector>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
 #include <emmintrin.h>
 #define BIDOO_DSP_SSE 1
#else
 #define BIDOO_DSP_SSE 0
#endif

namespace bidoo::dsp
{
// The sum over k < half of c[k] (h[k] + h[n - 1 - k]) (a symmetric FIR), in four interleaved
// partial sums (term k goes to sum k & 3), added as (s0 + s1) + (s2 + s3). The four sums are the
// four lanes of one SSE register. h[0] is `newest`, the sample just stored: it comes from the
// register, as a vector load straddling a fresh store would stall (half >= 4).
inline float symmetricDot (const float* c, const float* h, int half, int n, float newest)
{
    int k = 0;
#if BIDOO_DSP_SSE
    __m128 acc = _mm_setzero_ps();
    for (; k + 4 <= half; k += 4)
    {
        __m128 fwd;
        if (k == 0) // h[0 .. 3] as newest and h[1 .. 3] (loaded from h + 1, moved up a lane)
            fwd = _mm_move_ss (_mm_castsi128_ps (_mm_slli_si128 (_mm_castps_si128 (_mm_loadu_ps (h + 1)), 4)),
                               _mm_set_ss (newest));
        else
            fwd = _mm_loadu_ps (h + k);
        __m128 rev = _mm_loadu_ps (h + n - 4 - k);
        rev = _mm_shuffle_ps (rev, rev, _MM_SHUFFLE (0, 1, 2, 3)); // lane j: h[n - 1 - k - j]
        acc = _mm_add_ps (acc, _mm_mul_ps (_mm_loadu_ps (c + k), _mm_add_ps (fwd, rev)));
    }
    // the last terms (fewer than four) go to sums 0, 1, 2 (adding 0 leaves a sum as it is)
    float t[3] {};
    for (int j = 0; k + j < half; ++j)
        t[j] = c[k + j] * (h[k + j] + h[n - 1 - k - j]);
    acc = _mm_add_ps (acc, _mm_setr_ps (t[0], t[1], t[2], 0.0f));
    // (s0 + s1) + (s2 + s3)
    const __m128 pairs = _mm_add_ps (acc, _mm_shuffle_ps (acc, acc, _MM_SHUFFLE (2, 3, 0, 1)));
    return _mm_cvtss_f32 (_mm_add_ss (pairs, _mm_movehl_ps (pairs, pairs)));
#else
    (void) newest;
    float a[4] {};
    for (; k < half; ++k)
        a[k & 3] += c[k] * (h[k] + h[n - 1 - k]);
    return (a[0] + a[1]) + (a[2] + a[3]);
#endif
}

// A filter's frequency multiplier for one channel from its pan (-1..1): negative moves the left

//  Windowed-sinc fractional reads: 16 taps, 512 phases (linearly interpolated between phases).
//  Flat to about 18 kHz at 44.1 kHz, so repeats don't darken with every pass through the loop
//  and modulated times don't add grit (a 4-point cubic lost 3.5 dB at 15 kHz per pass).
// ---------------------------------------------------------------------------------------------
struct SincTable
{
    static constexpr int kTaps = 16, kHalf = kTaps / 2, kPhases = 512;
    float h[kPhases + 1][kTaps];
    float dh[kPhases + 1][kTaps]; // the step to the next phase, for the interpolation

    SincTable()
    {
        const double beta = 7.0, cutoff = 0.94; // fraction of Nyquist
        auto bessel0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        const double i0b = bessel0 (beta);
        for (int p = 0; p <= kPhases; ++p)
        {
            const double t = (double) p / (double) kPhases;
            double row[kTaps], sum = 0.0;
            for (int j = 0; j < kTaps; ++j)
            {
                const double u = t + (double) (kHalf - 1 - j); // distance from tap j (in samples)
                const double x = 3.14159265358979323846 * cutoff * u;
                const double sinc = std::abs (u) < 1.0e-9 ? 1.0 : std::sin (x) / x;
                const double r = u / (double) kHalf;
                const double w = std::abs (r) >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - r * r)) / i0b;
                row[j] = sinc * w;
                sum += row[j];
            }
            for (int j = 0; j < kTaps; ++j)
                h[p][j] = (float) (row[j] / sum); // unity gain at DC for every phase
        }
        for (int p = 0; p <= kPhases; ++p)
            for (int j = 0; j < kTaps; ++j)
                dh[p][j] = p < kPhases ? h[p + 1][j] - h[p][j] : 0.0f;
    }
};

// built once when the plug-in loads
inline const SincTable sincTable;

// ---------------------------------------------------------------------------------------------
//  Delay buffer: power-of-two circular buffer with windowed-sinc fractional reads.
// ---------------------------------------------------------------------------------------------
class SincDelayLine
{
public:
    // the shortest delay read() serves (the sinc reaches this many samples either side)
    static constexpr float kMinDelay = (float) SincTable::kHalf + 1.0f;

    void prepare (int minSize)
    {
        int size = 1;
        while (size < minSize)
            size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }

    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); }
    int size() const { return mask + 1; }

    void write (float x)
    {
        buf[(size_t) w] = x;
        w = (w + 1) & mask;
    }

    // Where a fractional read takes its samples and its sinc phase: x[-j] meets tap j.
    struct Spot
    {
        const float* x;
        const float* h;
        const float* dh;
        float pt;
        float wrapped[SincTable::kTaps]; // the samples, when they straddle the buffer's end
    };

    void locate (float d, Spot& s) const
    {
        d = std::clamp (d, kMinDelay, (float) (mask - SincTable::kTaps - 2));
        const int di = (int) d;
        const float pf = (d - (float) di) * (float) SincTable::kPhases;
        const int pi = std::min ((int) pf, SincTable::kPhases - 1);
        s.pt = pf - (float) pi;
        s.h = sincTable.h[pi];
        s.dh = sincTable.dh[pi];
        // tap j reads the sample written di - (kHalf - 1) + j ago: x[-j]
        const int first = (w - di + (SincTable::kHalf - 1)) & mask;
        constexpr int n = SincTable::kTaps;
        if (first - n + 1 >= 0) // no wrap: straight from the buffer
            s.x = buf.data() + first;
        else
        {
            for (int j = 0; j < n; ++j)
                s.wrapped[n - 1 - j] = buf[(size_t) ((first - j) & mask)];
            s.x = s.wrapped + n - 1;
        }
    }

    // The sum over the taps of (h + pt dh) x, added one after the other.
    static float interpolate (const Spot& s)
    {
        constexpr int n = SincTable::kTaps;
        const float *x = s.x, *h = s.h, *dh = s.dh;
        float acc = 0.0f;
#if BIDOO_DSP_SSE
        // the terms four side by side, then added in order from the register
        const __m128 vpt = _mm_set1_ps (s.pt);
        for (int j = 0; j < n; j += 4)
        {
            __m128 xs = _mm_loadu_ps (x - j - 3);
            xs = _mm_shuffle_ps (xs, xs, _MM_SHUFFLE (0, 1, 2, 3)); // lane i: x[-j - i]
            const __m128 coef = _mm_add_ps (_mm_loadu_ps (h + j), _mm_mul_ps (vpt, _mm_loadu_ps (dh + j)));
            const __m128 t = _mm_mul_ps (coef, xs);
            acc += _mm_cvtss_f32 (t);
            acc += _mm_cvtss_f32 (_mm_shuffle_ps (t, t, _MM_SHUFFLE (1, 1, 1, 1)));
            acc += _mm_cvtss_f32 (_mm_movehl_ps (t, t));
            acc += _mm_cvtss_f32 (_mm_shuffle_ps (t, t, _MM_SHUFFLE (3, 3, 3, 3)));
        }
#else
        for (int j = 0; j < n; ++j)
            acc += (h[j] + s.pt * dh[j]) * x[-j];
#endif
        return acc;
    }

    // Four reads at once (one per lane), each summed in the same order as interpolate(): the
    // same samples, for less work per read.
    static void interpolate4 (const Spot* const* s, float* out)
    {
#if BIDOO_DSP_SSE
        constexpr int n = SincTable::kTaps;
        __m128 acc = _mm_setzero_ps();
        __m128 vpt[4];
        for (int l = 0; l < 4; ++l)
            vpt[l] = _mm_set1_ps (s[l]->pt);
        for (int j = 0; j < n; j += 4)
        {
            __m128 t[4]; // read l's terms j .. j + 3
            for (int l = 0; l < 4; ++l)
            {
                __m128 xs = _mm_loadu_ps (s[l]->x - j - 3);
                xs = _mm_shuffle_ps (xs, xs, _MM_SHUFFLE (0, 1, 2, 3));
                const __m128 coef = _mm_add_ps (_mm_loadu_ps (s[l]->h + j), _mm_mul_ps (vpt[l], _mm_loadu_ps (s[l]->dh + j)));
                t[l] = _mm_mul_ps (coef, xs);
            }
            _MM_TRANSPOSE4_PS (t[0], t[1], t[2], t[3]); // t[i]: term j + i of each read
            acc = _mm_add_ps (acc, t[0]);
            acc = _mm_add_ps (acc, t[1]);
            acc = _mm_add_ps (acc, t[2]);
            acc = _mm_add_ps (acc, t[3]);
        }
        _mm_storeu_ps (out, acc);
#else
        for (int l = 0; l < 4; ++l)
            out[l] = interpolate (*s[l]);
#endif
    }

    // The sample written `d` samples ago (1 = the latest), between samples by windowed sinc.
    float read (float d) const
    {
        Spot s;
        locate (d, s);
        return interpolate (s);
    }

    // A whole sample (1 = the latest), no interpolation.
    float sampleAgo (int d) const { return buf[(size_t) ((w - d) & mask)]; }

    float readLinear (float d) const
    {
        d = std::clamp (d, 1.0f, (float) (mask - 1));
        const int di = (int) d;
        const float t = d - (float) di;
        const float a = buf[(size_t) ((w - di) & mask)], b = buf[(size_t) ((w - di - 1) & mask)];
        return a + t * (b - a);
    }

private:
    std::vector<float> buf { 0.0f };
    int mask = 0, w = 0;
};
} // namespace bidoo::dsp

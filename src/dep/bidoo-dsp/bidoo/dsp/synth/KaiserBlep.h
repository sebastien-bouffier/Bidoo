#pragma once

// The high-quality band-limiting tier for oscillators (moved from Mistral, bit-identical), next to
// the cheap 2-point polyBLEP of BlepOscillator.h.
//
// A Kaiser-windowed sinc kernel (cutoff 0.45 fs, beta 9.5), kKaiserBlepHalf = 12 samples to each
// side, integrated once (the band-limited step) and twice (the ramp), and up to four more times
// (smoothing the jumps in the higher derivatives: the rounded triangle's peaks, feedback FM,
// sync on smooth shapes). Replacing each discontinuity by them keeps the aliasing over 75 dB down
// below 0.42 fs (flat to 0.39 fs): on a 48 kHz grid a saw at 1.1 / 2.5 / 5.9 kHz has about
// -55 / -50 / -96 dB of alias power against -31 / -27 / -27 dB for the 2-point polyBLEP.
//
// Two ways to use the tables:
//  - KaiserBlepCorrector: the stateful form. It owns a ring of the oscillator's naive output and a
//    ring of corrections; every discontinuity adds its kernel into both sides (step, ramp, para,
//    cubic, residual), and the output is kKaiserBlepHalf samples late, the same for every
//    waveform so they stay aligned. Sub-sample events (hard sync, retrigger) are exact.
//  - kaiserBlep / kaiserBlamp: the stateless form, a drop-in for blep() / blamp() of
//    BlepOscillator.h (same arguments, same sign). They look ahead instead of delaying: for an
//    oscillator whose phase and increment dt are known now, the output at the current sample is
//    the band-limited waveform at this very sample (zero latency), assuming dt holds for the next
//    kKaiserBlepHalf samples. Right for a free-running oscillator (slow pitch changes); for audio
//    rate phase modulation or an external phase keep blep() / blamp() or the stateful form. They
//    cost nothing away from a discontinuity (one comparison) and sum the neighbouring cycles when
//    the window is wider than one cycle (above fs / 24).

#include "../core/Inline.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace bidoo::dsp
{
constexpr int kKaiserBlepHalf = 12;
constexpr int kKaiserBlepRes = 128; // table points per sample

struct KaiserBlepTables
{
    static constexpr int kSize = 2 * kKaiserBlepHalf * kKaiserBlepRes + 2;
    static constexpr int kOrders = 6; // value, slope, and the second to fifth derivatives
    float step[kSize], ramp[kSize];
    // from the second derivative on: the residual itself (band-limited minus naive t^n / n!)
    float residual[kOrders - 2][kSize];

    KaiserBlepTables()
    {
        constexpr int kHalf = kKaiserBlepHalf, kRes = kKaiserBlepRes;
        const double fc = 0.45;  // cutoff relative to the sample rate
        const double beta = 9.5; // Kaiser window
        auto bessel0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k)
            {
                term *= (x * 0.5 / k) * (x * 0.5 / k);
                sum += term;
            }
            return sum;
        };
        const int sub = 16; // integration steps per table point
        const int n = (kSize - 1) * sub;
        const double dt = 1.0 / (kRes * sub);
        const double w0 = bessel0 (beta);
        auto kernel = [&] (double t) // t in samples, -K..K
        {
            const double u = t / kHalf;
            if (std::abs (u) >= 1.0)
                return 0.0;
            const double x = 2.0 * 3.14159265358979323846 * fc * t;
            const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (x) / x;
            return 2.0 * fc * sinc * bessel0 (beta * std::sqrt (1.0 - u * u)) / w0;
        };
        // integrate kOrders times with the trapezoidal rule; the kernel's moments on the way
        double acc[kOrders] = {}, prevK = kernel (-kHalf), m2 = 0.0, m4 = 0.0;
        std::vector<double> at ((size_t) (kOrders * kSize), 0.0);
        for (int i = 1; i <= n; ++i)
        {
            const double t = -kHalf + i * dt;
            const double k = kernel (t);
            double lower = 0.5 * (k + prevK);
            prevK = k;
            m2 += lower * (t - 0.5 * dt) * (t - 0.5 * dt) * dt;
            m4 += lower * std::pow (t - 0.5 * dt, 4.0) * dt;
            for (int o = 0; o < kOrders; ++o)
            {
                const double before = acc[o];
                acc[o] += lower * dt;
                lower = 0.5 * (acc[o] + before);
            }
            if (i % sub == 0 && i / sub < kSize)
                for (int o = 0; o < kOrders; ++o)
                    at[(size_t) (o * kSize + i / sub)] = acc[o];
        }
        const double total = acc[0];
        m2 /= total;
        m4 /= total;
        for (auto& v : at)
            v /= total;
        // A windowed kernel's even moments are not quite zero: past the kernel its smoothed t^n / n!
        // sits off the naive one by lower powers. Taking those off with the (cleaned) lower orders
        // makes each residual end at zero, so nothing is left over after the last tap.
        auto clean = [&] (int o, int lower, double c)
        {
            for (int i = 0; i < kSize; ++i)
                at[(size_t) (o * kSize + i)] -= c * at[(size_t) (lower * kSize + i)];
        };
        clean (2, 0, m2 / 2.0);
        clean (3, 1, m2 / 2.0);
        clean (4, 2, m2 / 2.0);
        clean (4, 0, m4 / 24.0);
        clean (5, 3, m2 / 2.0);
        clean (5, 1, m4 / 24.0);
        const double fact[kOrders] = { 1.0, 1.0, 2.0, 6.0, 24.0, 120.0 };
        // what the integration's rounding leaves past the kernel (a tiny offset and slope) as well
        auto tail = [&] (int o, int i)
        {
            const double t = (double) i / kRes - kHalf;
            return at[(size_t) (o * kSize + i)] - std::pow (t, (double) o) / fact[o];
        };
        for (int o = 2; o < kOrders; ++o)
        {
            const double slope = (tail (o, kSize - 1) - tail (o, kSize - 2)) * kRes;
            const double offset = tail (o, kSize - 1) - slope * ((double) (kSize - 1) / kRes - kHalf);
            clean (o, 1, slope);
            clean (o, 0, offset);
        }
        for (int i = 0; i < kSize; ++i)
        {
            // the band-limited step and ramp themselves (continuous, so they interpolate well);
            // the naive step / ramp is taken off per tap
            step[i] = (float) at[(size_t) i];
            ramp[i] = (float) at[(size_t) (kSize + i)];
            const double t = (double) i / kRes - kHalf, tp = t > 0.0 ? t : 0.0;
            for (int o = 2; o < kOrders; ++o)
                residual[o - 2][i] = i == kSize - 1 ? 0.0f : (float) (at[(size_t) (o * kSize + i)] - std::pow (tp, (double) o) / fact[o]);
        }
        step[kSize - 1] = 1.0f;
        ramp[kSize - 1] = (float) kHalf;
    }

    // Built once, on first use (about 10 ms): call it from prepare so the audio thread never builds it.
    static const KaiserBlepTables& get()
    {
        static const KaiserBlepTables tables;
        return tables;
    }
};

// The stateful corrector: the naive output goes in, the band-limited one comes out
// kKaiserBlepHalf samples later. Per sample: advance(), (events), push (naive), pull().
class KaiserBlepCorrector
{
public:
    static constexpr int kBuf = 32, kMask = kBuf - 1;
    static constexpr int kHalf = kKaiserBlepHalf, kRes = kKaiserBlepRes;

    KaiserBlepCorrector() : tables (&KaiserBlepTables::get()) {}

    const KaiserBlepTables& getTables() const noexcept { return *tables; }

    // silence (the write position is kept)
    void reset() noexcept
    {
        for (int i = 0; i < kBuf; ++i)
            corr[i] = delayed[i] = 0.0f;
    }

    // Fills the delay line with the waveform's past: `back` samples before the current one.
    void prime (int back, float naive) noexcept { delayed[(pos - back) & kMask] = naive; }

    // Next sample (call before the events of that sample).
    void advance() noexcept { pos = (pos + 1) & kMask; }

    // The current sample's naive value.
    void push (float naive) noexcept { delayed[pos] = naive; }

    // The band-limited output for the sample kKaiserBlepHalf behind the current one.
    float pull() noexcept
    {
        const int o = (pos - kHalf) & kMask;
        const float y = delayed[o] + corr[o];
        corr[o] = 0.0f;
        return y;
    }

    // A step of height h, d samples (0..1) before the current sample, spread over the kHalf
    // samples on each side. The first tap is the sample kHalf before the current one, at
    // t = d - kHalf; taps from the current sample on (t >= 0) already hold the naive step.
    BIDOO_DSP_INLINE void step (float h, float d) noexcept
    {
        if (! (std::abs (h) > 0.0f))
            return;
        const float x = d * (float) kRes;
        int i = (int) x;
        const float f = x - (float) i;
        const float* table = tables->step;
        int at = (pos - kHalf) & kMask;
        for (int j = 0; j < 2 * kHalf; ++j, i += kRes)
        {
            const float a = table[i], b = table[i + 1];
            corr[at] += h * (a + (b - a) * f - (j >= kHalf ? 1.0f : 0.0f));
            at = (at + 1) & kMask;
        }
    }

    // A slope change of r (value per sample), d samples before the current sample.
    BIDOO_DSP_INLINE void ramp (float r, float d) noexcept
    {
        if (! (std::abs (r) > 0.0f))
            return;
        const float x = d * (float) kRes;
        int i = (int) x;
        const float f = x - (float) i;
        const float* table = tables->ramp;
        int at = (pos - kHalf) & kMask;
        for (int j = 0; j < 2 * kHalf; ++j, i += kRes)
        {
            const float a = table[i], b = table[i + 1];
            const float t = (float) (j - kHalf) + d;
            corr[at] += r * (a + (b - a) * f - (t > 0.0f ? t : 0.0f));
            at = (at + 1) & kMask;
        }
    }

    // A change of curvature (value per sample squared) or of its slope (per sample cubed), and
    // of the fourth and fifth derivatives: those tables hold the residual itself. order 2..5.
    BIDOO_DSP_INLINE void residual (int order, float h, float d) noexcept { residualTable (tables->residual[order - 2], h, d); }
    BIDOO_DSP_INLINE void para (float c, float d) noexcept { residualTable (tables->residual[0], c, d); }
    BIDOO_DSP_INLINE void cubic (float c, float d) noexcept { residualTable (tables->residual[1], c, d); }

private:
    BIDOO_DSP_INLINE void residualTable (const float* table, float h, float d) noexcept
    {
        if (! (std::abs (h) > 1.0e-12f))
            return;
        const float x = d * (float) kRes;
        int i = (int) x;
        const float f = x - (float) i;
        int at = (pos - kHalf) & kMask;
        for (int j = 0; j < 2 * kHalf; ++j, i += kRes)
        {
            const float a = table[i], b = table[i + 1];
            corr[at] += h * (a + (b - a) * f);
            at = (at + 1) & kMask;
        }
    }

    const KaiserBlepTables* tables;
    float corr[kBuf] = {}, delayed[kBuf] = {};
    int pos = 0;
};

// ---- stateless form ----------------------------------------------------------------------------

namespace kaiser_detail
{
// the band-limited step minus the naive one at x samples from the step (x < 0: before it);
// the caller guarantees -kKaiserBlepHalf < x < kKaiserBlepHalf
BIDOO_DSP_INLINE double stepAt (const KaiserBlepTables& t, double x) noexcept
{
    const double u = (x + (double) kKaiserBlepHalf) * (double) kKaiserBlepRes;
    const int i = (int) u;
    const double f = u - (double) i;
    const double a = (double) t.step[i], b = (double) t.step[i + 1];
    return a + (b - a) * f - (x >= 0.0 ? 1.0 : 0.0);
}

BIDOO_DSP_INLINE double rampAt (const KaiserBlepTables& t, double x) noexcept
{
    const double u = (x + (double) kKaiserBlepHalf) * (double) kKaiserBlepRes;
    const int i = (int) u;
    const double f = u - (double) i;
    const double a = (double) t.ramp[i], b = (double) t.ramp[i + 1];
    return a + (b - a) * f - (x > 0.0 ? x : 0.0);
}

// Sum over the images d + k of an event of a periodic waveform that fall in the kernel (a few
// above fs / 24, one at most below).
template <typename At>
BIDOO_DSP_INLINE double images (const KaiserBlepTables& t, double d, double dt, At at) noexcept
{
    const double reach = (double) kKaiserBlepHalf * dt;
    if (reach < 0.5)
        return std::fabs (d) < reach ? at (t, d / dt) : 0.0;
    const double inv = 1.0 / dt;
    double sum = 0.0;
    for (double e = d + std::ceil (-reach - d); e < reach; e += 1.0)
        sum += at (t, e * inv);
    return sum;
}
} // namespace kaiser_detail

// Drop-ins for blep() / blamp(): the residual of a unit step up at phase distance d (negative
// before it; the distance from phaseDistance(), in [-0.5, 0.5)) for phase increment dt per
// sample, to add as `naive + height * kaiserBlep (d, dt)`; and of a corner where the slope rises
// by one unit per sample, as `naive + slopeChangePerSample * kaiserBlamp (d, dt)` (the polyBLAMP
// u^3 / 6 shape, in samples). Pass the tables to keep a function-local static out of the loop.
BIDOO_DSP_INLINE double kaiserBlep (const KaiserBlepTables& t, double d, double dt) noexcept
{
    return kaiser_detail::images (t, d, dt, [] (const KaiserBlepTables& tt, double x) { return kaiser_detail::stepAt (tt, x); });
}

BIDOO_DSP_INLINE double kaiserBlamp (const KaiserBlepTables& t, double d, double dt) noexcept
{
    return kaiser_detail::images (t, d, dt, [] (const KaiserBlepTables& tt, double x) { return kaiser_detail::rampAt (tt, x); });
}

// (Away from every discontinuity they return before touching the tables.)
BIDOO_DSP_INLINE double kaiserBlep (double d, double dt) noexcept
{
    const double reach = (double) kKaiserBlepHalf * dt;
    return reach < 0.5 && ! (std::fabs (d) < reach) ? 0.0 : kaiserBlep (KaiserBlepTables::get(), d, dt);
}

BIDOO_DSP_INLINE double kaiserBlamp (double d, double dt) noexcept
{
    const double reach = (double) kKaiserBlepHalf * dt;
    return reach < 0.5 && ! (std::fabs (d) < reach) ? 0.0 : kaiserBlamp (KaiserBlepTables::get(), d, dt);
}
} // namespace bidoo::dsp

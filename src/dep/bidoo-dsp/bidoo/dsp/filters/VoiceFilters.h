#pragma once

// Synth voice filters: the families a modern synth's filter menu offers beyond the plain SVF and
// ladder types, written for Potion 0.59.0 and meant for any Bidoo synth. Each family is a mono
// kernel split three ways so a plugin can keep one set of coefficients for both channels and
// choose the kernel at compile time:
//   Coeffs                  set at control rate by design (coeffs, settings): the cutoff, resonance,
//                           a 0..1 character setting ("morph": spread, damping, gain, vowel...) and drive
//   State                   one channel's memory; reset() to silence
//   process (state, c, x)   one sample, inlined into the caller's loop
// Families with delay lines take their memory from the caller (memoryNeeded (fs) floats per channel,
// attach (state, memory, fs) once after allocating it), so a plugin can share one buffer between
// families it never runs at once. Nothing allocates and nothing throws. A NaN or an infinity puts the
// state back to silence by itself (settle(), below): no reset from outside is needed.
//
// Every resonant loop here is stable whatever the cutoff does (moved every sample, at audio rate):
// linear loops keep their gain under 1 at every frequency, nonlinear ones are limited by a bounded
// saturator. Resonance follows the SVF convention of the Bidoo synths, damping k = 2 - 1.98 res.
//
//   Slope<Low6 / High6 / Low18 / High18 / Peak24>  one-pole and 18 dB slopes; a sharper peak.
//   Dual<A, B>       two resonant 12 dB SVFs, the first below the cutoff and the second above it
//                    (morph = spread, up to two octaves each way); in parallel, or in series when
//                    one of them is a notch (so its notch stays deep).
//   Morph3<A, B, C, Two>  one SVF whose output morphs A -> B -> C (low / peak / high, low / notch /
//                    high, band / peak / notch); Two cascades a second Butterworth stage (24 dB).
//   FilteredComb<Loop, Negative>  a feedback comb tuned to the cutoff with a one-pole low-pass,
//                    high-pass or both inside the loop (morph moves them), the loop's phase taken off
//                    the delay so the comb stays in tune; positive or negative feedback.
//   Phaser<N, Negative>  N first-order all-passes spread around the cutoff (morph = spread, up to
//                    four octaves), feedback through a soft clip: N / 2 notches.
//   BandEq           a bell (Simper's SVF bell): morph = gain -24..+24 dB, res = narrowness.
//   RingMod          the input times a carrier at the cutoff; morph rounds the sine into a square
//                    (tanh), res feeds the output back.
//   ChordComb        four combs at the cutoff and at 4, 7 and 12 semitones above it, morph
//                    spreading them from unison to that chord.
//   SmallReverb      a four-line feedback delay network (Hadamard) sized by the cutoff (the lower,
//                    the larger, 5 to 28 ms), res = decay, morph = damping; dry plus the room.
//   AddBass          a 24 dB low-pass with a saturated bass band added back under it (morph, up to
//                    +12 dB): the resonance thins nothing.
//   Scream<Band>     a resonant SVF with a saturated positive feedback of its band-pass (morph, scaled
//                    by the resonance): from a plain low or band-pass to a screaming self-oscillation
//                    (full morph and resonance) the tanh holds.
//   Cmos12           an SVF whose integrators slow down with the level of their input (the secant of
//                    tanh, as in a CMOS-inverter filter): gritty and unstable-sounding with
//                    resonance; morph sweeps low -> band -> high.
//   DjFilter         a DJ mixer's one-knob filter: the cutoff's lower half closes a low-pass, its
//                    upper half opens a high-pass, the middle is flat (morph = width of the flat zone).
//   FormantBank      three band-passes at the formants of a vowel table (A, E, I, O, U), morph moving
//                    through the vowels and the cutoff shifting them (1 kHz leaves them in place).

#include "TptSvf.h"
#include "../core/Inline.h"
#include "../shaping/TanhPade.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>

namespace bidoo::dsp::voicefilters
{
constexpr float kPi = 3.14159265f;
constexpr float kSqrt2 = 1.41421356f;

// What a family's design reads. cutoffHz is clamped to 10 Hz .. 0.45 fs inside.
struct Settings
{
    double fs = 48000.0;
    float cutoffHz = 1000.0f;
    float res = 0.0f;   // 0..1
    float morph = 0.0f; // 0..1, the family's character setting
    float drive = 0.0f; // 0..1, used inside the families that saturate (Scream, Cmos12)
};

inline float clampHz (double fs, float hz) { return std::clamp (hz, 10.0f, (float) (fs * 0.45)); }
// The bilinear transform's prewarped integrator gain, tan (pi fc / fs).
inline float prewarp (double fs, float hz) { return std::tan (kPi * clampHz (fs, hz) / (float) fs); }
inline float damping (float res) { return 2.0f - 1.98f * std::clamp (res, 0.0f, 1.0f); }
// The secant of a saturator through the origin, u / v (in (0, 1]).
BIDOO_DSP_INLINE float secant (float u, float v) { return std::abs (v) > 1.0e-4f ? u / v : 1.0f; }

using SvfC = TptSvfCoeffs<float>;
using SvfS = TptSvfState<float>;
using SvfOut = TptSvfOut<float>;

// The responses an SVF gives from its three outputs; band is normalised (unity at the cutoff).
enum class Resp { Low, High, Band, Peak, Notch };
template <Resp R>
BIDOO_DSP_INLINE float pick (const SvfOut& o, float k)
{
    if constexpr (R == Resp::Low)
        return o.lp;
    else if constexpr (R == Resp::High)
        return o.hp;
    else if constexpr (R == Resp::Band)
        return o.bp * k;
    else if constexpr (R == Resp::Peak)
        return o.lp - o.hp;
    else
        return o.lp + o.hp;
}
// The response's gain at DC and at the top, for the level of a parallel pair.
constexpr float dcGain (Resp r) { return r == Resp::Low || r == Resp::Peak || r == Resp::Notch ? 1.0f : 0.0f; }
constexpr float topGain (Resp r) { return r == Resp::High || r == Resp::Notch ? 1.0f : (r == Resp::Peak ? -1.0f : 0.0f); }
constexpr float constAbs (float v) { return v < 0.0f ? -v : v; }

// A ring of floats in the caller's memory: read `delay` samples before the write position, then
// push. Hermite reads (4-point, 3rd order) as Potion's combs: a flat top and in-tune sweeps.
struct Ring
{
    float* d = nullptr;
    int n = 0, pos = 0;

    void attach (float* memory, int length)
    {
        d = memory;
        n = length;
        pos = 0;
    }
    void clear()
    {
        if (d != nullptr)
            std::fill (d, d + n, 0.0f);
        pos = 0;
    }
    BIDOO_DSP_INLINE void push (float x)
    {
        d[pos] = x;
        pos = pos + 1 < n ? pos + 1 : 0;
    }
    // a whole number of samples back, 1 <= delay <= n - 1
    BIDOO_DSP_INLINE float at (int delay) const
    {
        const int i = pos - delay;
        return d[i < 0 ? i + n : i];
    }
    // 2 <= delay <= n - 3
    BIDOO_DSP_INLINE float hermite (float delay) const
    {
        float readPos = (float) pos - delay;
        if (readPos < 0.0f)
            readPos += (float) n;
        int i0 = (int) readPos;
        const float t = readPos - (float) i0;
        if (i0 >= n)
            i0 -= n;
        const int im1 = i0 > 0 ? i0 - 1 : n - 1;
        const int i1 = i0 + 1 < n ? i0 + 1 : i0 + 1 - n;
        const int i2 = i0 + 2 < n ? i0 + 2 : i0 + 2 - n;
        const float xm1 = d[im1], x0 = d[i0], x1 = d[i1], x2 = d[i2];
        const float c = 0.5f * (x1 - xm1);
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + 0.5f * (x2 - x0);
        const float b = w + a;
        return ((a * t - b) * t + c) * t + x0;
    }
};

// Every family's process() ends here: a NaN or an infinity (in the input, or grown in a loop) would
// stay in the state forever, so the state goes back to silence (its reset(), memory included) and the
// sample is 0; from the next sample on the family plays as a freshly reset one. Every state reaches
// the output on the sample it goes bad, so the output is all there is to check: one compare per
// sample, and finite audio is untouched.
template <typename State>
BIDOO_DSP_NOINLINE float resetToSilence (State& st)
{
    st.reset();
    return 0.0f;
}

template <typename State>
BIDOO_DSP_INLINE float settle (State& st, float y)
{
    // NaN or infinity: false for NaN, so the test is written the way round that NaN fails it (one
    // compare, cheaper than std::isfinite in MSVC's loops)
    if (! (std::fabs (y) <= 3.40282347e38f))
        return resetToSilence (st);
    return y;
}

// The longest comb any family here tunes: 20 Hz.
inline int combLength (double fs) { return (int) (fs / 20.0) + 4; }

// ---------------------------------------------------------------------------------------------
enum class SlopeKind { Low6, High6, Low18, High18, Peak24 };

template <SlopeKind K>
struct Slope
{
    struct Coeffs
    {
        SvfC svf;
        float G = 0.1f; // the one-pole's G = g / (1 + g)
    };
    struct State
    {
        SvfS s1, s2;
        TptOnePole<float> p;
        void reset()
        {
            s1.reset();
            s2.reset();
            p.reset();
        }
    };

    static void design (Coeffs& c, const Settings& s)
    {
        const float g = prewarp (s.fs, s.cutoffHz);
        c.G = g / (1.0f + g);
        const float k = damping (s.res);
        // Peak 24: two peak stages, each with the square root of the 12 dB peak's height (2 / k), so
        // the peak is as tall but narrower, and both ends stay at unity
        c.svf = SvfC::make (g, K == SlopeKind::Peak24 ? std::sqrt (2.0f * k) : k);
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x)
    {
        if constexpr (K == SlopeKind::Low6)
            return settle (st, st.p.lowPass (x, c.G));
        else if constexpr (K == SlopeKind::High6)
            return settle (st, st.p.highPass (x, c.G));
        else if constexpr (K == SlopeKind::Low18)
            return settle (st, st.p.lowPass (st.s1.tick (x, c.svf).lp, c.G));
        else if constexpr (K == SlopeKind::High18)
            return settle (st, st.p.highPass (st.s1.tick (x, c.svf).hp, c.G));
        else
            return settle (st, pick<Resp::Peak> (st.s2.tick (pick<Resp::Peak> (st.s1.tick (x, c.svf), c.svf.k), c.svf), c.svf.k));
    }
};

// ---------------------------------------------------------------------------------------------
template <Resp A, Resp B>
struct Dual
{
    static constexpr bool kSeries = A == Resp::Notch || B == Resp::Notch;
    // a parallel pair's level: the sum held to unity at DC and at the top
    static constexpr float kGain = 1.0f
                                   / std::max (1.0f, std::max (constAbs (dcGain (A) + dcGain (B)), constAbs (topGain (A) + topGain (B))));

    struct Coeffs
    {
        SvfC a, b;
    };
    struct State
    {
        SvfS a, b;
        void reset()
        {
            a.reset();
            b.reset();
        }
    };

    static void design (Coeffs& c, const Settings& s)
    {
        const float spread = std::exp2 (2.0f * std::clamp (s.morph, 0.0f, 1.0f)); // up to two octaves each way
        const float k = damping (s.res);
        c.a = SvfC::make (prewarp (s.fs, s.cutoffHz / spread), k);
        c.b = SvfC::make (prewarp (s.fs, s.cutoffHz * spread), k);
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x)
    {
        if constexpr (kSeries)
            return settle (st, pick<B> (st.b.tick (pick<A> (st.a.tick (x, c.a), c.a.k), c.b), c.b.k));
        else
            return settle (st, (pick<A> (st.a.tick (x, c.a), c.a.k) + pick<B> (st.b.tick (x, c.b), c.b.k)) * kGain);
    }
};

// ---------------------------------------------------------------------------------------------
template <Resp A, Resp B, Resp C, bool Two>
struct Morph3
{
    struct Coeffs
    {
        SvfC first, second;
        float m = 0.0f; // 0..2
    };
    struct State
    {
        SvfS a, b;
        void reset()
        {
            a.reset();
            b.reset();
        }
    };

    static void design (Coeffs& c, const Settings& s)
    {
        const float g = prewarp (s.fs, s.cutoffHz);
        c.first = SvfC::make (g, damping (s.res));
        c.second = SvfC::make (g, kSqrt2);
        c.m = 2.0f * std::clamp (s.morph, 0.0f, 1.0f);
    }

    BIDOO_DSP_INLINE static float blend (const SvfOut& o, float k, float m)
    {
        const float a = pick<A> (o, k), b = pick<B> (o, k), cc = pick<C> (o, k);
        return m < 1.0f ? a + m * (b - a) : b + (m - 1.0f) * (cc - b);
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x)
    {
        const float y = blend (st.a.tick (x, c.first), c.first.k, c.m);
        if constexpr (! Two)
            return settle (st, y);
        else
            return settle (st, blend (st.b.tick (y, c.second), c.second.k, c.m));
    }
};

// ---------------------------------------------------------------------------------------------
enum class CombLoop { Low, High, Band };

template <CombLoop L, bool Negative>
struct FilteredComb
{
    struct Coeffs
    {
        float delay = 100.0f, fb = 0.0f, gLow = 0.9f, gHigh = 0.0f, out = 1.0f;
    };
    struct State
    {
        Ring ring;
        TptOnePole<float> low, high;
        void reset()
        {
            ring.clear();
            low.reset();
            high.reset();
        }
    };

    static int memoryNeeded (double fs) { return combLength (fs); }
    static void attach (State& st, float* memory, double fs) { st.ring.attach (memory, memoryNeeded (fs)); }

    static void design (Coeffs& c, const Settings& s)
    {
        const float fc = clampHz (s.fs, s.cutoffHz);
        const float m = std::clamp (s.morph, 0.0f, 1.0f);
        const float top = (float) (s.fs * 0.45);
        // Low (Damp): the loop's low-pass from 0.45 fs down to 1.5 x the cutoff; High (Thin): its
        // high-pass from four octaves under the cutoff up to 1.5 x; Band (Focus): both closing in on it
        float fl = top, fh = 0.0f;
        if constexpr (L == CombLoop::Low)
            fl = top * std::pow (std::min (1.5f * fc, top) / top, m);
        else if constexpr (L == CombLoop::High)
            fh = fc * std::exp2 (-4.0f + 4.585f * m);
        else
        {
            fl = top * std::pow (std::min (2.0f * fc, top) / top, m);
            fh = fc * std::exp2 (-4.0f + 3.4f * m);
        }
        const float gl = std::tan (kPi * std::min (fl, top) / (float) s.fs);
        const float gh = std::tan (kPi * std::min (fh, top) / (float) s.fs);
        c.gLow = gl / (1.0f + gl);
        c.gHigh = gh / (1.0f + gh);
        // the loop filters' phase delay comes off the delay, measured at whichever of the first four
        // peaks the loop passes loudest (the fundamental with the low-pass, upper ones with the
        // high-pass, whose phase turns fast near its corner): that peak stays exactly in tune
        auto loopAt = [&] (float ww)
        {
            const std::complex<float> z1 = std::polar (1.0f, -ww);
            auto onePoleLow = [&] (float gg) { return gg * (1.0f + z1) / ((1.0f + gg) - (1.0f - gg) * z1); };
            std::complex<float> loop = 1.0f;
            if constexpr (L != CombLoop::High)
                loop *= onePoleLow (gl);
            if constexpr (L != CombLoop::Low)
                loop *= 1.0f - onePoleLow (gh);
            return loop;
        };
        const float w1 = (Negative ? kPi : 2.0f * kPi) * fc / (float) s.fs; // the first peak
        float w = w1;
        float most = -1.0f;
        for (int n = 1; n <= 4; ++n)
        {
            const float wn = w1 * (float) (Negative ? 2 * n - 1 : n); // a negative comb peaks on odd multiples
            if (wn >= kPi)
                break;
            const float a = std::abs (loopAt (wn));
            if (a > most + 1.0e-3f)
                most = a, w = wn;
        }
        const float lag = -std::arg (loopAt (w)) / w;
        c.delay = std::clamp ((float) s.fs / fc - lag, 2.0f, (float) combLength (s.fs) - 3.0f);
        // the loop filters' gain is at most 1 everywhere, so |fb| < 1 keeps it stable
        const float fb = 0.985f * std::clamp (s.res, 0.0f, 1.0f);
        c.fb = Negative ? -fb : fb;
        c.out = 1.0f - 0.5f * fb;
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x)
    {
        float d = st.ring.hermite (c.delay);
        if constexpr (L != CombLoop::High)
            d = st.low.lowPass (d, c.gLow);
        if constexpr (L != CombLoop::Low)
            d = st.high.highPass (d, c.gHigh);
        // a soft limit at +-2 (linear to about +-1): the interpolated read's gain can pass 1 a little
        // when the delay moves every sample, and this keeps the loop bounded whatever the sweep
        const float y = x + c.fb * 2.0f * tanhPade (0.5f * d);
        st.ring.push (y);
        return settle (st, y * c.out);
    }
};

// ---------------------------------------------------------------------------------------------
template <int N, bool Negative>
struct Phaser
{
    static_assert (N >= 2 && N <= 32);
    struct Coeffs
    {
        float G[N] {};
        float fb = 0.0f;
    };
    struct State
    {
        TptOnePole<float> ap[N];
        float last = 0.0f;
        void reset()
        {
            for (auto& a : ap)
                a.reset();
            last = 0.0f;
        }
    };

    static void design (Coeffs& c, const Settings& s)
    {
        const float m = std::clamp (s.morph, 0.0f, 1.0f);
        for (int i = 0; i < N; ++i)
        {
            const float g = prewarp (s.fs, s.cutoffHz * std::exp2 (m * 4.0f * ((float) i / (float) (N - 1) - 0.5f)));
            c.G[i] = g / (1.0f + g);
        }
        const float fb = 0.9f * std::clamp (s.res, 0.0f, 1.0f);
        c.fb = Negative ? -fb : fb;
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x)
    {
        float u = x + c.fb * st.last;
        for (int i = 0; i < N; ++i)
            u = st.ap[i].allPass (u, c.G[i]);
        st.last = tanhPade (u); // the feedback soft-limited
        return settle (st, 0.5f * (x + u));
    }
};

// ---------------------------------------------------------------------------------------------
struct BandEq
{
    struct Coeffs
    {
        SvfC svf;
        float m1 = 0.0f;
    };
    struct State
    {
        SvfS s;
        void reset() { s.reset(); }
    };

    static void design (Coeffs& c, const Settings& s)
    {
        const float A = std::pow (10.0f, (std::clamp (s.morph, 0.0f, 1.0f) - 0.5f) * 48.0f / 40.0f); // -24..+24 dB
        const float q = 0.5f + 11.5f * std::clamp (s.res, 0.0f, 1.0f) * std::clamp (s.res, 0.0f, 1.0f);
        const float k = 1.0f / (q * A);
        c.svf = SvfC::make (prewarp (s.fs, s.cutoffHz), k);
        c.m1 = k * (A * A - 1.0f);
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x) { return settle (st, x + c.m1 * st.s.tick (x, c.svf).bp); }
};

// ---------------------------------------------------------------------------------------------
struct RingMod
{
    struct Coeffs
    {
        float inc = 0.02f, beta = 0.05f, norm = 1.0f, fb = 0.0f;
    };
    struct State
    {
        float phase = 0.0f, last = 0.0f;
        void reset() { phase = last = 0.0f; }
    };

    static void design (Coeffs& c, const Settings& s)
    {
        c.inc = clampHz (s.fs, s.cutoffHz) / (float) s.fs;
        c.beta = 0.05f + 5.0f * std::clamp (s.morph, 0.0f, 1.0f);
        c.norm = 1.0f / tanhPade (c.beta);
        c.fb = 0.9f * std::clamp (s.res, 0.0f, 1.0f);
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x)
    {
        st.phase += c.inc;
        if (st.phase >= 1.0f)
            st.phase -= 1.0f;
        const float carrier = tanhPade (c.beta * std::sin (2.0f * kPi * st.phase)) * c.norm;
        const float y = (x + c.fb * tanhPade (st.last)) * carrier;
        st.last = y;
        return settle (st, y);
    }
};

// ---------------------------------------------------------------------------------------------
struct ChordComb
{
    static constexpr int kCombs = 4;
    // the four combs' intervals at full morph, in semitones: root, major third, fifth, octave
    static constexpr float kSemis[kCombs] { 0.0f, 4.0f, 7.0f, 12.0f };

    struct Coeffs
    {
        float delay[kCombs] { 100.0f, 100.0f, 100.0f, 100.0f };
        float fb = 0.0f, out = 0.25f;
    };
    struct State
    {
        Ring ring[kCombs];
        void reset()
        {
            for (auto& r : ring)
                r.clear();
        }
    };

    // each comb tunes down to 40 Hz
    static int ringLength (double fs) { return (int) (fs / 40.0) + 4; }
    static int memoryNeeded (double fs) { return kCombs * ringLength (fs); }
    static void attach (State& st, float* memory, double fs)
    {
        for (int i = 0; i < kCombs; ++i)
            st.ring[i].attach (memory + i * ringLength (fs), ringLength (fs));
    }

    static void design (Coeffs& c, const Settings& s)
    {
        const float m = std::clamp (s.morph, 0.0f, 1.0f);
        const float fc = clampHz (s.fs, s.cutoffHz);
        for (int i = 0; i < kCombs; ++i)
            c.delay[i] = std::clamp ((float) s.fs / (fc * std::exp2 (m * kSemis[i] / 12.0f)), 2.0f, (float) ringLength (s.fs) - 3.0f);
        c.fb = 0.97f * std::clamp (s.res, 0.0f, 1.0f);
        c.out = (1.0f - 0.5f * c.fb) / (float) kCombs;
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x)
    {
        float sum = 0.0f;
        for (int i = 0; i < kCombs; ++i)
        {
            const float y = x + c.fb * 2.0f * tanhPade (0.5f * st.ring[i].hermite (c.delay[i])); // bounded as FilteredComb's
            st.ring[i].push (y);
            sum += y;
        }
        return settle (st, sum * c.out);
    }
};

// ---------------------------------------------------------------------------------------------
struct SmallReverb
{
    static constexpr int kLines = 4;
    static constexpr float kMaxSeconds = 0.028f;
    // the lines' lengths as fractions of the room's time (mutually prime-ish, a little different on
    // the right so the two channels decorrelate); the channel picks a set with `right` in attach
    static constexpr float kWeights[2][kLines] { { 0.731f, 0.829f, 0.917f, 1.0f }, { 0.703f, 0.851f, 0.893f, 0.983f } };

    struct Coeffs
    {
        float time = 0.01f; // seconds
        float fb = 0.5f, gDamp = 0.9f, in = 0.5f;
        double fs = 48000.0;
    };
    struct State
    {
        Ring ring[kLines];
        TptOnePole<float> damp[kLines];
        int len[kLines] { 2, 2, 2, 2 };
        int side = 0;
        void reset()
        {
            for (int i = 0; i < kLines; ++i)
            {
                ring[i].clear();
                damp[i].reset();
            }
        }
    };

    static int lineLength (double fs) { return (int) (fs * kMaxSeconds) + 4; }
    static int memoryNeeded (double fs) { return kLines * lineLength (fs); }
    // right: the channel takes the second set of line lengths
    static void attach (State& st, float* memory, double fs, bool right = false)
    {
        st.side = right ? 1 : 0;
        for (int i = 0; i < kLines; ++i)
            st.ring[i].attach (memory + i * lineLength (fs), lineLength (fs));
    }

    static void design (Coeffs& c, const Settings& s)
    {
        // the lower the cutoff, the larger the room: 28 ms at 20 Hz, 10.5 ms at 1 kHz, 5 ms at 20 kHz
        c.time = kMaxSeconds * std::pow (20.0f / std::max (20.0f, clampHz (s.fs, s.cutoffHz)), 0.25f);
        c.fb = 0.35f + 0.63f * std::clamp (s.res, 0.0f, 1.0f);
        // the input scaled by sqrt (1 - fb^2), so the room's level stays about the same whatever the decay
        c.in = std::sqrt (1.0f - c.fb * c.fb);
        const float top = (float) (s.fs * 0.45);
        const float fd = top * std::pow (1500.0f / top, std::clamp (s.morph, 0.0f, 1.0f));
        const float g = std::tan (kPi * std::min (fd, top) / (float) s.fs);
        c.gDamp = g / (1.0f + g);
        c.fs = s.fs;
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x)
    {
        float d[kLines];
        for (int i = 0; i < kLines; ++i)
        {
            const int n = std::clamp ((int) ((float) c.fs * c.time * kWeights[st.side][i]), 1, st.ring[i].n - 1);
            d[i] = st.damp[i].lowPass (st.ring[i].at (n), c.gDamp);
        }
        // the Hadamard matrix (orthogonal, so with fb < 1 and the dampers under unity the loop decays)
        const float a = d[0] + d[1], b = d[0] - d[1], e = d[2] + d[3], f = d[2] - d[3];
        const float h[kLines] { 0.5f * (a + e), 0.5f * (b + f), 0.5f * (a - e), 0.5f * (b - f) };
        for (int i = 0; i < kLines; ++i)
            st.ring[i].push (c.in * x + c.fb * h[i]);
        return settle (st, x + 0.6f * (d[0] - d[1] + d[2] - d[3]) * 0.5f);
    }
};

// ---------------------------------------------------------------------------------------------
struct AddBass
{
    struct Coeffs
    {
        SvfC first, second;
        float gBass = 0.1f, bass = 0.0f;
    };
    struct State
    {
        SvfS a, b;
        TptOnePole<float> low;
        void reset()
        {
            a.reset();
            b.reset();
            low.reset();
        }
    };

    static void design (Coeffs& c, const Settings& s)
    {
        const float g = prewarp (s.fs, s.cutoffHz);
        c.first = SvfC::make (g, damping (s.res));
        c.second = SvfC::make (g, kSqrt2);
        const float gb = prewarp (s.fs, std::min (0.5f * s.cutoffHz, 250.0f)); // the bass band: under the cutoff, at most 250 Hz
        c.gBass = gb / (1.0f + gb);
        c.bass = std::pow (10.0f, 12.0f * std::clamp (s.morph, 0.0f, 1.0f) / 20.0f) - 1.0f;
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x)
    {
        const float low = st.b.tick (st.a.tick (x, c.first).lp, c.second).lp;
        const float bass = st.low.lowPass (x, c.gBass);
        return settle (st, low + c.bass * 0.5f * tanhPade (2.0f * bass)); // the bass rounded as it grows
    }
};

// ---------------------------------------------------------------------------------------------
template <bool Band>
struct Scream
{
    struct Coeffs
    {
        SvfC svf;
        float fb = 0.0f, beta = 1.0f;
    };
    struct State
    {
        SvfS s;
        float last = 0.0f;
        void reset()
        {
            s.reset();
            last = 0.0f;
        }
    };

    static void design (Coeffs& c, const Settings& s)
    {
        c.svf = SvfC::make (prewarp (s.fs, s.cutoffHz), 2.0f - 1.6f * std::clamp (s.res, 0.0f, 1.0f));
        // past 1 the loop sings, held by the tanh: at full Morph from about 60 % resonance up
        c.fb = 1.25f * std::clamp (s.morph, 0.0f, 1.0f) * (0.5f + 0.5f * std::clamp (s.res, 0.0f, 1.0f));
        c.beta = 1.5f + 4.0f * std::clamp (s.drive, 0.0f, 1.0f);
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x)
    {
        const float u = x + c.fb * tanhPade (c.beta * st.last) / c.beta;
        const auto o = st.s.tick (u, c.svf);
        st.last = o.bp * c.svf.k;
        if constexpr (Band)
            return settle (st, st.last);
        else
            return settle (st, o.lp);
    }
};

// ---------------------------------------------------------------------------------------------
struct Cmos12
{
    struct Coeffs
    {
        float g = 0.1f, k = 2.0f, a = 1.0f, m = 0.0f;
    };
    struct State
    {
        SvfS s;
        float t = 1.0f; // the last sample's secant
        void reset()
        {
            s.reset();
            t = 1.0f;
        }
    };

    static void design (Coeffs& c, const Settings& s)
    {
        c.g = prewarp (s.fs, s.cutoffHz);
        c.k = damping (s.res);
        c.a = 0.8f + 3.0f * std::clamp (s.drive, 0.0f, 1.0f);
        c.m = 2.0f * std::clamp (s.morph, 0.0f, 1.0f);
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x)
    {
        // the integrators' gain bent by the secant of tanh at the last high-pass (their input): loud
        // or resonant signals slow the filter down, which is the grit; g t > 0, so it stays stable
        const float g = c.g * st.t;
        const float a1 = 1.0f / (1.0f + g * (g + c.k));
        const auto o = st.s.tick (x, a1, g * a1, g * g * a1, c.k);
        const float v = c.a * o.hp;
        st.t = secant (tanhPade (v), v);
        const float band = o.bp * c.k;
        return settle (st, c.m < 1.0f ? o.lp + c.m * (band - o.lp) : band + (c.m - 1.0f) * (o.hp - band));
    }
};

// ---------------------------------------------------------------------------------------------
struct DjFilter
{
    struct Coeffs
    {
        SvfC low, high;
    };
    struct State
    {
        SvfS a, b;
        void reset()
        {
            a.reset();
            b.reset();
        }
    };

    static void design (Coeffs& c, const Settings& s)
    {
        // the knob's place from 20 Hz (0) to 20 kHz (1); its middle third or so is flat
        const float p = std::clamp (std::log2 (std::max (s.cutoffHz, 20.0f) / 20.0f) / std::log2 (1000.0f), 0.0f, 1.0f);
        const float w = 0.03f + 0.17f * std::clamp (s.morph, 0.0f, 1.0f);
        const float edge = 0.5f - w;
        const float res = std::clamp (s.res, 0.0f, 1.0f);
        // each side's resonance fades in over the first tenth of its travel, so the parked filter
        // (the low-pass at the top, the high-pass at the bottom) adds no bump
        const float lowTravel = std::clamp ((edge - p) / edge, 0.0f, 1.0f);
        const float highTravel = std::clamp ((p - 0.5f - w) / edge, 0.0f, 1.0f);
        const float lowHz = lowTravel > 0.0f ? 20000.0f * std::pow (1.0f / 1000.0f, lowTravel) : 20000.0f;
        const float highHz = highTravel > 0.0f ? 20.0f * std::pow (1000.0f, highTravel) : 10.0f;
        // Butterworth at res 0, a Q of about 9 at full resonance (a DJ filter's bump, never a whistle)
        c.low = SvfC::make (prewarp (s.fs, lowHz), kSqrt2 - 1.3f * res * std::min (1.0f, 10.0f * lowTravel));
        c.high = SvfC::make (prewarp (s.fs, highHz), kSqrt2 - 1.3f * res * std::min (1.0f, 10.0f * highTravel));
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x) { return settle (st, st.b.tick (st.a.tick (x, c.low).lp, c.high).hp); }
};

// ---------------------------------------------------------------------------------------------
// A vowel: three formants, each a frequency (Hz) and a level (dB).
struct Vowel
{
    float f[3][2];
};
// A, E, I, O, U sung by a female voice (from the published averages of vowel formants)
inline constexpr Vowel kFemaleVowels[5] {
    { { { 850.0f, 0.0f }, { 1220.0f, -5.0f }, { 2810.0f, -28.0f } } },
    { { { 610.0f, 0.0f }, { 2330.0f, -14.0f }, { 2990.0f, -18.0f } } },
    { { { 310.0f, 0.0f }, { 2790.0f, -20.0f }, { 3310.0f, -24.0f } } },
    { { { 590.0f, 0.0f }, { 920.0f, -8.0f }, { 2710.0f, -30.0f } } },
    { { { 370.0f, 0.0f }, { 950.0f, -14.0f }, { 2670.0f, -36.0f } } },
};

template <const Vowel* Table>
struct FormantBank
{
    struct Coeffs
    {
        SvfC band[3];
        float gain[3] { 1.0f, 1.0f, 1.0f };
    };
    struct State
    {
        SvfS s[3];
        void reset()
        {
            for (auto& b : s)
                b.reset();
        }
    };

    static void design (Coeffs& c, const Settings& s)
    {
        const float pos = std::clamp (s.morph, 0.0f, 1.0f) * 4.0f;
        const int v0 = std::min (3, (int) pos);
        const float t = pos - (float) v0;
        const float shift = std::clamp (s.cutoffHz / 1000.0f, 0.25f, 4.0f);
        const float q = 4.0f + std::clamp (s.res, 0.0f, 1.0f) * 16.0f;
        for (int b = 0; b < 3; ++b)
        {
            const float hz = std::exp (std::log (Table[v0].f[b][0]) * (1.0f - t) + std::log (Table[v0 + 1].f[b][0]) * t) * shift;
            const float db = Table[v0].f[b][1] * (1.0f - t) + Table[v0 + 1].f[b][1] * t;
            c.band[b] = SvfC::make (std::tan (kPi * std::clamp (hz, 30.0f, (float) (s.fs * 0.45)) / (float) s.fs), 1.0f / q);
            c.gain[b] = std::pow (10.0f, db / 20.0f) * 2.0f;
        }
    }

    BIDOO_DSP_INLINE static float process (State& st, const Coeffs& c, float x)
    {
        float y = 0.0f;
        for (int b = 0; b < 3; ++b)
            y += st.s[b].tick (x, c.band[b]).bp * c.band[b].k * c.gain[b];
        return settle (st, y);
    }
};
} // namespace bidoo::dsp::voicefilters

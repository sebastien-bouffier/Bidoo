#pragma once

// Zero-delay first-order ADAA at the base rate, from FadaBox (0.54.2, its LIMITER's and TRANSIENT
// SHAPER's CLIP), for the clippers and saturators that run at 1x and must not add any delay: a
// limiter's or a transient shaper's ceiling, a clip in a path that has to stay in phase with a dry
// one, a drive whose plugin reports no latency.
//
// Plain first-order ADAA (shaping/Adaa.h's adaa<1>, AdaaMeanStep.h) averages the curve between the
// last two inputs: half a sample late, and dull at the top (12 dB down at 20 kHz at 48 kHz). This one
// averages it over the CURRENT sample's own one-sample window: from a, the signal half a sample back
// (the parabola through the last three inputs, a = (3 x0 + 6 x1 - x2) / 8), to as far ahead along the
// same line, b = 2 x0 - a; the output is the curve's mean over [a, b], (F1 (b) - F1 (a)) / (b - a).
// Past samples only, so:
//   - no delay: a straight line (a ramp, a slow signal) comes through as it is (its window is centred
//     on x0), so a 5 kHz tone's fundamental moves by less than 0.01 sample even driven hard;
//   - little treble change: the harmonics of a hard-driven 5 kHz tone move by 1.4 dB at most (plain
//     ADAA 1.8 dB, and half a sample late); white noise through a tanh driven +20 dB keeps its 12-20 kHz
//     band within 1.7 dB of the plain curve's (plain ADAA: 9 dB more, mostly aliases);
//   - the output stays within the curve's range (a mean of the curve), so a ceiling stays a ceiling.
// What would alias sees the one-sample box: on tones of 3 / 5 / 9 kHz through a hard-driven tanh or a
// hard clip the worst alias is 8 / 9 to 10 / 11 to 14 dB lower than the plain curve's (tests: tanh at
// gain 5.6, -50.8 -> -59.1, -32.2 -> -41.7, -22.8 -> -33.8 dB; FadaBox's TRANSIENT CLIP the same), about
// 1 / 2 / 9 dB less clean than plain ADAA at moderate drive (gain 5.6), within 1 dB at hard drive (16), which
// pays for its extra cleanliness with half a sample of delay; on a limiter's soft knee
// (FadaBox's CLIP 127, steady gain) 8 / 7 / 13 dB lower than the bare knee.
//
// Cost: two antiderivative evaluations per sample (F1 (a) and F1 (b): nothing carries over, the window
// moves by more than one sample's step), so about twice plain ADAA's (bench, stereo: 11.2 ns per frame
// with AdaaTanhTable<64> against 5.4 for adaa<1>; 4.8 ns with AdaaHardClip), plus the 3-sample history.
//
// Limits:
//   - first order only, and only what folds back from the one-sample box: for 20 dB or more, oversample
//     (oversampling/IirAdaaDrive.h, AdaaZeroDelay.h) and accept their latency;
//   - the window is extrapolated: on a step or a sharp transient it overshoots for a sample (the output
//     still stays within the curve's range, a tanh clip's peak does not rise);
//   - where the window is narrower than eps (the signal nearly still), the difference quotient would
//     cancel its digits: the output is the plain curve at x0 instead (exact for a still signal). Pick
//     eps from the antiderivative's error: about 2e-3 for a table F1 within 1e-9 (AdaaTanhTable<64>),
//     1e-5 for a closed-form double F1 (AdaaTanh, AdaaHardClip); scale it with the width of a narrow
//     knee (FadaBox's limiter: 4e-3 x the knee, in units of its ceiling).
//
// Use:
//   AdaaCentredT<T> state;                        // one per channel; T = double (FadaBox, bit for bit)
//   y = state.step (x, AdaaTanhTable<64> {}, 2e-3); // a curve type with f and F1 (static or not)
//   y = state.stepWith (x, eps, f, F1);             // or callables: f (T x) -> float, F1 (T u) -> double
// or, when the curve applies only sometimes (a CLIP amount that can be 0) or the fallback differs:
//   state.window (x, a, b);                       // EVERY sample, even with the clip off, so the
//                                                 // history is right when it comes on
//   if (clipOn) y = AdaaCentred::mean (a, b, eps, F1, [&] { return f (x); });
//   wide (a, b, eps) and quotient (a, b, F1) split mean () for a caller that scales the quotient.
// The history is the last two INPUTS of the curve (not outputs): it stays valid when the curve or its
// parameters change (nothing of F1 is kept). Clear it with reset () when the signal restarts (a voice,
// transport start after silence), or startAt (x) to start from a constant x without a step from 0.

#include "../core/Inline.h"

#include <cmath>

namespace bidoo::dsp
{
template <typename T = double>
struct AdaaCentredT
{
    T x1 = 0, x2 = 0; // the last two inputs

    void reset() noexcept { x1 = x2 = 0; }
    // as if the input had been x0 for ever
    void startAt (T x0) noexcept { x1 = x2 = x0; }

    // The current sample's window [a, b] (b - a is the signal's step at x, a <= b on a rising signal);
    // moves the history
    BIDOO_DSP_INLINE void window (T x, T& a, T& b) noexcept
    {
        a = ((T) 3 * x + (T) 6 * x1 - x2) * (T) 0.125;
        b = (T) 2 * x - a;
        x2 = x1;
        x1 = x;
    }

    // whether [a, b] is wide enough for the difference quotient
    static BIDOO_DSP_INLINE bool wide (T a, T b, T eps) noexcept { return std::abs (b - a) > eps; }

    // the curve's mean over [a, b] from its antiderivative (only where wide (a, b, eps))
    template <typename Anti>
    static BIDOO_DSP_INLINE auto quotient (T a, T b, Anti&& anti) noexcept
    {
        return (anti (b) - anti (a)) / (b - a);
    }

    // the curve's mean over [a, b], or plain () (the curve at the current input) where it is too narrow
    template <typename Anti, typename Plain>
    static BIDOO_DSP_INLINE float mean (T a, T b, T eps, Anti&& anti, Plain&& plain) noexcept
    {
        const T d = b - a;
        return std::abs (d) > eps ? (float) ((anti (b) - anti (a)) / d) : (float) plain();
    }

    // One sample through f / F1 given as callables: f (x) the curve, anti (u) its antiderivative
    template <typename Fn, typename Anti>
    BIDOO_DSP_INLINE float stepWith (T x, T eps, Fn&& f, Anti&& anti) noexcept
    {
        T a, b;
        window (x, a, b);
        return mean (a, b, eps, anti, [&] { return f (x); });
    }

    // One sample through a curve type with members f and F1 (AdaaTanh, AdaaHardClip, AdaaTanhTable<N>,
    // the curves of AdaaCurves.h and AdaaDriveCurves.h...)
    template <typename Curve>
    BIDOO_DSP_INLINE float step (T x, const Curve& curve, T eps) noexcept
    {
        return stepWith (x, eps, [&] (T v) { return curve.f (v); }, [&] (T u) { return curve.F1 (u); });
    }
};
using AdaaCentred = AdaaCentredT<double>;
using AdaaCentredF = AdaaCentredT<float>;
} // namespace bidoo::dsp

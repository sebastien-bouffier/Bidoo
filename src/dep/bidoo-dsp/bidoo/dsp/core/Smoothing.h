#pragma once

// Parameter smoothing.
//  - LinearRamp / MultiplicativeRamp: drop-in replacements for juce::SmoothedValue<T, Linear> and
//    <T, Multiplicative>. Same member names and the same arithmetic, sample for sample, so a
//    plugin switching over sounds identical.
//  - OnePoleSmoother: exponential approach (time constant in ms), for values that keep moving.
//  - BlockRamp: a linear ramp across one control block that ends on its accumulated value
//    (from Crame), with cur and step public for hand-unrolled loops.

#include "Inline.h"
#include <cmath>

namespace bidoo::dsp
{
namespace detail
{
template <typename T, bool multiplicative>
class Ramp
{
public:
    Ramp() noexcept = default;
    explicit Ramp (T initial) noexcept : current (initial), target (initial) {}

    // Ramp length in seconds at this sample rate; jumps to the target.
    void reset (double sampleRate, double rampLengthSeconds) noexcept
    {
        reset ((int) std::floor (rampLengthSeconds * sampleRate));
    }

    // Ramp length in samples; jumps to the target.
    void reset (int numSteps) noexcept
    {
        stepsToTarget = numSteps;
        setCurrentAndTargetValue (target);
    }

    void setCurrentAndTargetValue (T v) noexcept
    {
        target = current = v;
        countdown = 0;
    }

    void setTargetValue (T v) noexcept
    {
        if (v == target)
            return;
        if (stepsToTarget <= 0)
        {
            setCurrentAndTargetValue (v);
            return;
        }
        target = v;
        countdown = stepsToTarget;
        setStepSize();
    }

    BIDOO_DSP_INLINE T getNextValue() noexcept
    {
        if (! isSmoothing())
            return target;
        --countdown;
        if (isSmoothing())
        {
            if constexpr (multiplicative)
                current *= step;
            else
                current += step;
        }
        else
            current = target;
        return current;
    }

    T skip (int numSamples) noexcept
    {
        if (numSamples >= countdown)
        {
            setCurrentAndTargetValue (target);
            return target;
        }
        if constexpr (multiplicative)
            current *= (T) std::pow (step, (T) numSamples);
        else
            current += step * (T) numSamples;
        countdown -= numSamples;
        return current;
    }

    // Multiplies `samples` by the ramp (as SmoothedValue::applyGain on one channel).
    void applyGain (T* samples, int numSamples) noexcept
    {
        if (isSmoothing())
            for (int i = 0; i < numSamples; ++i)
                samples[i] *= getNextValue();
        else
            for (int i = 0; i < numSamples; ++i)
                samples[i] *= target;
    }

    bool isSmoothing() const noexcept { return countdown > 0; }
    T getCurrentValue() const noexcept { return current; }
    T getTargetValue() const noexcept { return target; }

private:
    void setStepSize() noexcept
    {
        if constexpr (multiplicative)
            step = (T) std::exp ((std::log (std::abs (target)) - std::log (std::abs (current))) / (T) countdown);
        else
            step = (target - current) / (T) countdown;
    }

    T current {}, target {}, step {};
    int countdown = 0, stepsToTarget = 0;
};
} // namespace detail

template <typename T>
using LinearRamp = detail::Ramp<T, false>;

// For gains and frequencies: equal ratios per sample. Never ramp through or to 0.
template <typename T>
using MultiplicativeRamp = detail::Ramp<T, true>;

template <typename T>
class OnePoleSmoother
{
public:
    // Time constant in ms (63 % of the way after that time).
    void prepare (double sampleRate, double timeMs) noexcept
    {
        coeff = timeMs <= 0.0 ? (T) 0 : (T) std::exp (-1.0 / (0.001 * timeMs * sampleRate));
    }

    void setCurrentAndTarget (T v) noexcept { value = targetValue = v; }
    void setTarget (T v) noexcept { targetValue = v; }

    BIDOO_DSP_INLINE T next() noexcept
    {
        value = targetValue + coeff * (value - targetValue);
        return value;
    }

    // Moves toward `target` and returns the new value.
    BIDOO_DSP_INLINE T next (T target) noexcept
    {
        targetValue = target;
        return next();
    }

    T getCurrentValue() const noexcept { return value; }
    T getTargetValue() const noexcept { return targetValue; }

    // The value alone (the target is left as it is), for peak followers that write their own state.
    void setCurrentValue (T v) noexcept { value = v; }
    // The pole: value moves to target + coefficient * (value - target) each step. 0 = no smoothing.
    T getCoefficient() const noexcept { return coeff; }
    void setCoefficient (T c) noexcept { coeff = c; }

private:
    T coeff {}, value {}, targetValue {};
};

// The other one-pole form, z += a (x - z) with a = 1 - exp (-1 / (tau fs)): the arithmetic of the
// plugins' inline smoothers and followers (Mangle Smooth, Mistral FX, Grotte, Calanque, Potion,
// Xpresso GainSmoother, Chourmo OutputStage Smoother...). OnePoleSmoother computes
// target + c (value - target) instead, which is the same filter but rounds differently, so those
// smoothers could not merge without a sound change; this one can, bit for bit.
//   T     the state and coefficient type (float or double).
//   ExpT  the type the coefficient is computed in (a float exp and a double exp round differently).
// snapEps > 0 lands exactly on the target once |x - z| < snapEps (Chourmo: "off" really is silent).
// A plugin that computes its coefficient another way passes it to setCoefficient.
template <typename T, typename ExpT = T>
class IncrementalSmoother
{
public:
    // a = 1 - exp (-1 / (tau fs)), tau in seconds, every step in ExpT. tau <= 0: no smoothing (a = 1).
    static T coefficientFor (double sampleRate, double tauSeconds) noexcept
    {
        if (! (tauSeconds > 0.0))
            return (T) 1;
        return (T) ((ExpT) 1 - std::exp ((ExpT) -1 / ((ExpT) tauSeconds * (ExpT) sampleRate)));
    }

    // The same with tau already in samples (tau fs taken by the caller, in double), as Mangle's
    // 1 - exp (-1.0f / (float) (0.015 * sr)): the product rounded once in double, then the rest in
    // ExpT, which is not what coefficientFor gives with a float ExpT. samples <= 0: a = 1.
    static T coefficientForSamples (double tauSamples) noexcept
    {
        if (! (tauSamples > 0.0))
            return (T) 1;
        return (T) ((ExpT) 1 - std::exp ((ExpT) -1 / (ExpT) tauSamples));
    }

    // Time constant in ms, as OnePoleSmoother::prepare (63 % of the way after that time).
    void prepare (double sampleRate, double timeMs, T snap = (T) 0) noexcept
    {
        a = coefficientFor (sampleRate, 0.001 * timeMs);
        snapEps = snap;
    }

    void setCoefficient (T c) noexcept { a = c; }
    T getCoefficient() const noexcept { return a; }
    void setSnap (T eps) noexcept { snapEps = eps; }

    void setCurrentValue (T v) noexcept { z = v; }
    T getCurrentValue() const noexcept { return z; }

    BIDOO_DSP_INLINE T next (T x) noexcept
    {
        z += a * (x - z);
        if (snapEps > (T) 0 && std::abs (x - z) < snapEps)
            z = x;
        return z;
    }

private:
    T a = (T) 1, z {}, snapEps {};
};

// A linear ramp from the last target to a new one across one control block (from Crame's Ramp).
// Unlike LinearRamp, it does not snap to the target on the block's last sample: it ends on the
// accumulated sum cur + n * step, and the target is only taken up at the next setTarget, so
// moving a plugin's per-block ramp here keeps its renders bit for bit. cur and step are public
// so oversampled stages can walk the ramp in their own unrolled loops (and write cur = target
// when they skip a block).
struct BlockRamp
{
    float cur = 0.0f, step = 0.0f, target = 0.0f;

    void reset (float v) noexcept
    {
        cur = target = v;
        step = 0.0f;
    }

    // Starts the next block at the previous target and spreads the move over n samples.
    void setTarget (float t, int n) noexcept
    {
        cur = target;
        target = t;
        step = (t - cur) / (float) (n > 1 ? n : 1);
    }

    BIDOO_DSP_INLINE float next() noexcept { return cur += step; }
};
} // namespace bidoo::dsp

#pragma once

// TptSvf: the topology-preserving state variable filter (Zavalishin; Simper's "Cytomic" form),
// linear, stable under per-sample modulation. One step gives low, band and high pass at once;
// notch, peak, all-pass and the unity-gain band pass are combinations of them.
//
// Coefficients are given directly (g = tan (pi fc / fs), k = 1 / Q = 2R), so each plugin keeps
// its own parameter mapping and frequency limits and stays bit-identical when switching to it:
// compute g and k the way the plugin always did (in float or double) and call setCoefficients.
// set (cutoffHz, q, sampleRate) is the plain mapping, g computed in double.
//
// TptSvfState + TptSvfCoeffs are the same step with the coefficients kept apart from the state,
// for loops that compute them once for several filters or per sample (Chourmo's filter kernels).
// TptOnePole is the one-pole of the same family (G = g / (1 + g)).
//
// Nonlinear SVFs (Calanque, Mistral, Crame's saturating styles) are separate and stay in their
// plugins.

#include "../core/Inline.h"
#include "../core/Math.h"
#include <algorithm>
#include <cmath>

namespace bidoo::dsp
{
template <typename T>
struct TptSvfOut
{
    T lp, bp, hp;
};

// One step of the filter on the two integrator states ic1, ic2, with a1 = 1 / (1 + g (g + k)),
// a2 = g a1, a3 = g a2. Every TPT SVF here runs this arithmetic.
template <typename T>
BIDOO_DSP_INLINE TptSvfOut<T> tptSvfStep (T& ic1, T& ic2, T x, T a1, T a2, T a3, T k) noexcept
{
    const T v3 = x - ic2;
    const T v1 = a1 * ic1 + a2 * v3;
    const T v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = (T) 2 * v1 - ic1;
    ic2 = (T) 2 * v2 - ic2;
    return { v2, v1, x - k * v1 - v2 };
}

// The coefficients alone: make (g, k) gives what TptSvf::setCoefficients stores. The default
// (g = 0) holds the filter still.
template <typename T>
struct TptSvfCoeffs
{
    T a1 = (T) 1, a2 = (T) 0, a3 = (T) 0, k = (T) 2;

    static TptSvfCoeffs make (T g, T damping) noexcept
    {
        TptSvfCoeffs c;
        c.a1 = (T) 1 / ((T) 1 + g * (g + damping));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        c.k = damping;
        return c;
    }
};

// The state alone (two plain values, so voice states can be copied and compared bitwise), the
// coefficients given per call.
template <typename T>
struct TptSvfState
{
    T ic1 {}, ic2 {};

    BIDOO_DSP_INLINE TptSvfOut<T> tick (T x, T a1, T a2, T a3, T k) noexcept { return tptSvfStep (ic1, ic2, x, a1, a2, a3, k); }
    BIDOO_DSP_INLINE TptSvfOut<T> tick (T x, const TptSvfCoeffs<T>& c) noexcept { return tptSvfStep (ic1, ic2, x, c.a1, c.a2, c.a3, c.k); }

    void reset() noexcept { ic1 = ic2 = (T) 0; }
    // After a blow-up: back to silence when a state is not finite or beyond +-limit.
    void sanitize (T limit) noexcept
    {
        if (! (std::abs (ic1) <= limit && std::abs (ic2) <= limit))
            reset();
    }
};

template <typename T>
class TptSvf
{
public:
    using Out = TptSvfOut<T>;

    void setCoefficients (T newG, T newK) noexcept
    {
        g = newG;
        k = newK;
        a1 = (T) 1 / ((T) 1 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // Cutoff in Hz (clamped to 1 Hz .. 0.4999 fs), Q > 0 (0.7071 = Butterworth).
    void set (double cutoffHz, double q, double sampleRate) noexcept
    {
        const double f = clamp (cutoffHz, 1.0, 0.4999 * sampleRate);
        setCoefficients ((T) std::tan (kPi * f / sampleRate), (T) (1.0 / std::max (q, 1.0e-3)));
    }

    void reset() noexcept { ic1 = ic2 = (T) 0; }

    BIDOO_DSP_INLINE Out process (T x) noexcept { return tptSvfStep (ic1, ic2, x, a1, a2, a3, k); }

    BIDOO_DSP_INLINE T lowPass (T x) noexcept { return process (x).lp; }
    BIDOO_DSP_INLINE T highPass (T x) noexcept { return process (x).hp; }
    BIDOO_DSP_INLINE T bandPass (T x) noexcept { return process (x).bp; }
    // Band pass with unity gain at the centre frequency.
    BIDOO_DSP_INLINE T bandPassNormalised (T x) noexcept { return k * process (x).bp; }
    BIDOO_DSP_INLINE T notch (T x) noexcept
    {
        const auto o = process (x);
        return o.lp + o.hp;
    }
    BIDOO_DSP_INLINE T allPass (T x) noexcept { return x - (T) 2 * k * process (x).bp; }

    // After a blow-up (NaN, infinity or a runaway state), start again from silence.
    void sanitize() noexcept
    {
        if (! (std::abs (ic1) < (T) 1.0e6 && std::abs (ic2) < (T) 1.0e6))
            reset();
    }

    // The two integrator states, for loops that keep them in registers or carry them across blocks.
    void getState (T& s1, T& s2) const noexcept
    {
        s1 = ic1;
        s2 = ic2;
    }
    void setState (T s1, T s2) noexcept
    {
        ic1 = s1;
        ic2 = s2;
    }
    T getIc1() const noexcept { return ic1; }
    T getIc2() const noexcept { return ic2; }

    T getG() const noexcept { return g; }
    T getK() const noexcept { return k; }

private:
    T g {}, k = (T) 1.41421356, a1 {}, a2 {}, a3 {}, ic1 {}, ic2 {};
};

// The TPT one-pole (Zavalishin), G = g / (1 + g) with g = tan (pi fc / fs) given per call, so the
// cutoff can move every sample. low + high = x; allPass = 2 low - x (unity gain, 0 to -180 degrees).
template <typename T>
struct TptOnePole
{
    T s {};

    BIDOO_DSP_INLINE T lowPass (T x, T G) noexcept
    {
        const T v = (x - s) * G;
        const T y = v + s;
        s = y + v;
        return y;
    }
    BIDOO_DSP_INLINE T highPass (T x, T G) noexcept { return x - lowPass (x, G); }
    BIDOO_DSP_INLINE T allPass (T x, T G) noexcept { return (T) 2 * lowPass (x, G) - x; }

    void reset() noexcept { s = (T) 0; }
    // After a blow-up: back to silence when the state is not finite or beyond +-limit.
    void sanitize (T limit) noexcept
    {
        if (! (std::abs (s) <= limit))
            s = (T) 0;
    }
};

// A TPT filter's integrator gain g gliding linearly across a control block, the damping k held
// (from Crame). Per block: set (g, k, jump); per sample, when moving(), setCoefficients from
// at (t) with t = (i + 1) / n, else from end(); then settle(). The first set always jumps.
struct TptGainGlide
{
    double from = -1.0, to = -1.0, k = 1.0;

    void set (double g, double damping, bool jump)
    {
        from = jump || to < 0.0 ? g : to;
        to = g;
        k = damping;
    }
    void settle() { from = to; }
    bool moving() const { return std::abs (to - from) > 1.0e-12 * std::abs (to); }

    struct Gk
    {
        double g, k; // TptSvf::setCoefficients's arguments
    };
    inline Gk at (double t) const { return { from + t * (to - from), k }; }
    Gk end() const { return { to, k }; }
};
} // namespace bidoo::dsp

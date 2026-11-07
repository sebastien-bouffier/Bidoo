#pragma once

// Filters shared by the suite:
//  - Biquad: transposed direct form II, double precision, with coefficient designs for every
//    usual shape (bell, shelves, cuts, band pass, notch, all pass, tilt) and their magnitude
//    response for drawing curves.
//  - Svf: the topology-preserving state variable filter (trapezoidal integration), stable under
//    fast modulation, for synth and filter plugins.
//  - OnePole: a one-pole low / high pass.

#include <cmath>
#include <complex>
#include <algorithm>
#include "../core/Math.h"
#include "TptSvf.h"

namespace bidoo::dsp
{
// kPi comes from core/Math.h

struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; // a0 normalised to 1

    // |H(e^jw)| at frequency f.
    double magnitude (double f, double sampleRate) const
    {
        const double w = 2.0 * kPi * f / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        return std::abs (num / den);
    }

    double magnitudeDb (double f, double sampleRate) const
    {
        return 20.0 * std::log10 (std::max (1.0e-12, magnitude (f, sampleRate)));
    }

    static BiquadCoeffs identity() { return {}; }
};

// RBJ cookbook designs, with the bell and shelves' gain in dB and Q as the bandwidth control.
// Frequencies are clamped below Nyquist.
namespace design
{
inline double clampFreq (double f, double sr) { return std::clamp (f, 1.0, sr * 0.4999); }

inline BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
{
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

inline BiquadCoeffs lowPass (double f, double q, double sr)
{
    const double w = 2.0 * kPi * clampFreq (f, sr) / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
    return normalise ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + a, -2.0 * c, 1.0 - a);
}

inline BiquadCoeffs highPass (double f, double q, double sr)
{
    const double w = 2.0 * kPi * clampFreq (f, sr) / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
    return normalise ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + a, -2.0 * c, 1.0 - a);
}

// Constant 0 dB peak gain.
inline BiquadCoeffs bandPass (double f, double q, double sr)
{
    const double w = 2.0 * kPi * clampFreq (f, sr) / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
    return normalise (a, 0.0, -a, 1.0 + a, -2.0 * c, 1.0 - a);
}

inline BiquadCoeffs notch (double f, double q, double sr)
{
    const double w = 2.0 * kPi * clampFreq (f, sr) / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
    return normalise (1.0, -2.0 * c, 1.0, 1.0 + a, -2.0 * c, 1.0 - a);
}

inline BiquadCoeffs allPass (double f, double q, double sr)
{
    const double w = 2.0 * kPi * clampFreq (f, sr) / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
    return normalise (1.0 - a, -2.0 * c, 1.0 + a, 1.0 + a, -2.0 * c, 1.0 - a);
}

inline BiquadCoeffs bell (double f, double q, double gainDb, double sr)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * kPi * clampFreq (f, sr) / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
    return normalise (1.0 + a * A, -2.0 * c, 1.0 - a * A, 1.0 + a / A, -2.0 * c, 1.0 - a / A);
}

// q = 0.7071 is the steepest shelf without overshoot; higher values add a resonant bump.
inline BiquadCoeffs lowShelf (double f, double q, double gainDb, double sr)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * kPi * clampFreq (f, sr) / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
    const double sa = 2.0 * std::sqrt (A) * a;
    return normalise (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
                      (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
}

inline BiquadCoeffs highShelf (double f, double q, double gainDb, double sr)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * kPi * clampFreq (f, sr) / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
    const double sa = 2.0 * std::sqrt (A) * a;
    return normalise (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
                      (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
}

// A tilt around f: gainDb/2 above, -gainDb/2 below (a high shelf with its gain split).
inline BiquadCoeffs tilt (double f, double gainDb, double sr)
{
    auto c = highShelf (f, 0.5, gainDb, sr);
    const double g = std::pow (10.0, -gainDb / 40.0);
    c.b0 *= g;
    c.b1 *= g;
    c.b2 *= g;
    return c;
}

inline BiquadCoeffs onePoleLowPass (double f, double sr)
{
    const double k = std::tan (kPi * clampFreq (f, sr) / sr);
    return normalise (k, k, 0.0, k + 1.0, k - 1.0, 0.0);
}

inline BiquadCoeffs onePoleHighPass (double f, double sr)
{
    const double k = std::tan (kPi * clampFreq (f, sr) / sr);
    return normalise (1.0, -1.0, 0.0, k + 1.0, k - 1.0, 0.0);
}

// The Q of second-order section i (0 .. order/2 - 1) of an order-n Butterworth. Odd orders also
// have one first-order section.
inline double butterworthQ (int order, int section)
{
    if (order % 2 == 1)
        return 1.0 / (2.0 * std::cos (kPi * (section + 1.0) / order));
    return 1.0 / (2.0 * std::cos (kPi * (2.0 * section + 1.0) / (2.0 * order)));
}
} // namespace design

class Biquad
{
public:
    void setCoeffs (const BiquadCoeffs& c) { k = c; }
    const BiquadCoeffs& coeffs() const { return k; }
    void reset() { s1 = s2 = 0.0; }
    // Rung down to below `eps` (both state values).
    bool quiet (double eps) const { return std::abs (s1) < eps && std::abs (s2) < eps; }

    inline double process (double x)
    {
        const double y = k.b0 * x + s1;
        s1 = k.b1 * x - k.a1 * y + s2;
        s2 = k.b2 * x - k.a2 * y;
        return y;
    }

    inline float process (float x) { return (float) process ((double) x); }

    // One sample with coefficients given per call (a BiquadCoeffGlide's), the stored ones unused.
    inline double process (double x, const BiquadCoeffs& c)
    {
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }

private:
    BiquadCoeffs k;
    double s1 = 0.0, s2 = 0.0;
};

// Biquad coefficients that glide linearly, sample by sample, across a control block (from
// Crame: stepping them once per block made modulated tone controls zipper). Interpolating the
// direct-form coefficients keeps the poles inside the stability triangle, which is convex.
// Per block: glideTo (new) or jump (new), then per sample Biquad::process (x, at (t)) with
// t = (i + 1) / n (or `to` when not moving), then settle().
struct BiquadCoeffGlide
{
    BiquadCoeffs from, to;
    bool moving = false;

    void jump (const BiquadCoeffs& c)
    {
        from = to = c;
        moving = false;
    }
    void glideTo (const BiquadCoeffs& c)
    {
        from = to;
        to = c;
        moving = true;
    }
    // After a block: the target reached.
    void settle()
    {
        from = to;
        moving = false;
    }
    // The coefficients at fraction t (0..1] of the block.
    inline BiquadCoeffs at (double t) const
    {
        BiquadCoeffs c;
        c.b0 = from.b0 + t * (to.b0 - from.b0);
        c.b1 = from.b1 + t * (to.b1 - from.b1);
        c.b2 = from.b2 + t * (to.b2 - from.b2);
        c.a1 = from.a1 + t * (to.a1 - from.a1);
        c.a2 = from.a2 + t * (to.a2 - from.a2);
        return c;
    }
};

// Cascade of up to 8 biquads (16th order), for steep cuts (6 to 96 dB/oct).
class BiquadCascade
{
public:
    static constexpr int kMax = 8;

    // Butterworth-style low / high cut of `slopeDb` (6, 12, 18 ... 96 dB/oct); q sets the
    // resonance of the last section (0.7071 = none).
    void setCut (bool high, double f, int slopeDb, double q, double sr)
    {
        const int order = std::clamp (slopeDb / 6, 1, 2 * kMax);
        n = 0;
        if (order % 2 == 1)
            stage[n++].setCoeffs (high ? design::onePoleHighPass (f, sr) : design::onePoleLowPass (f, sr));
        const int pairs = order / 2;
        for (int i = 0; i < pairs; ++i)
        {
            double qi = design::butterworthQ (order, i);
            if (i == pairs - 1)
                qi *= q / 0.7071067811865476;
            stage[n++].setCoeffs (high ? design::highPass (f, qi, sr) : design::lowPass (f, qi, sr));
        }
    }

    void setSingle (const BiquadCoeffs& c)
    {
        n = 1;
        stage[0].setCoeffs (c);
    }

    void setStages (const BiquadCoeffs* c, int count)
    {
        n = std::clamp (count, 0, kMax);
        for (int i = 0; i < n; ++i)
            stage[i].setCoeffs (c[i]);
    }

    void reset()
    {
        for (auto& s : stage)
            s.reset();
    }

    inline double process (double x)
    {
        for (int i = 0; i < n; ++i)
            x = stage[i].process (x);
        return x;
    }

    double magnitude (double f, double sr) const
    {
        double m = 1.0;
        for (int i = 0; i < n; ++i)
            m *= stage[i].coeffs().magnitude (f, sr);
        return m;
    }

    int numStages() const { return n; }

private:
    Biquad stage[kMax];
    int n = 0;
};

// Topology-preserving state variable filter with Fab's resonance mapping (res 0..1, 1 = the
// self-oscillation edge), on TptSvf. One instance gives low, band and high pass outputs at once.
class Svf
{
public:
    using Out = TptSvf<float>::Out;

    void set (float cutoff, float res, float sampleRate)
    {
        const float f = std::clamp (cutoff, 5.0f, sampleRate * 0.49f);
        svf.setCoefficients (std::tan ((float) kPi * f / sampleRate), 2.0f - 1.98f * std::clamp (res, 0.0f, 1.0f));
    }

    void reset() { svf.reset(); }

    inline Out process (float x) { return svf.process (x); }

    float damping() const { return svf.getK(); }

private:
    TptSvf<float> svf;
};

class OnePole
{
public:
    void setLowPass (float f, float sr) { b = std::exp (-2.0f * (float) kPi * std::clamp (f, 1.0f, sr * 0.49f) / sr); }
    void reset (float v = 0.0f) { z = v; }
    inline float lowPass (float x) { return z = x + b * (z - x); }
    inline float highPass (float x) { return x - lowPass (x); }

private:
    float b = 0.0f, z = 0.0f;
};

// First-order DC blocker, y = x - x[n-1] + r y[n-1] in float (r = 1 - 2 pi fc / fs puts the corner
// at about fc). r is given per call so each plugin keeps its own coefficient bit for bit (Quichar,
// Patriarch, Potion, Retour's CharacterSvf). prime (v) starts from a level v, so a blocker engaged
// in the middle of a signal makes no step.
struct DcBlocker
{
    float x1 = 0.0f, y1 = 0.0f;

    BIDOO_DSP_INLINE float process (float x, float r) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void reset() noexcept { x1 = y1 = 0.0f; }
    void prime (float v) noexcept { x1 = y1 = v; }
};

// Linkwitz-Riley crossover (LR4: two cascaded 2nd-order Butterworths) splitting into low and high,
// whose sum is flat in magnitude (all-pass in phase).
class LinkwitzRiley
{
public:
    void set (double f, double sr)
    {
        const double q = 0.7071067811865476;
        for (int i = 0; i < 2; ++i)
        {
            lp[i].setCoeffs (design::lowPass (f, q, sr));
            hp[i].setCoeffs (design::highPass (f, q, sr));
        }
    }

    void reset()
    {
        for (int i = 0; i < 2; ++i)
        {
            lp[i].reset();
            hp[i].reset();
        }
    }

    inline void process (double x, double& low, double& high)
    {
        low = lp[1].process (lp[0].process (x));
        high = hp[1].process (hp[0].process (x));
    }

private:
    Biquad lp[2], hp[2];
};

// The all-pass with the same phase as an LR4 split at f, to keep other bands aligned.
class LinkwitzRileyAllPass
{
public:
    void set (double f, double sr)
    {
        // LR4 low + high = 2nd-order all-pass at f with Q = 0.5... use the exact sum instead.
        xo.set (f, sr);
    }

    void reset() { xo.reset(); }

    inline double process (double x)
    {
        double l, h;
        xo.process (x, l, h);
        return l + h;
    }

private:
    LinkwitzRiley xo;
};
} // namespace bidoo::dsp

#pragma once

// The diode ladder of the classic silver acid box (TB-303), as a dedicated model: four coupled stages, the first
// capacitor half the others, a resonance loop with its coupling capacitors, and the coupling capacitors around the
// ladder that make its bass. Written from Tim Stinchcombe's published analysis ("Diode ladder filters", 2009 update,
// timstinchcombe.co.uk, pages diode and diode2):
//
//   core:  1 / (s^4 + 2^(11/4) s^3 + 10 sqrt 2 s^2 + 2^(13/4) s + 1), s normalised to wc
//          poles -3.24, -2.33, -1.04, -0.13: one far below the others, hence the soft slope that is so often called
//          18 dB/oct while the true asymptote is 24
//   whole: 1.06 s^3 (s + 109.9) (s + 34) (s + 7.41)
//          / [core x (s + 97.5)(s + 38.5)(s + 4.45)(s + 578.1)(s + 20)(s + 7.41) + 18.7 k s^4 (s + 46.5)(s + 4.40)]
//          (s in rad/s, k the resonance 0..1)
//
// What this is structurally: dividing through by the six poles gives the two chains below, the first on the signal
// (`A`), the second in the feedback (`F`), and y = A x / (core + 18.7 k F). F goes to 1 well above 100 Hz, where the
// loop is the plain core, whose phase turns by 180 degrees at wc 2^(1/4) = sqrt 2 wa, with a gain of 1 / 17: 17 is the
// loop gain at which the core would oscillate. Under 100 Hz F takes the feedback away, so resonance thins everything
// above the coupling corner (by 1 + K) and leaves the deep bass alone, with the second low peak Stinchcombe describes.
//
// The small-signal core is the same thing as a passive RC ladder (s^4 + 8 s^3 + 20 s^2 + 16 s + 2 in wa = 1 / (2 a C), a
// = 2 Vt / I), with one end stage twice as fast: the halved capacitor. That is the structure solved here, one node per
// capacitor, each branch between two nodes a diode pair: current tanh (xi d) / xi for a difference d, as the differential
// equations of a diode ladder have it. Zero-delay form, trapezoidal integrators, the branches' conductances taken from
// the last solution (one linear solve per sample, Mystran's cheap method), bounded at any cutoff.
//
// Against Potion's "Diode 24" (filters/... in Potion's Filter.h): that one is a four-stage ladder with half-strength
// coupling, equal stages, a single one-pole high-pass and a resonance normalised so that it stays musical in a synth
// voice. This one has the published polynomial and poles (half first capacitor), the published loop threshold, all the
// published coupling poles in their places, and a diode pair in every branch; it is meant to do this one job.
//
// setFeedback's K is Stinchcombe's 18.7 k. What the top of the knob should be is the voice's choice (AcidVoice.h).

#include "../core/Inline.h"
#include <cmath>

namespace bidoo::dsp
{
class DiodeLadder
{
public:
    // The core's loop oscillates at this gain (ideal core: |D (j w)| where its phase is 180 degrees).
    static constexpr double kOscillationGain = 17.0;
    // Stinchcombe's factor on k.
    static constexpr double kPublishedGain = 18.7;
    // The resonance peak of the core sits at this factor of the ladder's wa (and 2^(1/4) of Stinchcombe's wc).
    static constexpr double kSqrt2 = 1.41421356237309504880;
    static constexpr double kWcToPeak = 1.18920711500272106672;

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        const double c = 2.0 * sampleRate;
        auto set = [c] (Section& q, double zero, double pole)
        {
            q.b0 = (c + zero) / (c + pole);
            q.b1 = (zero - c) / (c + pole);
            q.a1 = (pole - c) / (c + pole);
        };
        set (pre[0], 109.9, 578.1);
        set (pre[1], 34.0, 97.5);
        set (post[0], 0.0, 38.5);
        set (post[1], 0.0, 20.0);
        set (post[2], 0.0, 4.45);
        set (fb[0], 0.0, 4.45);
        set (fb[1], 4.40, 7.41);
        set (fb[2], 0.0, 20.0);
        set (fb[3], 0.0, 38.5);
        set (fb[4], 46.5, 97.5);
        set (fb[5], 0.0, 578.1);
        fbGain = 1.0;
        for (const auto& q : fb)
            fbGain *= q.b0;
        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < 4; ++i)
            s[i] = v[i] = 0.0;
        uLast = 0.0;
        for (auto& q : pre)
            q.z = 0.0;
        for (auto& q : post)
            q.z = 0.0;
        for (auto& q : fb)
            q.z = 0.0;
    }

    // xi: how fast the diodes bend (1 / level of the tanh's knee, in the input's units); 0 is linear. asym: an offset in
    // the first branch's operating point (its transistors are not matched like a diode pair): even harmonics, and the
    // loop's asymmetry at high resonance.
    void setNonlinearity (double xi, double asym) noexcept
    {
        bend = xi;
        offset = asym;
    }

    // The integrators' gain that puts the resonance peak on hz, exactly at any rate (the bilinear warp applied at the
    // peak). Control rate: it has a tan in it.
    static double gainForResonance (double hz, double sampleRate) noexcept
    {
        return std::tan (3.14159265358979323846 * hz / sampleRate) / kSqrt2;
    }

    // One sample. g from gainForResonance (glide it across a control block), K the resonance loop's gain.
    BIDOO_DSP_INLINE double process (double x, double g, double K) noexcept
    {
        // the branch conductances, from the last solution
        const double r1 = conductance (bend * (uLast - v[0]) + offset);
        const double r2 = conductance (bend * (v[0] - v[1]));
        const double r3 = conductance (bend * (v[1] - v[2]));
        const double r4 = conductance (bend * (v[2] - v[3]));

        // tridiagonal elimination from the last node (the small capacitor: its branch counts twice)
        const double a4 = 2.0 * g * r4;
        const double den4 = 1.0 / (1.0 + a4);
        const double al4 = a4 * den4, be4 = s[3] * den4;
        const double g3 = g * r3, g4 = g * r4, g2 = g * r2, g1 = g * r1;
        const double den3 = 1.0 / (1.0 + g3 + g4 * (1.0 - al4));
        const double al3 = g3 * den3, be3 = (s[2] + g4 * be4) * den3;
        const double den2 = 1.0 / (1.0 + g2 + g3 * (1.0 - al3));
        const double al2 = g2 * den2, be2 = (s[1] + g3 * be3) * den2;
        const double den1 = 1.0 / (1.0 + g1 + g2 * (1.0 - al2));
        const double al1 = g1 * den1, be1 = (s[0] + g2 * be2) * den1;

        // what the feedback chain says with no new input
        double fs0 = 0.0;
        for (const auto& q : fb)
            fs0 = q.b0 * fs0 + q.z;

        // y = gamma u + delta, u = xIn - K (fbGain y + fs0): the delay-free loop
        double xIn = x;
        for (auto& q : pre)
            xIn = q.step (xIn);
        const double gamma = al4 * al3 * al2 * al1;
        const double delta = be4 + al4 * (be3 + al3 * (be2 + al2 * be1));
        const double y = (gamma * (xIn - K * fs0) + delta) / (1.0 + gamma * K * fbGain);

        double f = y;
        for (auto& q : fb)
            f = q.step (f);
        const double u = xIn - K * f;

        v[0] = al1 * u + be1;
        v[1] = al2 * v[0] + be2;
        v[2] = al3 * v[1] + be3;
        v[3] = y;
        for (int i = 0; i < 4; ++i)
            s[i] = 2.0 * v[i] - s[i];
        uLast = u;

        double out = 1.06 * y;
        for (auto& q : post)
            out = q.step (out);
        // A NaN or an infinity (in the input, or in g / K) would stay in every node: the ladder goes
        // back to silence by itself and plays on from there, as a reset one. Every state feeds the
        // output, so checking it is enough; finite audio is untouched.
        if (! std::isfinite (out))
            return resetToSilence();
        return out;
    }

    // tanh (x) / x; 7/6 Pade (as shaping/TanhPade.h) with the x's taken out, 1 / |x| past where it meets 1.
    static BIDOO_DSP_INLINE double conductance (double x) noexcept
    {
        const double x2 = x * x;
        if (x2 >= 24.7)
            return 1.0 / std::sqrt (x2);
        return (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2))) / (135135.0 + x2 * (62370.0 + x2 * (3150.0 + 28.0 * x2)));
    }

    // Always true since process() recovers by itself; kept for the callers that still check.
    bool isFinite() const noexcept { return std::isfinite (v[3]) && std::isfinite (s[0]) && std::isfinite (post[2].z); }

private:
    BIDOO_DSP_NOINLINE double resetToSilence() noexcept
    {
        reset();
        return 0.0;
    }

    // first order, y = b0 x + z, z' = b1 x - a1 y; double, the corners are thousands of times below the rate
    struct Section
    {
        double b0 = 1.0, b1 = 0.0, a1 = 0.0, z = 0.0;
        BIDOO_DSP_INLINE double step (double x) noexcept
        {
            const double y = b0 * x + z;
            z = b1 * x - a1 * y;
            return y;
        }
    };

    double fs = 48000.0;
    double s[4] = {}, v[4] = {};
    double uLast = 0.0;
    double bend = 1.0, offset = 0.0, fbGain = 1.0;
    Section pre[2], post[3], fb[6];
};
} // namespace bidoo::dsp

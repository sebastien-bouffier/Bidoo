#pragma once

// Fdn16: the feedback delay network of Grotte's reverb, as a reusable core. Sixteen delay lines
// (two blocks of eight joined by a rotation that sets the channel cross-feed), each with
//  - a gliding all-pass inside the loop (echo density grows with every pass),
//  - a 6-point Lagrange read (no top-end flutter under modulation), its length modulated by a
//    phasor LFO per line,
//  - ten fitted absorption stages (AttenuationFit, double precision, one stage across the 16
//    lines at a time so the compiler vectorises the lanes) so that the loop loses exactly
//    -60 dB * loopLength / RT60 (f) per pass and the tail follows a DecayModel,
// then a slowly turning orthogonal rotation of each pair of lines (the modes keep moving without
// changing the loss) and a Hadamard mixing matrix in two blocks of eight. Two inputs feed the two
// blocks; two outputs (the sum of each block) come out.
//
// The owner (a plugin's reverb) decides the room: per sub-block it sets the line length targets
// (setLengthTargets), all-pass lengths, LFO rates, rotation, and calls designAbsorption () when the
// decay or the lengths moved. See FdnReverb.h for a ready stereo reverb on top, and fab's Grotte
// for the full product (early reflections, diffusion, post EQ, ducking, gate).
//
// Per-sample cost is fixed by the structure; the filter design, the only expensive part, runs per
// sub-block and only while something moves.

#include "AttenuationFit.h"
#include "../core/Inline.h"
#include "GlideAllpass.h"
#include "../delay/GuardDelayLine.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace bidoo::dsp::reverb
{
class Fdn16
{
public:
    static constexpr int kLines = 16;
    static constexpr int kPoints = AttenuationFit::kPoints;
    static constexpr int kStages = AttenuationFit::kStages;

    // Allocates. The lines hold up to maxLineMs, the loop all-passes up to maxAllpassMs.
    void prepare (double sampleRate, double maxLineMs = 175.0, double maxAllpassMs = 12.0)
    {
        sr = sampleRate;
        fit.prepare (sr);
        const float msToS = (float) (sr * 0.001);
        for (auto& l : lines)
            l.prepare ((int) ((float) maxLineMs * msToS));
        for (auto& a : loopAp)
            a.prepare ((int) ((float) maxAllpassMs * msToS));
        double comp[kPoints];
        for (int k = 0; k < kPoints; ++k)
        {
            // under-compensate, and saturate towards Nyquist where the loss at t = 0 is nil: a smooth,
            // rising curve the high shelf can follow without ripple
            const double loss = -fit.interpLossAt (k);
            comp[k] = 0.85 * loss / (1.0 + loss / 0.15);
        }
        fit.fit (comp, interpGains.data(), false);
        // never give back more than the interpolation takes on average
        double resp[kPoints], excess = 0.0;
        fit.response (interpGains.data(), resp);
        for (int k = 0; k < kPoints; ++k)
            excess = std::max (excess, resp[k] + fit.interpLossAt (k));
        interpGains[0] -= excess;
        reset();
    }

    void reset()
    {
        for (auto& l : lines)
            l.reset();
        for (auto& a : loopAp)
            a.reset();
        for (auto& st : absorb)
            st.reset();
        for (int p = 0; p < kLines / 2; ++p)
            rotPhase[(size_t) p] = 2.0 * kPi * (0.37 + 0.381966 * p);
        for (int j = 0; j < kLines; ++j)
        {
            const double ph = 2.0 * kPi * (0.13 + 0.618034 * j);
            lfoC[(size_t) j] = (float) std::cos (ph);
            lfoS[(size_t) j] = (float) std::sin (ph);
        }
    }

    // ---- the room, set per sub-block ----
    void setLengthTargets (const float* samples)
    {
        for (int j = 0; j < kLines; ++j)
            tgtLen[(size_t) j] = samples[j];
    }
    void setLoopAllpass (int j, float samples)
    {
        tgtAp[(size_t) j] = samples;
        loopAp[(size_t) j].setTarget (samples);
    }
    void setLoopGain (float g) { loopG = g; }
    // The LFO of line j turns at hz (its phasor kept on the unit circle).
    void setLfoRate (int j, double hz)
    {
        const double w = 2.0 * kPi * hz / sr;
        lfoRc[(size_t) j] = (float) std::cos (w);
        lfoRs[(size_t) j] = (float) std::sin (w);
        renormaliseLfo (j);
    }
    // The phasors put back on the unit circle, their rates kept: what setLfoRate does besides the
    // rate, for a caller whose rates didn't change (the same numbers, without a cos and a sin per
    // line).
    void renormaliseLfos()
    {
        for (int j = 0; j < kLines; ++j)
            renormaliseLfo (j);
    }
    void renormaliseLfo (int j)
    {
        const float m = 1.0f / std::sqrt (lfoC[(size_t) j] * lfoC[(size_t) j] + lfoS[(size_t) j] * lfoS[(size_t) j]);
        lfoC[(size_t) j] *= m;
        lfoS[(size_t) j] *= m;
    }
    // The pair rotation: a slow sine per pair of lines (depth in radians, rates scaled by rateScale),
    // advanced by `len` samples and interpolated per sample over the next sub-block.
    void updateRotation (double depth, double rateScale, int len, bool first)
    {
        rotDepth = (float) depth;
        for (int p = 0; p < kLines / 2; ++p)
        {
            const double hz = (0.11 + 0.047 * ((p * 5) % 8)) * rateScale;
            rotPhase[(size_t) p] = std::fmod (rotPhase[(size_t) p] + 2.0 * kPi * hz * len / sr, 2.0 * kPi);
            const double th = (double) rotDepth * std::sin (rotPhase[(size_t) p]);
            rotC0[(size_t) p] = first ? (float) std::cos (th) : rotC1[(size_t) p];
            rotS0[(size_t) p] = first ? (float) std::sin (th) : rotS1[(size_t) p];
            rotC1[(size_t) p] = (float) std::cos (th);
            rotS1[(size_t) p] = (float) std::sin (th);
        }
    }
    // Jump to the targets (the first block, or after a reset).
    void snap()
    {
        for (int j = 0; j < kLines; ++j)
            curLen[(size_t) j] = tgtLen[(size_t) j];
        for (auto& ap : loopAp)
            ap.snap();
    }
    // True when a line length has moved more than half a sample since the absorption was designed.
    bool lengthsMoved() const
    {
        for (int j = 0; j < kLines; ++j)
            if (std::abs (curLen[(size_t) j] - lastFitLen[(size_t) j]) > 0.5f)
                return true;
        return false;
    }

    // Designs the absorption stages for the decay curve logT (log of the RT60 in seconds at each
    // fit point, AttenuationFit::freqAt (k)); freeze 0..1 fades towards a 5000 s tail;
    // modDepthSamples is the length modulation depth (the loops' mean length includes it).
    void designAbsorption (const double* logT, double freeze, float modDepthSamples)
    {
        // reference: the mean loop length (delay + in-loop all-pass)
        double ref = 0.0;
        for (int j = 0; j < kLines; ++j)
            ref += curLen[(size_t) j] + modDepthSamples + tgtAp[(size_t) j];
        ref /= kLines;

        double target[kPoints], gains[AttenuationFit::kFilters];
        const double fz = freeze;
        for (int k = 0; k < kPoints; ++k)
        {
            const double t = std::exp (logT[k]);
            const double live = -60.0 * ref / (sr * t), frozen = -60.0 * ref / (sr * 5000.0);
            target[k] = (1.0 - fz) * live + fz * frozen;
        }
        fit.fit (target, gains);

        for (int j = 0; j < kLines; ++j)
        {
            const double r = (curLen[(size_t) j] + modDepthSamples + tgtAp[(size_t) j]) / ref;
            lineGain[(size_t) j] = std::pow (10.0, (gains[0] * r + interpGains[0]) / 20.0);
            for (int st = 0; st < kStages; ++st)
            {
                const double g = gains[st + 1] * r + interpGains[(size_t) (st + 1)];
                absorb[(size_t) st].set (j, std::abs (g) < 1.0e-7 ? BiquadCoeffs::identity() : fit.designStage (st + 1, g));
            }
            lastFitLen[(size_t) j] = curLen[(size_t) j];
        }
    }

    // One sample. li0, li1: what goes into the two blocks; cosT, sinT: the cross-feed rotation
    // between the blocks; modDepth: length modulation (samples); glide: length smoothing per
    // sample; u: position in the sub-block (0..1] for the rotation interpolation. The two block
    // sums come out in tank0 and tank1.
    BIDOO_DSP_INLINE void tick (float li0, float li1, float cosT, float sinT, float modDepth, float glide, float u, float& tank0, float& tank1)
    {
        static const float sign[kLines] = { 1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1, 1, -1 };
        const float g8 = 0.35355339f; // 1 / sqrt (8)
        const float li[2] = { li0, li1 };

        // (in three passes: the lengths and the read weights of all the lines are independent
        // lanes the compiler vectorises; only the reads themselves go line by line)
        float y[kLines];
        double v[kLines];
        float frac[kLines], hw[6][kLines];
        int whole[kLines];
        for (int j = 0; j < kLines; ++j)
        {
            curLen[(size_t) j] += glide * (tgtLen[(size_t) j] - curLen[(size_t) j]);
            // advance the LFO phasor
            const float c0 = lfoC[(size_t) j], s0 = lfoS[(size_t) j];
            lfoC[(size_t) j] = c0 * lfoRc[(size_t) j] - s0 * lfoRs[(size_t) j];
            lfoS[(size_t) j] = c0 * lfoRs[(size_t) j] + s0 * lfoRc[(size_t) j];
            const float d = std::max (6.0f, curLen[(size_t) j] + modDepth * (1.0f + s0) - 1.0f);
            whole[j] = (int) d;
            frac[j] = d - (float) whole[j];
        }
        for (int j = 0; j < kLines; ++j)
        {
            // GuardDelayLine::lagrange6, lane by lane
            const float t = frac[j];
            const float a = t + 2, b = t + 1, c = t, d = t - 1, e = t - 2, f = t - 3;
            hw[0][j] = -b * c * d * e * f / 120.0f;
            hw[1][j] = a * c * d * e * f / 24.0f;
            hw[2][j] = -a * b * d * e * f / 12.0f;
            hw[3][j] = a * b * c * e * f / 12.0f;
            hw[4][j] = -a * b * c * d * f / 24.0f;
            hw[5][j] = a * b * c * d * e / 120.0f;
        }
        for (int j = 0; j < kLines; ++j)
        {
            const float h[6] = { hw[0][j], hw[1][j], hw[2][j], hw[3][j], hw[4][j], hw[5][j] };
            v[j] = (double) loopAp[(size_t) j].process (lines[(size_t) j].read6 (whole[j], h), loopG, glide) * lineGain[(size_t) j];
        }
        // the absorption, stage by stage across the lines (independent lanes: vectorised)
        for (auto& st : absorb)
            st.process (v);
        for (int j = 0; j < kLines; ++j)
            y[j] = (float) v[j];

        // turn each pair of lines a little (orthogonal: the loop stays exactly as lossy)
        for (int p = 0; p < kLines / 2; ++p)
        {
            const float c = rotC0[(size_t) p] + u * (rotC1[(size_t) p] - rotC0[(size_t) p]);
            const float sn = rotS0[(size_t) p] + u * (rotS1[(size_t) p] - rotS0[(size_t) p]);
            const float y0 = y[2 * p], y1 = y[2 * p + 1];
            y[2 * p] = c * y0 - sn * y1;
            y[2 * p + 1] = sn * y0 + c * y1;
        }

        float tank[2] = { 0.0f, 0.0f };
        for (int j = 0; j < 8; ++j)
        {
            tank[0] += sign[j] * y[j];
            tank[1] += sign[j + 8] * y[j + 8];
        }
        tank[0] *= g8;
        tank[1] *= g8;

        float a[8], b[8];
        for (int j = 0; j < 8; ++j)
        {
            a[j] = y[j];
            b[j] = y[j + 8];
        }
        hadamard8 (a);
        hadamard8 (b);
        for (int j = 0; j < 8; ++j)
        {
            const float va = g8 * (cosT * a[j] + sinT * b[j]);
            const float vb = g8 * (sinT * a[j] - cosT * b[j]);
            const float ia = sign[(j * 3) % 16] * li[0] * g8, ib = sign[(j * 5 + 1) % 16] * li[1] * g8;
            // a last-resort guard: a passive loop never gets near this
            lines[(size_t) j].push (std::clamp (va + ia, -32.0f, 32.0f));
            lines[(size_t) (j + 8)].push (std::clamp (vb + ib, -32.0f, 32.0f));
        }
        tank0 = tank[0];
        tank1 = tank[1];
    }

    float lineLength (int j) const { return curLen[(size_t) j]; }
    const AttenuationFit& fitter() const { return fit; }
    double sampleRate() const { return sr; }

private:
    // (the three butterfly passes written out: the generic loop didn't unroll)
    static inline void hadamard8 (float* x)
    {
        auto bf = [x] (int p, int q)
        {
            const float u = x[p], v = x[q];
            x[p] = u + v;
            x[q] = u - v;
        };
        bf (0, 1); bf (2, 3); bf (4, 5); bf (6, 7);
        bf (0, 2); bf (1, 3); bf (4, 6); bf (5, 7);
        bf (0, 4); bf (1, 5); bf (2, 6); bf (3, 7);
    }

    // one absorption stage of all the lines, laid out lane by lane
    struct StageLanes
    {
        double b0[kLines], b1[kLines], b2[kLines], a1[kLines], a2[kLines], s1[kLines], s2[kLines];

        StageLanes()
        {
            for (int j = 0; j < kLines; ++j)
                set (j, BiquadCoeffs::identity());
            reset();
        }

        void set (int j, const BiquadCoeffs& c)
        {
            b0[j] = c.b0;
            b1[j] = c.b1;
            b2[j] = c.b2;
            a1[j] = c.a1;
            a2[j] = c.a2;
        }

        void reset()
        {
            for (int j = 0; j < kLines; ++j)
                s1[j] = s2[j] = 0.0;
        }

        // the same transposed direct form II as DBiquad, per lane
        inline void process (double* x)
        {
            for (int j = 0; j < kLines; ++j)
            {
                const double in = x[j];
                const double out = b0[j] * in + s1[j];
                s1[j] = b1[j] * in - a1[j] * out + s2[j];
                s2[j] = b2[j] * in - a2[j] * out;
                x[j] = out;
            }
        }
    };

    double sr = 48000.0;
    AttenuationFit fit;
    std::array<GuardDelayLine, kLines> lines;
    std::array<GlideAllpass, kLines> loopAp;
    std::array<StageLanes, kStages> absorb;
    std::array<float, kLines> tgtAp {};
    float loopG = 0.55f;
    std::array<double, kLines / 2> rotPhase {};
    std::array<float, kLines / 2> rotC0 {}, rotS0 {}, rotC1 {}, rotS1 {};
    float rotDepth = 0.0f;
    std::array<float, kLines> tgtLen {}, curLen {}, lastFitLen {};
    std::array<double, kLines> lineGain {};
    std::array<float, kLines> lfoC {}, lfoS {}, lfoRc {}, lfoRs {};
    std::array<double, AttenuationFit::kFilters> interpGains {};
};
} // namespace bidoo::dsp::reverb

#pragma once

// FdnEcoReverb: the economy version of FdnReverb, for plugins where a reverb must cost about what a
// Freeverb did. Same parameters (FdnReverbParams), same wet-only contract, same 32-sample parameter
// grid. Instead of the 16-line network with ten fitted double-precision absorption stages per line,
// it runs 8 float lines (4-point Hermite reads, slowly modulated) mixed by a Hadamard matrix, each
// with an in-loop all-pass for echo density and a loss made of one gain and one one-pole low-pass
// per line: the gain sets the reverb time of the lows (DecayModel at 150 Hz), the one-pole is
// solved so that the highs (6 kHz) decay in the DecayModel's time for them. So the reverb time per
// band follows the model at the two ends and approximately between; the stereo field comes from
// the input all-passes and two orthogonal output taps.
// Idle skip as in FdnReverb (IdleGate).

#include "DecayModel.h"
#include "FdnReverb.h"
#include "IdleGate.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bidoo::dsp::reverb
{
class FdnEcoReverb
{
public:
    static constexpr int kSubBlock = 32;
    static constexpr int kLines = 8;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& l : lines)
            l.prepare ((int) (0.12 * sr));
        for (auto& a : loopAp)
            a.prepare ((int) (0.006 * sr) + 2);
        for (auto& c : diff)
            for (auto& a : c)
                a.prepare ((int) (0.016 * sr) + 2);
        glide = 1.0f - (float) std::exp (-1.0 / (0.25 * sr));
        reset();
    }

    void reset()
    {
        for (auto& l : lines)
            l.clear();
        for (auto& a : loopAp)
            a.clear();
        for (auto& c : diff)
            for (auto& a : c)
                a.clear();
        lp.fill (0.0f);
        inLp[0] = inLp[1] = 0.0f;
        lfoC = 1.0f;
        lfoS = 0.0f;
        subPos = 0;
        first = true;
        modelValid = false;
        freezeAmt = 0.0f;
        idle.reset();
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const FdnReverbParams& p)
    {
        int done = 0;
        while (done < n)
        {
            if (subPos == 0)
                update (p, kSubBlock);
            const int len = std::min (kSubBlock - subPos, n - done);
            float inPeak = 0.0f;
            for (int i = 0; i < len; ++i)
                inPeak = std::max (inPeak, std::abs (inL[done + i]) + std::abs (inR[done + i]));
            const bool frozen = p.freeze || freezeAmt > 0.0f;
            if (idle.skip (inPeak, frozen))
            {
                std::fill (outL + done, outL + done + len, 0.0f);
                std::fill (outR + done, outR + done + len, 0.0f);
            }
            else
            {
                run (inL + done, inR + done, outL + done, outR + done, len);
                float outPeak = 0.0f;
                for (int i = 0; i < len; ++i)
                    outPeak = std::max (outPeak, std::max (std::abs (outL[done + i]), std::abs (outR[done + i])));
                idle.ran (len, inPeak, outPeak, frozen);
            }
            subPos = (subPos + len) % kSubBlock;
            done += len;
        }
    }

private:
    // A power-of-two ring read at a fractional delay by linear interpolation (the loss of the
    // interpolation is part of the damping: the lines are low-passed anyway).
    struct Ring
    {
        std::vector<float> buf;
        int mask = 0, w = 0;
        void prepare (int maxLen)
        {
            int size = 16;
            while (size < maxLen + 8)
                size <<= 1;
            buf.assign ((size_t) size, 0.0f);
            mask = size - 1;
            w = 0;
        }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
        // d >= 1 samples back from the newest
        inline float read (float d) const
        {
            const int i = (int) d;
            const float t = d - (float) i;
            const float a = buf[(size_t) ((w - i) & mask)], b = buf[(size_t) ((w - i - 1) & mask)];
            return a + t * (b - a);
        }
    };

    // (4-point Hermite for the two modulated lines: no top-end loss that varies with the fraction)
    static inline float hermite (const Ring& r, float d)
    {
        const int i = (int) d;
        const float f = d - (float) i;
        const float xm1 = r.buf[(size_t) ((r.w - (i - 1)) & r.mask)], x0 = r.buf[(size_t) ((r.w - i) & r.mask)],
                    x1 = r.buf[(size_t) ((r.w - i - 1) & r.mask)], x2 = r.buf[(size_t) ((r.w - i - 2) & r.mask)];
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    // Integer-delay Schroeder all-pass.
    struct Ap
    {
        std::vector<float> buf;
        int pos = 0, len = 1;
        void prepare (int maxLen) { buf.assign ((size_t) std::max (2, maxLen), 0.0f); pos = 0; len = 1; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        void setLength (int l) { len = std::clamp (l, 1, (int) buf.size() - 1); }
        inline float process (float x, float g)
        {
            int r = pos - len;
            if (r < 0)
                r += (int) buf.size();
            const float d = buf[(size_t) r];
            const float v = x - g * d;
            buf[(size_t) pos] = v;
            if (++pos >= (int) buf.size())
                pos = 0;
            return d + g * v;
        }
    };

    // |H|^2 in dB of y = (1 - d) x + d y at w
    static double onePoleDb (double d, double w)
    {
        return 10.0 * std::log10 ((1.0 - d) * (1.0 - d) / (1.0 - 2.0 * d * std::cos (w) + d * d));
    }

    void update (const FdnReverbParams& p, int len)
    {
        const double space = std::clamp (p.size, 0.0, 1.0);
        const double msToS = sr * 0.001;
        const double rt60 = std::clamp (p.rt60, 0.1, 60.0), bright = std::clamp (p.brightness, 0.0, 1.0);
        const double modAmt = std::clamp (p.modulation, 0.0, 1.0);

        // line lengths: geometric spread, a few ms to a few tens
        const double lmin = 8.0 * std::pow (2.2, space), ratio = 2.1 + 0.6 * (1.0 - space);
        for (int j = 0; j < kLines; ++j)
        {
            const int rank = (j < 4) ? 2 * j : 2 * (j - 4) + 1;
            const double u = ((double) rank + 0.4 * std::sin (1.7 * j + 0.3)) / (kLines - 0.5);
            tgtLen[(size_t) j] = (float) std::round (lmin * std::pow (ratio, std::clamp (u, 0.0, 1.0)) * msToS); // whole samples: a still line has no interpolation loss
        }
        static const double apMs[kLines] = { 0.71, 1.93, 1.13, 2.61, 0.89, 2.27, 1.49, 3.07 };
        for (int j = 0; j < kLines; ++j)
        {
            apLen[(size_t) j] = (float) std::max (2.0, std::round (apMs[j] * (0.6 + 0.9 * space) * msToS));
            loopAp[(size_t) j].setLength ((int) apLen[(size_t) j]);
        }
        static const double dMs[2][2] = { { 2.13, 5.93 }, { 2.41, 6.67 } };
        const double dScale = (0.6 + 1.5 * space) * std::clamp (0.08 * rt60 / 0.02, 0.25, 1.0);
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < 2; ++k)
                diff[(size_t) ch][(size_t) k].setLength ((int) std::max (2.0, std::round (dMs[ch][k] * dScale * msToS)));
        inCoef = (float) std::exp (-2.0 * 3.14159265358979323846 * std::min (17000.0, 0.45 * sr) / sr);

        // modulation (chorus in the lengths; the rotation of the big network is not here)
        const double c = modAmt * 0.5, cA = c / 0.5;
        modDepthT = (float) ((0.004 + 0.136 * cA * cA) * msToS);
        {
            const double w = 2.0 * 3.14159265358979323846 * 0.7 / sr; // one slow phasor for lines 1 and 4
            lfoRc = (float) std::cos (w);
            lfoRs = (float) std::sin (w);
        }

        // reverb time at the two ends
        if (! modelValid || rt60 != cachedTime || bright != cachedBrightness)
        {
            DecayModel m;
            m.baseTime = rt60;
            m.brightness = bright;
            cachedTime = rt60;
            cachedBrightness = bright;
            modelValid = true;
            tLow = m.timeAt (150.0);
            tHigh = m.timeAt (6000.0);
            t1k = m.timeAt (1000.0);
            coefDirty = true;
        }
        side = (float) std::clamp (p.width, 0.0, 1.0);
        lateGain = (float) (kTrim * std::pow (std::max (0.1, t1k), 0.0));

        const float aF = first ? 1.0f : 1.0f - (float) std::exp (-len / (0.08 * sr));
        const float freezeT = p.freeze ? 1.0f : 0.0f;
        freezeAmt += aF * (freezeT - freezeAmt);
        if (std::abs (freezeAmt - freezeT) < 1.0e-4f)
            freezeAmt = freezeT;

        if (first)
        {
            for (int j = 0; j < kLines; ++j)
                curLen[(size_t) j] = tgtLen[(size_t) j];
            modDepth = modDepthT;
        }
        // loss per line (from the lengths the lines have now; redone while they glide or the time moves)
        bool moving = coefDirty || first || std::abs (freezeAmt - lastFreeze) > 1.0e-7f;
        for (int j = 0; j < kLines && ! moving; ++j)
            moving = std::abs (curLen[(size_t) j] - fitLen[(size_t) j]) > 0.5f;
        if (moving)
        {
            const double w6 = 2.0 * 3.14159265358979323846 * std::min (6000.0, 0.45 * sr) / sr;
            const double fz = freezeAmt;
            for (int j = 0; j < kLines; ++j)
            {
                const double loop = curLen[(size_t) j] + apLen[(size_t) j];
                const double lossLow = 60.0 * loop / (sr * tLow), lossHigh = 60.0 * loop / (sr * tHigh);
                const double extra = std::max (0.0, lossHigh - lossLow); // dB the one-pole must add at 6 kHz
                double lo = 0.0, hi = 0.97;
                for (int it = 0; it < 24; ++it)
                {
                    const double mid = 0.5 * (lo + hi);
                    (-onePoleDb (mid, w6) < extra ? lo : hi) = mid;
                }
                const double d = 0.5 * (lo + hi);
                const double frozenLoss = 60.0 * loop / (sr * 5000.0);
                const double gDb = -((1.0 - fz) * lossLow + fz * frozenLoss);
                gain[(size_t) j] = (float) std::pow (10.0, gDb / 20.0);
                damp[(size_t) j] = (float) ((1.0 - fz) * d);
                fitLen[(size_t) j] = curLen[(size_t) j];
            }
            lastFreeze = freezeAmt;
            coefDirty = false;
        }
        first = false;
    }

    void run (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        constexpr float g8 = 0.35355339f;
        for (int i = 0; i < n; ++i)
        {
            const float inGain = 1.0f - freezeAmt;
            modDepth += glide * (modDepthT - modDepth);
            float li[2];
            const float x[2] = { inL[i], inR[i] };
            for (int ch = 0; ch < 2; ++ch)
            {
                float v = x[ch];
                inLp[ch] = v + inCoef * (inLp[ch] - v);
                v = inLp[ch];
                v = diff[(size_t) ch][0].process (v, 0.55f);
                v = diff[(size_t) ch][1].process (v, 0.55f);
                li[ch] = v * inGain;
            }
            const float c0 = lfoC, s0 = lfoS;
            lfoC = c0 * lfoRc - s0 * lfoRs;
            lfoS = c0 * lfoRs + s0 * lfoRc;
            const float mod = modDepth * s0;
            float v[kLines];
            for (int j = 0; j < kLines; ++j)
            {
                curLen[(size_t) j] += glide * (tgtLen[(size_t) j] - curLen[(size_t) j]);
                const float m = (j == 1) ? mod : (j == 4) ? -mod : 0.0f;
                const float dl = std::max (2.0f, curLen[(size_t) j] + m);
                float y = (j == 1 || j == 4) ? hermite (lines[(size_t) j], dl) : lines[(size_t) j].read (dl);
                y = loopAp[(size_t) j].process (y, 0.5f);
                const float d = damp[(size_t) j];
                lp[(size_t) j] = y + d * (lp[(size_t) j] - y);
                v[j] = lp[(size_t) j] * gain[(size_t) j];
            }
            // taps for the two outputs (orthogonal sign patterns)
            const float oL = v[0] - v[1] + v[2] + v[3] - v[4] + v[5] - v[6] - v[7];
            const float oR = v[0] + v[1] - v[2] - v[3] + v[4] + v[5] - v[6] - v[7];
            // Hadamard 8 (the three butterfly passes written out)
            float t0 = v[0] + v[1], t1 = v[0] - v[1], t2 = v[2] + v[3], t3 = v[2] - v[3];
            float t4 = v[4] + v[5], t5 = v[4] - v[5], t6 = v[6] + v[7], t7 = v[6] - v[7];
            const float u0 = t0 + t2, u1 = t1 + t3, u2 = t0 - t2, u3 = t1 - t3;
            const float u4 = t4 + t6, u5 = t5 + t7, u6 = t4 - t6, u7 = t5 - t7;
            const float h[kLines] = { u0 + u4, u1 + u5, u2 + u6, u3 + u7, u0 - u4, u1 - u5, u2 - u6, u3 - u7 };
            static const float sgn[kLines] = { 1, -1, 1, 1, -1, 1, -1, -1 };
            for (int j = 0; j < kLines; ++j)
                lines[(size_t) j].push ((h[j] + ((j & 1) ? li[1] : li[0]) * sgn[(j * 3) & 7]) * g8);
            const float wl = lateGain * g8 * oL, wr = lateGain * g8 * oR;
            const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * side;
            outL[i] = m + sd;
            outR[i] = m - sd;
        }
    }

    static constexpr double kTrim = 2.25;

    double sr = 48000.0;
    std::array<Ring, kLines> lines;
    std::array<Ap, kLines> loopAp;
    std::array<std::array<Ap, 2>, 2> diff;
    std::array<float, kLines> tgtLen {}, curLen {}, fitLen {}, apLen {}, gain {}, damp {}, lp {};
    float lfoC = 1.0f, lfoS = 0.0f, lfoRc = 1.0f, lfoRs = 0.0f;
    double tLow = 1.0, tHigh = 1.0, t1k = 1.0, cachedTime = 0.0, cachedBrightness = 0.0;
    float glide = 0.0f, inCoef = 0.0f, inLp[2] {}, modDepth = 0.0f, modDepthT = 0.0f, side = 1.0f, lateGain = 1.0f;
    float freezeAmt = 0.0f, lastFreeze = -1.0f;
    IdleGate idle;
    int subPos = 0;
    bool first = true, modelValid = false, coefDirty = true;
};
} // namespace bidoo::dsp::reverb

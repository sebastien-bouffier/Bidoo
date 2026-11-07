#pragma once

// FdnReverb: a ready stereo reverb on the Grotte core (Fdn16), for plugins that need "a good
// reverb" with a few controls: reverb time, brightness, size, width, freeze. It replaces the
// Freeverb / juce::Reverb combs: 16 lines instead of 8 combs per channel, an exact RT60 at every
// frequency (AttenuationFit absorption, DecayModel curve) instead of a one-pole damping, a
// per-line all-pass for echo density, and modulation by a slowly turning feedback matrix so a
// held note doesn't ring tuned. Wet signal only: the plugin keeps its own dry/wet law.
//
// Cost is about 4-6 times a Freeverb (it runs 160 double biquads and 16 Lagrange reads per
// sample); the filter design runs per 32-sample sub-block and only while the time or size moves.
//
// For the whole product (early reflections, input saturation, post EQ, ducking, gate, styles)
// see fab's Grotte, which uses the same core.

#include "DecayModel.h"
#include "Fdn16.h"
#include "GlideAllpass.h"
#include "IdleGate.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace bidoo::dsp::reverb
{
struct FdnReverbParams
{
    double rt60 = 2.0;       // seconds at 1 kHz before the brightness tilt (0.1 .. 60)
    double brightness = 0.7; // 0..1: how long the highs live compared with the mids (0.5 = a normal room)
    double size = 0.5;       // 0..1: room size (line lengths 5.5 ms .. 40 ms and up)
    double width = 1.0;      // 0..1: 0 mono wet, 1 full stereo
    double modulation = 0.5; // 0..1: how much the modes are kept moving
    bool freeze = false;     // hold the tail (the input is shut out)
};

// The reverb time of a Freeverb-style comb reverb with feedback 0.7 + 0.28 * room (room 0..1) and
// 30.6 ms combs: about 0.6 s to 11 s. For plugins whose presets carry a Freeverb "size".
inline double rt60FromCombRoom (double room01)
{
    return -3.0 * 0.0306 / std::log10 (0.7 + 0.28 * std::clamp (room01, 0.0, 1.0));
}

class FdnReverb
{
public:
    static constexpr int kSubBlock = 32;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        fdn.prepare (sr, 175.0, 12.0);
        const float msToS = (float) (sr * 0.001);
        for (auto& c : diff)
            for (auto& a : c)
                a.prepare ((int) (14.0f * msToS));
        auto pole = [this] (double seconds) { return (float) std::exp (-1.0 / (seconds * sr)); };
        glide = 1.0f - pole (0.25); // lengths glide over ~250 ms
        for (int k = 0; k < kPoints; ++k)
            pointFreq[(size_t) k] = fdn.fitter().freqAt (k);
        reset();
    }

    void reset()
    {
        fdn.reset();
        for (auto& c : diff)
            for (auto& a : c)
                a.reset();
        inLp[0] = inLp[1] = 0.0f;
        subPos = 0;
        idle.reset();
        first = true;
        modelValid = false;
        freezeAmt = lastFreeze = 0.0f;
    }

    // Wet only. l / r may alias the input arrays. The parameters are taken on a fixed grid of
    // kSubBlock samples whatever the host's block size.
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
                // nothing in and the tail is gone (under -120 dB): no network, silence
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

    float lineLength (int j) const { return fdn.lineLength (j); }

private:
    static constexpr int kPoints = AttenuationFit::kPoints;
    static constexpr int kLines = Fdn16::kLines;
    // Level of the tail at 1 s so that it sits where a Freeverb's did (see the tests).
    static constexpr double kTrim = 2.85;

    void update (const FdnReverbParams& p, int len)
    {
        const double space = std::clamp (p.size, 0.0, 1.0);
        const double msToS = sr * 0.001;
        DecayModel model;
        model.baseTime = std::clamp (p.rt60, 0.1, 60.0);
        model.brightness = std::clamp (p.brightness, 0.0, 1.0);
        model.style = Modern;
        if (! modelValid || model.baseTime != cachedTime || model.brightness != cachedBrightness)
        {
            cachedTime = model.baseTime;
            cachedBrightness = model.brightness;
            modelValid = true;
            for (int k = 0; k < kPoints; ++k)
                modelLogT[(size_t) k] = std::log (model.timeAt (pointFreq[(size_t) k]));
            modelT1k = model.timeAt (1000.0);
        }

        // line lengths: geometric spread between lmin and lmax, interleaved between the blocks;
        // in small rooms the longer lines are stretched so a fast decay sounds like noise, not metal
        double lmin = 5.5 * std::pow(7.27, space), ratio = 2.5;
        const double stretch = 1.0 + 2.0 * std::max (0.0, 1.0 - space / 0.3);
        ratio *= stretch;
        float tgtLen[kLines];
        for (int j = 0; j < kLines; ++j)
        {
            const int rank = (j < 8) ? 2 * j : 2 * (j - 8) + 1;
            const double u = ((double) rank + 0.45 * std::sin (1.7 * j + 0.3)) / (kLines - 0.5);
            tgtLen[j] = (float) (lmin * std::pow (ratio, std::clamp (u, 0.0, 1.0)) * msToS);
        }
        fdn.setLengthTargets (tgtLen);
        static const double apMs[kLines] = { 0.71, 1.93, 1.13, 2.61, 0.89, 2.27, 1.49, 3.07,
                                             1.01, 2.43, 0.79, 1.71, 2.89, 1.27, 2.09, 0.97 };
        const double apScale = 0.55 + 0.9 * space;
        for (int j = 0; j < kLines; ++j)
            fdn.setLoopAllpass (j, (float) std::max (2.0, std::round (apMs[j] * apScale * msToS)));
        fdn.setLoopGain (0.55f);

        // input diffusion, kept under a tenth of the decay time so it doesn't ring on its own
        diffG = 0.55f;
        double dScale = 0.6 + 1.5 * space;
        {
            const double longest = 6.67 * dScale * 0.001;
            const double ring = 3.0 * longest / -std::log10 (std::max (0.05, (double) diffG));
            dScale *= std::clamp (0.08 * modelT1k / ring, 0.15, 1.0);
        }
        static const double diffMs[2][4] = { { 1.31, 2.13, 3.71, 5.93 }, { 1.47, 2.41, 4.19, 6.67 } };
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < 4; ++k)
                diff[(size_t) ch][(size_t) k].setTarget ((float) (diffMs[ch][k] * dScale * msToS));
        inCoef = (float) std::exp (-2.0 * 3.14159265358979323846 * std::min (17000.0, 0.45 * sr) / sr);

        // modulation: mostly the feedback matrix turning, a little chorus in the lengths
        const double c = std::clamp (p.modulation, 0.0, 1.0) * 0.5; // the "Character" of Grotte, up to 50 %
        const double cA = c / 0.5;
        const double depthMs = 0.004 + 0.136 * cA * cA;
        modDepthT = (float) (depthMs * msToS);
        for (int j = 0; j < kLines; ++j)
            fdn.setLfoRate (j, 0.22 + 0.06 * ((j * 7) % 16));
        fdn.updateRotation (0.06 + 0.5 * cA, 1.0, len, first);

        // the two blocks cross-feed a little (two separate halves would each have half the modal density)
        const double theta = 0.5;
        cosT = (float) std::cos (theta);
        sinT = (float) std::sin (theta);
        side = (float) std::clamp (p.width, 0.0, 1.0);
        lateGain = (float) (kTrim * std::pow (std::max (0.1, modelT1k), -0.15));

        // freeze
        const float aF = first ? 1.0f : 1.0f - (float) std::exp (-len / (0.08 * sr));
        freezeAmt += aF * ((p.freeze ? 1.0f : 0.0f) - freezeAmt);
        if (std::abs (freezeAmt - (p.freeze ? 1.0f : 0.0f)) < 1.0e-4f)
            freezeAmt = p.freeze ? 1.0f : 0.0f;

        // the attenuation targets: the decay curve in log time, glided
        const float a = first ? 1.0f : 1.0f - (float) std::exp (-len / (0.03 * sr));
        bool moving = first;
        for (int k = 0; k < kPoints; ++k)
        {
            const double lt = modelLogT[(size_t) k];
            auto& cur = logT[(size_t) k];
            if (first)
                cur = lt;
            else if (std::abs (lt - cur) > 1.0e-5)
            {
                cur += a * (lt - cur);
                moving = true;
            }
        }
        if (first)
        {
            fdn.snap();
            for (auto& ch : diff)
                for (auto& ap : ch)
                    ap.snap();
            modDepth = modDepthT;
        }
        else if (! moving)
            moving = fdn.lengthsMoved();
        if (std::abs (freezeAmt - lastFreeze) > 1.0e-7f || std::abs (modDepthT - lastFitMod) > 0.5f)
            moving = true;
        if (moving)
        {
            fdn.designAbsorption (logT.data(), (double) freezeAmt, modDepthT);
            lastFreeze = freezeAmt;
            lastFitMod = modDepthT;
        }
        first = false;
    }

    void run (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float inGain = 1.0f - freezeAmt;
            modDepth += glide * (modDepthT - modDepth);
            const float x[2] = { inL[i], inR[i] };
            float li[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                float v = x[ch];
                inLp[ch] = v + inCoef * (inLp[ch] - v);
                v = inLp[ch];
                for (auto& ap : diff[(size_t) ch])
                    v = ap.process (v, diffG, glide);
                li[ch] = v * inGain;
            }
            float tank[2];
            fdn.tick (li[0], li[1], cosT, sinT, modDepth, glide, (float) (subPos + i + 1) / (float) kSubBlock, tank[0], tank[1]);
            const float wl = lateGain * tank[0], wr = lateGain * tank[1];
            const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * side;
            outL[i] = m + sd;
            outR[i] = m - sd;
        }
    }

    double sr = 48000.0;
    Fdn16 fdn;
    std::array<std::array<GlideAllpass, 4>, 2> diff;
    std::array<double, kPoints> logT {}, pointFreq {}, modelLogT {};
    double modelT1k = 1.0, cachedTime = 0.0, cachedBrightness = 0.0;
    float glide = 0.0f, diffG = 0.55f, inCoef = 0.0f, inLp[2] {}, modDepth = 0.0f, modDepthT = 0.0f, lastFitMod = -1.0f;
    float cosT = 0.7f, sinT = 0.7f, side = 1.0f, lateGain = 0.5f, freezeAmt = 0.0f, lastFreeze = -1.0f;
    IdleGate idle;
    int subPos = 0;
    bool first = true, modelValid = false;
};
} // namespace bidoo::dsp::reverb

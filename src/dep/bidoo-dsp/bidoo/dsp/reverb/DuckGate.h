#pragma once

// DuckGate: a reverb's wet gain from its dry input, Grotte's ducking and auto gate (fab plugins/Grotte/GrotteDsp.h,
// the same arithmetic, written once for the Bidoo VCV REI on 2026-11-26).
// Ducking: the wet dips while the input plays, by up to duckDb from -50 dB of input to -10 dB (5 ms attack, 250 ms
// release, glided over 20 ms). Auto gate: the wet stays open while the input is above a threshold that follows its
// recent peak (-26 dB under it, -60 dB floor), then for holdMs, then closes (2 ms open, 80 ms close).
// Per sample, no allocation; the levels are +-1 full scale.

#include <algorithm>
#include <cmath>

namespace bidoo::dsp::reverb
{
class DuckGate
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        auto pole = [this] (float seconds) { return std::exp (-1.0f / (seconds * sr)); };
        gGlide = 1.0f - pole (0.02f);
        duckA = pole (0.005f);
        duckR = pole (0.25f);
        gateA = pole (0.002f);
        gateR = pole (0.08f);
        envR = pole (0.03f);
        peakR = pole (2.0f);
        reset();
    }

    void reset()
    {
        duckEnv = gateEnv = gatePeak = 0.0f;
        duckGain = gateGain = 1.0f;
        gateHold = 0;
    }

    // duckDb 0..30; gate on or off; holdMs the gate's hold (from 5 ms)
    void set (float duckDbIn, bool gateIn, float holdMs)
    {
        duckDb = std::clamp (duckDbIn, 0.0f, 30.0f);
        duckFull = std::pow (10.0f, -duckDb / 20.0f);
        gateOn = gateIn;
        holdSamples = (int) (std::max (5.0f, holdMs) * 0.001f * sr);
    }

    // the wet gain for this sample, from the dry input's two channels
    float process (float inL, float inR)
    {
        const float lev = std::max (std::abs (inL), std::abs (inR));
        duckEnv = lev > duckEnv ? lev + duckA * (duckEnv - lev) : lev + duckR * (duckEnv - lev);
        gateEnv = lev > gateEnv ? lev : lev + envR * (gateEnv - lev);
        gatePeak = lev > gatePeak ? lev : lev * (1.0f - peakR) + peakR * gatePeak;

        float duckT = 1.0f;
        // (under -50 dB no dip and from -10 dB the full one: no log or power there)
        if (duckDb > 0.05f && duckEnv > 0.0031f)
        {
            if (duckEnv > 0.317f)
                duckT = duckFull;
            else
            {
                const float envDb = 20.0f * std::log10 (std::max (1.0e-6f, duckEnv));
                const float amt = std::clamp ((envDb + 50.0f) / 40.0f, 0.0f, 1.0f);
                duckT = std::pow (10.0f, -duckDb * amt / 20.0f);
            }
        }
        duckGain += gGlide * (duckT - duckGain);

        float gateT = 1.0f;
        if (gateOn)
        {
            const float thr = std::max (0.001f, gatePeak * 0.05f);
            if (gateEnv > thr)
                gateHold = holdSamples;
            else if (gateHold > 0)
                --gateHold;
            gateT = gateHold > 0 ? 1.0f : 0.0f;
        }
        gateGain = gateT > gateGain ? gateT + gateA * (gateGain - gateT) : gateT + gateR * (gateGain - gateT);
        return duckGain * gateGain;
    }

    // open (for a light): the gate's gain above one half
    bool gateOpen() const { return gateGain > 0.5f; }

private:
    float sr = 48000.0f;
    float gGlide = 0, duckA = 0, duckR = 0, gateA = 0, gateR = 0, envR = 0, peakR = 0;
    float duckDb = 0, duckFull = 1, duckEnv = 0, duckGain = 1;
    bool gateOn = false;
    int holdSamples = 0, gateHold = 0;
    float gateEnv = 0, gatePeak = 0, gateGain = 1;
};
} // namespace bidoo::dsp::reverb

#pragma once

// GlideAllpass: a Schroeder all-pass whose length glides (a fractional, Hermite-read delay), from
// Grotte. Once the glide has settled the Hermite read becomes a fixed 4-tap FIR whose weights are
// worked out once. Used as the diffusers and the in-loop all-passes of the reverbs (echo density
// grows with every pass instead of with the line count alone).

#include "../delay/GuardDelayLine.h"
#include <algorithm>

namespace bidoo::dsp::reverb
{
// Schroeder all-pass with a gliding (fractional) length.
class GlideAllpass
{
public:
    void prepare (int maxDelay) { d.prepare (maxDelay); }
    void reset()
    {
        d.reset();
        held = false;
    }
    void setTarget (float samples) { target = std::max (2.0f, samples); }
    void snap()
    {
        len = target;
        held = false;
    }

    inline float process (float x, float g, float glide)
    {
        const float before = len;
        len += glide * (target - len);
        float v;
        if (len < before || len > before)
        {
            held = false;
            v = d.read (len - 1.0f);
        }
        else
        {
            // the glide has settled (the length no longer changes): the Hermite read becomes a
            // fixed 4-tap FIR, its weights worked out once
            if (! held)
            {
                const float dl = len - 1.0f;
                whole = (int) dl;
                double h[4];
                GuardDelayLine::hermiteWeights ((double) (dl - (float) whole), h);
                for (int k = 0; k < 4; ++k)
                    wt[k] = (float) h[k];
                held = true;
            }
            const float* p = d.window (whole);
            v = wt[0] * p[0] + wt[1] * p[1] + wt[2] * p[2] + wt[3] * p[3];
        }
        const float u = x + g * v;
        d.push (u);
        return v - g * u;
    }

private:
    GuardDelayLine d;
    float len = 2.0f, target = 2.0f;
    float wt[4] = {};
    int whole = 0;
    bool held = false;
};

// tanh (k x) / k with first-order antiderivative anti-aliasing (bidoo-dsp shaping/Adaa.h): the
} // namespace bidoo::dsp::reverb

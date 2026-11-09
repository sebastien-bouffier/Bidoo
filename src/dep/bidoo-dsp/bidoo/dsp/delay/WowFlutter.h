#pragma once

// TapeWowFlutter: the speed variations of a tape transport, as a delay-time offset for a delay line
// read (a tape echo, a chorus, a lo-fi effect), so the pitch wobbles the way tape does.
//
// What it models (from the published descriptions of tape transports and of wow and flutter
// measurement, IEC 60386 / DIN 45507):
//  - Wow: slow speed variations, about 0.3 to 2 Hz (reel eccentricity, uneven tape tension). Three
//    sine components around the wow rate (the rate, a slower one near 0.44x and a faster one near
//    2x), each with its own slowly drifting frequency (+-12 %) and depth (+-50 %), so the wobble
//    never repeats exactly.
//  - Flutter: faster variations, about 4 to 12 Hz (capstan and pinch roller): three components (the
//    rate, near 1.6x for the pinch roller, 2x for the capstan's second harmonic), drifting less
//    (+-6 %, +-40 %), plus scrape flutter: broadband speed noise (the tape rubbing on the heads and
//    guides), band-passed around 100 Hz and gently low-passed at 400 Hz, a share of the flutter's power.
//
// Units: depths are the nominal PEAK pitch deviation in cents, as for one sine (the speed
// deviation's RMS is that of a sine of that peak: depth / sqrt 2). The output is the delay
// offset in samples to add to a delay line's read (positive = later); the read's pitch then
// follows the speed: pitch ratio = 1 - d(offset)/dn. speed() gives that ratio.
//
// Cost: the components run at a control rate near 3 kHz (every 16 samples at 48 kHz, 32 at 96 kHz)
// with rotators and polynomial increments (no trig per tick, no allocation); between two control
// points the offset moves in a straight line, so the read never steps (the speed is constant over
// each 5 ms tick; everything it carries is far below the control rate's Nyquist). About one add per
// sample plus the ticks: a few ns per sample. Idle (both depths at 0 and landed) the output is
// exactly 0 and nothing runs.
//
// Deterministic: the same seed, sample rate and settings give the same output (reset() starts it
// again). Real-time safe: no allocation anywhere; prepare() only computes constants.

#include <bidoo/dsp/core/Inline.h>
#include <bidoo/dsp/core/Math.h>
#include <bidoo/dsp/core/Random.h>

#include <cmath>
#include <cstdint>

namespace bidoo::dsp
{
class TapeWowFlutter
{
public:
    static constexpr int kWowComponents = 3, kFlutterComponents = 3;
    static constexpr int kComponents = kWowComponents + kFlutterComponents;
    static constexpr float kDefaultWowHz = 0.6f, kDefaultFlutterHz = 6.5f;
    static constexpr float kScrapeHz = 100.0f; // centre of the scrape band
    static constexpr float kDefaultScrapeShare = 0.3f;

    void prepare (double sampleRate, uint64_t seedValue = 1)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        seed = seedValue;
        interval = (int) std::lround (sr / 3000.0);
        interval = interval < 1 ? 1 : interval;
        const double fc = sr / (double) interval; // the control rate
        invFc = (float) (1.0 / fc);
        // the scrape's depth glide, about 60 ms (the components glide at their own pace, control())
        glide = (float) (1.0 - std::exp (-1.0 / (0.06 * fc)));
        // scrape: a TPT state-variable band pass (peak gain 1), white noise in; its output power is
        // measured once here from the impulse response so the share is calibrated at any rate
        {
            const double g = std::tan (kPi * (double) kScrapeHz / fc);
            const double q = 0.5;
            const double k = 1.0 / q;
            svfA1 = (float) (1.0 / (1.0 + g * (g + k)));
            svfA2 = (float) g * svfA1;
            svfA3 = (float) g * svfA2;
            svfK = (float) k;
            // then a one-pole low pass at 400 Hz, so the speed doesn't jump from tick to tick
            lpC = (float) (1.0 - std::exp (-kTwoPi * 400.0 / fc));
            float z1 = 0.0f, z2 = 0.0f, h = 0.0f;
            double energy = 0.0;
            for (int n = 0; n < 1 << 16; ++n)
            {
                const float x = n == 0 ? 1.0f : 0.0f;
                h += lpC * (svfStep (x, z1, z2) - h);
                energy += (double) h * (double) h;
            }
            // uniform noise in [-1, 1] has power 1/3; the band's output power is that times energy
            scrapeNorm = (float) (1.0 / std::sqrt (energy / 3.0));
        }
        leak = (float) std::exp (-kTwoPi * 2.0 / fc); // the scrape's integrator forgets below ~2 Hz
        update();
        reset();
    }

    // Depth: nominal peak pitch deviation in cents (0 = none). Rate: the main wow frequency in Hz.
    void setWow (float depthCents, float rateHz = kDefaultWowHz)
    {
        wowTarget = centsToSpeed (depthCents);
        wowRateTarget = clampf (rateHz, 0.05f, 20.0f);
        update();
    }

    // Depth in cents (peak, including the scrape), rate the main flutter frequency in Hz, scrape
    // the share of the flutter's power that is broadband scrape noise (0..1).
    void setFlutter (float depthCents, float rateHz = kDefaultFlutterHz, float scrape = kDefaultScrapeShare)
    {
        flutterTarget = centsToSpeed (depthCents);
        flutterRateTarget = clampf (rateHz, 0.5f, 60.0f);
        scrapeShare = clampf (scrape, 0.0f, 1.0f);
        update();
    }

    // How much the rates and depths drift (0 = steady sines, 1 = the full drift described above).
    void setIrregularity (float amount)
    {
        irregularity = clampf (amount, 0.0f, 1.0f);
        update();
    }

    // Starts again from the seed, with the depths and rates already at their targets.
    void reset()
    {
        rng.reseed (seed);
        flutterDepth = flutterTarget;
        for (int k = 0; k < kComponents; ++k)
        {
            const float phase = (float) kTwoPi * rng.uniform();
            re[k] = std::cos (phase);
            im[k] = std::sin (phase);
            for (auto& n : drift[k])
            {
                n.a = rng.bipolar();
                n.b = rng.bipolar();
                n.t = rng.uniform();
            }
            // the glides start landed (at the drift's starting point)
            w1[k] = w[k] = (float) (kTwoPi * comp[k].hz / sr);
            amp1[k] = amp2[k] = 0.0f;
        }
        s1 = s2 = lp = scrape = 0.0f;
        lastReach = prevReach = 0.0f;
        from = to = 0.0f;
        step = 0.0f;
        countdown = 0;
        idleNow = wowTarget == 0.0f && flutterTarget == 0.0f;
        snapDepth = ! idleNow;
        if (! idleNow)
        {
            to = control();
            from = to;
        }
        value = to;
    }

    // True while the output is exactly 0 and nothing is computed (both depths 0 and landed).
    bool isIdle() const { return idleNow && wowTarget == 0.0f && flutterTarget == 0.0f; }

    // The next sample's delay offset, in samples.
    BIDOO_DSP_INLINE float next()
    {
        if (countdown <= 0)
        {
            if (isIdle())
                return 0.0f;
            from = to;
            to = control();
            step = (to - from) / (float) interval;
            value = from;
            countdown = interval;
        }
        --countdown;
        value += step;
        return value;
    }

    // n offsets (samples) into out.
    void process (float* out, int n)
    {
        int i = 0;
        while (i < n)
        {
            if (countdown <= 0)
            {
                if (isIdle())
                {
                    for (; i < n; ++i)
                        out[i] = 0.0f;
                    return;
                }
                out[i++] = next();
                continue;
            }
            // the rest of this tick's straight line
            const int run = countdown < n - i ? countdown : n - i;
            float v = value;
            for (int j = 0; j < run; ++j)
            {
                v += step;
                out[i + j] = v;
            }
            value = v;
            countdown -= run;
            i += run;
        }
    }

    // The tape speed now (1 = nominal): the read's pitch ratio over the current control tick.
    float speed() const { return 1.0f - step; }

    int controlInterval() const { return interval; }

    // An upper bound of |offset| over the current tick (samples): the room a read needs on either
    // side of its delay (a short delay can scale the offset down to fit).
    float reach() const { return idleNow ? 0.0f : (lastReach > prevReach ? lastReach : prevReach); }

    static float centsToSpeed (float cents) { return cents > 0.0f ? std::exp2 (cents / 1200.0f) - 1.0f : 0.0f; }
    static float speedToCents (float s) { return 1200.0f * std::log2 (1.0f + s); }

private:
    // the component frequencies (x the wow or flutter rate) and weights (normalised in control())
    static constexpr float kRatio[kComponents] { 1.0f, 0.437f, 2.03f, 1.0f, 1.61f, 2.0f };
    static constexpr float kWeight[kComponents] { 1.0f, 0.6f, 0.35f, 1.0f, 0.5f, 0.3f };

    // what control() needs of each component, from the settings
    struct Component
    {
        float hz = 1.0f, scale = 0.0f, freqDrift = 0.0f, depthDrift = 0.0f, dtFreq = 0.0f, dtDepth = 0.0f, perHz = 0.0f;
    };

    void update()
    {
        static constexpr float wowNorm = 0.8323f;     // 1 / sqrt (1 + 0.6^2 + 0.35^2)
        static constexpr float flutterNorm = 0.8638f; // 1 / sqrt (1 + 0.5^2 + 0.3^2)
        const float irr = irregularity;
        // how far the frequencies and depths drift at full irregularity; E[(1 + d n)^2] =
        // 1 + d^2 var(n), var(n) = 0.2476 for smoothstep value noise in [-1, 1]
        const float wowDepthDrift = 0.5f * irr, flutterDepthDrift = 0.4f * irr;
        const float wowComp = 1.0f / std::sqrt (1.0f + 0.2476f * wowDepthDrift * wowDepthDrift);
        const float flutterComp = 1.0f / std::sqrt (1.0f + 0.2476f * flutterDepthDrift * flutterDepthDrift);
        const float wowScale = wowTarget * wowNorm * wowComp;
        const float flutterScale = flutterTarget * flutterNorm * flutterComp * std::sqrt (1.0f - scrapeShare);
        // the drift points come every 1.7 s (wow) or 0.35 s (flutter), a little sooner for the
        // higher components
        const float wowDt = invFc / 1.7f, flutterDt = invFc / 0.35f;
        for (int k = 0; k < kComponents; ++k)
        {
            const bool isWow = k < kWowComponents;
            Component& p = comp[k];
            p.hz = (isWow ? wowRateTarget : flutterRateTarget) * kRatio[k];
            p.scale = (isWow ? wowScale : flutterScale) * kWeight[k];
            p.freqDrift = (isWow ? 0.12f : 0.06f) * irr;
            p.depthDrift = isWow ? wowDepthDrift : flutterDepthDrift;
            const float dt = isWow ? wowDt : flutterDt;
            p.dtFreq = dt * (1.0f + 0.13f * (float) k);
            p.dtDepth = dt * (0.9f + 0.11f * (float) k);
            p.perHz = (float) interval / (float) sr; // a glide pole's step per Hz of its component
        }
        // the scrape's speed noise: unit power times this, RMS of a sine of the flutter's peak
        scrapeGain = std::sqrt (scrapeShare) * 0.70710678f * scrapeNorm;
    }

    // value noise: a new random point every period, smoothstep in between (no corners)
    struct Drift
    {
        float a = 0.0f, b = 0.0f, t = 0.0f;
    };

    static float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    BIDOO_DSP_INLINE float svfStep (float x, float& z1, float& z2) const
    {
        const float v3 = x - z2;
        const float v1 = svfA1 * z1 + svfA2 * v3;
        const float v2 = z2 + svfA2 * z1 + svfA3 * v3;
        z1 = 2.0f * v1 - z1;
        z2 = 2.0f * v2 - z2;
        return v1 * svfK; // band pass with a peak gain of 1
    }

    BIDOO_DSP_INLINE float driftStep (Drift& n, float dt)
    {
        n.t += dt;
        if (n.t >= 1.0f)
        {
            n.t -= 1.0f;
            n.a = n.b;
            n.b = rng.bipolar();
        }
        const float s = n.t * n.t * (3.0f - 2.0f * n.t);
        return n.a + (n.b - n.a) * s;
    }

    // One control point: advances every component by one tick and returns the delay offset there.
    //
    // A component is x = A sin(phase) in the delay, A = a / w for a peak speed deviation a at w rad
    // per sample. Changing A (a depth or rate change, the drift) adds dA/dt sin(phase) to the
    // speed, a lot for slow components (A is hundreds of samples for the wow): so a and w glide
    // per component over about a third to half of its period (two poles each, so the speed
    // never steps), which keeps that extra term under about a quarter of the component's own swing.
    float control()
    {
        flutterDepth += glide * (flutterTarget - flutterDepth); // (the scrape's depth)
        if (flutterTarget == 0.0f && flutterDepth < 1.0e-9f)
            flutterDepth = 0.0f;

        const float tick = (float) interval;
        const float twoPiOverSr = (float) (kTwoPi / sr);

        float y = 0.0f, reach = 0.0f;
        for (int k = 0; k < kComponents; ++k)
        {
            const Component& p = comp[k];
            const float nf = driftStep (drift[k][0], p.dtFreq);
            const float na = driftStep (drift[k][1], p.dtDepth);
            const float hz = p.hz * (1.0f + p.freqDrift * nf);
            const float a = p.scale * (1.0f + p.depthDrift * na);
            // the glides, two poles each: the rate over about half a period per pole, the depth over
            // about a third of one
            const float cw = clampf (p.perHz * hz * 2.0f, 0.0f, 0.5f);
            const float ca = clampf (p.perHz * hz * 2.857f, 0.0f, 0.5f);
            if (snapDepth)
            {
                w1[k] = w[k] = twoPiOverSr * hz;
                amp1[k] = amp2[k] = a;
            }
            w1[k] += cw * (twoPiOverSr * hz - w1[k]);
            w[k] += cw * (w1[k] - w[k]);
            amp1[k] += ca * (a - amp1[k]);
            amp2[k] += ca * (amp1[k] - amp2[k]);
            // the phase step of one tick: cos and sin by their series (the angle stays under 0.5 rad)
            const float th = w[k] * tick, th2 = th * th;
            const float c = 1.0f - th2 * (0.5f - th2 * (1.0f / 24.0f - th2 * (1.0f / 720.0f)));
            const float s = th * (1.0f - th2 * (1.0f / 6.0f - th2 * (1.0f / 120.0f)));
            const float r = re[k] * c - im[k] * s;
            const float i = re[k] * s + im[k] * c;
            const float renorm = 1.5f - 0.5f * (r * r + i * i);
            re[k] = r * renorm;
            im[k] = i * renorm;
            const float swing = amp2[k] / w[k]; // samples
            y += swing * im[k];
            reach += std::abs (swing);
        }

        snapDepth = false;

        // scrape: band-passed speed noise, integrated into a delay (the integrator leaks below the band)
        lp += lpC * (svfStep (rng.bipolar(), s1, s2) - lp);
        const float e = flutterDepth * scrapeGain * lp;
        scrape = leak * scrape - e * tick;
        y += scrape;
        reach += std::abs (scrape);

        // both depths off and everything faded under a thousandth of a sample: rest at exactly 0
        // (the last tick ramps there)
        if (wowTarget == 0.0f && flutterTarget == 0.0f && flutterDepth == 0.0f && reach < 1.0e-3f)
        {
            for (int k = 0; k < kComponents; ++k)
                amp1[k] = amp2[k] = 0.0f;
            s1 = s2 = lp = scrape = 0.0f;
            y = 0.0f;
            idleNow = true;
        }
        else
            idleNow = false;
        prevReach = lastReach;
        lastReach = reach;
        return y;
    }

    double sr = 48000.0;
    uint64_t seed = 1;
    int interval = 16, countdown = 0;
    float invFc = 1.0f / 3000.0f, glide = 0.005f, leak = 0.996f;
    float wowTarget = 0.0f, flutterTarget = 0.0f, flutterDepth = 0.0f;
    float wowRateTarget = kDefaultWowHz, flutterRateTarget = kDefaultFlutterHz;
    float w1[kComponents] {}, w[kComponents] {}, amp1[kComponents] {}, amp2[kComponents] {}; // the glides
    bool snapDepth = false; // the first control point after reset() starts with the depths landed
    float scrapeShare = kDefaultScrapeShare, irregularity = 1.0f, scrapeGain = 0.0f;
    Component comp[kComponents] {};
    float re[kComponents] {}, im[kComponents] {};
    Drift drift[kComponents][2] {};
    float svfA1 = 0.0f, svfA2 = 0.0f, svfA3 = 0.0f, svfK = 2.0f, scrapeNorm = 1.0f;
    float s1 = 0.0f, s2 = 0.0f, lp = 0.0f, lpC = 0.5f, scrape = 0.0f;
    float from = 0.0f, to = 0.0f, step = 0.0f, value = 0.0f;
    float lastReach = 0.0f, prevReach = 0.0f;
    bool idleNow = true;
    Rng rng { 1 };
};
} // namespace bidoo::dsp

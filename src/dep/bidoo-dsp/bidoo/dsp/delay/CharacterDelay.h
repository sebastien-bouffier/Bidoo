#pragma once

// CharacterDelay: a stereo delay with six characters over one feedback loop (Bidoo VCV: rICOcHEt).
// Written from the published descriptions of these machines, no code of any product or other project.
//
//  - Digital: exact repeats. A whole-sample delay is read with no interpolation at all, so with
//    CHARACTER at 0 and TONE centred a repeat is the input bit for bit (below the limiter's knee and
//    apart from the 5 Hz DC blocker). CHARACTER adds an early rack unit: fewer bits, a lower rate, less top.
//    A time change crossfades two heads.
//  - Tape: wow and flutter (TapeWowFlutter), the record side's pre-emphasis and saturation, head and
//    gap losses, hiss and dropouts. A time change moves the head: the pitch swoops.
//  - Bbd: a bucket brigade: its clock is stages / (2 time), so long times get darker and dirtier
//    (hold at the clock's rate, anti-alias and reconstruction filters tied to it, a clock leak,
//    noise that follows the signal as a compander's does). A time change swoops.
//  - Lofi: an old sampler: sample and hold with no anti-alias filter and fewer companded bits in the
//    loop (mu-law, so quiet signals survive 4 bits), so each repeat is grittier. It truncates towards
//    zero, so tails end in silence instead of a limit cycle.
//  - Diffuse: four modulated all-pass filters per side in the loop: each repeat is more blurred.
//  - Reverse: slices of the input play backwards (CHARACTER: the chance that a slice plays forwards).
//    The slices feed a plain delay, so every repeat stays backwards.
//
// Time and feedback: the delay is measured from input to first echo. Feedback knob 0..1, loop gain
// 1 at kUnityKnob, kMaxLoopGain at the top: every type can self-oscillate and stays bounded, by a
// saturation or limiter on the record side. Freeze recirculates the line itself, whatever the type,
// so a frozen loop neither decays nor changes; the entry and exit are crossfaded so the seam doesn't click.
//
// Type changes crossfade the two types' outputs and record sides over kTypeFadeMs. All the audio is
// float, times are double. Allocation only in prepare(). About 12 MB per instance at 48 kHz.
//
// Idle skip (on by default, setIdleSkip): silent in and decayed (nothing over -100 dB for as long as
// the line can be read back), process() writes zeros and costs nearly nothing; see setIdleSkip.

#include "FractionalReads.h"
#include "ModDelayLine.h"
#include "WowFlutter.h"
#include "../core/Inline.h"
#include "../core/Math.h"
#include "../core/Random.h"
#include "../shaping/Adaa.h"
#include "../shaping/AdaaTanhTable.h"
#include "../shaping/TanhPade.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace bidoo::dsp
{
class CharacterDelay
{
public:
    enum Type { Digital, Tape, Bbd, Lofi, Diffuse, Reverse, kNumTypes };

    static constexpr double kMinSeconds = 0.001, kMaxSeconds = 8.0;
    static constexpr float kMaxLoopGain = 1.15f;
    static constexpr float kUnityKnob = 1.0f / kMaxLoopGain;
    static constexpr float kTypeFadeMs = 40.0f;

    struct Settings
    {
        double timeL = 0.3, timeR = 0.3; // seconds
        float feedback = 0.4f;           // knob 0..1 (see kUnityKnob)
        float tone = 0.0f;               // -1 dark .. 0 flat .. 1 thin
        float mix = 0.35f;               // 0 dry .. 1 wet
        float character = 0.0f;          // 0..1, see each type
        bool pingPong = false;
        bool freeze = false;
        int type = Digital;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        const int margin = (int) (0.6 * sr); // wow, and a little room
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& c = chans[(size_t) ch];
            c.line.prepare ((int) (kMaxSeconds * sr) + margin);
            c.input.prepare ((int) (2.0 * (kMaxSeconds + kSliceFadeSeconds) * sr) + 16);
            for (int i = 0; i < kAllpasses; ++i)
            {
                const double base = (ch == 0 ? kApMsL[(size_t) i] : kApMsR[(size_t) i]) * 0.001 * sr;
                c.apBase[(size_t) i] = (float) base;
                c.ap[(size_t) i].prepare ((int) (base * 1.1) + 16);
            }
        }
        wow.prepare (sr, 11);
        flutter[0].prepare (sr, 23);
        flutter[1].prepare (sr, 37);
        rng.reseed (1234);
        smooth = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
        typeFadeStep = 1.0f / (kTypeFadeMs * 0.001f * (float) sr);
        freezeStep = 1.0f / (0.03f * (float) sr);
        pingStep = 1.0f / (0.02f * (float) sr);
        envRelease = std::exp (-1.0f / (1.5f * (float) sr));
        lineHold = chans[0].line.maxDelay() + 8;
        inputHold = chans[0].input.maxDelay() + 8;
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : chans)
        {
            c.line.clear();
            c.input.clear();
        }
        // counted again from here, although the lines are empty: the first seconds after a reset
        // play as they always did (the Reverse slice clock runs from the reset)
        lineQuiet = inputQuiet = 0;
        idle = false;
        resetState();
    }

    // The idle skip (on by default): with the input silent (under 1e-9) and everything the delay
    // still holds under -100 dB (the wet output and what went into the line, for as long as the line
    // is; for as long as the input buffer too when Reverse may read it), and no freeze, process()
    // stops working: it writes zeros and leaves its state alone. The first sample that is not silent
    // wakes it as reset() would leave it, except that the big lines are not cleared (they hold
    // nothing above -100 dB): so a delay whose tail had decayed to zeros wakes bit for bit as a reset
    // one, and before it sleeps nothing changes at all. The free-running parts (wow and flutter, the
    // noise, the Diffuse LFO, the slice clock) start again from where reset() puts them.
    void setIdleSkip (bool on) noexcept
    {
        idleSkip = on;
        if (! on && idle)
            wake();
    }
    bool isIdle() const noexcept { return idle; }

    // One stereo sample.
    BIDOO_DSP_INLINE void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        if (idleSkip)
        {
            const bool silentIn = std::fabs (inL) < kIdleIn && std::fabs (inR) < kIdleIn; // false for NaN
            if (silentIn && canSleep())
            {
                idle = true;
                wet = {};
                outL = outR = 0.0f;
                return;
            }
            if (idle)
                wake();
        }
        processAwake (inL, inR, outL, outR);
    }

    float getWet (int ch) const noexcept { return wet[(size_t) ch]; }

private:
    static constexpr float kIdleIn = 1.0e-9f, kIdleOut = 1.0e-5f; // -180 dB in, -100 dB held

    bool canSleep() const noexcept
    {
        if (lineQuiet < lineHold || settings.freeze || freezeMix > 0.0f)
            return false;
        const bool reverse = settings.type == Reverse || typeNew == Reverse || typeOld == Reverse;
        return ! reverse || inputQuiet >= inputHold;
    }

    void wake() noexcept
    {
        idle = false;
        resetState();
    }

    // Everything reset() does but clearing the line and the input buffer.
    void resetState() noexcept
    {
        for (auto& c : chans)
        {
            for (auto& a : c.ap)
                a.clear();
            for (auto& v : c.v)
                v = Voice {};
            for (auto& g : c.grains)
                g = Grain {};
            c.sliceLeft = 0;
            c.freezeLength = 0;
        }
        wow.reset();
        flutter[0].reset();
        flutter[1].reset();
        frames = {};
        typeOld = typeNew = std::clamp (settings.type, 0, kNumTypes - 1);
        typeFade = 1.0f;
        freezeMix = settings.freeze ? 1.0f : 0.0f;
        pingMix = settings.pingPong ? 1.0f : 0.0f;
        fbSmoothed = settings.feedback * kMaxLoopGain;
        const float m = std::clamp (settings.mix, 0.0f, 1.0f);
        drySmoothed = std::cos (m * kHalfPi);
        wetSmoothed = std::sin (m * kHalfPi);
        charSmoothed = settings.character;
        toneLpK = toneHpK = -1.0f;
        tick = 0;
        wet = {};
        updateTargets();
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& c = chans[(size_t) ch];
            c.glide = c.target;
            c.crossA = c.crossB = c.target;
            c.crossing = false;
            c.crossPos = 0.0f;
            c.freezeLength = std::max (1, (int) std::lround (c.target));
        }
        control();
    }

public:
    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        settings.timeL = std::clamp (s.timeL, kMinSeconds, kMaxSeconds);
        settings.timeR = std::clamp (s.timeR, kMinSeconds, kMaxSeconds);
        settings.feedback = std::clamp (s.feedback, 0.0f, 1.0f);
        settings.tone = std::clamp (s.tone, -1.0f, 1.0f);
        settings.mix = std::clamp (s.mix, 0.0f, 1.0f);
        settings.character = std::clamp (s.character, 0.0f, 1.0f);
        settings.type = std::clamp (s.type, 0, kNumTypes - 1);
        updateTargets();
    }

    // Starts a new Reverse slice now, the old one fades out from where it is (a clock input lines
    // the slices up with its pulses). Harmless in the other types.
    void restartSlices() noexcept
    {
        chans[0].sliceLeft = 0;
        chans[1].sliceLeft = 0;
    }

private:
    BIDOO_DSP_INLINE void processAwake (float inL, float inR, float& outL, float& outR) noexcept
    {
        if (--tick <= 0)
        {
            tick = kControl;
            control();
        }
        inL = std::isfinite (inL) ? inL : 0.0f;
        inR = std::isfinite (inR) ? inR : 0.0f;

        fbSmoothed += smooth * (settings.feedback * kMaxLoopGain - fbSmoothed);
        drySmoothed += smooth * (dryTarget - drySmoothed);
        wetSmoothed += smooth * (wetTarget - wetSmoothed);
        pingMix = moveTowards (pingMix, settings.pingPong ? 1.0f : 0.0f, pingStep);

        // a type change waits for the fade under way
        if (typeFade >= 1.0f && settings.type != typeNew)
            startType (settings.type);
        typeFade = std::min (1.0f, typeFade + typeFadeStep);

        // freeze: the length is taken when the fade starts from live
        if (settings.freeze && freezeMix <= 0.0f)
            for (auto& c : chans)
                c.freezeLength = std::max (1, (int) std::lround (c.crossing ? c.crossB : (usesGlide (typeNew) ? c.glide : c.crossA)));
        freezeMix = moveTowards (freezeMix, settings.freeze ? 1.0f : 0.0f, freezeStep);

        const float mono = 0.5f * (inL + inR);
        const std::array<float, 2> routed { inL + pingMix * (mono - inL), inR * (1.0f - pingMix) };

        const bool needMod = usesGlide (typeOld) || usesGlide (typeNew);
        const float wowOffset = needMod ? wow.next() : 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& c = chans[(size_t) ch];
            advanceHeads (c);
            c.input.push (routed[(size_t) ch]);
            c.modOffset = needMod ? (float) std::min (1.0, c.glide / (0.05 * sr)) * (wowOffset + flutter[ch].next()) : 0.0f;
        }

        const bool fading = typeFade < 1.0f;
        runType (typeNew, routed);
        if (fading)
            runType (typeOld, routed);

        const float fadeIn = typeFade;
        bool loud = false; // anything above -100 dB going out or into the line (the idle skip)
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& c = chans[(size_t) ch];
            auto& fNew = frames[(size_t) typeNew];
            float w = fNew.wet[(size_t) ch], rec = fNew.rec[(size_t) ch];
            if (fading)
            {
                auto& fOld = frames[(size_t) typeOld];
                w = fOld.wet[(size_t) ch] + fadeIn * (w - fOld.wet[(size_t) ch]);
                rec = fOld.rec[(size_t) ch] + fadeIn * (rec - fOld.rec[(size_t) ch]);
            }
            if (! std::isfinite (w) || ! std::isfinite (rec))
            {
                // cannot happen with finite input and bounded loops; never let it into the line
                for (auto& v : c.v)
                    v = Voice {};
                w = rec = 0.0f;
            }
            if (freezeMix > 0.0f)
                rec += freezeMix * (c.line.at (c.freezeLength - 1) - rec);
            c.line.push (rec);
            wet[(size_t) ch] = w;
            loud = loud || std::fabs (w) > kIdleOut || std::fabs (rec) > kIdleOut;
        }
        outL = drySmoothed * inL + wetSmoothed * wet[0];
        outR = drySmoothed * inR + wetSmoothed * wet[1];

        // how long the line and the input buffer have been taking in nothing (up to their length)
        lineQuiet = loud ? 0 : std::min (lineQuiet + 1, lineHold);
        const bool silentIn = std::fabs (inL) < kIdleIn && std::fabs (inR) < kIdleIn;
        inputQuiet = silentIn ? std::min (inputQuiet + 1, inputHold) : 0;
    }

    static constexpr int kControl = 16, kAllpasses = 4, kGrains = 3;
    static constexpr float kHalfPi = 1.5707963268f;
    static constexpr double kSliceFadeSeconds = 0.02;
    static constexpr std::array<float, kAllpasses> kApMsL { 11.3f, 17.9f, 29.1f, 43.7f };
    static constexpr std::array<float, kAllpasses> kApMsR { 12.7f, 19.3f, 31.7f, 47.3f };

    struct Voice
    {
        float toneLp = 0.0f, toneHp = 0.0f;   // TONE
        float lp1 = 0.0f, lp2 = 0.0f;         // read-side losses or reconstruction
        float rec1 = 0.0f, rec2 = 0.0f;       // record-side filters
        float hp = 0.0f, de = 0.0f;           // the type's low cut, Tape's de-emphasis
        float hold = 0.0f, holdPhase = 1.0f;  // sample and hold
        float env = 0.0f;                     // follows the loop, gates the noise
        float drop = 1.0f, dropTarget = 1.0f; // Tape's dropouts
        int dropLeft = 0;
        float leak = 1.0f;                    // Bbd's clock leak, a square at the clock
        AdaaState sat;
    };

    struct Grain
    {
        int n = 0, length = 0, fade = 1, stopAt = 0;
        bool on = false, forward = false;
    };

    struct Channel
    {
        ModDelayLine line, input;
        std::array<ModDelayLine, kAllpasses> ap;
        std::array<float, kAllpasses> apBase {}, apDelay {};
        std::array<Voice, kNumTypes> v {};
        std::array<Grain, kGrains> grains {};
        double target = 1000.0, glide = 1000.0, crossA = 1000.0, crossB = 1000.0;
        float crossPos = 0.0f, modOffset = 0.0f;
        bool crossing = false;
        int sliceLeft = 0, freezeLength = 0;
        // per channel because they follow the time
        float bbdK = 1.0f, bbdInc = 1.0f;
    };

    struct Frame
    {
        std::array<float, 2> y {}, wet {}, rec {};
    };

    static bool usesGlide (int t) noexcept { return t == Tape || t == Bbd; }
    static BIDOO_DSP_INLINE float moveTowards (float x, float to, float step) noexcept
    {
        return x < to ? std::min (to, x + step) : std::max (to, x - step);
    }
    static BIDOO_DSP_INLINE float onePole (float& z, float x, float k) noexcept
    {
        z += k * (x - z);
        return z;
    }
    float coef (float hz) const noexcept
    {
        return 1.0f - std::exp (-6.2831853f * std::min (hz, 0.49f * (float) sr) / (float) sr);
    }

    // Transparent below 1 (5 V in VCV), then bends to a ceiling of 1.5.
    static BIDOO_DSP_INLINE float limit (float x) noexcept
    {
        const float a = std::fabs (x);
        if (a <= 1.0f)
            return x;
        const float y = 1.0f + 0.5f * tanhPade (std::min ((a - 1.0f) * 2.0f, 4.0f));
        return x < 0.0f ? -y : y;
    }
    // Bits from the top, towards zero: quiet tails end in silence, a limit cycle cannot form.
    static BIDOO_DSP_INLINE float truncate (float x, float q) noexcept { return std::trunc (x * q) / q; }

    // Mu-law (mu = 255) and back around the truncation, for Lofi: runs at its hold rate.
    static float crushLaw (float x, float q) noexcept
    {
        const float a = std::fmin (std::fabs (x), 1.2f);
        const float y = truncate (std::log1p (255.0f * a) * (1.0f / 5.5451774f), q);
        const float back = std::expm1 (y * 5.5451774f) * (1.0f / 255.0f);
        return x < 0.0f ? -back : back;
    }

    // The delay is counted from the input: a read before this sample's push is 1 sample late already.
    BIDOO_DSP_INLINE float readAt (const ModDelayLine& line, double delay) const noexcept
    {
        const double age = std::clamp (delay - 1.0, 2.0, (double) (line.maxDelay() - 8));
        const int i = (int) age;
        const float f = (float) (age - (double) i);
        if (f < 1.0e-4f)
            return line.at (i);
        float h[6];
        lagrange6Weights (f, h);
        float acc = 0.0f;
        for (int k = 0; k < 6; ++k)
            acc += h[k] * line.at (i + k - 2);
        return acc;
    }

    BIDOO_DSP_INLINE float readHermite (const ModDelayLine& line, double delay) const noexcept
    {
        const double age = std::clamp (delay - 1.0, 2.0, (double) (line.maxDelay() - 8));
        const int i = (int) age;
        const float f = (float) (age - (double) i);
        const float xm1 = line.at (i - 1), x0 = line.at (i), x1 = line.at (i + 1), x2 = line.at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    BIDOO_DSP_INLINE float readCross (const Channel& c) const noexcept
    {
        const float a = readAt (c.line, c.crossA);
        if (! c.crossing)
            return a;
        const float b = readAt (c.line, c.crossB);
        return a + c.crossPos * (b - a);
    }

    void updateTargets() noexcept
    {
        chans[0].target = settings.timeL * sr;
        chans[1].target = settings.timeR * sr;
        const float m = settings.mix;
        dryTarget = std::cos (m * kHalfPi);
        wetTarget = std::sin (m * kHalfPi);
    }

    // The heads move for every type: the one that isn't heard is ready when its type comes.
    BIDOO_DSP_INLINE void advanceHeads (Channel& c) noexcept
    {
        // Tape and Bbd: a slewed head, the read's speed held to a range so a big jump is a swoop, not a screech
        const double error = c.target - c.glide;
        if (std::fabs (error) < 1.0e-6)
            c.glide = c.target;
        else
            c.glide += std::clamp (error * glideCoef, -glideFall, glideRise);
        // Digital, Lofi, Diffuse and Reverse: two still heads and a crossfade
        if (c.crossing)
        {
            c.crossPos += crossStep;
            if (c.crossPos >= 1.0f)
            {
                c.crossA = c.crossB;
                c.crossing = false;
                c.crossPos = 0.0f;
            }
        }
        else if (std::fabs (c.target - c.crossA) > 0.25)
        {
            c.crossB = c.target;
            c.crossing = true;
            c.crossPos = 0.0f;
        }
    }

    void startType (int t) noexcept
    {
        typeOld = typeNew;
        typeNew = t;
        typeFade = 0.0f;
        for (auto& c : chans)
        {
            c.v[(size_t) t] = Voice {};
            if (usesGlide (t))
                c.glide = c.target; // no swoop from a head that wasn't heard
            if (t == Diffuse)
                for (auto& a : c.ap)
                    a.clear();
            if (t == Reverse)
            {
                for (auto& g : c.grains)
                    g = Grain {};
                c.sliceLeft = 0;
            }
        }
        control();
    }

    // Everything that takes logs and exponentials, every kControl samples.
    void control() noexcept
    {
        charSmoothed += 0.05f * (settings.character - charSmoothed);
        const float ch = charSmoothed;

        // TONE: a low pass below the centre, the DC blocker rises above it
        const float t = settings.tone;
        const float lpTarget = t < 0.0f ? coef (22000.0f * std::pow (400.0f / 22000.0f, -t)) : 1.0f;
        const float hpTarget = coef (5.0f * std::pow (400.0f, std::max (t, 0.0f)));
        toneLpK = toneLpK < 0.0f ? lpTarget : toneLpK + 0.2f * (lpTarget - toneLpK);
        toneHpK = toneHpK < 0.0f ? hpTarget : toneHpK + 0.2f * (hpTarget - toneHpK);

        // the heads follow the type that is coming in
        const float rate = (float) sr;
        if (typeNew == Tape)
        {
            glideCoef = 1.0 / (0.25 * sr);
            glideRise = 0.75;
            glideFall = 2.0;
        }
        else
        {
            glideCoef = 1.0 / (0.06 * sr);
            glideRise = 0.9;
            glideFall = 4.0;
        }
        crossStep = 1.0f / ((typeNew == Lofi ? 0.005f : 0.03f) * rate);

        // Digital: nothing at all at 0
        digitalOn = ch > 0.002f;
        digitalK = digitalOn ? coef (18000.0f * std::pow (7000.0f / 18000.0f, ch)) : 1.0f;
        digitalQ = std::exp2 ((22.0f - 11.0f * std::sqrt (ch)) - 1.0f);
        digitalInc = digitalOn ? std::min (1.0f, 44100.0f * std::pow (20000.0f / 44100.0f, ch) / rate) : 1.0f;

        // Tape: new at 0, worn at 1
        tapeDrive = 1.15f + 1.85f * ch;
        tapeLossK = coef (24000.0f * std::pow (5000.0f / 24000.0f, ch));
        tapeHiss = 1.5e-4f + 3.0e-3f * ch * ch;
        tapePreK = coef (3000.0f);
        tapeDeK = coef (1500.0f); // the exact inverse of a +6 dB shelf at 3 kHz has its pole an octave down
        tapeHpK = coef (35.0f);
        tapeDropChance = ch > 0.35f ? (ch - 0.35f) * (ch - 0.35f) * 6.0f * (float) kControl / rate : 0.0f;

        // Bbd: more stages are cleaner
        const float stages = 16384.0f * std::pow (2048.0f / 16384.0f, ch);
        for (auto& c : chans)
        {
            const float seconds = (float) (c.glide / sr);
            const float clock = stages / (2.0f * std::max (seconds, 0.001f));
            c.bbdK = coef (std::clamp (clock / 1.6f, 900.0f, 24000.0f));
            c.bbdInc = std::min (1.0f, std::max (clock, 1500.0f) / rate);
        }
        bbdDrive = 1.0f + 1.4f * ch;
        bbdNoise = 3.0e-4f + 4.0e-3f * ch;
        bbdLeak = 1.2e-3f * ch;
        bbdHpK = coef (50.0f);

        // Lofi
        lofiInc = std::min (1.0f, 32000.0f * std::pow (4000.0f / 32000.0f, ch) / rate);
        lofiQ = std::exp2 ((12.0f - 8.0f * ch) - 1.0f);
        lofiK = coef (std::min (0.5f * 32000.0f * std::pow (4000.0f / 32000.0f, ch), 15000.0f));

        // Diffuse: the all-passes fade in over the first quarter, then get denser
        diffuseBlend = std::min (ch * 4.0f, 1.0f);
        diffuseG = 0.35f + 0.4f * ch;
        diffuseK = coef (15000.0f * std::pow (6500.0f / 15000.0f, ch));
        diffuseCross = 0.25f * ch;
        if (typeOld == Diffuse || typeNew == Diffuse)
        {
            lfoPhase += (double) kControl / sr * 0.31;
            if (lfoPhase >= 1.0)
                lfoPhase -= 1.0;
            const float depth = 0.0003f * rate;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < kAllpasses; ++i)
                {
                    const float phase = 6.2831853f * (float) lfoPhase * (1.0f + 0.27f * (float) i) + 1.7f * (float) i + 2.3f * (float) c;
                    chans[(size_t) c].apDelay[(size_t) i] = chans[(size_t) c].apBase[(size_t) i] + depth * std::sin (phase);
                }
        }

        reverseForward = ch;

        // the transport follows the type (depths in cents, they glide on their own)
        const bool tape = typeNew == Tape;
        const float wowDepth = tape ? 2.0f + 30.0f * ch * std::sqrt (ch) : (typeNew == Bbd ? 3.0f + 5.0f * ch : 0.0f);
        const float wowHz = tape ? 0.6f : 0.35f;
        const float flutterDepth = tape ? 1.0f + 11.0f * ch : 0.0f;
        if (wowDepth != wowSet || wowHz != wowHzSet)
        {
            wowSet = wowDepth;
            wowHzSet = wowHz;
            wow.setWow (wowDepth, wowHz);
        }
        if (flutterDepth != flutterSet)
        {
            flutterSet = flutterDepth;
            flutter[0].setFlutter (flutterDepth);
            flutter[1].setFlutter (flutterDepth);
        }
    }

    // One type's two channels: read sides, then record sides (ping-pong needs the other side's read).
    BIDOO_DSP_INLINE void runType (int t, const std::array<float, 2>& routed) noexcept
    {
        auto& f = frames[(size_t) t];
        for (int ch = 0; ch < 2; ++ch)
            f.y[(size_t) ch] = applyTone (chans[(size_t) ch].v[(size_t) t], readSide (t, ch), t != Reverse);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& c = chans[(size_t) ch];
            auto& v = c.v[(size_t) t];
            const float own = f.y[(size_t) ch], other = f.y[(size_t) (1 - ch)];
            float fbSource = own + pingMix * (other - own);
            if (t == Diffuse)
                fbSource += diffuseCross * (1.0f - pingMix) * (other - own);
            const float loop = fbSmoothed * fbSource;
            if (t == Reverse)
            {
                // the slices go straight out, the line only repeats them
                const float w = limit (applyTone (v, reverseSlices (c) + loop, true));
                f.wet[(size_t) ch] = freezeMix > 0.0f ? w + freezeMix * (own - w) : w;
                f.rec[(size_t) ch] = w;
            }
            else
            {
                f.wet[(size_t) ch] = own;
                f.rec[(size_t) ch] = recordSide (t, c, v, routed[(size_t) ch] + loop);
            }
        }
    }

    BIDOO_DSP_INLINE float applyTone (Voice& v, float x, bool on) noexcept
    {
        if (! on)
            return x;
        const float lp = toneLpK >= 1.0f ? x : onePole (v.toneLp, x, toneLpK);
        return lp - onePole (v.toneHp, lp, toneHpK);
    }

    BIDOO_DSP_INLINE float readSide (int t, int ch) noexcept
    {
        auto& c = chans[(size_t) ch];
        auto& v = c.v[(size_t) t];
        switch (t)
        {
            case Tape:
            {
                float y = readHermite (c.line, c.glide + (double) c.modOffset);
                y += tapeHiss * std::min (1.0f, v.env * 30.0f) * (2.0f * rng.uniform() - 1.0f);
                if (tick == kControl && --v.dropLeft <= 0)
                {
                    v.dropTarget = 1.0f;
                    if (rng.uniform() < tapeDropChance)
                    {
                        v.dropTarget = 0.35f + 0.5f * rng.uniform();
                        v.dropLeft = 1 + (int) (rng.uniform() * 0.05f * (float) sr / (float) kControl);
                    }
                }
                v.drop += 0.004f * (v.dropTarget - v.drop);
                y *= v.drop;
                y = onePole (v.lp2, onePole (v.lp1, y, tapeLossK), tapeLossK);
                y = 0.5f * y + 0.5f * onePole (v.de, y, tapeDeK); // the record side's emphasis undone
                return y - onePole (v.hp, y, tapeHpK);
            }
            case Bbd:
            {
                float y = readHermite (c.line, c.glide + (double) c.modOffset);
                y += bbdNoise * std::min (1.0f, v.env * 8.0f) * (2.0f * rng.uniform() - 1.0f);
                if (bbdLeak > 0.0f)
                {
                    if (v.holdPhase + c.bbdInc >= 1.0f)
                        v.leak = -v.leak;
                    y += bbdLeak * std::min (1.0f, v.env * 8.0f) * v.leak;
                }
                y = onePole (v.lp2, onePole (v.lp1, y, c.bbdK), c.bbdK);
                return y - onePole (v.hp, y, bbdHpK);
            }
            case Lofi:
                return onePole (v.lp1, readCross (c), lofiK);
            case Diffuse:
            {
                const float x = onePole (v.lp1, readCross (c), diffuseK);
                float y = x;
                for (int i = 0; i < kAllpasses; ++i)
                {
                    auto& a = c.ap[(size_t) i];
                    const float d = a.readHermite (c.apDelay[(size_t) i] - 1.0f);
                    const float u = y + diffuseG * d;
                    a.push (u);
                    y = d - diffuseG * u;
                }
                return x + diffuseBlend * (y - x);
            }
            case Reverse:
                return readCross (c);
            case Digital:
            default:
            {
                const float y = readCross (c);
                return digitalOn ? onePole (v.lp2, onePole (v.lp1, y, digitalK), digitalK) : y;
            }
        }
    }

    BIDOO_DSP_INLINE float recordSide (int t, Channel& c, Voice& v, float x) noexcept
    {
        v.env = std::max (std::fabs (x), v.env * envRelease);
        switch (t)
        {
            case Tape:
            {
                // the top is pushed up before the saturation and brought back after the head:
                // high frequencies saturate first, as on tape
                const float pushed = x + (x - onePole (v.rec1, x, tapePreK));
                return (float) adaa (v.sat, (double) (pushed * tapeDrive), 1, AdaaTanhTable<64> {}) / tapeDrive;
            }
            case Bbd:
            {
                const float filtered = onePole (v.rec2, onePole (v.rec1, x, c.bbdK), c.bbdK);
                const float driven = (float) adaa (v.sat, (double) (filtered * bbdDrive), 1, AdaaTanhTable<64> {}) / bbdDrive;
                v.holdPhase += c.bbdInc;
                if (v.holdPhase >= 1.0f)
                {
                    v.holdPhase -= 1.0f;
                    v.hold = driven;
                }
                return v.hold;
            }
            case Lofi:
                v.holdPhase += lofiInc;
                if (v.holdPhase >= 1.0f)
                {
                    v.holdPhase -= 1.0f;
                    v.hold = crushLaw (x, lofiQ);
                }
                return v.hold;
            case Digital:
                if (digitalOn)
                {
                    v.holdPhase += digitalInc;
                    if (v.holdPhase >= 1.0f)
                    {
                        v.holdPhase -= 1.0f;
                        v.hold = truncate (limit (x), digitalQ);
                    }
                    return v.hold;
                }
                return limit (x);
            case Diffuse:
            default:
                return limit (x);
        }
    }

    // A slice is the last `length` samples, played from the newest back (age 2n + 1: the read moves
    // back while the writing moves on), overlapping the next slice by its fade, so the slices repeat
    // exactly every `length` samples. Whole-sample reads, nothing to interpolate.
    BIDOO_DSP_INLINE float reverseSlices (Channel& c) noexcept
    {
        if (--c.sliceLeft <= 0)
        {
            const int length = (int) std::max (2.0, std::round (c.target));
            const int fade = std::clamp ((int) std::min ((double) length / 4.0, kSliceFadeSeconds * sr), 1, length);
            Grain* slot = nullptr;
            for (auto& g : c.grains)
            {
                if (g.on && g.stopAt > g.n)
                    g.stopAt = g.n; // the running slice starts its fade from here
                if (! g.on)
                    slot = &g;
            }
            if (slot == nullptr) // every slot fading: the one nearest its end makes way
            {
                slot = &c.grains[0];
                for (auto& g : c.grains)
                    if (g.n - g.stopAt > slot->n - slot->stopAt)
                        slot = &g;
            }
            *slot = Grain { 0, length, fade, length, true, rng.uniform() < reverseForward };
            c.sliceLeft = length;
        }
        float y = 0.0f;
        for (auto& g : c.grains)
        {
            if (! g.on)
                continue;
            const int age = g.forward ? g.length : 2 * g.n + 1;
            float gain = 1.0f;
            if (g.n < g.fade)
                gain = std::sin (kHalfPi * (float) g.n / (float) g.fade);
            if (g.n >= g.stopAt)
                gain *= std::cos (kHalfPi * (float) (g.n - g.stopAt) / (float) g.fade);
            y += gain * c.input.at (age);
            if (++g.n >= g.stopAt + g.fade)
                g.on = false;
        }
        return y;
    }

    double sr = 48000.0;
    Settings settings;
    std::array<Channel, 2> chans;
    std::array<Frame, kNumTypes> frames {};
    TapeWowFlutter wow, flutter[2];
    Rng rng;
    std::array<float, 2> wet {};

    int typeOld = Digital, typeNew = Digital, tick = 0;
    float typeFade = 1.0f, typeFadeStep = 0.001f, freezeMix = 0.0f, freezeStep = 0.001f;
    float pingMix = 0.0f, pingStep = 0.001f, smooth = 0.001f, envRelease = 0.9999f;
    float fbSmoothed = 0.0f, drySmoothed = 1.0f, wetSmoothed = 0.0f, dryTarget = 1.0f, wetTarget = 0.0f, charSmoothed = 0.0f;
    float toneLpK = 1.0f, toneHpK = 0.001f, crossStep = 0.001f;
    double glideCoef = 0.001, glideRise = 0.75, glideFall = 2.0, lfoPhase = 0.0;

    bool digitalOn = false;
    float digitalK = 1.0f, digitalQ = 1.0f, digitalInc = 1.0f;
    float tapeDrive = 1.0f, tapeLossK = 1.0f, tapeHiss = 0.0f, tapePreK = 0.1f, tapeDeK = 0.1f, tapeHpK = 0.01f, tapeDropChance = 0.0f;
    float bbdDrive = 1.0f, bbdNoise = 0.0f, bbdLeak = 0.0f, bbdHpK = 0.01f;
    float lofiInc = 1.0f, lofiQ = 1.0f, lofiK = 1.0f;
    float diffuseBlend = 0.0f, diffuseG = 0.35f, diffuseK = 1.0f, diffuseCross = 0.0f;
    float reverseForward = 0.0f;
    float wowSet = -1.0f, wowHzSet = -1.0f, flutterSet = -1.0f;

    // the idle skip
    bool idleSkip = true, idle = false;
    int lineQuiet = 0, inputQuiet = 0;
    int lineHold = 1, inputHold = 1; // more samples than the line / the input buffer can be read back
};
} // namespace bidoo::dsp

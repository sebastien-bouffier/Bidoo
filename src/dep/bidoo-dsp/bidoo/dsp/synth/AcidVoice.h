#pragma once

// A mono acid bass voice in the style of the classic silver box (TB-303): oscillator, diode ladder, the two envelopes,
// accent with its sweep circuit, slide, VCA. No sequencer: pitch, gate, accent and slide come in per sample. Written from
// published descriptions, no code from anyone; nothing was compared to a real unit.
//
// Sources, and what comes from where:
//  - Tim Stinchcombe, Diode ladder filters: the filter (DiodeLadder.h), and the feedback at the top of RES.
//  - Robin Whittle, Devil Fish manual and "303 unique" page (firstpr.com.au): MEG 200 ms to 2 s, 200 ms on accents
//    whatever DECAY says; VEG about 3 to 4 s; about 4 ms before the note starts, 3 ms attack; an unaccented note ends with
//    8 ms of normal level and 8 ms of linear decay (the note's click); slide 100 k into 0.22 uF ("60 ms"); the accent
//    sweep circuit's parts (diode and 47 k, 100 k pot, the Resonance pot's second section, 1 uF, 100 k into the summing
//    node) and the VCA's accent path (47 k, 0.033 uF); cutoff typically up to about 2.5 kHz; Env Mod never reaches zero,
//    and its top is a third of the Devil Fish's.
//  - KVR, Open303 thread, measurements by antto and Mike Janney: the square is a saw through a one-transistor shaper, a
//    high-pass on the saw first (about 54 Hz), a pulse width of 86 to 98 % (of a true square, depending on the unit), a
//    gain of 60 in a fit, mixed with some of the saw; filter CV about 200 Hz to 25 kHz with the envelope up.
//  - mystran, KVR: limited gain makes the mid-saw transition slower than the reset edge.
//  - The VCF303 clone's manual: at rest ENV MOD gives an offset on the cutoff.
// Values no source gives, chosen by ear and named kXxx below with "by ear": see the constants.
//
// Real-time safe; prepare() allocates nothing but builds the BLEP tables once (first use only).

#include "../core/Inline.h"
#include "../filters/DiodeLadder.h"
#include "../oversampling/IirHalfBand.h"
#include "../shaping/TanhPade.h"
#include "KaiserBlep.h"
#include <algorithm>
#include <cmath>

namespace bidoo::dsp
{
struct AcidVoiceConfig
{
    static constexpr double kSlideTau = 0.022;      // 100 k x 0.22 uF, Whittle; about 3 of them make his "60 ms"
    static constexpr double kMegAttackTau = 0.0003; // "sharp attack"; by ear
    // A decay "time" here is when the envelope is down by 26 dB (3 time constants), where a sweep of a few octaves has
    // about stopped being heard as moving: no source says what its figures measure to. By ear.
    static constexpr double kTimeToTau = 1.0 / 3.0;
    static constexpr double kAccentDecay = 0.2;     // Whittle
    static constexpr double kVegDecay = 3.5;        // "~ 3 to 4 seconds", Whittle
    static constexpr double kVegDelay = 0.004;      // Whittle, typical
    static constexpr double kVegAttack = 0.003;     // Whittle
    static constexpr double kVegHold = 0.008;       // Whittle: 8 ms at normal level
    static constexpr double kVegRamp = 0.008;       // and 8 ms linear
    static constexpr double kRampUp = 0.001;        // a tied note cancelling a release
    static constexpr double kAccentVcaTau = 47.0e3 * 0.033e-6; // Whittle's RC

    // Cutoff: summing in octaves (the expo converter). Depth of ENV MOD at its two ends: from Whittle's "never zero" and
    // Janney's top end (2.5 kHz up to about 25 kHz); the bottom is by ear.
    static constexpr double kEnvModMin = 0.7;       // octaves, by ear
    static constexpr double kEnvModMax = 3.4;       // octaves: 2.5 kHz to about 25 kHz
    static constexpr double kEnvModRest = -1.0;     // octaves at full ENV MOD with the MEG at rest; direction and size by ear
    static constexpr double kSweepOctaves = 6.0;    // the sweep circuit's full voltage, by ear
    static constexpr double kSweepDiode = 0.1;      // the diode's drop, against a MEG of 1, by ear
    static constexpr double kAccentVcaGain = 0.5;   // accent at full and MEG at 1 adds this to a VEG of 1: by ear
    // The loop's gain against RES. A straight line up to the knee, under the ideal core's oscillation at 17, then a curve
    // leaving it with the same slope and ending on Stinchcombe's k = 1, 18.7 (feedbackForResonance). So the lower travel is
    // what it always was and only the top goes on: there the filter can self-oscillate at high cutoffs, which Whittle and
    // Janney say a stock unit does not do (the user chose Stinchcombe's value by ear).
    static constexpr double kFeedbackLine = 16.9;
    static constexpr double kFeedbackKnee = 0.85;
    static constexpr double kFeedbackTop = 18.7;
    static constexpr double feedbackForResonance (double r01) noexcept
    {
        const double x = (r01 - kFeedbackKnee) / (1.0 - kFeedbackKnee);
        return r01 <= kFeedbackKnee ? kFeedbackLine * r01 : kFeedbackLine * r01 + (kFeedbackTop - kFeedbackLine) * x * x;
    }
    static constexpr double kMaxHz = 20000.0;       // Whittle limits his to about this
    // The diodes (by ear; nothing published on the ladder's level)
    static constexpr double kBend = 0.9;
    static constexpr double kAsymmetry = 0.15;
    // The square (antto's fit, what it needs of it)
    static constexpr double kSquareHpHz = 54.0;
    // A fixed high-pass eats a long ramp: below 1 kHz the threshold would ride the saw and the pulse narrow with the note
    // (width 54 % at 33 Hz). The corner stays at this ratio of the pitch once the pitch has fallen to it, which holds the
    // shape, and the width, that the square has at C6. Above that nothing changes.
    static constexpr double kSquareHpRatio = 54.0 / 1046.5;
    static constexpr double kSquareGain = 60.0;
    static constexpr double kSquareThreshold = 0.08; // a pulse width of 92 %, between antto's two units
    static constexpr double kSquareSaw = 0.1;        // how much saw stays in it: "mixed", no number; by ear
    static constexpr double kSquareLevel = 1.0;
    // The saw: a capacitor charging, not a straight line: ear, the circuit does not say how much
    static constexpr double kSawCurve = 0.12;
    // The output: +-1 is +-5 V in the module. The ladder's level falls by about 1 + K above its coupling corner, so that
    // an unmade-up voice is 25 dB quieter from RES 30 % up than at RES 0. A voice in a patch needs one level: the output
    // gain follows RES by (1 + K)^0.8. Not from any source, and it does not touch the filter's shape, only its level.
    static constexpr double kOutputGain = 1.0;
    static constexpr double kLevelMakeup = 0.8;
};

class AcidVoice
{
public:
    using Cfg = AcidVoiceConfig;

    // oversample 0: what the measurements asked for (2 x up to 50 kHz, none from there).
    void prepare (double sampleRate, int oversample = 0) noexcept
    {
        fs = sampleRate;
        os = oversample > 0 ? oversample : (sampleRate <= 50000.0 ? 2 : 1);
        fsOs = fs * os;
        ladder.prepare (fsOs);
        ladder.setNonlinearity (Cfg::kBend, Cfg::kAsymmetry);
        ctrlSamples = std::max (1, (int) std::lround (fs / 6000.0));
        ctrlPeriod = ctrlSamples / fs;
        cSlide = 1.0 - std::exp (-1.0 / (Cfg::kSlideTau * fs));
        cMegAttack = 1.0 - std::exp (-1.0 / (Cfg::kMegAttackTau * fs));
        cAccentDecay = decayCoef (Cfg::kAccentDecay);
        cNormalDecay = decayCoef (1.0);
        cVegDecay = decayCoef (Cfg::kVegDecay);
        cAccentVca = 1.0 - std::exp (-1.0 / (Cfg::kAccentVcaTau * fs));
        vegDelaySamples = (int) std::lround (Cfg::kVegDelay * fs);
        vegAttackStep = 1.0 / (Cfg::kVegAttack * fs);
        relHoldSamples = (int) std::lround (Cfg::kVegHold * fs);
        relDownStep = 1.0 / (Cfg::kVegRamp * fs);
        relUpStep = 1.0 / (Cfg::kRampUp * fs);
        windowSamples = (int) std::lround (0.001 * fs);
        tieSamples = (int) std::lround (fs);
        cLpTop = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * Cfg::kSquareHpHz / fsOs);
        cLp = cLpTop;
        waveStep = 1.0 / (0.003 * fsOs);
        reset();
    }

    void reset() noexcept
    {
        ladder.reset();
        blep.reset();
        down2 = {};
        down4 = {};
        phase = 0.0;
        lp = 0.0;
        wave = waveTarget;
        gatePrev = effPrev = slidePrev = false;
        tieCount = 0;
        accentLatched = false;
        window = 0;
        meg = 0.0;
        megAttack = false;
        veg = 0.0;
        vegStage = VegOff;
        vegCount = 0;
        rel = 1.0;
        relCount = 0;
        releasing = false;
        vcaAccent = 0.0;
        sweepCap = sweepOut = 0.0;
        tick = 0;
        lastVolts = 1.0e9;
        slewVolts = 0.0;
        dt = 0.001;
        tickControl (0.0);
        g = gTarget;
        K = kTarget;
        gStep = kStep = 0.0;
    }

    // ---- settings, read at the control rate
    void setCutoff (double hz) noexcept { cutoffHz = hz; }       // the resonant frequency, envelope aside
    void setResonance (double r01) noexcept { res = clamp01 (r01); }
    void setEnvMod (double e01) noexcept { envMod = clamp01 (e01); }
    void setDecay (double seconds) noexcept
    {
        cNormalDecay = decayCoef (seconds);
    }
    void setAccent (double a01) noexcept { accentAmount = clamp01 (a01); }
    void setSquare (bool square) noexcept { waveTarget = square ? 1.0 : 0.0; }
    void setTune (double volts) noexcept { tune = volts; lastVolts = 1.0e9; }

    // ---- what a test or a screen may want to see
    double megValue() const noexcept { return meg; }
    double vegValue() const noexcept { return veg * rel; }
    double sweepValue() const noexcept { return sweepOut; }
    double resonantHz() const noexcept { return lastHz; }
    double pitchVolts() const noexcept { return slewVolts; }
    bool accentOn() const noexcept { return accentLatched; }
    int oversampling() const noexcept { return os; }
    float oscillatorValue() const noexcept { return lastOsc; }

    // One sample: volts per octave (0 V = C4), gate, accent, slide. Returns about +-1 at full level.
    BIDOO_DSP_INLINE float process (float pitch, bool gate, bool accent, bool slide) noexcept
    {
        notes (gate, accent, slide);

        // pitch: straight while nothing is sounding on, through the RC while a note is held
        const bool held = effPrev;
        if (held && window == 0)
            slewVolts += cSlide * ((double) pitch - slewVolts);
        else
            slewVolts = (double) pitch;
        if (slewVolts != lastVolts)
        {
            lastVolts = slewVolts;
            const double hz = 261.6255653005986 * std::exp2 (slewVolts + tune);
            dt = std::min (hz, 0.48 * fsOs) / fsOs;
            const double corner = 2.0 * 3.14159265358979323846 * Cfg::kSquareHpRatio * hz / fsOs;
            cLp = hz * Cfg::kSquareHpRatio >= Cfg::kSquareHpHz ? cLpTop : corner * (1.0 - 0.5 * corner);
        }

        // the envelopes
        if (megAttack)
        {
            meg += cMegAttack * (1.0 - meg);
            if (meg >= 0.98)
                megAttack = false;
        }
        else
        {
            meg *= accentLatched ? cAccentDecay : cNormalDecay;
        }
        vcaAccent += cAccentVca * ((accentLatched ? accentAmount * meg : 0.0) - vcaAccent);
        stepVeg();

        if (--tick <= 0)
        {
            tick = ctrlSamples;
            tickControl (ctrlPeriod);
        }

        // oscillator and filter, at the oversampled rate
        float o0 = 0.f, o1 = 0.f, o2 = 0.f, o3 = 0.f;
        double y;
        if (os == 1)
        {
            y = osSample();
        }
        else
        {
            o0 = (float) osSample();
            o1 = (float) osSample();
            if (os == 2)
            {
                y = (double) down2.down (Coefs8::c, o0, o1);
            }
            else
            {
                o2 = (float) osSample();
                o3 = (float) osSample();
                y = (double) down2.down (Coefs8::c, down4.down (Coefs4::c, o0, o1), down4.down (Coefs4::c, o2, o3));
            }
        }

        const double gain = veg * rel + Cfg::kAccentVcaGain * vcaAccent;
        outGain += outStep;
        const float out = (float) (outGain * y * gain);
        // A NaN or an infinite pitch stays in the slide's RC and the phase (and NaN settings in the
        // ladder): the voice goes back to silence by itself, as reset() leaves it, and plays on from
        // the next sample. The output carries every other state; the pitch and phase are added because
        // an infinite pitch is first clamped by the oscillator. Finite audio is untouched.
        if (! std::isfinite ((double) out + slewVolts + phase))
            return resetToSilence();
        return out;
    }

    // Always true since process() recovers by itself; kept for the callers that still check.
    bool isFinite() const noexcept { return ladder.isFinite() && std::isfinite (meg) && std::isfinite (phase); }

private:
    using Coefs8 = IirHalfBandCoefs<8, 40>;
    using Coefs4 = IirHalfBandCoefs<4, 130>;
    enum VegStage { VegOff, VegDelay, VegAttack, VegDecay };

    BIDOO_DSP_NOINLINE float resetToSilence() noexcept
    {
        reset();
        return 0.f;
    }

    static double clamp01 (double x) noexcept { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }
    double decayCoef (double seconds) const noexcept { return std::exp (-1.0 / (seconds * Cfg::kTimeToTau * fs)); }

    BIDOO_DSP_INLINE void notes (bool gate, bool accent, bool slide) noexcept
    {
        const bool rise = gate && ! gatePrev;
        if (rise)
        {
            // Held through by SLIDE, or SLIDE up while the last note is still sounding: tied, nothing retriggers.
            const bool tied = effPrev || (slide && vegStage != VegOff);
            accentLatched = accent;
            if (tied)
            {
                releasing = false;
                effPrev = true;
            }
            else
            {
                meg = std::min (meg, 1.0);
                megAttack = true;
                vegStage = vegDelaySamples > 0 ? VegDelay : VegAttack;
                vegCount = vegDelaySamples;
                releasing = false;
                window = windowSamples;
            }
        }
        else if (window > 0)
        {
            // Whatever the sequencer sets a sample or so after the gate still belongs to this note.
            --window;
            if (accent)
                accentLatched = true;
        }
        // SLIDE up when the gate falls ties this note to the next, as a slide flag does on its step: the note is held until
        // the next gate (a second at most). A sequencer may drop SLIDE on the very sample the gate falls: the last sample's
        // value counts.
        if (gatePrev && ! gate && (slide || slidePrev))
            tieCount = tieSamples;
        else if (rise)
            tieCount = 0;
        else if (tieCount > 0)
            --tieCount;
        slidePrev = slide;
        const bool eff = gate || ((slide || tieCount > 0) && effPrev);
        if (effPrev && ! eff)
        {
            releasing = true;
            relCount = relHoldSamples;
        }
        gatePrev = gate;
        effPrev = eff;
    }

    BIDOO_DSP_INLINE void stepVeg() noexcept
    {
        switch (vegStage)
        {
            case VegOff:
                veg = 0.0;
                break;
            case VegDelay:
                veg *= cVegDecay;
                if (--vegCount <= 0)
                    vegStage = VegAttack;
                break;
            case VegAttack:
                veg += vegAttackStep;
                if (veg >= 1.0)
                {
                    veg = 1.0;
                    vegStage = VegDecay;
                }
                break;
            case VegDecay:
                veg *= cVegDecay;
                break;
        }
        if (releasing)
        {
            if (relCount > 0)
            {
                --relCount;
            }
            else
            {
                rel -= relDownStep;
                if (rel <= 0.0)
                {
                    rel = 0.0;
                    releasing = false;
                    vegStage = VegOff;
                    veg = 0.0;
                }
            }
        }
        else if (rel < 1.0)
        {
            rel = std::min (1.0, rel + (vegStage == VegOff ? 1.0 : relUpStep));
        }
    }

    // The sweep circuit (Whittle): MEG x ACCENT through a diode and 47 k onto one end of the 100 k pot; the other end has
    // 1 uF to ground; the wiper feeds the summing node through 100 k. At RES 0 the wiper sits on the driven end and the
    // pulse goes straight through, at RES 1 on the capacitor, which charges through 147 k with the diode on, drains through
    // the 100 k alone, and is not empty by the next accent: that is the build-up.
    void stepSweep (double period) noexcept
    {
        const double drive = (accentLatched ? accentAmount * meg : 0.0) - Cfg::kSweepDiode;
        constexpr double r47 = 47.0, rMix = 100.0, c1 = 1.0; // kilo-ohms and microfarads: milliseconds
        const double ra = std::max (res, 0.002) * 100.0, rb = std::max (1.0 - res, 0.002) * 100.0;
        // With the diode off the wiper only sees the capacitor through rb and the mix resistor
        const double offA = (1.0 / rb) / (1.0 / rb + 1.0 / rMix);
        double aW = offA, bW = 0.0;
        if (drive > aW * sweepCap)
        {
            // Diode on: 47 k and the pot's upper part in series from the drive to the wiper
            const double gIn = 1.0 / (r47 + ra); // the series path from the drive to the wiper
            const double den = gIn + 1.0 / rb + 1.0 / rMix;
            aW = (1.0 / rb) / den;
            bW = gIn * drive / den;
        }
        const double tau = rb * c1 * 0.001;                 // seconds
        const double target = bW / std::max (1.0 - aW, 1.0e-9);
        const double e = std::exp (-(1.0 - aW) * period / tau);
        if (period > 0.0)
            sweepCap = target + (sweepCap - target) * e;
        sweepOut = aW * sweepCap + bW;
    }

    void tickControl (double period) noexcept
    {
        stepSweep (period);
        const double depth = Cfg::kEnvModMin + (Cfg::kEnvModMax - Cfg::kEnvModMin) * envMod;
        const double octaves = Cfg::kEnvModRest * envMod + depth * meg + Cfg::kSweepOctaves * sweepOut;
        const double hz = std::clamp (cutoffHz * std::exp2 (octaves), 10.0, std::min (Cfg::kMaxHz, 0.45 * fsOs));
        lastHz = hz;
        gTarget = DiodeLadder::gainForResonance (hz, fsOs);
        kTarget = Cfg::feedbackForResonance (res);
        const double outTarget = Cfg::kOutputGain * std::pow (1.0 + kTarget, Cfg::kLevelMakeup);
        outStep = period > 0.0 ? (outTarget - outGain) / (double) ctrlSamples : 0.0;
        if (period <= 0.0)
            outGain = outTarget;
        if (period > 0.0)
        {
            const double n = (double) (ctrlSamples * os);
            gStep = (gTarget - g) / n;
            kStep = (kTarget - K) / n;
        }
    }

    // One oversampled sample of oscillator and ladder.
    BIDOO_DSP_INLINE double osSample() noexcept
    {
        g += gStep;
        K += kStep;
        blep.advance();

        if (wave != waveTarget)
        {
            wave = waveTarget > wave ? std::min (waveTarget, wave + waveStep) : std::max (waveTarget, wave - waveStep);
        }
        const float sawShare = (float) (1.0 - wave + wave * Cfg::kSquareSaw);
        const float squareShare = (float) (wave * (1.0 - Cfg::kSquareSaw) * Cfg::kSquareLevel);
        // The shaper's slope falls with pitch so that its transition never gets under 2.5 oversampled samples.
        const float shapeGain = wave > 0.0 ? (float) std::min (Cfg::kSquareGain, 0.44 / dt) : 0.f;
        const float thr = (float) Cfg::kSquareThreshold + (float) lp;

        const double before = phase + dt;
        if (before >= 1.0)
        {
            phase = before - 1.0;
            const float d = (float) (phase / dt); // samples since the reset
            float h = -2.f * sawShare;
            if (squareShare > 0.f)
                h += squareShare * (tanhPade (shapeGain * (-1.f - thr)) - tanhPade (shapeGain * (1.f - thr)));
            blep.step (h, d);
            blep.ramp ((float) (sawShare * 4.0 * Cfg::kSawCurve * dt), d);
        }
        else
        {
            phase = before;
        }

        const double p = phase;
        const float ramp = (float) (2.0 * (p + Cfg::kSawCurve * p * (1.0 - p)) - 1.0);
        float naive = sawShare * ramp;
        if (squareShare > 0.f)
            naive += squareShare * tanhPade (shapeGain * (ramp - thr));
        lp += cLp * ((double) ramp - lp);
        blep.push (naive);
        lastOsc = blep.pull();
        return ladder.process ((double) lastOsc, g, K);
    }

    DiodeLadder ladder;
    KaiserBlepCorrector blep;
    IirHalfBand<8> down2;
    IirHalfBand<4> down4;

    double fs = 48000.0, fsOs = 96000.0;
    int os = 2;
    // settings
    double cutoffHz = 800.0, res = 0.0, envMod = 0.5, accentAmount = 0.5, tune = 0.0;
    double waveTarget = 0.0, wave = 0.0, waveStep = 0.001;
    // oscillator
    double phase = 0.0, dt = 0.001, lp = 0.0, cLp = 0.0, cLpTop = 0.0;
    float lastOsc = 0.f;
    double slewVolts = 0.0, lastVolts = 1.0e9, cSlide = 0.0;
    // notes
    bool gatePrev = false, effPrev = false, accentLatched = false;
    int window = 0, windowSamples = 48;
    bool slidePrev = false;
    int tieCount = 0, tieSamples = 48000;
    // envelopes
    double meg = 0.0;
    bool megAttack = false;
    double cMegAttack = 0.0, cNormalDecay = 0.999, cAccentDecay = 0.999;
    double veg = 0.0, vegAttackStep = 0.0, cVegDecay = 0.9999;
    VegStage vegStage = VegOff;
    int vegCount = 0, vegDelaySamples = 0;
    double rel = 1.0, relDownStep = 0.0, relUpStep = 0.0;
    int relCount = 0, relHoldSamples = 0;
    bool releasing = false;
    double vcaAccent = 0.0, cAccentVca = 0.0;
    double sweepCap = 0.0, sweepOut = 0.0;
    // control rate
    int tick = 0, ctrlSamples = 8;
    double ctrlPeriod = 0.0;
    double g = 0.01, gTarget = 0.01, gStep = 0.0;
    double K = 0.0, kTarget = 0.0, kStep = 0.0;
    double lastHz = 0.0;
    double outGain = 1.0, outStep = 0.0;
};
} // namespace bidoo::dsp

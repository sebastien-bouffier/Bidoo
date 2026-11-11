#pragma once

// The engine: clock, step-accurate block splitting, the history buffer, the slot chain and the
// master section. Mangle's, moved here unchanged; cOPIA (Bidoo VCV) runs it on a captured loop.
//
// Every audio block is cut at step boundaries (and into pieces of at most 32 samples, the control
// rate), so a block in the pattern starts on its step whatever the host's buffer size. On each
// piece each slot gets one drive (0..1), Turnado's knob: from the pattern's gesture blocks, or from
// the live knob when a hand (or a controller, or automation) is on it. The drive moves the slot's
// parameters along their macro curves, the LFOs and the envelope follower are added on top, then
// the slots run in order, each one fading in and out over a few milliseconds.

#include "Effects.h"
#include "Pattern.h"
#include "Macros.h"
#include "../../core/Math.h"
#include "../../core/Smoothing.h"
#include "../../synth/ModPolarity.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bidoo::dsp::glitch
{
struct ModSettings
{
    int shape = 0;           // LFO shape (lfoShapeNames)
    double rateBeats = 1.0;  // LFO cycle length
    float depth = 0.0f;
    bool byDrive = false;    // depth scaled by the drive (the modulation grows as the knob is pushed)
};

// What one slot's parameters say for the coming block.
struct SlotSettings
{
    Fx fx = Fx::Off;
    float base[kNumTargets] {};    // where the targets' knobs are, 0..1 (P1..P5, frequency, mix)
    float amount[kNumTargets] {};  // macro amounts, -1..1
    Curve curve[kNumTargets] {};
    uint8_t polarity[kNumTargets][kNumMods] {}; // per assignment: 0 none, 1 UNI, 2 BI (Macros::Polarity)
    float depth[kNumTargets][kNumMods] {};      // per assignment, signed, -1..1 (1: the modulator's full swing)
    int filter = 0;
    float q = 0.707f, pan = 0.0f, gain = 1.0f;

    float knob = 0.0f;          // the live knob, 0..1
    bool knobInControl = false; // touched, held or moving: it punches in over the pattern
    ModSettings lfo[2];
    float envAttackMs = 5.0f, envReleaseMs = 120.0f, envDepth = 0.0f;
    bool envByDrive = false;
};

// The dry / wet laws (Turnado's five): the dry and wet gains for a mix of 0..1.
inline void lawGains (int law, float m, float& dry, float& wet)
{
    switch (law)
    {
        case 1:  dry = 1.0f - m; wet = m; break;                     // X-Fade
        case 2:  dry = 1.0f; wet = m; break;                         // Dry: the dry stays
        case 3:  dry = 1.0f - m; wet = 1.0f; break;                  // Wet: the wet stays
        case 4:  dry = 0.0f; wet = m; break;                         // Wet only
        default: dry = std::cos (m * Pi<float>::halfPi);
                 wet = std::sin (m * Pi<float>::halfPi); break; // Equal power
    }
}

// The engine protects itself from NaN and infinity (core/Math.h's zeroNonFinite, the plugins' block
// guard): what comes in (the input, the sidechain, cOPIA's live history: HistoryBuffer::write) is
// heard as silence where it is not finite, and a slot whose output goes non-finite starts again from
// silence. Each check is a block's sum, the samples scanned only when it is not finite, so finite
// audio goes through bit for bit and the cost is one add per sample.
inline bool blockIsFinite (const float* x, int n)
{
    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += x[i];
    return std::isfinite (sum);
}

inline bool blockIsFinite (const float* l, const float* r, int n)
{
    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += l[i] + r[i];
    return std::isfinite (sum);
}

// A gain that glides towards its target sample by sample (about 15 ms). Targets change only at
// control points, so the result is the same whatever the host's buffer size.
struct Smooth
{
    bidoo::dsp::IncrementalSmoother<float> s;
    float target = 0.0f;
    bool started = false;

    void prepare (double sr)
    {
        s.setCoefficient (bidoo::dsp::IncrementalSmoother<float>::coefficientForSamples (0.015 * sr));
        started = false;
    }

    void set (float t)
    {
        target = t;
        if (! started)
        {
            s.setCurrentValue (t);
            started = true;
        }
    }

    float next() { return s.next (target); }
};

// The control rate: the targets are worked out every 32 samples of the history's counter (and at
// every step), whatever the host's buffer size.
constexpr int kControl = 32;

// The slot's input, kept for the few milliseconds an effect with latency (Texture, Cross Mod, Resonator, Modal) is
// late, so that the slot's dry signal can be delayed by as much and the two stay in phase.
class DryDelay
{
public:
    void prepare (double sampleRate)
    {
        int size = 1;
        while (size < (int) (sampleRate * 0.02) + 2)
            size <<= 1;
        for (auto& b : buf)
            b.assign ((size_t) size, 0.0f);
        mask = size - 1;
        pos = 0;
    }

    void push (float l, float r)
    {
        buf[0][(size_t) pos] = l;
        buf[1][(size_t) pos] = r;
        pos = (pos + 1) & mask;
    }

    // `delay` samples ago (0: the one pushed last)
    void read (int delay, float& l, float& r) const
    {
        const auto at = (size_t) ((pos - 1 - jlimit (0, mask, delay)) & mask);
        l = buf[0][at];
        r = buf[1][at];
    }

private:
    std::vector<float> buf[2];
    int mask = 0, pos = 0;
};

// ---------------------------------------------------------------------------------------------
class Slot
{
public:
    static constexpr int kMaxPiece = 32;
    static constexpr float kKnobThreshold = 0.02f; // the live knob engages the slot above this

    void prepare (double sampleRate, int index)
    {
        sr = sampleRate;
        slot = index;
        fadeStep = (float) (1.0 / (0.004 * sampleRate)); // 4 ms fades in and out
        declickLen = jmax (16, (int) (0.003 * sampleRate));
        declickLeft = 0;
        retrigger = false;
        lastWet[0] = lastWet[1] = prevWet[0] = prevWet[1] = 0.0f;
        inFade = 1.0f;
        effect.prepare (sampleRate);
        dryDelay.prepare (sampleRate);
        filter.reset();
        fade = 0.0f;
        playing = false;
        lastFx = Fx::Off;
        dryLatency = -1;
        dryFadeLeft = 0;
        drive = target = env = 0.0f;
        sinceTrigger = 0;
        lastControl = 0;
        freqLog = -1.0f;
        for (auto* g : { &dryGain, &wetGain, &panL, &panR })
            g->prepare (sampleRate);
    }

    // The word for this piece: playing or not, a block (or the knob) starting here, and the drive.
    // True when the slot starts from silence.
    bool setLane (bool on, bool blockStart, float driveTarget, const SlotSettings& s, const Clock& c, int seed)
    {
        target = driveTarget;
        bool fromSilence = false;
        if (on && (blockStart || ! playing))
        {
            const bool continuing = fade > 0.0f;
            fromSilence = ! continuing;
            if (! continuing)
            {
                inFade = 0.0f; // the effect's input comes in with the slot (its delay lines start empty)
                filter.reset();
                drive = driveTarget;
                dryLatency = -1; // from silence the dry tap jumps straight to the effect's latency
            }
            sinceTrigger = 0;
            lastControl = c.counter;
            computeTargets (s, c, seed);
            effect.trigger (s.fx, p, c, continuing);
            retrigger = continuing; // a block on a block: the effect starts afresh while it sounds
            lastFx = s.fx;
        }
        playing = on;
        return fromSilence;
    }

    bool isSounding() const { return playing || fade > 0.0f; }
    const float* globals = nullptr;      // the global sources' values now, natural range (the engine's)
    const float* globalScales = nullptr; // and their depths
    float getDrive() const { return playing ? drive : 0.0f; }
    float getTarget (int t) const { return live[t]; }
    float getMod (int m) const { return modNow[m]; }            // modulator m now, in its natural range (-1..1 or 0..1)
    float getModScale (int m) const { return modScaleNow[m]; }  // and its depth (x the drive with By drive)
    float getLfoPhase (int m) const { return lfoPhaseNow[m]; }  // 0..1 through the cycle

    // `control`: a control point (every kControl samples of the history, and every step).
    // (sideL, sideR): the sidechain (the plugin's input without one), for the effects that take a second signal.
    void process (const SlotSettings& s, int law, int seed, const History& h, const Clock& c, float* l, float* r, int n, bool control,
                  const float* sideL = nullptr, const float* sideR = nullptr)
    {
        if (! playing && fade <= 0.0f)
        {
            follow (s, l, r, n); // the envelope follower listens all the time
            effect.listen (s.fx, s.base, l, r, n);
            for (int i = 0; i < n; ++i)
                dryDelay.push (l[i], r[i]);
            sinceTrigger += n;
            return;
        }

        if (control)
        {
            // the drive glides (about 8 ms); the targets follow it and the modulators
            const float elapsed = (float) (c.counter - lastControl);
            lastControl = c.counter;
            drive += (1.0f - std::exp (-elapsed / (float) (0.008 * sr))) * (target - drive);
            computeTargets (s, c, seed);
            if (s.fx != lastFx)
            {
                effect.trigger (s.fx, p, c, true); // another effect while playing: starts afresh, crossfaded
                lastFx = s.fx;
                retrigger = true;
            }
            if (s.filter > 0)
            {
                const float f = std::log (freqMap().convertFrom0to1 (live[kTargetFreq]));
                freqLog = freqLog < 0.0f ? f : freqLog + (1.0f - std::exp (-elapsed / (float) (0.015 * sr))) * (f - freqLog);
                filter.set (std::exp (freqLog), s.q, sr);
            }
            float dg, wg;
            lawGains (law, live[kTargetMix], dg, wg);
            dryGain.set (dg);
            wetGain.set (wg * s.gain);
            panL.set (jmin (1.0f, 1.0f - s.pan));
            panR.set (jmin (1.0f, 1.0f + s.pan));
        }

        follow (s, l, r, n); // after the control point: what it read is the same whatever the buffer size

        float wl[kMaxPiece], wr[kMaxPiece];
        // From silence the effect's input fades in with the slot, as its output does: a delay line starts
        // empty, and an input let in at once would come out of it as a step (Delay's first echo, the
        // flanger's tap) long after the slot's own fade
        const float* effL = l;
        const float* effR = r;
        float fl[kMaxPiece], fr[kMaxPiece];
        if (inFade < 1.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                inFade = jmin (1.0f, inFade + fadeStep);
                fl[i] = l[i] * inFade;
                fr[i] = r[i] * inFade;
            }
            effL = fl;
            effR = fr;
        }
        effect.process (lastFx, p, seed, slot, h, c, effL, effR, wl, wr, n, sideL, sideR);
        const int latency = effect.latency (lastFx, p);
        if (latency != dryLatency)
        {
            // another effect (or Cross Mod's vocoder type) while playing: the wet crossfades, so the
            // dry crossfades from its old tap to the new one too (a jump between taps clicks when
            // the mix lets the dry through)
            if (dryLatency >= 0)
            {
                dryFrom = dryLatency;
                dryFadeLeft = kDryFade;
            }
            dryLatency = latency;
        }

        if (s.filter > 0)
            for (int i = 0; i < n; ++i)
            {
                wl[i] = filter.tick (0, wl[i], s.filter);
                wr[i] = filter.tick (1, wr[i], s.filter);
            }

        // An effect started afresh while it sounds (Gate's envelope, Tape's head, the grains, the flanger's
        // delay start over) would switch its output in one sample: the step to where it was glides away over
        // 3 ms (raised cosine), so the wet carries on from its last two samples
        if (retrigger)
        {
            retrigger = false;
            declick[0] = 2.0f * lastWet[0] - prevWet[0] - wl[0];
            declick[1] = 2.0f * lastWet[1] - prevWet[1] - wr[0];
            declickLeft = declickLen;
        }
        for (int i = 0; i < n; ++i)
        {
            if (declickLeft > 0)
            {
                const float t = 0.5f - 0.5f * std::cos (Pi<float>::pi * (float) declickLeft-- / (float) declickLen);
                wl[i] += declick[0] * t;
                wr[i] += declick[1] * t;
            }
            prevWet[0] = lastWet[0];
            prevWet[1] = lastWet[1];
            lastWet[0] = wl[i];
            lastWet[1] = wr[i];
            fade = playing ? jmin (1.0f, fade + fadeStep) : jmax (0.0f, fade - fadeStep);
            const float d = dryGain.next(), w = wetGain.next();
            dryDelay.push (l[i], r[i]);
            float dl = l[i], dr = r[i]; // the dry signal, as late as the wet one
            if (latency > 0)
                dryDelay.read (latency, dl, dr);
            if (dryFadeLeft > 0)
            {
                float ol, orr;
                dryDelay.read (dryFrom, ol, orr);
                // the old tap's share, a raised cosine: no kink in the slope at either end
                const float t = 0.5f - 0.5f * std::cos (Pi<float>::pi * (float) dryFadeLeft-- / (float) kDryFade);
                dl += (ol - dl) * t;
                dr += (orr - dr) * t;
            }
            const float outL = dl * d + wl[i] * w * panL.next();
            const float outR = dr * d + wr[i] * w * panR.next();
            l[i] += (outL - l[i]) * fade;
            r[i] += (outR - r[i]) * fade;
        }
        sinceTrigger += n;

        // A non-finite output (an overflow: the slot's input is always finite) would stay in the
        // effect's loops and reach every slot after this one: the bad samples go silent and the slot
        // starts again from silence. The check is the piece's sum, so finite audio is untouched.
        if (! blockIsFinite (l, r, n))
        {
            float* ch[2] = { l, r };
            bidoo::dsp::zeroNonFinite (ch, 2, n);
            effect.recover();
            filter.reset();
            freqLog = -1.0f;
            env = 0.0f;
        }
    }

private:
    // An LFO's value (-1..1), `beats` after the slot was triggered (the knob or a block restarts it).
    float lfoValue (const ModSettings& m, double beats, int seed, int index) const
    {
        const double cycles = beats / jmax (1.0e-6, m.rateBeats);
        const float ph = (float) (cycles - std::floor (cycles));
        switch (m.shape)
        {
            case 1:  return 1.0f - 4.0f * std::abs (ph - 0.5f);     // triangle
            case 2:  return 2.0f * ph - 1.0f;                        // saw up
            case 3:  return 1.0f - 2.0f * ph;                        // saw down
            case 4:  return ph < 0.5f ? 1.0f : -1.0f;                // square
            case 5:                                                  // random, seeded per cycle
            {
                float a, b;
                draw2 (seed, 16 + slot * 4 + index, (int64_t) std::floor (cycles), a, b);
                return 2.0f * a - 1.0f;
            }
            default: return std::sin (ph * Pi<float>::twoPi);
        }
    }

    void follow (const SlotSettings& s, const float* l, const float* r, int n)
    {
        const float att = 1.0f - std::exp (-1.0f / (float) (jmax (0.1f, s.envAttackMs) * 0.001 * sr));
        const float rel = 1.0f - std::exp (-1.0f / (float) (jmax (1.0f, s.envReleaseMs) * 0.001 * sr));
        for (int i = 0; i < n; ++i)
        {
            const float x = jmax (std::abs (l[i]), std::abs (r[i]));
            env += (x > env ? att : rel) * (x - env);
        }
        if (env < 1.0e-9f)
            env = 0.0f;
    }

    // The targets now: knob + amount x curve(drive) + the modulators, 0..1.
    void computeTargets (const SlotSettings& s, const Clock& c, int seed)
    {
        const double beats = (double) sinceTrigger * c.beatsPerSample;
        for (int m = 0; m < 2; ++m)
        {
            const double cycles = beats / jmax (1.0e-6, s.lfo[m].rateBeats);
            lfoPhaseNow[m] = (float) (cycles - std::floor (cycles));
        }
        // Each modulator in its natural range (LFOs and random -1..1, the envelopes and the step 0..1)
        // and its depth; each assignment converts it to its own UNI / BI (the shared modulation model,
        // bidoo-dsp ModPolarity) and scales it by its signed depth. At depth 100 % on both, a BI
        // modulator swings its target by the knob's whole range both ways.
        float mods[kNumMods], scales[kNumMods];
        for (int m = 0; m < 2; ++m)
        {
            const bool on = s.lfo[m].depth > 0.0f;
            scales[m] = on ? s.lfo[m].depth * (s.lfo[m].byDrive ? drive : 1.0f) : 0.0f;
            mods[m] = on ? lfoValue (s.lfo[m], beats, seed, m) : 0.0f;
        }
        mods[2] = env > 1.0e-5f ? jlimit (0.0f, 1.0f, (gainToDecibels (env) + 48.0f) / 48.0f) : 0.0f;
        scales[2] = s.envDepth * (s.envByDrive ? drive : 1.0f);
        for (int m = kNumSlotMods; m < kNumMods; ++m)
        {
            mods[m] = globals != nullptr ? globals[m - kNumSlotMods] : 0.0f;
            scales[m] = globalScales != nullptr ? globalScales[m - kNumSlotMods] : 0.0f;
        }

        for (int m = 0; m < kNumMods; ++m)
        {
            modNow[m] = mods[m];
            modScaleNow[m] = scales[m];
        }
        for (int t = 0; t < kNumTargets; ++t)
        {
            float v = s.base[t] + s.amount[t] * curveValue (s.curve[t], drive);
            for (int m = 0; m < kNumMods; ++m)
                if (const auto pol = s.polarity[t][m]; pol != 0)
                    v += s.depth[t][m]
                         * (scales[m] * bidoo::dsp::convertModSource (mods[m], modIsBipolar (m), pol == 2 ? bidoo::dsp::ModPolarity::Bi
                                                                                                         : bidoo::dsp::ModPolarity::Uni));
            live[t] = jlimit (0.0f, 1.0f, v);
        }
        for (int k = 0; k < kNumEffectParams; ++k)
            p[k] = live[k];
    }

    double sr = 44100.0;
    int slot = 0;
    float fade = 0.0f, fadeStep = 0.01f;
    bool playing = false;
    float drive = 0.0f, target = 0.0f, env = 0.0f;
    int64_t sinceTrigger = 0, lastControl = 0;
    float freqLog = -1.0f; // the filter frequency's log, gliding (below 0: not started)
    float live[kNumTargets] {}, p[kNumEffectParams] {};
    float modNow[kNumMods] {}, modScaleNow[kNumMods] {}, lfoPhaseNow[2] {};
    Fx lastFx = Fx::Off;
    static constexpr int kDryFade = 64; // samples: the dry tap's crossfade when the latency changes
    int dryLatency = -1, dryFrom = 0, dryFadeLeft = 0;
    bool retrigger = false;             // the effect started afresh while sounding: its wet glides from where it was
    int declickLen = 128, declickLeft = 0;
    float declick[2] {}, lastWet[2] {}, prevWet[2] {};
    float inFade = 1.0f;                // the effect's input, fading in after a start from silence
    Effect effect;
    DryDelay dryDelay;
    Svf filter;
    Smooth dryGain, wetGain, panL, panR;
};

// ---------------------------------------------------------------------------------------------
// Where the host is (or nothing: the engine runs its own clock).
struct HostPosition
{
    bool valid = false, playing = false;
    double ppq = 0.0, bpm = 120.0;
};

// A note of the host's MIDI that plays a scene (the processor keeps the knobs' keys and CCs).
struct NoteEvent
{
    int pos, note;
    bool on;
};

// The global modulation sources, the same for every slot.
struct GlobalSettings
{
    float scAttackMs = 5.0f, scReleaseMs = 150.0f, scDepth = 0.5f;   // sidechain envelope (the input without a sidechain)
    double randomBeats = 0.5;                                        // seeded random: a new value every...
    float randomSmooth = 0.0f, randomDepth = 0.5f;                   // ...glided over this part of it
    int stepMode = 0;                                                // step index over the pattern, the bar or the beat
    float stepDepth = 0.5f;
};

struct EngineSettings
{
    SlotSettings slots[kNumSlots];
    bool laneOn[kNumSlots] {}; // the effect, mute and solo applied
    float weight[kNumSlots] {}; // the Chance lane's odds for each slot
    int trigger = 0;            // what a scene note does: 0 Host (the scene only), 1 Gate, 2 Latch
    bool gated = false;         // Gate and Latch modes: the pattern runs only while a note has it open
    double quantizeBeats = 0.0; // the grid a scene note waits for (0: at once)
    bool sceneLock = false;     // notes leave the scene alone (they still start and stop)
    Pattern* record = nullptr;  // REC: the knobs in control write their moves into this pattern
    int order[kNumSlots] { 0, 1, 2, 3, 4, 5, 6, 7 }; // the slots, in the order the signal goes through them
    bool dynamicFlow = false;   // the slot started last goes first (Turnado's dynamic signal flow)
    GlobalSettings global;
    int law = 0, seed = 1;
    bool internalClock = false;
    double tempo = 120.0;
    int beats = 4, divisions = 4;
    float masterMix = 1.0f, masterPan = 0.0f, masterGain = 1.0f;
    // A second history the capture effects of some slots read instead of the engine's own (cOPIA: the
    // engine runs on a captured loop, and a LIVE slot stutters the live input). The caller writes it
    // in step with the engine: as many samples, from the same moment, before each process() call, so
    // the engine's counter addresses both. Null (Mangle): every slot reads the engine's history.
    const History* liveHistory = nullptr;
    bool liveSource[kNumSlots] {};
};

class Engine
{
public:
    static constexpr int kChunk = 2048;

    // maxBlock: the host's block size (a mono input's second side is that long)
    void prepare (double sampleRate, int maxBlock = kChunk)
    {
        sr = sampleRate;
        monoRight.assign ((size_t) jmax (1, maxBlock), 0.0f);
        pendingScene = -1;
        pendingStart = pendingStop = pendingDue = false;
        activeNote = -1;
        open = false; // patternStart stays: only a start sets it
        history.prepare (sampleRate, 12.0, 2); // half- and quarter-rate copies for readFast
        for (int s = 0; s < kNumSlots; ++s)
        {
            slots[s].prepare (sampleRate, s);
            slots[s].globals = globalMods;
            slots[s].globalScales = globalScales;
        }
        scEnv = randomValue = 0.0f;
        flowWasDynamic = false;
        lastGlobalControl = 0;
        for (auto& g : globalMods)
            g = 0.0f;
        for (auto& g : globalScales)
            g = 0.0f;
        intClock.reset();
        lastStep = -1;
        chosen = -1;
        for (auto& w : laneWasOn)
            w = false;
        for (auto& b : blocks)
            b = {};
        for (auto& rec : recording)
            rec = {};
        limiterGain = 1.0f;
        limiterRelease = 1.0f - std::exp (-1.0f / (float) (0.12 * sampleRate));
        for (auto* g : { &mDry, &mWet, &mPanL, &mPanR })
            g->prepare (sampleRate);
        playStep.store (-1.0f);
    }

    // In place on (l, r), any length. (scL, scR): the sidechain, or null (then the input).
    void process (float* l, float* r, int n, const HostPosition& host, const EngineSettings& set, const Pattern& pattern,
                  const float* scL = nullptr, const float* scR = nullptr)
    {
        const bool internal = set.internalClock || ! host.valid;
        const double bpm = jlimit (20.0, 999.0, internal ? set.tempo : host.bpm);
        const double beatsPerSample = bpm / 60.0 / sr;
        const bool running = (internal || host.playing) && (! set.gated || open);
        playStart = set.gated ? patternStart : 0.0;

        // the internal clock counts samples at its tempo (a tempo change starts a new origin)
        if (internal)
            intClock.setTempo (bpm, sr);
        const double origin = internal ? intClock.origin() : host.ppq;
        const int64_t first = internal ? intClock.samples() : 0;

        for (int done = 0; done < n;)
        {
            const int len = jmin (kChunk, n - done);
            processChunk (l + done, r + done, len, origin, first + done, beatsPerSample, running, set, pattern,
                          scL != nullptr ? scL + done : nullptr, scR != nullptr ? scR + done : nullptr);
            done += len;
        }
        if (internal)
            intClock.advance (n);
    }

    const History& getHistory() const { return history; }

    // The play head position (beats) `i` samples into the next block, as process() will count it.
    double ppqAt (int i, const HostPosition& host, const EngineSettings& set) const
    {
        const bool internal = set.internalClock || ! host.valid;
        const double bpm = jlimit (20.0, 999.0, internal ? set.tempo : host.bpm);
        if (! internal)
            return host.ppq + i * bpm / 60.0 / sr;
        return intClock.ppqAt (i, bpm, sr);
    }

    // The pattern starts again from its first step (a scene note in Gate and Latch modes).
    void restart()
    {
        lastStep = -1;
        for (auto& w : laneWasOn)
            w = false;
    }

    // Gate and Latch modes: a note has the pattern running.
    bool isOpen() const { return open; }

    // Whether slot s's lane (its block, a Chance pick or the knob) had it playing at the end of the last block.
    bool isLaneOn (int s) const { return laneWasOn[s]; }

    // The host's block, scene notes included. The notes ask for a scene switch, a start or a stop,
    // due at the next step of the quantize grid (scheduleNotes); the block is cut where one is due.
    // r: null for a mono input (the engine runs on the one channel on both sides, then averages them).
    // `scenes` is the processor's side of a switch:
    //   const EngineSettings& settings()   this block's settings (as they are after a switch)
    //   const Pattern& pattern()           the pattern that plays
    //   bool switchTo (int scene)          the switch; false when the scenes' store is busy (try again
    //                                      on the next sample)
    //   void reread (int numSamples)       the settings again, for the rest of the block
    template <class Scenes>
    void processBlock (float* l, float* r, int n, const HostPosition& host, const NoteEvent* notes, int numNotes,
                       const float* scL, const float* scR, Scenes& scenes)
    {
        scheduleNotes (notes, numNotes, host, scenes.settings());

        // mono: the engine runs on monoRight's length at most (a host may send more than it said)
        const int maxRun = r != nullptr ? n : (int) monoRight.size();
        const auto& first = scenes.settings();
        const double bpm = first.internalClock || ! host.valid ? first.tempo : host.bpm;
        const double beatsPerSample = jlimit (20.0, 999.0, bpm) / 60.0 / sr;
        for (int done = 0; done < n;)
        {
            auto hostHere = host;
            hostHere.ppq = host.ppq + done * beatsPerSample;
            int end = n;
            if (pendingDue)
            {
                const double ppqHere = ppqAt (0, hostHere, scenes.settings()); // the engine's position now
                const int due = jlimit (0, n - done, (int) std::ceil ((pendingPpq - ppqHere) / beatsPerSample - 1.0e-6));
                if (due == 0)
                {
                    // now: the scene, then the start or stop
                    if (pendingScene < 0 || scenes.switchTo (pendingScene))
                    {
                        pendingScene = -1;
                        if (pendingStart)
                        {
                            open = true;
                            patternStart = pendingPpq;
                            restart();
                        }
                        if (pendingStop)
                            open = false;
                        pendingStart = pendingStop = pendingDue = false;
                        scenes.reread (n - done);
                    }
                    else
                    {
                        pendingPpq = ppqAt (1, hostHere, scenes.settings()); // the store is busy: try on the next sample
                    }
                }
                end = done + jmax (1, due);
            }
            end = jmin (end, done + maxRun);

            float* right = r != nullptr ? r + done : monoRight.data();
            if (r == nullptr)
                std::copy (l + done, l + end, right);
            process (l + done, right, end - done, hostHere, scenes.settings(), scenes.pattern(),
                     scL != nullptr ? scL + done : nullptr, scR != nullptr ? scR + done : nullptr);
            if (r == nullptr)
                for (int i = 0; i < end - done; ++i)
                    l[done + i] = 0.5f * (l[done + i] + right[i]);
            done = end;
        }
    }

    // For the editor: the step under the play head (fractional, -1 when stopped), which slots sound,
    // their drives and where their targets are.
    std::atomic<float> playStep { -1.0f };
    std::atomic<bool> sounding[kNumSlots] {};
    std::atomic<float> liveDrive[kNumSlots] {};
    std::atomic<float> liveTarget[kNumSlots][kNumTargets] {};
    std::atomic<float> liveMod[kNumSlots][kNumMods] {};      // each modulator now, natural range, per slot
    std::atomic<float> liveModScale[kNumSlots][kNumMods] {}; // and its depth
    std::atomic<float> liveLfoPhase[kNumSlots][2] {};     // the LFOs' position in their cycle, 0..1
    std::atomic<float> liveGlobal[3] {}; // the sidechain envelope, the random value, the step index (0..1, -1..1, 0..1)

private:
    // What the notes ask for, due at the next step of the quantize grid. In Host mode a note switches
    // the scene under the host's play head; in Gate mode it also starts the pattern from its first step
    // and stops it on release; in Latch mode it starts it, and the same note again stops it.
    void scheduleNotes (const NoteEvent* notes, int numNotes, const HostPosition& host, const EngineSettings& set)
    {
        const int mode = set.trigger;
        const double grid = set.quantizeBeats;
        const bool running = set.internalClock || ! host.valid || host.playing;
        for (int i = 0; i < numNotes; ++i)
        {
            const auto& e = notes[i];
            const double at = ppqAt (e.pos, host, set);
            const double due = grid > 0.0 && running ? std::ceil (at / grid - 1.0e-9) * grid : at;
            if (e.on)
            {
                if (mode == 2 && open && e.note == activeNote && ! pendingStart)
                {
                    pendingStop = true; // Latch: the same note again
                    pendingScene = -1;
                }
                else
                {
                    pendingScene = set.sceneLock ? -1 : e.note;
                    pendingStart = mode != 0;
                    pendingStop = false;
                    activeNote = e.note;
                }
                pendingPpq = due;
                pendingDue = true;
            }
            else if (mode == 1 && e.note == activeNote)
            {
                // Gate: the release stops at once (and drops a start still waiting)
                pendingStart = false;
                pendingStop = true;
                pendingPpq = at;
                pendingDue = true;
                activeNote = -1;
            }
        }
    }

    struct BlockState
    {
        bool on = false, start = false;
        int first = 0, length = 1;
        float level = 1.0f;
        Shape shape = Shape::Hold;
        int ratchets = 1;
        bool locked = false;
        float lock[kLockValues] {};
    };

    // A knob's moves being written into its lane (REC): the run of steps it has held up so far.
    struct Recording
    {
        int first = -1, last = -1;
        float start = 0.0f, peak = 0.0f;
    };

    void processChunk (float* l, float* r, int n, double origin, int64_t offset, double beatsPerSample, bool running,
                       const EngineSettings& set, const Pattern& pattern, const float* scL, const float* scR)
    {
        // a NaN or an infinity in comes in as silence (the processor's own guard, if any, makes this a no-op)
        {
            float* ch[2] = { l, r };
            bidoo::dsp::zeroNonFinite (ch, 2, n);
        }
        std::copy (l, l + n, dryL);
        std::copy (r, r + n, dryR);
        if (set.dynamicFlow != flowWasDynamic)
        {
            // dynamic flow from the fixed order, as it is now
            std::copy (set.order, set.order + kNumSlots, flow);
            flowWasDynamic = set.dynamicFlow;
        }
        const float* sideL = scL != nullptr ? scL : dryL; // no sidechain: the input
        const float* sideR = scR != nullptr ? scR : dryR;
        // the sidechain is the caller's (const): a copy with the bad samples silent, only when it has any
        if (scL != nullptr && ! blockIsFinite (scL, n))
        {
            std::copy (scL, scL + n, sideSafeL);
            float* ch[1] = { sideSafeL };
            bidoo::dsp::zeroNonFinite (ch, 1, n);
            sideL = sideSafeL;
        }
        if (scR != nullptr && ! blockIsFinite (scR, n))
        {
            std::copy (scR, scR + n, sideSafeR);
            float* ch[1] = { sideSafeR };
            bidoo::dsp::zeroNonFinite (ch, 1, n);
            sideR = sideSafeR;
        }

        const int beats = set.beats, div = set.divisions;
        const int steps = jmin (Pattern::kMaxSteps, beats * div);
        const double samplesPerBeat = 1.0 / beatsPerSample;

        for (int i = 0; i < n;)
        {
            const Clock c { history.getWritten(), origin, offset + i, beatsPerSample, samplesPerBeat, beats, playStart };
            const double stepF = c.posAt (0) * div;
            const int step = jlimit (0, steps - 1, (int) std::floor (stepF + 1.0e-9));
            const double toNext = ((double) (step + 1) - stepF) / div * samplesPerBeat;
            const int toControl = kControl - (int) (c.counter % kControl);
            const int len = jlimit (1, jmin (toControl, n - i), (int) std::ceil (toNext - 1.0e-6));

            const bool stepChanged = ! running || step != lastStep;
            const bool control = c.counter % kControl == 0 || stepChanged;
            if (control)
                updateGlobals (set, c, step, steps, div);
            if (stepChanged)
            {
                // REC: the knobs in control write the step they start
                if (set.record != nullptr && running)
                    for (int s = 0; s < kNumSlots; ++s)
                    {
                        if (set.slots[s].knobInControl && set.laneOn[s])
                            recordStep (*set.record, s, step, set.slots[s].knob);
                        else
                            recording[s] = {};
                    }
                // the blocks under the play head, read again when the step changes
                const auto loop = (int64_t) std::floor ((c.ppqAt (0) - c.patternStart) / beats + 1.0e-9);
                for (int lane = 0; lane < Pattern::kLanes; ++lane)
                    readBlock (pattern, lane, step, steps, running, loop, set.seed);
                // a Chance block plays one slot, picked by weight when it starts
                auto& ch = blocks[Pattern::kChanceLane];
                if (ch.on && (ch.start || chosen < 0))
                    chosen = pickSlot (set, ch.first);
                if (! ch.on)
                    chosen = -1;
            }
            for (int s = 0; s < kNumSlots; ++s)
            {
                const auto& ss = set.slots[s];
                const auto& own = blocks[s];
                // a slot's own block, or the Chance lane's when it picked this slot
                const auto& b = own.on || chosen != s ? own : blocks[Pattern::kChanceLane];

                bool on, start = false;
                float driveTarget;
                used[s] = &ss;
                if (ss.knobInControl)
                {
                    // the hand punches in over the pattern
                    on = set.laneOn[s] && ss.knob > Slot::kKnobThreshold;
                    driveTarget = ss.knob;
                }
                else
                {
                    on = set.laneOn[s] && b.on;
                    start = on && stepChanged && b.start;
                    const float t = (float) ((stepF - b.first) / b.length);
                    driveTarget = b.on ? shapeValue (b.shape, b.level, t) : 0.0f;
                    if (on && b.ratchets > 1 && control)
                    {
                        // ratchets: the slot starts again on every equal part of the block (seen at
                        // the control points, so on the same sample whatever the buffer size)
                        const int sub = jlimit (0, b.ratchets - 1, (int) (t * (float) b.ratchets));
                        if (sub > 0 && (sub != ratchetSub[s] || b.first != ratchetFirst[s]))
                            start = true;
                        ratchetSub[s] = sub;
                        ratchetFirst[s] = b.first;
                    }
                    if (on && b.locked)
                    {
                        // a parameter lock: the block's own values instead of the knobs
                        locked[s] = ss;
                        for (int k = 0; k < kLockValues; ++k)
                            locked[s].base[k] = b.lock[k];
                        used[s] = &locked[s];
                    }
                }
                if (slots[s].setLane (on, start || ! laneWasOn[s], driveTarget, *used[s], c, set.seed) && set.dynamicFlow)
                {
                    // dynamic flow: this slot goes first
                    int at = 0;
                    while (at < kNumSlots - 1 && flow[at] != s)
                        ++at;
                    for (; at > 0; --at)
                        flow[at] = flow[at - 1];
                    flow[0] = s;
                }
                laneWasOn[s] = on;
            }
            lastStep = running ? step : -1;

            history.write (l + i, r + i, len);
            followSidechain (set.global, sideL + i, sideR + i, len); // after the control point: the same at every buffer size
            for (int position = 0; position < kNumSlots; ++position)
            {
                const int s = set.dynamicFlow ? flow[position] : set.order[position];
                const History& source = set.liveHistory != nullptr && set.liveSource[s] ? *set.liveHistory : history;
                slots[s].process (*used[s], set.law, set.seed, source, c, l + i, r + i, len, control, sideL + i, sideR + i);
            }

            i += len;
        }

        // ---- master: mix with the input, pan, volume, safety limiter ----
        float dg, wg;
        lawGains (set.law, set.masterMix, dg, wg);
        mDry.set (dg);
        mWet.set (wg * set.masterGain);
        mPanL.set (jmin (1.0f, 1.0f - set.masterPan));
        mPanR.set (jmin (1.0f, 1.0f + set.masterPan));
        constexpr float ceiling = 0.977f; // -0.2 dBFS
        for (int i = 0; i < n; ++i)
        {
            const float w = mWet.next(), d = mDry.next();
            const float ol = (dryL[i] * d + l[i] * w) * mPanL.next();
            const float orr = (dryR[i] * d + r[i] * w) * mPanR.next();
            const float peak = jmax (std::abs (ol), std::abs (orr)) * limiterGain;
            if (peak > ceiling)
                limiterGain *= ceiling / peak;
            else
                limiterGain += limiterRelease * (1.0f - limiterGain);
            l[i] = ol * limiterGain;
            r[i] = orr * limiterGain;
        }
        // the slots' outputs are finite; only an overflow in the mix can get here (a huge finite input)
        if (! blockIsFinite (l, r, n))
        {
            float* ch[2] = { l, r };
            bidoo::dsp::zeroNonFinite (ch, 2, n);
            if (! std::isfinite (limiterGain))
                limiterGain = 1.0f;
        }

        const Clock end { 0, origin, offset, beatsPerSample, samplesPerBeat, beats, playStart };
        playStep.store (running ? (float) (end.posAt (n) * div) : -1.0f);
        for (int s = 0; s < kNumSlots; ++s)
        {
            sounding[s].store (slots[s].isSounding());
            liveDrive[s].store (slots[s].getDrive());
            for (int t = 0; t < kNumTargets; ++t)
                liveTarget[s][t].store (slots[s].getTarget (t));
            for (int m = 0; m < kNumMods; ++m)
            {
                liveMod[s][m].store (slots[s].getMod (m));
                liveModScale[s][m].store (slots[s].getModScale (m));
            }
            for (int m = 0; m < 2; ++m)
                liveLfoPhase[s][m].store (slots[s].getLfoPhase (m));
        }
    }

    // The global sources at a control point: the sidechain envelope, the seeded random (a new value
    // on its grid, gliding), the step index.
    void updateGlobals (const EngineSettings& set, const Clock& c, int step, int steps, int div)
    {
        const auto& g = set.global;
        const float elapsed = (float) jmax ((int64_t) 1, c.counter - lastGlobalControl);
        lastGlobalControl = c.counter;

        const float sc = scEnv > 1.0e-5f ? jlimit (0.0f, 1.0f, (gainToDecibels (scEnv) + 48.0f) / 48.0f) : 0.0f;

        const auto index = (int64_t) std::floor (c.posAt (0) / jmax (1.0e-3, g.randomBeats) + 1.0e-9);
        float u, v;
        draw2 (set.seed, 900, index, u, v);
        const float target = 2.0f * u - 1.0f;
        const float glide = (float) (g.randomSmooth * g.randomBeats * c.samplesPerBeat * 0.5);
        randomValue = glide < 1.0f ? target : randomValue + (1.0f - std::exp (-elapsed / glide)) * (target - randomValue);

        float stepIndex;
        switch (g.stepMode)
        {
            case 1:  { const int n = jmin (steps, div * 4); stepIndex = n > 1 ? (float) (step % n) / (float) (n - 1) : 0.0f; break; } // the bar
            case 2:  stepIndex = div > 1 ? (float) (step % div) / (float) (div - 1) : 0.0f; break;                                         // the beat
            default: stepIndex = steps > 1 ? (float) step / (float) (steps - 1) : 0.0f; break;                                             // the pattern
        }

        globalMods[0] = sc; // natural ranges; the depths scale them after each assignment's UNI / BI
        globalMods[1] = randomValue;
        globalMods[2] = stepIndex;
        globalScales[0] = g.scDepth;
        globalScales[1] = g.randomDepth;
        globalScales[2] = g.stepDepth;
        for (int k = 0; k < 3; ++k)
            liveGlobal[k].store (k == 0 ? sc : k == 1 ? randomValue : stepIndex);
    }

    void followSidechain (const GlobalSettings& g, const float* sl, const float* sr2, int n)
    {
        const float att = 1.0f - std::exp (-1.0f / (float) (jmax (0.1f, g.scAttackMs) * 0.001 * sr));
        const float rel = 1.0f - std::exp (-1.0f / (float) (jmax (1.0f, g.scReleaseMs) * 0.001 * sr));
        for (int i = 0; i < n; ++i)
        {
            const float x = jmax (std::abs (sl[i]), std::abs (sr2[i]));
            scEnv += (x > scEnv ? att : rel) * (x - scEnv);
        }
        if (scEnv < 1.0e-9f)
            scEnv = 0.0f;
    }

    void readBlock (const Pattern& pattern, int lane, int step, int steps, bool running, int64_t loop, int seed)
    {
        auto& b = blocks[lane];
        b.on = running && pattern.isOn (lane, step);
        b.start = b.on && (pattern.isStart (lane, step) || step == 0);
        if (! b.on)
            return;
        int first = step, last = step;
        pattern.blockAt (lane, step, first, last);
        if (step == 0)
            first = 0; // a block never runs on from the end of the pattern
        b.first = first;
        b.length = jmax (1, jmin (last, steps - 1) - first + 1);
        b.level = pattern.levelAt (lane, first);
        b.shape = pattern.shapeAt (lane, first);
        b.ratchets = pattern.ratchetsAt (lane, first);
        b.locked = pattern.isLocked (lane, first);
        for (int k = 0; k < kLockValues; ++k)
            b.lock[k] = pattern.lockValue (lane, first, k);

        // its probability: a seeded draw for this block on this loop (the same on every playback)
        if (const float prob = pattern.probabilityAt (lane, first); prob < 1.0f)
        {
            float u, v;
            draw2 (seed, 300 + lane, loop * Pattern::kMaxSteps + first, u, v);
            if (u >= prob)
                b.on = b.start = false;
        }
    }

    // REC: the knob of slot `s` at the start of `step`, written into its lane. Above the engage
    // threshold the steps it holds make one block (its level the highest the knob reached, ramping
    // when the knob clearly rose or fell); below, the step is erased.
    void recordStep (Pattern& into, int s, int step, float knob)
    {
        auto& r = recording[s];
        if (knob <= Slot::kKnobThreshold)
        {
            into.erase (s, step, step);
            r = {};
            return;
        }
        if (r.first < 0 || r.last != step - 1)
            r = { step, step, knob, knob };
        r.last = step;
        r.peak = jmax (r.peak, knob);
        BlockInfo info;
        info.level = (uint8_t) roundToInt (r.peak * 100.0f);
        info.shape = (uint8_t) (knob > r.start + 0.2f && r.start < 0.35f * knob ? Shape::RampUp
                                : r.start > knob + 0.2f && knob < 0.35f * r.start ? Shape::RampDown
                                                                                   : Shape::Hold);
        into.setBlock (s, r.first, step, info);
    }

    // The slot a Chance block starting at `first` plays: by weight, the same on every loop.
    int pickSlot (const EngineSettings& set, int first) const
    {
        float total = 0.0f;
        for (int s = 0; s < kNumSlots; ++s)
            if (set.laneOn[s])
                total += set.weight[s];
        if (total <= 0.0f)
            return -1;
        float u, v;
        draw2 (set.seed, 99, first, u, v);
        float x = u * total;
        for (int s = 0; s < kNumSlots; ++s)
            if (set.laneOn[s] && set.weight[s] > 0.0f)
            {
                if (x < set.weight[s])
                    return s;
                x -= set.weight[s];
            }
        return -1;
    }

    double sr = 44100.0;
    History history;
    Slot slots[kNumSlots];
    bidoo::dsp::InternalClock intClock; // the internal clock: beats at its last tempo change and samples since
    int lastStep = -1;
    bool laneWasOn[kNumSlots] {};
    BlockState blocks[Pattern::kLanes];
    Recording recording[kNumSlots];
    int ratchetSub[kNumSlots] {}, ratchetFirst[kNumSlots] {};
    SlotSettings locked[kNumSlots];               // a slot's settings with its block's parameter lock
    const SlotSettings* used[kNumSlots] {};       // what each slot plays this piece
    int chosen = -1; // the slot the Chance block under the play head plays
    int flow[kNumSlots] { 0, 1, 2, 3, 4, 5, 6, 7 }; // dynamic flow: the slots, the last started first
    bool flowWasDynamic = false;
    float globalMods[3] {}, globalScales[3] {}, scEnv = 0.0f, randomValue = 0.0f;
    int64_t lastGlobalControl = 0;
    float dryL[kChunk] {}, dryR[kChunk] {};
    float sideSafeL[kChunk] {}, sideSafeR[kChunk] {}; // a sidechain block that had NaN or inf in it, cleaned
    float limiterGain = 1.0f, limiterRelease = 0.001f;
    Smooth mDry, mWet, mPanL, mPanR;

    // scene notes: a switch, a start or a stop waiting for its quantized time; Gate and Latch's state
    int pendingScene = -1;
    bool pendingStart = false, pendingStop = false, pendingDue = false;
    double pendingPpq = 0.0;
    int activeNote = -1;
    bool open = false;
    double patternStart = 0.0, playStart = 0.0; // where a gated pattern started; where this block's starts
    std::vector<float> monoRight;               // the second side of a mono input
};
} // namespace bidoo::dsp::glitch

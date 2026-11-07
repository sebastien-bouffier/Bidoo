#pragma once

// The history buffer and the Phase 1 effects.
//
// Mangle's glitch engine (effects/glitch, moved from Mangle unchanged), shared with cOPIA (Bidoo VCV).
// BIDOO_DSP_GLITCH_LITE leaves out the effects that need compiled code (Spectral: the FFT; Texture,
// Cross Mod, Resonator, Modal: Bidoo::mutable): they pass their input through.
//
// Every capture effect (Repeat, Reverser, Slicer) reads the one history buffer: the plugin's input,
// always recording, addressed by a sample counter. Engaging one before audio arrives is never
// silent, and a slice "one beat ago" is just an address. Reads go through a Tap, which crossfades
// a few milliseconds whenever it jumps, so slice edges never click.

#include "EffectDefs.h"
#include "../../core/Random.h"
#include "../../delay/FractionalReads.h"
#include "../../delay/HistoryTap.h"
#include "../../delay/ModDelayLine.h"
#ifndef BIDOO_DSP_GLITCH_LITE
#include "SpectralDelay.h"
#include "../../effects/mutable/CrossModulator.h"
#include "../../effects/mutable/ModalFx.h"
#include "../../effects/mutable/Resonator.h"
#include "../../effects/mutable/Texture.h"
#endif
#include "../../filters/TptSvf.h"
#include "../../oversampling/IirHalfBand.h"
#include "../../reverb/FdnEcoReverb.h"
#include "../../shaping/AdaaTanhTable.h"
#include "../../synth/KaiserBlep.h"
#include "../../time/Clock.h"
#include <cstdint>
#include <vector>

namespace bidoo::dsp::glitch
{
// ---------------------------------------------------------------------------------------------
// The history buffer and the crossfading read head: bidoo-dsp's HistoryBuffer (with two decimated
// copies, half and quarter rate, that band-limit a read faster than real time: readFast) and
// CrossfadeTap (delay/HistoryTap.h).
using History = bidoo::dsp::HistoryBuffer;
using Tap = bidoo::dsp::CrossfadeTap;

// ---------------------------------------------------------------------------------------------
// Where the slot is, sample by sample: bidoo-dsp's Clock (time/Clock.h), moved from here unchanged
// (bit-identical).
using Clock = bidoo::dsp::Clock;

// Counter-based randomness: the same (seed, slot, index) gives the same draw on every loop,
// after a jump of the playhead and in an offline render (bidoo-dsp's splitmix64).

inline void draw2 (int seed, int slot, int64_t index, float& a, float& b)
{
    const uint64_t h = bidoo::dsp::splitmix64 ((uint64_t) (uint32_t) seed * 0x100000001b3ull ^ (uint64_t) slot << 56 ^ (uint64_t) index);
    a = (float) (h >> 40) / (float) (1 << 24);
    b = (float) ((h >> 16) & 0xffffff) / (float) (1 << 24);
}

inline double semitonesToRatio (int st) { return std::pow (2.0, st / 12.0); }

// Topology-preserving state variable filter (Cytomic), stereo: bidoo-dsp's TptSvf per channel,
// with Mangle's own mapping (10 Hz .. 0.49 fs, g = tan in float, Q floored at 0.1).
class Svf
{
public:
    void reset()
    {
        for (auto& s : svf)
            s.reset();
    }

    void set (float freq, float q, double sr)
    {
        const float f = jlimit (10.0f, (float) (sr * 0.49), freq);
        const float g = std::tan (Pi<float>::pi * f / (float) sr);
        k = 1.0f / jmax (0.1f, q);
        for (auto& s : svf)
            s.setCoefficients (g, k);
    }

    float tick (int ch, float v0, int mode)
    {
        const auto o = svf[ch].process (v0);
        switch (mode)
        {
            case 1:  return o.lp;                 // low pass
            case 2:  return o.hp;                 // high pass
            case 3:  return o.bp * k;             // band pass (unity at the centre)
            case 4:  return v0 - k * o.bp;        // notch
            default: return v0;
        }
    }

private:
    float k = 1.0f;
    bidoo::dsp::TptSvf<float> svf[2];
};

// ModDelayLine::readHermite's 4-point Hermite read, at an age given as a whole part and a fraction
// (`whole` samples behind the newest one, then `frac` further): Delay's line holds seconds, and a
// float age that long keeps too few bits of fraction for a gliding read.
inline float hermiteAt (const bidoo::dsp::ModDelayLine& b, int whole, float frac)
{
    if (whole == 0) // under one sample: no newer sample to interpolate with
        return b.at (0) + frac * (b.at (1) - b.at (0));
    const float xm1 = b.at (whole - 1), x0 = b.at (whole), x1 = b.at (whole + 1), x2 = b.at (whole + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * frac + c2) * frac + c1) * frac + x0;
}

// A read at an age that holds for the block (a comb tuned to a note): 6-point Lagrange
// (bidoo-dsp's FractionalReads.h), its weights once per block; Hermite under two samples, where
// Lagrange has too few newer samples.
struct FixedRead
{
    FixedRead() = default;
    explicit FixedRead (double age) : whole ((int) age), frac ((float) (age - (double) (int) age))
    {
        if (whole >= 2)
            bidoo::dsp::lagrange6Weights (frac, h);
    }

    float read (const bidoo::dsp::ModDelayLine& b) const
    {
        if (whole < 2)
            return hermiteAt (b, whole, frac);
        float acc = 0.0f;
        for (int k = 0; k < 6; ++k)
            acc += h[k] * b.at (whole + k - 2);
        return acc;
    }

    int whole = 0;
    float frac = 0.0f;
    float h[6] {};
};

// ---------------------------------------------------------------------------------------------
// Every effect, one class with one state per kind (a slot can change effect). The capture effects
// (Repeat, Reverser, Slicer, Tape, Grain) read the history; the others process the chain's signal.
// Delay and Reverb keep the dry signal in their output, so a slot's mix is their send.
// Texture, Cross Mod, Resonator and Modal (bidoo-dsp's effects/mutable) give the wet signal only, a little late
// (latency()): the slot delays its own dry signal by the same amount, so the two stay in phase.
class Effect
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        xfSamples = jmax (8, (int) (sampleRate * 0.003));
        delayMax = (int) (sampleRate * 2.5); // Delay (up to 2 s), the comb filter, the flanger
        for (auto& b : delayLine)
            b.prepare (delayMax + 4);
        tonalMax = (int) (sampleRate / 25.0); // Tonalizer: down to 25 Hz
        for (auto& v : tonalLine)
            for (auto& b : v)
                b.prepare (tonalMax + 4);
        bidoo::dsp::KaiserBlepTables::get(); // built here, never on the audio thread (Ring)
        reverb.prepare (sampleRate);
#ifndef BIDOO_DSP_GLITCH_LITE
        spectral.prepare (sampleRate);
        texture.prepare (sampleRate, 512);
        crossMod.prepare (sampleRate, 512);
        resonator.prepare (sampleRate, 512);
        modal.prepare (sampleRate, 512);
#endif
        activeFx = Fx::Off;
        vocReady = false; // the vocoder's bands again at this rate
        reset();
        clearTails();
        tailsDirty = tailsDue = 0; // the buffers were just made, empty
    }

    void reset()
    {
        phaserLive = false; // the phaser's coefficients again from its LFO, not from where they were
        tap.reset();
        gateEnv = 1.0f;
        lastSlice = -1;
        repeatIndex = 0;
        repeatGain = 1.0f;
        tapLive = false;
    }

    // A block starts at the sub-block's first sample. `continuing`: the slot was sounding, so the
    // read head crossfades from where it was.
    void trigger (Fx fx, const float* p, const Clock& c, bool continuing)
    {
        const Tap before = tap;
        reset();
        if (continuing)
            tap = before;
        else
            clearTails();
        trigCounter = c.counter;
        tapePos = (double) c.counter;
        grainLeft[0] = grainLeft[1] = grainWait[0] = 0;
        grainWait[1] = -1; // half a grain, once the size is known
        grainIndex = 0;
        switch (fx)
        {
            case Fx::Repeat:
            {
                // the first slice is what plays now; the repeats come from it
                sliceLen = jmax (32.0, lengthBeats (paramDef (fx, 0), p[0]) * c.samplesPerBeat);
                segStart = c.counter;
                segLen = sliceLen;
                tap.jump ((double) c.counter, 1.0, crossfadeFor (sliceLen));
                break;
            }
            case Fx::Reverser:
                segStart = c.counter;
                segLen = 0.0; // starts a segment on the first sample
                chunk = -1;
                break;
            case Fx::Reactor:
                tap.jump ((double) c.counter, 1.0, xfSamples); // live until a transient
                reactorLooping = false;
                break;
            case Fx::FilterPattern:
                svf.set (std::exp (std::log (60.0f)), 0.7f, sr);
                break;
#ifndef BIDOO_DSP_GLITCH_LITE
            case Fx::Spectral:
                if (! continuing)
                    spectral.clear(); // only this effect's frames (a second of them): not for every slot start
                break;
            case Fx::Texture:
                texture.trigger(); // a grain now, or the read point starts over. Its recording is not cleared: it listens while the slot rests
                break;
            case Fx::CrossMod:
                if (! continuing || activeFx != fx)
                    crossMod.reset(); // starts from silence, whatever the buffer size (the slot stops being fed a few samples after its fade)
                break;
            case Fx::Resonator:
                if (! continuing || activeFx != fx)
                    resonator.reset(); // what rang the last time it played has gone
                break;
            case Fx::Modal:
                if (! continuing || activeFx != fx)
                    modal.reset();
                break;
#endif
            default:
                break;
        }
        activeFx = fx;
    }

    // The vendored code draws from one shared random sequence (stmlib's): a test seeds it for repeatable renders.
#ifndef BIDOO_DSP_GLITCH_LITE
    static void seedRandom (uint32_t seed) { stmlib::Random::Seed (seed); }
#endif

    // The delay, in samples, between the slot's input and this effect's wet output (0 for the others).
    int latency (Fx fx, const float* p) const
    {
        switch (fx)
        {
#ifndef BIDOO_DSP_GLITCH_LITE
            case Fx::Texture:   return texture.latencySamples();
            case Fx::CrossMod:  return crossMod.latencySamples() + (crossModType (p) == 6 ? crossMod.vocoderExtraLatencySamples() : 0);
            case Fx::Resonator: return resonator.latencySamples();
            case Fx::Modal:     return modal.latencySamples();
#endif
            case Fx::Drive:     return stepValue (paramDef (fx, 0), p[0]) == 2 ? kDriveFoldLatency : 0;
            default:            return 0;
        }
    }

    // A slot at rest: Texture keeps recording what comes in, so that the next block has something
    // to play; record only (Texture::listen: nothing is synthesised while nobody hears it). `base`
    // is the slot's P1..P5 knobs.
    void listen (Fx fx, const float* base, const float* inL, const float* inR, int n)
    {
#ifdef BIDOO_DSP_GLITCH_LITE
        (void) fx; (void) base; (void) inL; (void) inR; (void) n;
#else
        if (fx != Fx::Texture)
            return;
        const int mode = stepValue (paramDef (fx, 0), base[0]);
        if (mode != texture.mode())
            texture.setMode (mode);
        texture.listen (inL, inR, n);
#endif
    }

    // Wet signal for n samples: `inL/inR` the chain's signal (the gate), the history for the rest.
    void process (Fx fx, const float* p, int seed, int slot, const History& h, const Clock& c,
                  const float* inL, const float* inR, float* outL, float* outR, int n,
                  const float* scL = nullptr, const float* scR = nullptr)
    {
        switch (fx)
        {
            case Fx::Repeat:   repeat (p, h, c, outL, outR, n); break;
            case Fx::Reverser: reverser (p, h, c, outL, outR, n); break;
            case Fx::Slicer:   slicer (p, seed, slot, h, c, outL, outR, n); break;
            case Fx::Gate:     gate (p, c, inL, inR, outL, outR, n); break;
            case Fx::Tape:     tape (p, h, c, outL, outR, n); break;
            case Fx::Grain:    grain (p, seed, slot, h, c, outL, outR, n); break;
            case Fx::Crush:    crush (p, inL, inR, outL, outR, n); break;
            case Fx::Drive:    drive (p, inL, inR, outL, outR, n); break;
            case Fx::Ring:     ring (p, inL, inR, outL, outR, n); break;
            case Fx::Delay:    delay (p, c, inL, inR, outL, outR, n); break;
            case Fx::Filter:   filterFx (p, inL, inR, outL, outR, n); break;
            case Fx::Reverb:   reverbFx (p, inL, inR, outL, outR, n); break;
            case Fx::Flanger:  flanger (p, inL, inR, outL, outR, n); break;
            case Fx::Phaser:   phaser (p, c, inL, inR, outL, outR, n); break;
            case Fx::Vowel:    vowel (p, inL, inR, outL, outR, n); break;
            case Fx::Reactor:  reactor (p, h, c, inL, inR, outL, outR, n); break;
            case Fx::Tonalizer: tonalizer (p, inL, inR, outL, outR, n); break;
            case Fx::Vocoder:  vocoder (p, inL, inR, outL, outR, n); break;
#ifndef BIDOO_DSP_GLITCH_LITE
            case Fx::Spectral: spectralFx (p, seed, slot, c, inL, inR, outL, outR, n); break;
#endif
            case Fx::FilterPattern: filterPattern (p, c, inL, inR, outL, outR, n); break;
#ifndef BIDOO_DSP_GLITCH_LITE
            case Fx::Texture:   textureFx (p, inL, inR, outL, outR, n); break;
            case Fx::CrossMod:  crossModFx (p, inL, inR, scL != nullptr ? scL : inL, scR != nullptr ? scR : inR, outL, outR, n); break;
            case Fx::Resonator: resonatorFx (p, inL, inR, outL, outR, n); break;
            case Fx::Modal:     modalFx (p, inL, inR, outL, outR, n); break;
#endif
            default:
                std::copy (inL, inL + n, outL);
                std::copy (inR, inR + n, outR);
                break;
        }
    }

private:
    int crossfadeFor (double segment) const { return jlimit (4, xfSamples, (int) (segment * 0.25)); }

    // What rings on after the input stops (echoes, the reverb): cleared when a slot starts from silence.
    // The big buffers (the delay line, the Tonalizer's combs, the reverb: about a megabyte) are only
    // marked: the effect that uses one clears it before it next plays (useTail), and only if something
    // was written since it was last cleared. Clearing them all here made every slot start a load spike,
    // whatever its effect; the output is the same.
    void clearTails()
    {
        tailsDue |= tailsDirty;
        delayTime = -1.0;
        toneL = toneR = 0.0f;
        svf.reset();
        patternFreq = -1.0f;
        for (auto& t : tonalLp)
            t[0] = t[1] = 0.0f;
        for (auto& a : allpass)
            a[0] = a[1] = 0.0f;
        phaserLast[0] = phaserLast[1] = 0.0f;
        for (auto& f : formant)
            f.reset();
        for (auto& b : vocAnalysis)
            b.reset();
        for (auto& b : vocSynthesis)
            b.reset();
        for (auto& e : vocEnv)
            e = 0.0f;
        for (auto& ph : sawPhase)
            ph = 0.0;
        reactorFast = reactorSlow = 0.0f;
        reactorHoldoff = 0;
        reactorLooping = false;
        crushPhase = 1.0f;
        holdL = holdR = 0.0f;
        dcIn[0] = dcIn[1] = dcOut[0] = dcOut[1] = 0.0f;
        for (auto& a : driveAdaa)
            a.reset();
        for (auto& o : driveOs)
            o.reset();
        ringPhase[0] = ringPhase[1] = 0.0;
    }

    enum Tail { kDelayTail = 1, kTonalTail = 2, kReverbTail = 4 };

    // An effect about to use one of the big buffers: cleared first if a start from silence asked for it.
    void useTail (Tail t)
    {
        if ((tailsDue & t) != 0)
        {
            switch (t)
            {
                case kDelayTail:
                    for (auto& b : delayLine)
                        b.clear();
                    break;
                case kTonalTail:
                    for (auto& v : tonalLine)
                        for (auto& b : v)
                            b.clear();
                    break;
                case kReverbTail:
                    reverb.reset();
                    break;
            }
            tailsDue &= ~t;
        }
        tailsDirty |= t;
    }

    // A read head on the history moving `rate` samples per sample: Hermite up to one sample per
    // sample; faster (Tape's scratch, Grain's pitch up), band-limited for the speed it really moves
    // at, `step` (History::readFast: a sinc kernel on the decimated copy that fits), so neither the
    // interpolation's images nor what lies above sr / (2 step) fold back as inharmonic aliases.
    static void readHistory (const History& h, double pos, double rate, double step, int64_t now, float& a, float& b)
    {
        if (std::abs (rate) > 1.0)
            h.readFast (pos, step, now, a, b);
        else
            h.read (pos, now, a, b);
    }

    static float curved (float u, float curve) { return std::pow (jlimit (0.0f, 1.0f, u), std::pow (4.0f, (curve - 0.5f) * 2.0f)); }

    // ---- Tape & Vinyl: the read head slows down, speeds up or scratches ----
    void tape (const float* p, const History& h, const Clock& c, float* l, float* r, int n)
    {
        const auto fx = Fx::Tape;
        const int mode = stepValue (paramDef (fx, 0), p[0]);
        const double time = jmax (64.0, lengthBeats (paramDef (fx, 1), p[1]) * c.samplesPerBeat);
        const double scratch = jmax (64.0, lengthBeats (paramDef (fx, 3), p[3]) * c.samplesPerBeat);
        const float depth = p[4];
        for (int i = 0; i < n; ++i)
        {
            const int64_t now = c.counter + i;
            const double t = (double) (now - trigCounter);
            float rate = 1.0f;
            switch (mode)
            {
                case 0: rate = 1.0f - curved ((float) (t / time), p[2]); break;                     // stop
                case 1: rate = curved ((float) (t / time), p[2]); break;                            // start
                case 2: rate = t < time ? 1.0f - curved ((float) (t / time), p[2])                  // stop, then start
                                        : curved ((float) ((t - time) / time), p[2]); break;
                default: rate = (1.0f - depth) + depth * 2.5f * (float) std::sin (Pi<double>::twoPi * t / scratch); break;
            }
            // caught up with the input, the head moves one sample per sample whatever the rate
            const double step = rate > 1.0f ? jmin ((double) rate, (double) now + 1.0 - tapePos) : (double) rate;
            float a, b;
            readHistory (h, tapePos, rate, step, now, a, b);
            const float g = jmin (1.0f, std::abs (rate) * 4.0f); // no stuck sample when the tape stands still
            l[i] = a * g;
            r[i] = b * g;
            tapePos = jmin ((double) now + 1.0, tapePos + rate);
        }
    }

    // ---- Grain: two overlapping grains, read from the history ----
    void grain (const float* p, int seed, int slot, const History& h, const Clock& c, float* l, float* r, int n)
    {
        const auto fx = Fx::Grain;
        const int size = jmax (32, (int) (rangeValue (paramDef (fx, 0), p[0]) * 0.001f * (float) sr));
        const double pitch = semitonesToRatio (semitoneValue (paramDef (fx, 1), p[1]));
        const int mode = stepValue (paramDef (fx, 3), p[3]);
        for (int i = 0; i < n; ++i)
        {
            const int64_t now = c.counter + i;
            l[i] = r[i] = 0.0f;
            for (int v = 0; v < 2; ++v)
            {
                // the second grain starts half a grain after the first
                if (grainWait[v] < 0)
                    grainWait[v] = size / 2;
                if (grainWait[v] > 0)
                {
                    --grainWait[v];
                    continue;
                }
                if (grainLeft[v] <= 0)
                {
                    // a new grain: where it reads, how fast, with its own (seeded) jitter and spray
                    float u, w;
                    draw2 (seed, 32 + slot, (int64_t) grainIndex++, u, w);
                    const double back = (double) p[2] * u * sr * 0.25;
                    const double rate = pitch * std::pow (2.0, (double) (w * 2.0f - 1.0f) * p[4]);
                    const double anchor = mode == 0 ? (double) trigCounter - size * rate * 2.0 : (double) now - size * rate * 2.0;
                    grainPos[v] = anchor - back + (mode == 2 ? size * rate : 0.0);
                    grainRate[v] = mode == 2 ? -rate : rate;
                    grainLen[v] = grainLeft[v] = size;
                }
                const float ph = 1.0f - (float) grainLeft[v] / (float) grainLen[v];
                const float win = std::sin (ph * Pi<float>::pi);
                float a, b;
                readHistory (h, grainPos[v], grainRate[v], grainRate[v], now, a, b);
                l[i] += a * win * win;
                r[i] += b * win * win;
                grainPos[v] += grainRate[v];
                --grainLeft[v];
            }
        }
    }

    // ---- Crush: bits and sample rate ----
    void crush (const float* p, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Crush;
        const float levels = std::pow (2.0f, (float) countValue (paramDef (fx, 0), p[0]) - 1.0f);
        const float step = jmin (1.0f, rangeValue (paramDef (fx, 1), p[1]) / (float) sr);
        const bool unsignedMode = stepValue (paramDef (fx, 2), p[2]) == 1;
        auto quantise = [&] (float x) { return (unsignedMode ? std::floor (x * levels) : std::round (x * levels)) / levels; };
        for (int i = 0; i < n; ++i)
        {
            crushPhase += step;
            if (crushPhase >= 1.0f)
            {
                crushPhase -= 1.0f;
                holdL = quantise (inL[i]);
                holdR = quantise (inR[i]);
            }
            l[i] = holdL;
            r[i] = holdR;
        }
    }

    // ---- Drive: Razor (hard clip), Shape (tanh), Fold, Shift (asymmetric), then a tone ----
    // Each curve runs through first-order ADAA (bidoo-dsp's shaping/Adaa.h: what would fold back
    // above Nyquist is taken down, for half a sample of delay); Fold, whose partials reach far
    // higher, also runs at twice the rate. tanh's antiderivative (log cosh) is read from bidoo-dsp's
    // table (shaping/AdaaTanhTable.h, 128 segments per unit on [0, 10]: no exp or log per sample).
    void drive (const float* p, const float* inL, const float* inR, float* l, float* r, int n)
    {
        using namespace bidoo::dsp;
        const auto fx = Fx::Drive;
        const int curve = stepValue (paramDef (fx, 0), p[0]);
        const float gain = decibelsToGain (rangeValue (paramDef (fx, 1), p[1]));
        const float tone = 1.0f - std::exp (-Pi<float>::twoPi * rangeValue (paramDef (fx, 2), p[2]) / (float) sr);
        const float dcCoeff = 1.0f - 2.0f * Pi<float>::pi * 10.0f / (float) sr;
        if (curve != driveCurve)
        {
            // another curve (or another rate for Fold): the antiderivative kept for the last input is the old curve's
            for (auto& s : driveAdaa)
                s.primed = 0.0;
            if ((curve == 2) != (driveCurve == 2))
                for (auto& o : driveOs)
                    o.reset();
            driveCurve = curve;
        }
        float* outs[2] = { l, r };
        const float* ins[2] = { inL, inR };
        float* tones[2] = { &toneL, &toneR };
        auto run = [&] (const auto& shape, int factor)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& state = driveAdaa[ch];
                for (int i = 0; i < n; ++i)
                {
                    const float x = ins[ch][i] * gain;
                    const float y = (factor == 1 ? (float) adaa<1> (state, (double) x, shape)
                                                 : driveOs[ch].process (x, factor, [&] (float v) { return (float) adaa<1> (state, (double) v, shape); }))
                                    * 0.8f;
                    // the DC the asymmetric curve leaves goes
                    const float dc = y - dcIn[ch] + dcCoeff * dcOut[ch];
                    dcIn[ch] = y;
                    dcOut[ch] = dc;
                    *tones[ch] += tone * (dc - *tones[ch]);
                    outs[ch][i] = *tones[ch];
                }
            }
        };
        switch (curve)
        {
            case 0:  run (AdaaHardClip {}, 1); break;
            case 2:  run (AdaaScaled<AdaaSine> (1.0, Pi<double>::halfPi), kDriveFoldFactor); break;
            case 3:  run (AdaaBiased<AdaaTanhTable<128>> (0.5), 1); break; // tanh (x + 0.5) - tanh (0.5)
            default: run (AdaaTanhTable<128> {}, 1); break;
        }
    }

    // ---- Ring: the input times an oscillator ----
    void ring (const float* p, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Ring;
        const double f = rangeValue (paramDef (fx, 0), p[0]);
        const int wave = stepValue (paramDef (fx, 1), p[1]);
        const double inc[2] = { f / sr, f * (1.0 + 0.1 * p[2]) / sr };
        // the triangle's corners and the square's and saw's edges band-limited (bidoo-dsp's 24-tap
        // Kaiser BLEP / BLAMP, looking ahead: no delay); the carrier reaches 5 kHz
        const auto& tables = bidoo::dsp::KaiserBlepTables::get();
        auto osc = [wave, &tables] (double ph, double dt) {
            const float x = (float) ph;
            const double d0 = ph < 0.5 ? ph : ph - 1.0, dHalf = ph - 0.5; // from the wrap and from the middle
            switch (wave)
            {
                case 1:  return 1.0f - 4.0f * std::abs (x - 0.5f)
                                + (float) (8.0 * dt * (bidoo::dsp::kaiserBlamp (tables, d0, dt) - bidoo::dsp::kaiserBlamp (tables, dHalf, dt)));
                case 2:  return (ph < 0.5 ? 1.0f : -1.0f)
                                + (float) (2.0 * (bidoo::dsp::kaiserBlep (tables, d0, dt) - bidoo::dsp::kaiserBlep (tables, dHalf, dt)));
                case 3:  return 2.0f * x - 1.0f - (float) (2.0 * bidoo::dsp::kaiserBlep (tables, d0, dt));
                default: return std::sin (x * Pi<float>::twoPi);
            }
        };
        for (int i = 0; i < n; ++i)
        {
            l[i] = inL[i] * osc (ringPhase[0], inc[0]);
            r[i] = inR[i] * osc (ringPhase[1], inc[1]);
            for (int ch = 0; ch < 2; ++ch)
            {
                ringPhase[ch] += inc[ch];
                ringPhase[ch] -= std::floor (ringPhase[ch]);
            }
        }
    }

    // ---- Delay: synced time, feedback through a tone, ping-pong or a tape that warps ----
    void delay (const float* p, const Clock& c, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Delay;
        useTail (kDelayTail);
        const double target = jlimit (16.0, (double) delayMax, lengthBeats (paramDef (fx, 0), p[0]) * c.samplesPerBeat);
        const float feedback = p[1] * 0.95f;
        const int mode = stepValue (paramDef (fx, 2), p[2]);
        const float tone = 1.0f - std::exp (-Pi<float>::twoPi * rangeValue (paramDef (fx, 3), p[3]) / (float) sr);
        // a time change glides: quickly, or slowly enough to bend the pitch (Tape)
        const double glide = 1.0 - std::exp (-1.0 / ((mode == 2 ? 0.25 : 0.01) * sr));
        if (delayTime < 0.0)
            delayTime = target;
        for (int i = 0; i < n; ++i)
        {
            delayTime += glide * (target - delayTime);
            // delayTime after the sample was written: delayTime - 1 behind the newest one
            const double age = delayTime - 1.0;
            const auto whole = (int) age;
            const float f = (float) (age - whole);
            const float dl = hermiteAt (delayLine[0], whole, f), dr = hermiteAt (delayLine[1], whole, f);
            toneL += tone * (dl - toneL);
            toneR += tone * (dr - toneR);
            const float fbL = mode == 1 ? toneR : toneL, fbR = mode == 1 ? toneL : toneR;
            const float sendL = mode == 1 ? 0.5f * (inL[i] + inR[i]) : inL[i];
            delayLine[0].push (sendL + fbL * feedback);
            delayLine[1].push ((mode == 1 ? 0.0f : inR[i]) + fbR * feedback);
            l[i] = inL[i] + dl;
            r[i] = inR[i] + dr;
        }
    }

    // ---- Filter: resonant low / high / band pass, notch, or a comb tuned to the cutoff ----
    void filterFx (const float* p, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Filter;
        const int type = stepValue (paramDef (fx, 0), p[0]);
        const float cutoff = rangeValue (paramDef (fx, 1), p[1]);
        const float reso = p[2];
        if (type < 4)
        {
            svf.set (cutoff, 0.5f + reso * reso * 14.0f, sr);
            for (int i = 0; i < n; ++i)
            {
                l[i] = svf.tick (0, inL[i], type + 1);
                r[i] = svf.tick (1, inR[i], type + 1);
            }
            return;
        }
        useTail (kDelayTail);
        const double d = jlimit (2.0, (double) delayMax, sr / cutoff);
        const float fb = reso * 0.97f;
        const FixedRead tap (d - 1.0); // d after the sample was written
        for (int i = 0; i < n; ++i)
        {
            float y[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                y[ch] = (ch == 0 ? inL[i] : inR[i]) + fb * tap.read (delayLine[ch]);
                delayLine[ch].push (y[ch]);
            }
            l[i] = y[0] * (1.0f - fb * 0.5f);
            r[i] = y[1] * (1.0f - fb * 0.5f);
        }
    }

    // ---- Flanger / Chorus: a short delay swept by an LFO (one per side, spread apart) ----
    void flanger (const float* p, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Flanger;
        const bool chorus = stepValue (paramDef (fx, 0), p[0]) == 1;
        const double rate = rangeValue (paramDef (fx, 1), p[1]);
        const float depth = p[2], feedback = chorus ? 0.0f : p[3] * 0.9f, spread = p[4] * 0.5f;
        useTail (kDelayTail);
        const float centreMs = chorus ? 15.0f : 3.0f, sweepMs = chorus ? 10.0f * depth : 2.8f * depth;
        const float* ins[2] = { inL, inR };
        float* outs[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                const float lfo = std::sin ((float) (modPhase + (ch == 1 ? spread : 0.0f)) * Pi<float>::twoPi);
                const double d = jlimit (2.0, (double) delayMax, (double) (centreMs + sweepMs * lfo) * 0.001 * sr);
                const float delayed = delayLine[ch].readHermite ((float) (d - 1.0)); // d after the sample was written
                delayLine[ch].push (ins[ch][i] + feedback * delayed);
                outs[ch][i] = 0.7f * (ins[ch][i] + delayed);
            }
            modPhase += rate / sr;
            modPhase -= std::floor (modPhase);
        }
    }

    // ---- Phaser: swept all-pass stages with feedback ----
    // The all-pass coefficient follows the LFO exactly every kPhaserStep samples of the history's
    // counter (so whatever the buffer size) and in a straight line between: the sine, power and
    // tangent once per step instead of every sample.
    static constexpr int kPhaserStep = 8;

    void phaser (const float* p, const Clock& c, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Phaser;
        const double rate = rangeValue (paramDef (fx, 0), p[0]);
        const float depth = p[1], feedback = p[2] * 0.9f;
        const int stages = std::array<int, 4> { 4, 6, 8, 12 }[(size_t) stepValue (paramDef (fx, 3), p[3])];
        const float centre = rangeValue (paramDef (fx, 4), p[4]);
        const double inc = rate / sr;
        const float* ins[2] = { inL, inR };
        float* outs[2] = { l, r };
        auto coefficient = [&] (double phase, int ch)
        {
            const float lfo = std::sin ((float) (phase + (ch == 1 ? 0.25 : 0.0)) * Pi<float>::twoPi);
            const float f = jlimit (20.0f, (float) sr * 0.45f, centre * std::pow (2.0f, depth * 2.0f * lfo));
            const float t = std::tan (Pi<float>::pi * f / (float) sr);
            return (t - 1.0f) / (t + 1.0f);
        };
        for (int i = 0; i < n; ++i)
        {
            const auto into = (int) ((c.counter + i) & (kPhaserStep - 1));
            if (into == 0 || ! phaserLive)
            {
                // the next point on the step grid: the LFO's value there, reached in a line from now
                const int left = kPhaserStep - into;
                for (int ch = 0; ch < 2; ++ch)
                {
                    if (! phaserLive)
                        phaserA[ch] = coefficient (modPhase, ch);
                    phaserSlope[ch] = (coefficient (modPhase + left * inc, ch) - phaserA[ch]) / (float) left;
                }
                phaserLive = true;
            }
            for (int ch = 0; ch < 2; ++ch)
            {
                const float a = phaserA[ch];
                phaserA[ch] += phaserSlope[ch];
                float x = ins[ch][i] + feedback * phaserLast[ch];
                for (int s = 0; s < stages; ++s)
                {
                    // first-order all-pass: y = a x + z, z = x - a y
                    const float y = a * x + allpass[s][ch];
                    allpass[s][ch] = x - a * y;
                    x = y;
                }
                phaserLast[ch] = x;
                outs[ch][i] = 0.5f * (ins[ch][i] + x);
            }
            modPhase += rate / sr;
            modPhase -= std::floor (modPhase);
        }
    }

    // ---- Vowel: three band passes on the formants of A, E, I, O, U (gliding between them) ----
    void vowel (const float* p, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Vowel;
        static const float table[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                           { 570, 840, 2410 }, { 300, 870, 2240 } };
        const float v = rangeValue (paramDef (fx, 0), p[0]);
        const int a = jlimit (0, 4, (int) v), b = jmin (4, a + 1);
        const float t = v - (float) a;
        const float shift = (float) semitonesToRatio (semitoneValue (paramDef (fx, 2), p[2]));
        const float q = 2.0f + p[1] * p[1] * 18.0f;
        static const float gains[3] = { 1.0f, 0.7f, 0.45f };
        for (int k = 0; k < 3; ++k)
            formant[k].set ((table[a][k] + (table[b][k] - table[a][k]) * t) * shift, q, sr);
        for (int i = 0; i < n; ++i)
        {
            float yl = 0.0f, yr = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                yl += gains[k] * formant[k].tick (0, inL[i], 3);
                yr += gains[k] * formant[k].tick (1, inR[i], 3);
            }
            l[i] = yl * q * 0.9f; // narrow bands pass little: brought back to about the input's level
            r[i] = yr * q * 0.9f;
        }
    }

    // ---- Reactor: a transient starts a loop of what follows it (from the history), played Repeats
    // times (16: until the next transient); between loops the input passes ----
    void reactor (const float* p, const History& h, const Clock& c, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Reactor;
        const float sense = p[0];
        const double len = jmax (64.0, lengthBeats (paramDef (fx, 1), p[1]) * c.samplesPerBeat);
        const int repeats = countValue (paramDef (fx, 2), p[2]);
        const float decay = p[3];
        const float ratio = 1.3f + (1.0f - sense) * 4.0f;           // how much the fast envelope must jump
        const float fastC = 1.0f - std::exp (-1.0f / (0.001f * (float) sr));
        const float slowC = 1.0f - std::exp (-1.0f / (0.08f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            const int64_t now = c.counter + i;
            const float x = jmax (std::abs (inL[i]), std::abs (inR[i]));
            reactorFast += fastC * (x - reactorFast);
            reactorSlow += slowC * (x - reactorSlow);
            if (reactorHoldoff > 0)
                --reactorHoldoff;
            else if (reactorFast > 0.02f && reactorFast > reactorSlow * ratio)
            {
                // a transient: its slice plays now, then repeats
                reactorLooping = true;
                loopStart = now;
                segStart = now;
                segLen = len;
                repeatIndex = 0;
                repeatGain = 1.0f;
                reactorHoldoff = (int) len;
                tap.jump ((double) now, 1.0, crossfadeFor (len));
            }
            if (reactorLooping && (double) (now - segStart) >= segLen)
            {
                ++repeatIndex;
                segStart = now;
                if (repeats < 16 && repeatIndex >= repeats)
                {
                    reactorLooping = false; // done: the input again
                    tap.jump ((double) now, 1.0, crossfadeFor (len));
                }
                else
                {
                    repeatGain *= 1.0f - 0.5f * decay;
                    tap.jump ((double) loopStart, 1.0, crossfadeFor (len));
                }
            }
            tap.read (h, now, l[i], r[i]);
            if (reactorLooping && repeatIndex > 0)
            {
                l[i] *= repeatGain;
                r[i] *= repeatGain;
            }
        }
    }

    // ---- Tonalizer: comb resonators tuned to the notes of a chord ----
    void tonalizer (const float* p, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Tonalizer;
        const int root = stepValue (paramDef (fx, 0), p[0]);
        const auto& chord = chordNotes()[(size_t) stepValue (paramDef (fx, 1), p[1])];
        const int octave = countValue (paramDef (fx, 2), p[2]);
        const float fb = 0.5f + 0.48f * p[3];
        const float damp = 1.0f - std::exp (-Pi<float>::twoPi * rangeValue (paramDef (fx, 4), p[4]) / (float) sr);
        useTail (kTonalTail);
        FixedRead taps[4];
        for (int v = 0; v < 4; ++v)
        {
            const double midi = 12.0 * (octave + 1) + root + chord[(size_t) v];
            const double d = jlimit (2.0, (double) tonalMax, sr / (440.0 * std::pow (2.0, (midi - 69.0) / 12.0)));
            taps[v] = FixedRead (d - 1.0); // d after the sample was written
        }
        const float* ins[2] = { inL, inR };
        float* outs[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                float sum = 0.0f;
                for (int v = 0; v < 4; ++v)
                {
                    auto& b = tonalLine[v][ch];
                    const float d = taps[v].read (b);
                    tonalLp[v][ch] += damp * (d - tonalLp[v][ch]);
                    const float y = ins[ch][i] * 0.25f + fb * tonalLp[v][ch];
                    b.push (y);
                    sum += y;
                }
                outs[ch][i] = sum * 0.8f;
            }
        }
    }

    // ---- Vocoder: sixteen bands of the input, played by a chord of saws ----
    void vocoder (const float* p, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Vocoder;
        const int root = stepValue (paramDef (fx, 0), p[0]);
        const auto& chord = chordNotes()[(size_t) stepValue (paramDef (fx, 1), p[1])];
        const int octave = countValue (paramDef (fx, 2), p[2]);
        const float release = 1.0f - std::exp (-1.0f / (rangeValue (paramDef (fx, 3), p[3]) * 0.001f * (float) sr));
        const float attack = 1.0f - std::exp (-1.0f / (0.002f * (float) sr));
        const float formantShift = (float) semitonesToRatio (semitoneValue (paramDef (fx, 4), p[4]));
        if (std::abs (formantShift - vocShift) > 1.0e-4f || ! vocReady)
        {
            for (int k = 0; k < kVocBands; ++k)
            {
                // 100 Hz .. 8 kHz, 0.42 octave apart; Q 3.4 is a bandwidth of that spacing, so the bands
                // meet near -3 dB (at Q 6 the summed response rippled 7 dB, notes between bands sank)
                const float f = 100.0f * std::pow (80.0f, (float) k / (float) (kVocBands - 1));
                vocAnalysis[k].set (f, kVocQ, sr);
                vocSynthesis[k].set (jmin ((float) sr * 0.45f, f * formantShift), kVocQ, sr);
            }
            vocShift = formantShift;
            vocReady = true;
        }
        double inc[4];
        for (int v = 0; v < 4; ++v)
            inc[v] = 440.0 * std::pow (2.0, (12.0 * (octave + 1) + root + chord[(size_t) v] - 69.0) / 12.0) / sr;
        for (int i = 0; i < n; ++i)
        {
            // the carrier: four band-limited saws (polyBLEP)
            float carrier = 0.0f;
            for (int v = 0; v < 4; ++v)
            {
                double& ph = sawPhase[v];
                const double dt = inc[v];
                double blep = 0.0;
                if (ph < dt)
                {
                    const double t = ph / dt;
                    blep = t + t - t * t - 1.0;
                }
                else if (ph > 1.0 - dt)
                {
                    const double t = (ph - 1.0) / dt;
                    blep = t * t + t + t + 1.0;
                }
                const float s = (float) (2.0 * ph - 1.0 - blep);
                carrier += s * 0.25f;
                ph += dt;
                ph -= std::floor (ph);
            }
            const float x = 0.5f * (inL[i] + inR[i]);
            float y = 0.0f;
            for (int k = 0; k < kVocBands; ++k)
            {
                const float band = std::abs (vocAnalysis[k].tick (0, x, 3));
                vocEnv[k] += (band > vocEnv[k] ? attack : release) * (band - vocEnv[k]);
                y += vocSynthesis[k].tick (0, carrier, 3) * vocEnv[k];
            }
            l[i] = r[i] = y * 3.0f; // the wider bands pass 5 to 7 dB more (was 6 at Q 6): about the same level
        }
    }

    // ---- Filter Pattern: the cutoff steps through a pattern on the grid (from 60 Hz up Range
    // octaves), gliding over 2 ms; the coefficients every 8 samples of the history's counter ----
    void filterPattern (const float* p, const Clock& c, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::FilterPattern;
        const char* pattern = filterPatterns()[(size_t) (countValue (paramDef (fx, 0), p[0]) - 1)];
        const double beats = lengthBeats (paramDef (fx, 1), p[1]);
        const float octaves = rangeValue (paramDef (fx, 2), p[2]);
        const float q = 0.6f + p[3] * p[3] * 12.0f;
        const int mode = std::array<int, 3> { 1, 3, 2 }[(size_t) stepValue (paramDef (fx, 4), p[4])];
        const float glide = 1.0f - std::exp (-1.0f / (0.002f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            const auto step = (int64_t) std::floor (c.posAt (i) / beats + 1.0e-9);
            const char digit = pattern[(size_t) (((step % 16) + 16) % 16)];
            const float level = (float) hexDigitValue (digit) / 15.0f;
            const float target = std::log (60.0f) + level * octaves * 0.6931472f; // ln 2
            patternFreq = patternFreq < 0.0f ? target : patternFreq + glide * (target - patternFreq);
            if (((c.counter + i) & 7) == 0)
                svf.set (std::exp (patternFreq), q, sr);
            l[i] = svf.tick (0, inL[i], mode);
            r[i] = svf.tick (1, inR[i], mode);
        }
    }

#ifndef BIDOO_DSP_GLITCH_LITE
    // ---- Spectral: a delay for each of 32 bands, spread by a pattern ----
    void spectralFx (const float* p, int seed, int slot, const Clock& c, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Spectral;
        const float time = (float) (lengthBeats (paramDef (fx, 0), p[0]) * c.samplesPerBeat);
        const int pattern = stepValue (paramDef (fx, 1), p[1]);
        const float spread = p[2];
        float delays[SpectralDelay::kBands];
        for (int b = 0; b < SpectralDelay::kBands; ++b)
        {
            const float x = (float) b / (float) (SpectralDelay::kBands - 1);
            float shape;
            switch (pattern)
            {
                case 1:  shape = 1.0f - x; break;                          // falling: the lows last
                case 2:  shape = b % 2 == 0 ? 0.25f : 1.0f; break;         // alternate
                case 3:  shape = std::abs (x * 2.0f - 1.0f); break;        // V: lows and highs last
                case 4:                                                    // random (the scene's seed)
                {
                    float u, v;
                    draw2 (seed, 500 + slot, b, u, v);
                    shape = u;
                    break;
                }
                default: shape = x; break;                                 // rising: the highs last
            }
            delays[b] = time * ((1.0f - spread) + spread * shape);
        }
        spectral.setBands (delays, p[3], stepValue (paramDef (fx, 4), p[4]) == 1);
        spectral.process (inL, inR, l, r, n);
    }

#endif

    // ---- Reverb (bidoo-dsp's FdnEcoReverb, the economy Grotte-style network), with the dry kept and a freeze ----
    // (It replaced juce::Reverb, a Freeverb: Size keeps the same reverb times through
    // rt60FromCombRoom, Damp the same share of dark; the wet is at JUCE's level. The dry stays at the
    // input's level, as Delay's does: JUCE's doubled dry made a slot 6 dB louder at full mix.)
    void reverbFx (const float* p, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Reverb;
        const float room = 0.3f + 0.69f * p[0];
        bidoo::dsp::reverb::FdnReverbParams rp;
        rp.rt60 = bidoo::dsp::reverb::rt60FromCombRoom (room);
        rp.brightness = 1.0 - 0.7 * p[1];
        rp.size = 0.3 + 0.4 * room;
        rp.width = p[2];
        rp.freeze = stepValue (paramDef (fx, 3), p[3]) == 1;
        useTail (kReverbTail);
        for (int i = 0; i < n; i += 256)
        {
            const int m = std::min (256, n - i);
            float wl[256], wr[256];
            reverb.process (inL + i, inR + i, wl, wr, m, rp);
            for (int k = 0; k < m; ++k)
            {
                l[i + k] = inL[i + k] + wl[k] * 0.9f;
                r[i + k] = inR[i + k] + wr[k] * 0.9f;
            }
        }
    }

#ifndef BIDOO_DSP_GLITCH_LITE
    // ---- Texture: the recording of the input played back as grains, a stretch, a looping delay or a spectral smear ----
    void textureFx (const float* p, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Texture;
        const int mode = stepValue (paramDef (fx, 0), p[0]);
        if (mode != texture.mode())
            texture.setMode (mode); // restarts the recording
        texture.setPosition (p[1]);
        texture.setSize (p[2]);
        texture.setPitchSemitones ((float) semitoneValue (paramDef (fx, 3), p[3]));
        texture.setDensity (p[4]);
        texture.setTexture (0.5f);
        texture.setSpread (0.5f);
        texture.setBlend (1.0f); // wet only: the slot's mix and its delayed dry do the rest
        texture.process (inL, inR, l, r, n);
    }

    // ---- Cross Mod: the input and a second signal (an oscillator, or the side input) multiplied, folded, vocoded... ----
    static int crossModType (const float* p) { return stepValue (paramDef (Fx::CrossMod, 0), p[0]); }

    void crossModFx (const float* p, const float* inL, const float* inR, const float* sideL, const float* sideR,
                     float* l, float* r, int n)
    {
        using Algorithm = bidoo::dsp::CrossModulator::Algorithm;
        const auto fx = Fx::CrossMod;
        const int type = crossModType (p);
        const int source = stepValue (paramDef (fx, 2), p[2]);
        const float hz = rangeValue (paramDef (fx, 3), p[3]);
        const bool shifter = type == 7;
        const bool osc = source < 3;
        if (shifter)
        {
            crossMod.setFrequencyShifter (true);
            crossMod.setShiftHz (hz);
            crossMod.setShifterMix (1.0f);
            crossMod.setShifterFeedback (0.0f);
            crossMod.setOscillatorShape (osc ? source : 0);
        }
        else
        {
            static constexpr Algorithm algorithms[] = { Algorithm::Crossfade, Algorithm::Fold, Algorithm::DiodeRingMod, Algorithm::RingMod,
                                                        Algorithm::Xor, Algorithm::Comparator, Algorithm::Vocoder };
            crossMod.setFrequencyShifter (false);
            crossMod.setAlgorithm (algorithms[type]);
            crossMod.setOscillator (osc);
            crossMod.setOscillatorShape (jmin (source, 2));
            crossMod.setOscillatorHz (hz);
            crossMod.setLevel1 (p[4]);
            crossMod.setLevel2 (p[4]);
        }
        crossMod.setTimbre (p[1]);

        for (int i = 0; i < n; i += 64)
        {
            const int m = std::min (64, n - i);
            float a[64], b[64], x[64], y[64];
            for (int k = 0; k < m; ++k)
            {
                const float in = 0.5f * (inL[i + k] + inR[i + k]);
                if (shifter || osc)
                {
                    a[k] = 0.0f; // an oscillator is the carrier (the shifter has none): the input is what is modulated
                    b[k] = in;
                }
                else
                {
                    a[k] = in;
                    b[k] = 0.5f * (sideL[i + k] + sideR[i + k]);
                }
            }
            crossMod.process (a, b, x, y, m);
            for (int k = 0; k < m; ++k)
            {
                l[i + k] = x[k];
                r[i + k] = shifter ? y[k] : x[k]; // the shifter's other sideband on the right
            }
        }
    }

    // ---- Resonator: the input rings a string, a body or an FM voice ----
    static float noteOf (float hz) { return 69.0f + 12.0f * std::log2 (hz * (1.0f / 440.0f)); }

    void resonatorFx (const float* p, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Resonator;
        const int model = stepValue (paramDef (fx, 0), p[0]);
        if (model != resonator.model())
            resonator.setModel (model);
        const float note = noteOf (rangeValue (paramDef (fx, 1), p[1]));
        resonator.setNote (note);
        resonator.setTonic (note);
        resonator.setStructure (p[2]);
        resonator.setBrightness (p[3]);
        resonator.setDamping (p[4]);
        resonator.setPosition (0.5f);
        resonator.setMix (1.0f);
        resonator.process (inL, inR, l, r, n);
    }

    // ---- Modal: the input strikes a modal body or a string ----
    void modalFx (const float* p, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Modal;
        modal.setFrequencyHz (rangeValue (paramDef (fx, 0), p[0]));
        modal.setGeometry (p[1]);
        modal.setBrightness (p[2]);
        modal.setDamping (p[3]);
        modal.setSpace (p[4]);
        modal.setPosition (0.5f);
        modal.setMix (1.0f);
        modal.process (inL, inR, l, r, n);
    }

#endif

    // ---- Repeat (Glitch's Retrigger): slice, shrinking repeats, pitch, decay ----
    void repeat (const float* p, const History& h, const Clock& c, float* l, float* r, int n)
    {
        const auto fx = Fx::Repeat;
        const float shrink = p[1] * 0.5f; // each repeat up to half as long as the one before
        const double rate = semitonesToRatio (semitoneValue (paramDef (fx, 2), p[2]));
        const float decay = p[3];
        for (int i = 0; i < n; ++i)
        {
            const int64_t now = c.counter + i;
            if ((double) (now - segStart) >= segLen)
            {
                ++repeatIndex;
                segStart = now;
                segLen = jmax (32.0, sliceLen * std::pow (1.0 - (double) shrink, (double) repeatIndex));
                repeatGain *= 1.0f - 0.5f * decay;
                tap.jump ((double) trigCounter, rate, crossfadeFor (segLen));
            }
            tap.read (h, now, l[i], r[i]);
            l[i] *= repeatGain;
            r[i] *= repeatGain;
        }
    }

    // ---- Reverser: each slice plays the one before it backwards (Reverse), or the slice before the
    // block back and forth (Ping-pong) ----
    void reverser (const float* p, const History& h, const Clock& c, float* l, float* r, int n)
    {
        const auto fx = Fx::Reverser;
        const double len = jmax (32.0, lengthBeats (paramDef (fx, 0), p[0]) * c.samplesPerBeat);
        const bool pingPong = stepValue (paramDef (fx, 1), p[1]) == 1;
        const double rate = semitonesToRatio (semitoneValue (paramDef (fx, 2), p[2]));
        const double source = len * rate; // source samples one segment covers
        for (int i = 0; i < n; ++i)
        {
            const int64_t now = c.counter + i;
            if ((double) (now - segStart) >= segLen)
            {
                ++chunk;
                segStart = now;
                segLen = len;
                if (! pingPong || chunk % 2 == 0)
                {
                    // backwards from just before this segment (Reverse) or from the block's start (Ping-pong)
                    const double from = pingPong ? (double) trigCounter - 1.0 : (double) now - 1.0;
                    tap.jump (from, -rate, crossfadeFor (len));
                }
                else
                {
                    tap.jump ((double) trigCounter - source, rate, crossfadeFor (len));
                }
            }
            tap.read (h, now, l[i], r[i]);
        }
    }

    // ---- Slicer (Glitch's Shuffler, chance mode) ----
    void slicer (const float* p, int seed, int slot, const History& h, const Clock& c, float* l, float* r, int n)
    {
        const auto fx = Fx::Slicer;
        const double beats = lengthBeats (paramDef (fx, 0), p[0]);
        const double len = beats * c.samplesPerBeat;
        const int range = countValue (paramDef (fx, 1), p[1]);
        float chance[3] = { p[2], p[3], p[4] }; // shuffle, repeat, reverse
        const float total = chance[0] + chance[1] + chance[2];
        if (total > 1.0f)
            for (auto& x : chance)
                x /= total;

        for (int i = 0; i < n; ++i)
        {
            const int64_t now = c.counter + i;
            const auto slice = (int64_t) std::floor (c.posAt (i) / beats + 1.0e-9);
            if (slice != lastSlice)
            {
                lastSlice = slice;
                float u, v;
                draw2 (seed, slot, slice, u, v);
                const int xf = crossfadeFor (len);
                if (u < chance[0])
                {
                    const int back = 1 + jmin (range - 1, (int) (v * (float) range));
                    tap.jump ((double) now - back * len, 1.0, xf);
                    tapLive = false;
                }
                else if (u < chance[0] + chance[1])
                {
                    tap.jump ((double) now - len, 1.0, xf);
                    tapLive = false;
                }
                else if (u < chance[0] + chance[1] + chance[2])
                {
                    tap.jump ((double) now - 1.0, -1.0, xf);
                    tapLive = false;
                }
                else if (! tapLive)
                {
                    tap.jump ((double) now, 1.0, xf);
                    tapLive = true;
                }
            }
            tap.read (h, now, l[i], r[i]);
        }
    }

    // ---- Gate: a 16-step pattern, smoothed, with a depth ----
    void gate (const float* p, const Clock& c, const float* inL, const float* inR, float* l, float* r, int n)
    {
        const auto fx = Fx::Gate;
        const double beats = lengthBeats (paramDef (fx, 0), p[0]);
        const char* pattern = gatePatterns()[(size_t) (countValue (paramDef (fx, 1), p[1]) - 1)];
        const float smoothMs = 0.5f + p[2] * p[2] * 30.0f;
        const float coeff = 1.0f - std::exp (-1.0f / (float) (smoothMs * 0.001 * sr));
        const float floorGain = 1.0f - p[3];
        for (int i = 0; i < n; ++i)
        {
            const auto step = (int64_t) std::floor (c.posAt (i) / beats + 1.0e-9);
            const bool on = pattern[(size_t) (((step % 16) + 16) % 16)] == 'x';
            gateEnv += coeff * ((on ? 1.0f : floorGain) - gateEnv);
            l[i] = inL[i] * gateEnv;
            r[i] = inR[i] * gateEnv;
        }
    }

    double sr = 44100.0;
    int xfSamples = 128;
    Tap tap;
    int64_t trigCounter = 0, segStart = 0, lastSlice = -1, chunk = -1;
    double sliceLen = 0.0, segLen = 0.0;
    int repeatIndex = 0;
    float repeatGain = 1.0f, gateEnv = 1.0f;
    bool tapLive = false;

    // Tape, Grain
    double tapePos = 0.0;
    double grainPos[2] {}, grainRate[2] { 1.0, 1.0 };
    int grainLeft[2] {}, grainLen[2] { 1, 1 }, grainWait[2] {};
    int64_t grainIndex = 0;
    // Crush, Drive, Ring
    float crushPhase = 1.0f, holdL = 0.0f, holdR = 0.0f;
    float dcIn[2] {}, dcOut[2] {}, toneL = 0.0f, toneR = 0.0f;
    // Fold runs at twice the rate: the half-band filters there and back (IirHalfBand's 8 coefficients
    // at 0.040) delay it by about 3.07 samples at any rate, ADAA by a quarter more (half a sample of
    // the doubled rate); the slot delays its dry by the whole samples.
    static constexpr int kDriveFoldFactor = 2, kDriveFoldLatency = 3;
    bidoo::dsp::AdaaState driveAdaa[2];
    bidoo::dsp::IirShaperOversampler<> driveOs[2];
    int driveCurve = -1;
    double ringPhase[2] {};
    // Delay and the comb filter (and the flanger), the filter, the reverb
    bidoo::dsp::ModDelayLine delayLine[2];
    int delayMax = 0; // the longest delay, samples (2.5 s)
    double delayTime = -1.0;
    Svf svf;
    bidoo::dsp::reverb::FdnEcoReverb reverb;
    // Flanger, Phaser (their LFO), Vowel
    double modPhase = 0.0;
    float allpass[12][2] {}, phaserLast[2] {};
    float phaserA[2] {}, phaserSlope[2] {}; // the all-pass coefficient now and its step per sample
    bool phaserLive = false;                 // false: phaserA is not set yet
    Svf formant[3];
    // Reactor
    float reactorFast = 0.0f, reactorSlow = 0.0f;
    int reactorHoldoff = 0;
    bool reactorLooping = false;
    int64_t loopStart = 0;
    // Tonalizer
    bidoo::dsp::ModDelayLine tonalLine[4][2];
    float tonalLp[4][2] {};
    int tonalMax = 0; // the longest comb, samples (25 Hz)
    // the big buffers written since they were last cleared, and those a start from silence asked to clear (Tail bits)
    int tailsDirty = 0, tailsDue = 0;
    // Vocoder
    static constexpr int kVocBands = 16;
    static constexpr float kVocQ = 3.4f;
    Svf vocAnalysis[kVocBands], vocSynthesis[kVocBands];
    float vocEnv[kVocBands] {}, vocShift = 1.0f;
    bool vocReady = false;
    double sawPhase[4] {};
#ifndef BIDOO_DSP_GLITCH_LITE
    // Texture, Cross Mod, Resonator, Modal
    bidoo::dsp::Texture texture;
    bidoo::dsp::CrossModulator crossMod;
    bidoo::dsp::ResonatorFx resonator;
    bidoo::dsp::ModalFx modal;
#endif
    Fx activeFx = Fx::Off; // the effect the last trigger started
    // Spectral, Filter Pattern
#ifndef BIDOO_DSP_GLITCH_LITE
    SpectralDelay spectral;
#endif
    float patternFreq = -1.0f; // the cutoff's log, gliding (below 0: not started)
};
} // namespace bidoo::dsp::glitch

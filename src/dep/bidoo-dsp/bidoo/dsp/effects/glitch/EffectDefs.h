#pragma once

// What Mangle's effects are and what their parameters mean, without JUCE: the effect list, the
// musical lengths, each effect parameter's definition and how a normalised value reads in the
// effect's own terms. The engine and the effects use it; Params.h adds the host's side (the
// names, the value texts, the parameter class).
//
// Effect types are only ever appended, so a session keeps its effects when new ones arrive.

#include "Plain.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>

namespace bidoo::dsp::glitch
{
constexpr int kNumSlots = 8;
constexpr int kNumEffectParams = 5;

enum class Fx { Off = 0, Repeat, Reverser, Slicer, Gate, Tape, Grain, Crush, Drive, Ring, Delay, Filter, Reverb,
                Flanger, Phaser, Vowel, Reactor, Tonalizer, Vocoder, Spectral, FilterPattern,
                Texture, CrossMod, Resonator, Modal, count };

inline const char* const kEffectNames[] = { "Off", "Repeat", "Reverser", "Slicer", "Gate", "Tape", "Grain",
                                            "Crush", "Drive", "Ring", "Delay", "Filter", "Reverb",
                                            "Flanger", "Phaser", "Vowel", "Reactor", "Tonalizer", "Vocoder", "Spectral",
                                            "Filter Pattern", "Texture", "Cross Mod", "Resonator", "Modal" };
static_assert (std::size (kEffectNames) == (size_t) Fx::count);

// ---- musical lengths, longest first ----------------------------------------------------------
struct Length
{
    const char* label;
    double beats;
};

inline const std::array<Length, 10>& lengths()
{
    static const std::array<Length, 10> l { { { "1 bar", 4.0 }, { "1/2", 2.0 }, { "1/4", 1.0 }, { "1/8", 0.5 },
                                              { "1/8T", 1.0 / 3.0 }, { "1/16", 0.25 }, { "1/16T", 1.0 / 6.0 },
                                              { "1/32", 0.125 }, { "1/32T", 1.0 / 12.0 }, { "1/64", 0.0625 } } };
    return l;
}

// ---- the gate's 16-step patterns ---------------------------------------------------------------
inline const std::array<const char*, 12>& gatePatterns()
{
    static const std::array<const char*, 12> p { { "x.x.x.x.x.x.x.x.", "xx.xxx.xxx.xxx.x", "xxx.xxx.xxx.xxx.",
                                                   "xx..xx..xx..xx..", "x...x...x...x...", "x..x..x.x..x..x.",
                                                   "xx.x.xx.xx.x.xx.", "xxxx....xxxx....", "xxxxxxxx........",
                                                   "x.xx.x.xx.x.xxx.", "x..xx..xx..x.x.x", "xxxxxxx.x.x.xx.x" } };
    return p;
}

// ---- what each effect parameter means -------------------------------------------------------
enum class Kind { Unused, Length, Percent, Semitones, Choice, Count, Range };

struct ParamDef
{
    const char* name = "";
    Kind kind = Kind::Unused;
    float def = 0.0f;     // normalised default
    int lo = 0, hi = 0;   // Semitones: -lo..hi; Count: lo..hi; Length: index range in lengths()
    const char* const* choices = nullptr;
    int numChoices = 0;
    // Range: a value from `minV` to `maxV` (on a log scale with `logScale`), shown with `unit`
    float minV = 0.0f, maxV = 1.0f;
    bool logScale = false;
    const char* unit = "";
    int decimals = 0;
};

inline const char* const kReverseModes[] = { "Reverse", "Ping-pong" };
inline const char* const kTapeModes[] = { "Stop", "Start", "Stop + start", "Scratch" };
inline const char* const kGrainModes[] = { "Freeze", "Follow", "Reverse" };
inline const char* const kCrushModes[] = { "Signed", "Unsigned" };
inline const char* const kDriveModes[] = { "Razor", "Shape", "Fold", "Shift" };
inline const char* const kRingWaves[] = { "Sine", "Triangle", "Square", "Saw" };
inline const char* const kDelayModes[] = { "Normal", "Ping-pong", "Tape" };
inline const char* const kFilterModes[] = { "Low pass", "High pass", "Band pass", "Notch", "Comb" };
inline const char* const kOffOn[] = { "Off", "On" };
inline const char* const kFlangerModes[] = { "Flanger", "Chorus" };
inline const char* const kPhaserStages[] = { "4", "6", "8", "12" };
inline const char* const kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
inline const char* const kFilterPatternTypes[] = { "Low pass", "Band pass", "High pass" };

// Filter Pattern's 16-step cutoff patterns: one hex digit per step, 0 the bottom of the range, f the top.
inline const std::array<const char*, 12>& filterPatterns()
{
    static const std::array<const char*, 12> p { { "0123456789abcdef", "fedcba9876543210", "0f0f0f0f0f0f0f0f", "00ff00ff00ff00ff",
                                                   "048c048c048c048c", "c840c840c840c840", "08f808f808f808f8", "f0a05080f0a05080",
                                                   "0369cfc9630369cf", "f7f3f7f1f7f3f7f0", "0a3f0a3f50c50c5f", "0f8c3a6e1b7d2954" } };
    return p;
}

// Texture, Cross Mod, Resonator and Modal: bidoo-dsp's effects/mutable
inline const char* const kTextureModes[] = { "Granular", "Stretch", "Loop delay", "Spectral" };
inline const char* const kCrossModTypes[] = { "Crossfade", "Fold", "Diode ring", "Ring mod", "XOR", "Comparator", "Vocoder", "Freq shift" };
inline const char* const kCrossModSources[] = { "Osc 1", "Osc 2", "Osc 3", "Side input" };
inline const char* const kResonatorModels[] = { "Modal", "Sympathetic strings", "Inharmonic string", "FM voice", "Sympathetic chords",
                                                "String + reverb" };

inline const char* const kSpectralPatterns[] = { "Rising", "Falling", "Alternate", "V", "Random" };
inline const char* const kChords[] = { "Octaves", "Fifths", "Major", "Minor", "Sus4", "Maj7", "Min7" };

// The chords of the Tonalizer and the Vocoder: semitones above the root (up to four notes).
inline const std::array<std::array<int, 4>, 7>& chordNotes()
{
    static const std::array<std::array<int, 4>, 7> c { { { 0, 12, 24, 36 }, { 0, 7, 12, 19 }, { 0, 4, 7, 12 }, { 0, 3, 7, 12 },
                                                         { 0, 5, 7, 12 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 } } };
    return c;
}

inline float rangeValue (const ParamDef& d, float norm)
{
    norm = jlimit (0.0f, 1.0f, norm);
    return d.logScale ? d.minV * std::pow (d.maxV / d.minV, norm) : d.minV + (d.maxV - d.minV) * norm;
}

inline float rangeNorm (const ParamDef& d, float v)
{
    v = jlimit (jmin (d.minV, d.maxV), jmax (d.minV, d.maxV), v);
    return d.logScale ? std::log (v / d.minV) / std::log (d.maxV / d.minV) : (v - d.minV) / (d.maxV - d.minV);
}

// index in a stepped range <-> normalised value
inline float stepToNorm (int index, int numSteps) { return numSteps > 1 ? (float) index / (float) (numSteps - 1) : 0.0f; }
inline int normToStep (float norm, int numSteps) { return jlimit (0, numSteps - 1, (int) std::lround (norm * (float) (numSteps - 1))); }

inline float lengthNorm (const char* label, int lo = 0, int hi = 9)
{
    for (int i = lo; i <= hi; ++i)
        if (std::strcmp (lengths()[(size_t) i].label, label) == 0)
            return stepToNorm (i - lo, hi - lo + 1);
    return 0.0f;
}

inline const ParamDef& paramDef (Fx fx, int k)
{
    static const ParamDef none {};
    static const std::array<std::array<ParamDef, kNumEffectParams>, (size_t) Fx::count> defs = [] {
        std::array<std::array<ParamDef, kNumEffectParams>, (size_t) Fx::count> d {};
        auto length = [] (const char* n, const char* def, int lo, int hi) {
            ParamDef p; p.name = n; p.kind = Kind::Length; p.lo = lo; p.hi = hi; p.def = lengthNorm (def, lo, hi); return p;
        };
        auto percent = [] (const char* n, float def) { ParamDef p; p.name = n; p.kind = Kind::Percent; p.def = def; return p; };
        auto semis = [] (const char* n, int range) {
            ParamDef p; p.name = n; p.kind = Kind::Semitones; p.lo = range; p.hi = range; p.def = 0.5f; return p;
        };
        auto choice = [] (const char* n, const char* const* c, int num, int def) {
            ParamDef p; p.name = n; p.kind = Kind::Choice; p.choices = c; p.numChoices = num; p.def = stepToNorm (def, num); return p;
        };
        auto range = [] (const char* n, float lo, float hi, float def, bool lg, const char* unit, int decimals) {
            ParamDef p; p.name = n; p.kind = Kind::Range; p.minV = lo; p.maxV = hi; p.logScale = lg; p.unit = unit;
            p.decimals = decimals; p.def = rangeNorm (p, def); return p;
        };
        auto count = [] (const char* n, int lo, int hi, int def) {
            ParamDef p; p.name = n; p.kind = Kind::Count; p.lo = lo; p.hi = hi; p.def = stepToNorm (def - lo, hi - lo + 1); return p;
        };

        // Repeat: the block's first slice plays, then repeats, each repeat shorter (Shrink), pitched and quieter
        d[(size_t) Fx::Repeat] = { length ("Size", "1/16", 1, 9), percent ("Shrink", 0.0f), semis ("Pitch", 12),
                                   percent ("Decay", 0.0f), {} };
        // Reverser: each slice plays the one before it backwards, or ping-pongs on the slice before the block
        ParamDef mode; mode.name = "Mode"; mode.kind = Kind::Choice; mode.choices = kReverseModes; mode.numChoices = 2;
        d[(size_t) Fx::Reverser] = { length ("Size", "1/8", 0, 7), mode, semis ("Pitch", 12), {}, {} };
        // Slicer: on every slice a seeded draw plays it as it is, or another one from up to Range slices back,
        // the one before again, or the one before backwards
        d[(size_t) Fx::Slicer] = { length ("Size", "1/16", 2, 9), count ("Range", 1, 16, 4), percent ("Shuffle", 0.4f),
                                   percent ("Repeat", 0.25f), percent ("Reverse", 0.15f) };
        // Gate: a 16-step pattern on the grid
        ParamDef pat; pat.name = "Pattern"; pat.kind = Kind::Count; pat.lo = 1; pat.hi = (int) gatePatterns().size(); pat.def = 0.0f;
        d[(size_t) Fx::Gate] = { length ("Rate", "1/16", 3, 9), pat, percent ("Smooth", 0.2f), percent ("Depth", 1.0f), {} };

        // Tape & Vinyl: the tape slows to a stop, speeds up from one, or scratches back and forth
        d[(size_t) Fx::Tape] = { choice ("Mode", kTapeModes, 4, 0), length ("Time", "1/4", 0, 7), percent ("Curve", 0.5f),
                                 length ("Scratch", "1/8", 2, 7), percent ("Depth", 0.7f) };
        // Grain: grains from the history, frozen where the block starts or following the input
        d[(size_t) Fx::Grain] = { range ("Size", 10.0f, 400.0f, 80.0f, true, " ms", 0), semis ("Pitch", 12), percent ("Jitter", 0.2f),
                                  choice ("Mode", kGrainModes, 3, 0), percent ("Spray", 0.1f) };
        // Crush: fewer bits, a lower sample rate
        d[(size_t) Fx::Crush] = { count ("Bits", 1, 16, 6), range ("Rate", 200.0f, 44100.0f, 6000.0f, true, " Hz", 0),
                                  choice ("Mode", kCrushModes, 2, 0), {}, {} };
        // Drive: four curves, a tone
        d[(size_t) Fx::Drive] = { choice ("Curve", kDriveModes, 4, 1), range ("Drive", 0.0f, 36.0f, 12.0f, false, " dB", 1),
                                  range ("Tone", 500.0f, 20000.0f, 8000.0f, true, " Hz", 0), {}, {} };
        // Ring: a ring modulator, its oscillator spread across the sides
        d[(size_t) Fx::Ring] = { range ("Freq", 20.0f, 5000.0f, 440.0f, true, " Hz", 0), choice ("Wave", kRingWaves, 4, 0),
                                 percent ("Spread", 0.0f), {}, {} };
        // Delay: synced, with feedback, ping-pong or a tape that warps when the time changes
        d[(size_t) Fx::Delay] = { length ("Time", "1/8", 1, 9), percent ("Feedback", 0.45f), choice ("Mode", kDelayModes, 3, 0),
                                  range ("Tone", 500.0f, 20000.0f, 6000.0f, true, " Hz", 0), {} };
        // Filter: a resonant filter, or a comb
        d[(size_t) Fx::Filter] = { choice ("Type", kFilterModes, 5, 0), range ("Cutoff", 30.0f, 18000.0f, 1200.0f, true, " Hz", 0),
                                   percent ("Reso", 0.3f), {}, {} };
        // Reverb: with a freeze
        d[(size_t) Fx::Reverb] = { percent ("Size", 0.6f), percent ("Damp", 0.4f), percent ("Width", 1.0f), choice ("Freeze", kOffOn, 2, 0), {} };

        // Flanger (or chorus): a swept short delay, with feedback for the flanger
        d[(size_t) Fx::Flanger] = { choice ("Mode", kFlangerModes, 2, 0), range ("Rate", 0.05f, 8.0f, 0.3f, true, " Hz", 2),
                                    percent ("Depth", 0.6f), percent ("Feedback", 0.5f), percent ("Spread", 0.5f) };
        // Phaser: 4 to 12 swept all-pass stages around a centre
        d[(size_t) Fx::Phaser] = { range ("Rate", 0.05f, 8.0f, 0.4f, true, " Hz", 2), percent ("Depth", 0.7f), percent ("Feedback", 0.5f),
                                   choice ("Stages", kPhaserStages, 4, 1), range ("Centre", 200.0f, 5000.0f, 1000.0f, true, " Hz", 0) };
        // Vowel: three formants, gliding from A to E, I, O and U
        d[(size_t) Fx::Vowel] = { range ("A E I O U", 0.0f, 4.0f, 0.0f, false, "", 2), percent ("Reso", 0.6f), semis ("Shift", 12), {}, {} };
        // Reactor: a transient starts a loop of what follows it, played Repeats times
        d[(size_t) Fx::Reactor] = { percent ("Sense", 0.5f), length ("Size", "1/16", 2, 9), count ("Repeats", 1, 16, 4),
                                    percent ("Decay", 0.0f), {} };
        // Tonalizer: resonators tuned to a chord
        d[(size_t) Fx::Tonalizer] = { choice ("Root", kNoteNames, 12, 0), choice ("Chord", kChords, 7, 3), count ("Octave", 1, 5, 3),
                                      percent ("Ring", 0.85f), range ("Damp", 1000.0f, 16000.0f, 6000.0f, true, " Hz", 0) };
        // Vocoder: the input's sixteen bands played by a chord of saws
        d[(size_t) Fx::Vocoder] = { choice ("Root", kNoteNames, 12, 0), choice ("Chord", kChords, 7, 3), count ("Octave", 1, 5, 2),
                                    range ("Release", 5.0f, 500.0f, 60.0f, true, " ms", 0), semis ("Formant", 12) };
        // Spectral (Turnado's Spectralizer): 32 bands, each with its own delay
        // Filter Pattern: a resonant filter stepping through a 16-step cutoff pattern on the grid
        ParamDef fpat; fpat.name = "Pattern"; fpat.kind = Kind::Count; fpat.lo = 1; fpat.hi = (int) filterPatterns().size(); fpat.def = 0.0f;
        d[(size_t) Fx::FilterPattern] = { fpat, length ("Rate", "1/16", 2, 8), range ("Range", 1.0f, 7.0f, 4.0f, false, " oct", 1),
                                          percent ("Reso", 0.5f), choice ("Type", kFilterPatternTypes, 3, 0) };
        d[(size_t) Fx::Spectral] = { length ("Time", "1/8", 2, 9), choice ("Pattern", kSpectralPatterns, 5, 0), percent ("Spread", 1.0f),
                                     percent ("Feedback", 0.4f), choice ("Freeze", kOffOn, 2, 0) };
        // Texture: records the input and plays it back in four ways (changing the mode restarts the recording)
        d[(size_t) Fx::Texture] = { choice ("Mode", kTextureModes, 4, 0), percent ("Position", 0.5f), percent ("Size", 0.5f),
                                    semis ("Pitch", 24), percent ("Density", 0.7f) };
        // Cross Mod: the input is the carrier and the Side input (the sidechain, or the plugin's input without one) the
        // modulator; with an Osc the oscillator is the carrier and the input the modulator. Freq is the oscillator's pitch
        // or the frequency shift, Timbre the algorithm's parameter (for Freq shift: 1 up, 0 down, 0.5 both)
        d[(size_t) Fx::CrossMod] = { choice ("Type", kCrossModTypes, 8, 3), percent ("Timbre", 0.5f), choice ("Source", kCrossModSources, 4, 0),
                                     range ("Freq", 1.0f, 2000.0f, 220.0f, true, " Hz", 0), percent ("Level", 0.5f) };
        // Resonator: the input rings a string, a body or an FM voice at Freq
        d[(size_t) Fx::Resonator] = { choice ("Model", kResonatorModels, 6, 0), range ("Freq", 30.0f, 2000.0f, 110.0f, true, " Hz", 0),
                                      percent ("Structure", 0.4f), percent ("Brightness", 0.5f), percent ("Damping", 0.6f) };
        // Modal: the input strikes a modal body or a string, with a space around it
        d[(size_t) Fx::Modal] = { range ("Freq", 30.0f, 2000.0f, 110.0f, true, " Hz", 0), percent ("Geometry", 0.3f), percent ("Brightness", 0.5f),
                                  percent ("Damping", 0.5f), percent ("Space", 0.3f) };
        return d;
    }();
    if (fx <= Fx::Off || fx >= Fx::count || k < 0 || k >= kNumEffectParams)
        return none;
    return defs[(size_t) fx][(size_t) k];
}

inline int numSteps (const ParamDef& d)
{
    switch (d.kind)
    {
        case Kind::Length:    return d.hi - d.lo + 1;
        case Kind::Choice:    return d.numChoices;
        case Kind::Count:     return d.hi - d.lo + 1;
        case Kind::Semitones: return d.lo + d.hi + 1;
        default:              return 0; // continuous
    }
}

// ---- reading a value in the effect's own terms (the DSP and the texts) ----
inline double lengthBeats (const ParamDef& d, float norm) { return lengths()[(size_t) (d.lo + normToStep (norm, numSteps (d)))].beats; }
inline int stepValue (const ParamDef& d, float norm) { return normToStep (norm, numSteps (d)); }
inline int countValue (const ParamDef& d, float norm) { return d.lo + normToStep (norm, numSteps (d)); }
inline int semitoneValue (const ParamDef& d, float norm) { return normToStep (norm, numSteps (d)) - d.lo; }

// The line-up a new instance starts with: the four effects, twice.
inline Fx defaultEffect (int slot)
{
    static const Fx lineUp[kNumSlots] = { Fx::Repeat, Fx::Slicer, Fx::Reverser, Fx::Gate,
                                          Fx::Repeat, Fx::Slicer, Fx::Reverser, Fx::Gate };
    return lineUp[slot];
}

// ---- the modulators ----
inline const std::array<Length, 10>& lfoRates()
{
    static const std::array<Length, 10> r { { { "4 bars", 16.0 }, { "2 bars", 8.0 }, { "1 bar", 4.0 }, { "1/2", 2.0 },
                                              { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3.0 }, { "1/16", 0.25 },
                                              { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 } } };
    return r;
}

// The range of a slot's filter frequency (the processor's parameter, freqRange() in Params.h, has
// the same one): the engine's modulation moves in it.
inline const SkewRange& freqMap()
{
    static const SkewRange r { 20.0f, 20000.0f, 1000.0f };
    return r;
}
} // namespace bidoo::dsp::glitch

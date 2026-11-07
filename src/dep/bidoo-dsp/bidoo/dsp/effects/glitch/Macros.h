#pragma once

// The macro mapping (Turnado's one knob): what a slot's drive does to its parameters, and which
// modulators move them.
//
// Every slot has seven targets: its five effect parameters, its filter frequency and its mix. Each
// target has
//   - an amount (-100..100 % of the knob's range) and a curve: the drive (0..1) moves the target
//     from where its knob is by amount x curve(drive);
//   - an assignment for each modulator (LFO 1, LFO 2, the envelope follower, and the global
//     sidechain envelope, seeded random and step index): none, UNI or BI (the shared modulation
//     model: one way from the knob, or both ways around it), and a signed depth (-1..1, 1 = the
//     modulator's full swing: at depth 100 % a BI LFO moves the target by the knob's whole range
//     both ways) that scales what that one assignment adds. Invert (a menu item) negates the depth.
//
// Edited on the message thread, read by the audio thread (atomics), part of the sound (presets,
// undo, A/B) through the kit's extra state; as XML in MacrosText.h.

#include "Plain.h"
#include <atomic>
#include <cmath>

namespace bidoo::dsp::glitch
{
constexpr int kNumTargets = 7;   // P1..P5, Frequency, Mix
constexpr int kTargetFreq = 5, kTargetMix = 6;
constexpr int kNumMods = 6;      // LFO 1, LFO 2, envelope follower (the slot's); sidechain, random, step (global)
constexpr int kNumSlotMods = 3;

enum class Curve : uint8_t { Linear = 0, Log, Exp, Delayed, Stepped, Gap, count };

inline float curveValue (Curve c, float d)
{
    d = jlimit (0.0f, 1.0f, d);
    switch (c)
    {
        case Curve::Log:     return 1.0f - (1.0f - d) * (1.0f - d) * (1.0f - d);
        case Curve::Exp:     return d * d * d;
        case Curve::Delayed: return jmax (0.0f, d * 2.0f - 1.0f);
        case Curve::Stepped: return std::floor (d * 4.0f + 1.0e-4f) * 0.25f;
        case Curve::Gap:     return d < 0.5f ? 0.0f : d;
        default:             return d;
    }
}

// The modulators' natural ranges: the LFOs and the random swing both ways (-1..1), the envelope
// follower, the sidechain and the step move one way (0..1). A new assignment starts there.
inline bool modIsBipolar (int mod) { return mod == 0 || mod == 1 || mod == 4; }

// The depth a new assignment gets: +25 % (bidoo::look::mod::kNewConnectionAmount, the same in every plugin).
constexpr float kNewAssignmentDepth = 0.25f;

class Macros
{
public:
    // An assignment's polarity (the shared modulation model): none, UNI (the target moves one way from
    // its knob: up with a positive depth, down with a negative one) or BI (both ways around it).
    enum Polarity : uint8_t { None = 0, Uni = 1, Bi = 2 };

    Macros() { clear(); }

    float amount (int slot, int target) const { return amounts[slot][target].load (std::memory_order_relaxed); }
    Curve curve (int slot, int target) const { return (Curve) curves[slot][target].load (std::memory_order_relaxed); }
    Polarity polarity (int slot, int target, int mod) const { return (Polarity) assigns[slot][target][mod].load (std::memory_order_relaxed); }
    bool isAssigned (int slot, int target, int mod) const { return polarity (slot, target, mod) != None; }
    bool isBipolar (int slot, int target, int mod) const { return polarity (slot, target, mod) == Bi; }

    float depth (int slot, int target, int mod) const { return depths[slot][target][mod].load (std::memory_order_relaxed); } // signed

    void setDepth (int slot, int target, int mod, float d)
    {
        depths[slot][target][mod].store (jlimit (-1.0f, 1.0f, d));
        touch();
    }

    void setAmount (int slot, int target, float a)
    {
        amounts[slot][target].store (jlimit (-1.0f, 1.0f, a));
        touch();
    }

    void setCurve (int slot, int target, Curve c)
    {
        curves[slot][target].store ((uint8_t) c);
        touch();
    }

    // Sets an assignment (None removes it) with its signed depth.
    void setAssignment (int slot, int target, int mod, Polarity p, float d)
    {
        depths[slot][target][mod].store (p == None ? 0.0f : jlimit (-1.0f, 1.0f, d));
        assigns[slot][target][mod].store ((uint8_t) p);
        touch();
    }

    void setPolarity (int slot, int target, int mod, Polarity p)
    {
        if (isAssigned (slot, target, mod) && p != None)
        {
            assigns[slot][target][mod].store ((uint8_t) p);
            touch();
        }
    }

    // Invert (the menus): negates the assignment's depth.
    void invert (int slot, int target, int mod) { setDepth (slot, target, mod, -depth (slot, target, mod)); }

    // A modulator dropped on a target (the editor's drag and drop): a new assignment gets the
    // modulator's natural range at +25 % (-25 % with `negative`); an existing one stays as it is.
    void assignMod (int slot, int target, int mod, bool negative)
    {
        if (! isAssigned (slot, target, mod))
            setAssignment (slot, target, mod, modIsBipolar (mod) ? Bi : Uni, negative ? -kNewAssignmentDepth : kNewAssignmentDepth);
    }

    // Everything of one target back to nothing (when its effect changes, the meaning goes).
    void clearTarget (int slot, int target)
    {
        amounts[slot][target].store (0.0f);
        curves[slot][target].store (0);
        for (int m = 0; m < kNumMods; ++m)
        {
            assigns[slot][target][m].store (None);
            depths[slot][target][m].store (0.0f);
        }
        touch();
    }

    void clear()
    {
        for (int s = 0; s < 8; ++s)
            for (int t = 0; t < kNumTargets; ++t)
                clearTarget (s, t);
    }

    void copyFrom (const Macros& other)
    {
        for (int s = 0; s < 8; ++s)
            for (int t = 0; t < kNumTargets; ++t)
            {
                amounts[s][t].store (other.amount (s, t));
                curves[s][t].store ((uint8_t) other.curve (s, t));
                for (int m = 0; m < kNumMods; ++m)
                {
                    assigns[s][t][m].store ((uint8_t) other.polarity (s, t, m));
                    depths[s][t][m].store (other.depth (s, t, m));
                }
            }
        touch();
    }

    int getRevision() const { return revision.load(); }

private:
    void touch() { revision.fetch_add (1); }

    std::atomic<float> amounts[8][kNumTargets];
    std::atomic<uint8_t> curves[8][kNumTargets];
    std::atomic<uint8_t> assigns[8][kNumTargets][kNumMods]; // Polarity
    std::atomic<float> depths[8][kNumTargets][kNumMods];    // signed, -1..1
    std::atomic<int> revision { 0 };
};
} // namespace bidoo::dsp::glitch

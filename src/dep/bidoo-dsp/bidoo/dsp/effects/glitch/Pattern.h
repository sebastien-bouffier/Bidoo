#pragma once

// The pattern: one lane per slot, plus the Chance lane (each of its blocks plays one slot, picked
// by the slots' weights with the scene's seed), up to 128 steps (16 beats of 8 steps). A lane holds
// blocks: runs of steps where the slot plays, each starting on a step marked as a block start, so
// two blocks can touch and still trigger twice.
//
// A block is a gesture: its start step also holds a level (0..100 %) and a shape (Hold, Ramp up,
// Ramp down, Swell, Steps). Together they are the slot's drive while the block plays, as if a hand
// turned the slot's knob. A block may also have
//   - a probability (it plays on a loop or not, a seeded draw for each loop),
//   - ratchets (it restarts its slot 2 to 8 times across its length),
//   - a parameter lock: the slot's seven knob targets (P1..P5, frequency, mix) as they were when
//     the lock was taken, used instead of the knobs while the block plays.
//
// The editor changes it (message thread), the audio thread reads it (and writes recorded knob
// moves): every cell is atomic, and a block being edited is never more than a moment's mix of
// before and after. As text and XML (presets, sessions): PatternText.h.

#include "Plain.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <utility>

namespace bidoo::dsp::glitch
{
enum class Shape : uint8_t { Hold = 0, RampUp, RampDown, Swell, Steps, count };

// The drive of a block of `level` (0..1) and `shape`, at `t` (0..1) through the block.
inline float shapeValue (Shape shape, float level, float t)
{
    t = jlimit (0.0f, 1.0f, t);
    switch (shape)
    {
        case Shape::RampUp:   return level * t;
        case Shape::RampDown: return level * (1.0f - t);
        case Shape::Swell:    return level * std::sin (t * Pi<float>::pi);
        case Shape::Steps:    return level * (std::floor (t * 4.0f - 1.0e-4f) + 1.0f) * 0.25f;
        default:              return level;
    }
}

constexpr int kLockValues = 7; // the slot's targets: P1..P5, frequency, mix (0..1)

// Everything a block holds besides its steps, kept at its start step.
struct BlockInfo
{
    uint8_t level = 100, shape = 0, probability = 100, ratchets = 1;
    bool locked = false;
    float lock[kLockValues] {};

    bool isPlain() const { return level == 100 && shape == 0 && probability == 100 && ratchets == 1 && ! locked; }
};

class Pattern
{
public:
    static constexpr int kLanes = 9, kMaxSteps = 128;
    static constexpr int kChanceLane = 8;
    enum : uint8_t { kOff = 0, kOn = 1, kStart = 3 };

    // A lane's content, to keep and put back (the editor's drags).
    struct Lane
    {
        uint8_t cell[kMaxSteps] {};
        BlockInfo info[kMaxSteps] {};
    };

    Pattern()
    {
        for (int l = 0; l < kLanes; ++l)
            for (int s = 0; s < kMaxSteps; ++s)
                writeInfo (l, s, {});
        clear();
    }

    uint8_t cell (int lane, int step) const { return cells[lane][step].load (std::memory_order_relaxed); }
    bool isOn (int lane, int step) const { return (cell (lane, step) & kOn) != 0; }
    bool isStart (int lane, int step) const { return cell (lane, step) == kStart; }

    // A block's gesture and the rest, read at its start step.
    float levelAt (int lane, int start) const { return (float) levels[lane][start].load (std::memory_order_relaxed) * 0.01f; }
    Shape shapeAt (int lane, int start) const { return (Shape) jmin ((int) Shape::count - 1, (int) shapes[lane][start].load (std::memory_order_relaxed)); }
    float probabilityAt (int lane, int start) const { return (float) probs[lane][start].load (std::memory_order_relaxed) * 0.01f; }
    int ratchetsAt (int lane, int start) const { return jlimit (1, 8, (int) ratchets[lane][start].load (std::memory_order_relaxed)); }
    bool isLocked (int lane, int start) const { return locked[lane][start].load (std::memory_order_relaxed) != 0; }
    float lockValue (int lane, int start, int k) const { return locks[lane][start][k].load (std::memory_order_relaxed); }

    BlockInfo infoAt (int lane, int start) const
    {
        BlockInfo b;
        b.level = levels[lane][start].load (std::memory_order_relaxed);
        b.shape = shapes[lane][start].load (std::memory_order_relaxed);
        b.probability = probs[lane][start].load (std::memory_order_relaxed);
        b.ratchets = ratchets[lane][start].load (std::memory_order_relaxed);
        b.locked = isLocked (lane, start);
        for (int k = 0; k < kLockValues; ++k)
            b.lock[k] = lockValue (lane, start, k);
        return b;
    }

    void setInfo (int lane, int start, const BlockInfo& b)
    {
        writeInfo (lane, start, b);
        touch();
    }

    // The block holding `step`: its first and last steps (or false).
    bool blockAt (int lane, int step, int& first, int& last) const
    {
        if (! isOn (lane, step))
            return false;
        first = step;
        while (first > 0 && ! isStart (lane, first))
            --first;
        last = step;
        while (last + 1 < kMaxSteps && cell (lane, last + 1) == kOn)
            ++last;
        return true;
    }

    // A block over [from, to], replacing what was there.
    void setBlock (int lane, int from, int to, const BlockInfo& info)
    {
        clampRange (from, to);
        const auto after = infoAfter (lane, to);
        for (int s = from; s <= to; ++s)
            set (lane, s, s == from ? kStart : kOn);
        writeInfo (lane, from, info);
        startAfter (lane, to, after);
        touch();
    }

    void setBlock (int lane, int from, int to, int level = 100, Shape shape = Shape::Hold)
    {
        BlockInfo b;
        b.level = (uint8_t) jlimit (0, 100, level);
        b.shape = (uint8_t) shape;
        setBlock (lane, from, to, b);
    }

    void setGesture (int lane, int start, int level, Shape shape)
    {
        auto b = infoAt (lane, start);
        b.level = (uint8_t) jlimit (0, 100, level);
        b.shape = (uint8_t) shape;
        setInfo (lane, start, b);
    }

    void erase (int lane, int from, int to)
    {
        clampRange (from, to);
        const auto after = infoAfter (lane, to);
        for (int s = from; s <= to; ++s)
            set (lane, s, kOff);
        startAfter (lane, to, after);
        touch();
    }

    // Splits a block at `step` (the new block takes everything of the first), or joins it to the block before.
    void splitOrJoin (int lane, int step)
    {
        if (! isOn (lane, step))
            return;
        if (isStart (lane, step))
        {
            if (step > 0 && isOn (lane, step - 1))
                set (lane, step, kOn);
        }
        else
        {
            int first, last;
            blockAt (lane, step, first, last);
            set (lane, step, kStart);
            writeInfo (lane, step, infoAt (lane, first));
        }
        touch();
    }

    void clearLane (int lane)
    {
        for (int s = 0; s < kMaxSteps; ++s)
            set (lane, s, kOff);
        touch();
    }

    void clear()
    {
        for (int l = 0; l < kLanes; ++l)
            clearLane (l);
    }

    void getLane (int lane, Lane& out) const
    {
        for (int s = 0; s < kMaxSteps; ++s)
        {
            out.cell[s] = cell (lane, s);
            out.info[s] = infoAt (lane, s);
        }
    }

    void setLane (int lane, const Lane& in)
    {
        for (int s = 0; s < kMaxSteps; ++s)
        {
            set (lane, s, in.cell[s]);
            writeInfo (lane, s, in.info[s]);
        }
        touch();
    }

    // Everything of `other` (another scene's pattern) into this one.
    void copyFrom (const Pattern& other)
    {
        Lane l;
        for (int lane = 0; lane < kLanes; ++lane)
        {
            other.getLane (lane, l);
            setLane (lane, l);
        }
        setSeed (other.getSeed());
    }

    int getSeed() const { return seed.load(); }
    void setSeed (int s) { seed.store (s); touch(); }

    // Changes whenever the pattern does (the kit's undo and preset reference watch it).
    int getRevision() const { return revision.load(); }

private:
    void set (int lane, int step, uint8_t v) { cells[lane][step].store (v, std::memory_order_relaxed); }
    void touch() { revision.fetch_add (1); }

    void writeInfo (int lane, int step, const BlockInfo& b)
    {
        levels[lane][step].store (b.level, std::memory_order_relaxed);
        shapes[lane][step].store (b.shape, std::memory_order_relaxed);
        probs[lane][step].store (b.probability, std::memory_order_relaxed);
        ratchets[lane][step].store (b.ratchets, std::memory_order_relaxed);
        locked[lane][step].store (b.locked ? 1 : 0, std::memory_order_relaxed);
        for (int k = 0; k < kLockValues; ++k)
            locks[lane][step][k].store (b.lock[k], std::memory_order_relaxed);
    }

    static void clampRange (int& from, int& to)
    {
        if (from > to)
            std::swap (from, to);
        from = jlimit (0, kMaxSteps - 1, from);
        to = jlimit (0, kMaxSteps - 1, to);
    }

    // What the block going on after `to` holds (to keep it when what is before changes).
    BlockInfo infoAfter (int lane, int to) const
    {
        int first, last;
        if (to + 1 < kMaxSteps && cell (lane, to + 1) == kOn && blockAt (lane, to + 1, first, last))
            return infoAt (lane, first);
        return {};
    }

    // What follows [.., to] starts a block of its own, holding what the block it was in held.
    void startAfter (int lane, int to, const BlockInfo& info)
    {
        if (to + 1 < kMaxSteps && cell (lane, to + 1) == kOn)
        {
            set (lane, to + 1, kStart);
            writeInfo (lane, to + 1, info);
        }
    }

    std::atomic<uint8_t> cells[kLanes][kMaxSteps];
    std::atomic<uint8_t> levels[kLanes][kMaxSteps], shapes[kLanes][kMaxSteps];
    std::atomic<uint8_t> probs[kLanes][kMaxSteps], ratchets[kLanes][kMaxSteps], locked[kLanes][kMaxSteps];
    std::atomic<float> locks[kLanes][kMaxSteps][kLockValues];
    std::atomic<int> seed { 1 }, revision { 0 };
};
} // namespace bidoo::dsp::glitch

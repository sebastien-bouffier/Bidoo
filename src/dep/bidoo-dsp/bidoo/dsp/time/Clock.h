#pragma once

// Sample-accurate musical clock.
//  - Clock: Mangle's. Where a block is, sample by sample: positions are computed from whole sample
//    counts (origin + (offset + i) * beatsPerSample), never by adding a rounded increment, so a
//    step or a slice starts on the same sample whatever the host's buffer size.
//  - InternalClock: the position when there is no host (or the plugin's own tempo is wanted): a
//    sample counter at a tempo; a tempo change starts a new origin so the position never jumps.
//  - phase views (Chourmo): the position as a 0..1 ramp over a number of beats or steps, for
//    modulation that follows the transport (a transport LFO, "step access").

#include "../core/Inline.h"
#include <cmath>
#include <cstdint>

namespace bidoo::dsp
{
struct Clock
{
    std::int64_t counter = 0;    // a caller's own counter at the block's first sample (e.g. a history position)
    double ppqOrigin = 0.0;      // the play head at sample offset 0
    std::int64_t offset = 0;     // the block's first sample, counted from that origin
    double beatsPerSample = 0.0;
    double samplesPerBeat = 0.0;
    int patternBeats = 4;
    double patternStart = 0.0;   // where the pattern starts (a scene's note in Gate and Latch modes)

    BIDOO_DSP_INLINE double ppqAt (int i) const noexcept { return ppqOrigin + (double) (offset + i) * beatsPerSample; }

    // Beats into the pattern, i samples into the block.
    BIDOO_DSP_INLINE double posAt (int i) const noexcept
    {
        const double pos = std::fmod (ppqAt (i) - patternStart, (double) patternBeats);
        return pos < 0.0 ? pos + patternBeats : pos;
    }
};

class InternalClock
{
public:
    // Call when the tempo may have changed, before reading origin()/samples().
    BIDOO_DSP_INLINE void setTempo (double bpm, double sampleRate) noexcept
    {
        if (bpm != tempo)
        {
            originPpq += (double) counted / 60.0 / sampleRate * tempo;
            counted = 0;
            tempo = bpm;
        }
    }
    BIDOO_DSP_INLINE double origin() const noexcept { return originPpq; }
    BIDOO_DSP_INLINE std::int64_t samples() const noexcept { return counted; }
    BIDOO_DSP_INLINE void advance (int n) noexcept { counted += n; }

    // The position i samples after the next block's start, at the clock's current tempo.
    BIDOO_DSP_INLINE double ppqAt (int i, double bpm, double sampleRate) const noexcept
    {
        const double base = originPpq + (tempo > 0.0 ? (double) counted / 60.0 / sampleRate * tempo : 0.0);
        return base + i * bpm / 60.0 / sampleRate;
    }

    void reset() noexcept { originPpq = 0.0; counted = 0; tempo = 0.0; }

private:
    double originPpq = 0.0;
    std::int64_t counted = 0;
    double tempo = 0.0;
};

// x wrapped to [0, 1) (0 for a non-finite x), with the rounding case of a result of exactly 1 folded to 0.
BIDOO_DSP_INLINE double wrapPhase (double x) noexcept
{
    if (x >= 0.0 && x < 1.0)
        return x;
    if (x >= 1.0 && x < 2.0)
        return x - 1.0;
    if (! (x - x == 0.0))
        return 0.0;
    const double y = x - std::floor (x);
    return y >= 1.0 ? 0.0 : y;
}

// The position as a ramp over `beats` quarter notes (a transport LFO), in float as Chourmo outputs
// it (never exactly 1).
BIDOO_DSP_INLINE float beatPhase (double ppq, double beats) noexcept
{
    const float v = (float) wrapPhase (ppq / beats);
    return v >= 1.0f ? 0.0f : v;
}

// The position as a ramp over `steps` steps of `stepBeats` each, starting `offsetSteps` steps after
// the origin, in float as Chourmo's Step Access outputs it (never exactly 1).
BIDOO_DSP_INLINE float stepPhase (double ppq, double stepBeats, double steps, double offsetSteps) noexcept
{
    const float v = (float) wrapPhase ((ppq / stepBeats - offsetSteps) / steps);
    return v >= 1.0f ? 0.0f : v;
}
} // namespace bidoo::dsp

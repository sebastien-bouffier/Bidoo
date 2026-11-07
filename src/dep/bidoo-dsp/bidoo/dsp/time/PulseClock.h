#pragma once

// PulseClock: a clock input's pulses (ppqn per beat) turned into a play position in beats, a tempo and
// a running state, sample by sample. For modules that follow an external clock (Bidoo VCV: cOPIA;
// the same tempo logic as sOUNAiRe's and MU's clock inputs).
//
//  - The tempo comes from the interval between pulses: it jumps to a change of more than 10 %, and
//    a steady clock's small differences move it halfway (sOUNAiRe's smoothing).
//  - The position is the pulses counted since the first one (or since reset()), plus the part of an
//    interval since the last pulse, held just below the next pulse so it never runs past it: a
//    clock that slows down stalls the position until its pulse comes, then it carries on from there.
//  - The clock counts as stopped before it has given two pulses, and once no pulse has come for
//    two seconds plus one interval.

#include "../core/Inline.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bidoo::dsp
{
class PulseClock
{
public:
    void setPpqn (int pulsesPerBeat) noexcept { ppqn = std::max (1, pulsesPerBeat); }
    int getPpqn() const noexcept { return ppqn; }

    // The next pulse is beat 0 (a RESET input). The tempo is kept.
    void reset() noexcept
    {
        pulses = -1;
        since = 0;
    }

    // Forget everything (a new sample rate, the clock unplugged).
    void clear() noexcept
    {
        reset();
        interval = 0.0;
        sinceLast = -1;
    }

    // One sample; `pulse`: a rising edge on this sample. Then position() and the rest are for this sample.
    BIDOO_DSP_INLINE void tick (bool pulse, double sampleRate) noexcept
    {
        rate = sampleRate;
        ++since;
        if (sinceLast >= 0)
            ++sinceLast;
        if (pulse)
        {
            if (sinceLast > 0)
            {
                const double measured = (double) sinceLast;
                if (interval <= 0.0 || std::abs (measured - interval) > 0.1 * interval)
                    interval = measured;
                else
                    interval += 0.5 * (measured - interval);
            }
            sinceLast = 0;
            pulses = pulses < 0 ? 0 : pulses + 1;
            since = 0;
        }
    }

    bool running() const noexcept
    {
        return interval > 0.0 && pulses >= 0 && sinceLast >= 0 && (double) sinceLast <= 2.0 * rate + interval;
    }

    // Beats since the first pulse after reset (0 before it).
    double position() const noexcept
    {
        if (pulses < 0)
            return 0.0;
        const double part = interval > 0.0 ? std::min ((double) since / interval, kMaxPart) : 0.0;
        return ((double) pulses + part) / (double) ppqn;
    }

    // Beats per minute, 0 before two pulses.
    double bpm() const noexcept { return interval > 0.0 ? 60.0 * rate / (interval * (double) ppqn) : 0.0; }

    // Samples per pulse, 0 before two pulses.
    double samplesPerPulse() const noexcept { return interval; }

private:
    static constexpr double kMaxPart = 0.999;
    int ppqn = 1;
    std::int64_t pulses = -1;    // counted since reset (-1: none yet)
    std::int64_t since = 0;      // samples since the last pulse counted in `pulses`
    std::int64_t sinceLast = -1; // samples since the last pulse at all (-1: none yet)
    double interval = 0.0;       // samples between pulses
    double rate = 48000.0;
};
} // namespace bidoo::dsp

#pragma once

// IdleGate: lets a reverb skip its network while nothing goes in and the tail is gone. After
// `holdSamples` in a row with an input peak under 1e-9 and an output peak under 1e-6 (-120 dB) the
// gate reports idle: the caller writes zeros and skips the network until an input sample arrives.
// While active (anything in, or a tail still audible) nothing changes, the output is bit-identical.
// The network keeps the (inaudible, under -120 dB) state it had when it went idle.

#include <algorithm>

namespace bidoo::dsp::reverb
{
struct IdleGate
{
    int quiet = 0;
    int holdSamples = 16384;

    void reset() { quiet = 0; }
    // before the block: true if it can be skipped (input peak of the block, freeze on or not)
    bool skip (float inputPeak, bool frozen) const { return ! frozen && inputPeak < 1.0e-9f && quiet >= holdSamples; }
    // after a block that was run
    void ran (int n, float inputPeak, float outputPeak, bool frozen)
    {
        quiet = (! frozen && inputPeak < 1.0e-9f && outputPeak < 1.0e-6f) ? std::min (quiet + n, holdSamples) : 0;
    }
};
} // namespace bidoo::dsp::reverb

#pragma once

// Euclidean rhythms as bitmasks (FadaBox): `hits` spread as evenly as possible over `steps`,
// rotated. Bresenham form: step i is a hit when the running total i * hits crosses a multiple of
// steps; it gives the same necklaces as Bjorklund's algorithm. No allocation.

#include "../core/Inline.h"
#include <algorithm>
#include <cstdint>

namespace bidoo::dsp
{
// Whether step i of the un-rotated pattern is a hit (steps > 0, 0 <= hits <= steps).
BIDOO_DSP_INLINE bool euclidHit (int i, int hits, int steps) noexcept { return (i * hits) % steps < hits; }

// Bit i of the result = step i is a hit. steps 1..64.
BIDOO_DSP_INLINE std::uint64_t euclidMask (int hits, int steps, int rotation = 0) noexcept
{
    steps = std::clamp (steps, 1, 64);
    hits = std::clamp (hits, 0, steps);
    rotation = ((rotation % steps) + steps) % steps;
    std::uint64_t bits = 0;
    for (int i = 0; i < steps; ++i)
        if (euclidHit (i, hits, steps))
            bits |= (std::uint64_t) 1 << ((i + rotation) % steps);
    return bits;
}

// The same pattern for any length, into out[0..steps) (what a generator with long patterns uses).
BIDOO_DSP_INLINE void euclidFill (bool* out, int steps, int hits, int rotation = 0) noexcept
{
    if (steps <= 0)
        return;
    hits = std::clamp (hits, 0, steps);
    for (int i = 0; i < steps; ++i)
        out[((i + rotation) % steps + steps) % steps] = euclidHit (i, hits, steps);
}
} // namespace bidoo::dsp

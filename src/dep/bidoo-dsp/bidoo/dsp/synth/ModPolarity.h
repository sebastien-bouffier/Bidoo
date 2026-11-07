#pragma once

// The polarity of one modulation connection (a slot, a cable, a destination row), shared by every
// Bidoo plugin with routable modulation. Each connection is explicitly UNI or BI and keeps its signed
// amount, which together give the three ranges from any source:
//
//   UNI +x : the target moves from its knob up to knob + x        (0 -> +x)
//   UNI -x : the target moves from its knob down to knob - x      (0 -> -x)
//   BI  +-x: the target swings x both ways around its knob        (-x -> +x, the sign flips the phase)
//
// The source is converted once, before the amount, from its natural range: a unipolar source u in
// 0..1 (envelope, follower, velocity, wheel, aftertouch, macro) gives u as UNI and 2u - 1 as BI; a
// bipolar source b in -1..1 (LFO, random, pitch bend, key) gives (b + 1) / 2 as UNI and b as BI.
// A new connection starts at its source's natural range (naturalPolarity).

#include "../core/Inline.h"

namespace bidoo::dsp
{
enum class ModPolarity : unsigned char { Uni = 0, Bi = 1 };

// The source value in the connection's range: 0..1 for UNI, -1..1 for BI (for a source inside its
// natural range). A few instructions, per sample or per block.
BIDOO_DSP_INLINE float convertModSource (float value, bool sourceIsBipolar, ModPolarity polarity) noexcept
{
    if (polarity == ModPolarity::Bi)
        return sourceIsBipolar ? value : value * 2.0f - 1.0f;
    return sourceIsBipolar ? (value + 1.0f) * 0.5f : value;
}

// The setting a new connection from a source starts with: the source's own range.
BIDOO_DSP_INLINE ModPolarity naturalPolarity (bool sourceIsBipolar) noexcept
{
    return sourceIsBipolar ? ModPolarity::Bi : ModPolarity::Uni;
}

// The span a connection can move its target through, relative to the knob, in amount units (for
// the range rings): UNI +x gives [0, x], UNI -x gives [-x, 0], BI +-x gives [-|x|, |x|].
struct ModSpan
{
    float low = 0.0f, high = 0.0f;
};

BIDOO_DSP_INLINE ModSpan modSpan (float amount, ModPolarity polarity) noexcept
{
    const float a = amount < 0.0f ? -amount : amount;
    if (polarity == ModPolarity::Bi)
        return { -a, a };
    return amount < 0.0f ? ModSpan { amount, 0.0f } : ModSpan { 0.0f, amount };
}
} // namespace bidoo::dsp

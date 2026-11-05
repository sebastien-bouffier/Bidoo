#pragma once

// tanh by its Pade 7/6 approximant: error below 1e-4 everywhere, slope exactly 1 at
// 0, no branch but the clamps, so it vectorises. Closer to tanh than fastTanh
// (shaping/Shapers.h, a 3/2 rational that is flat beyond +-3) and a little dearer; for the
// per-sample soft clips that should sound like tanh. From Quichar, Peneque and Xpresso (the same
// function in all three).

#include "../core/Inline.h"
#include <algorithm>

namespace bidoo::dsp
{
BIDOO_DSP_INLINE float tanhPade (float x)
{
    x = std::clamp (x, -4.97f, 4.97f); // where the approximant reaches 1
    const float x2 = x * x;
    const float n = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float d = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2));
    return std::clamp (n / d, -1.0f, 1.0f);
}
} // namespace bidoo::dsp

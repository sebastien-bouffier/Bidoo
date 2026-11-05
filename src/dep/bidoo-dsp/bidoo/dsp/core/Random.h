#pragma once

// Random numbers that give the same sequence for the same seed on every platform and compiler
// (the std distributions don't), cheap enough for the audio thread.
//  - Rng: a stream (xoshiro128**), from Diktat's generator. Use one per voice or per lane;
//    fork() gives independent child streams.
//  - hash01 / hashU64: stateless randomness from integers (splitmix64), for "the random value
//    of step k" that must not depend on what was drawn before (loops, jumps, offline renders).

#include "Inline.h"
#include <cstdint>
#include <vector>

namespace bidoo::dsp
{
BIDOO_DSP_INLINE uint64_t splitmix64 (uint64_t x) noexcept
{
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

// A 64-bit hash of up to three integers.
BIDOO_DSP_INLINE uint64_t hashU64 (uint64_t a, uint64_t b = 0, uint64_t c = 0) noexcept
{
    return splitmix64 (a ^ splitmix64 (b ^ splitmix64 (c)));
}

// [0, 1) from up to three integers.
BIDOO_DSP_INLINE double hash01 (uint64_t a, uint64_t b = 0, uint64_t c = 0) noexcept
{
    return (double) (hashU64 (a, b, c) >> 11) * (1.0 / 9007199254740992.0);
}

// xorshift32 (Marsaglia): one word of state, the cheapest stream, for the plugins whose numbers are
// part of their sound (FadaBox's voices and FX, Prism's burst patterns, Chourmo's per-voice
// generators). A zero seed becomes 1 (zero is the generator's fixed point). New code takes Rng.
struct XorShift32
{
    uint32_t s = 0x9E3779B9u;

    explicit XorShift32 (uint32_t seed = 0x9E3779B9u) noexcept : s (seed ? seed : 1u) {}

    BIDOO_DSP_INLINE uint32_t next() noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    // [0, 1), 24 bits
    BIDOO_DSP_INLINE float uniform() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    // [-1, 1)
    BIDOO_DSP_INLINE float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }
};

class Rng
{
public:
    explicit Rng (uint64_t seed = 1) noexcept { reseed (seed); }

    void reseed (uint64_t seed) noexcept
    {
        uint64_t z = seed;
        for (auto& w : s)
        {
            z += 0x9e3779b97f4a7c15ull;
            uint64_t x = z;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
            w = (uint32_t) (x ^ (x >> 31));
        }
    }

    BIDOO_DSP_INLINE uint32_t next() noexcept
    {
        const uint32_t result = rotl (s[1] * 5, 7) * 9;
        const uint32_t t = s[1] << 9;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl (s[3], 11);
        return result;
    }

    // [0, 1)
    BIDOO_DSP_INLINE float uniform() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    BIDOO_DSP_INLINE float uniform (float lo, float hi) noexcept { return lo + (hi - lo) * uniform(); }
    // [-1, 1)
    BIDOO_DSP_INLINE float bipolar() noexcept { return 2.0f * uniform() - 1.0f; }

    // [lo, hi] inclusive
    int range (int lo, int hi) noexcept
    {
        if (hi <= lo)
            return lo;
        return lo + (int) (next() % (uint32_t) (hi - lo + 1));
    }

    bool chance (float p) noexcept { return uniform() < p; }

    // Approximate standard normal (sum of uniforms), bounded to about +-3.
    float gaussian() noexcept
    {
        float sum = 0.0f;
        for (int i = 0; i < 6; ++i)
            sum += uniform();
        return (sum - 3.0f) * 1.41421356f;
    }

    template <typename T>
    const T& pick (const std::vector<T>& v) noexcept { return v[(size_t) range (0, (int) v.size() - 1)]; }

    // Index chosen by weight.
    int weighted (const float* weights, int count) noexcept
    {
        float total = 0.0f;
        for (int i = 0; i < count; ++i)
            total += weights[i];
        float r = uniform() * total;
        for (int i = 0; i < count; ++i)
        {
            r -= weights[i];
            if (r < 0.0f)
                return i;
        }
        return count - 1;
    }

    int weighted (const std::vector<float>& weights) noexcept { return weighted (weights.data(), (int) weights.size()); }

    // Independent child stream, so one voice's choices don't shift another's.
    Rng fork (uint32_t salt) noexcept { return Rng (((uint64_t) next() << 32) ^ salt); }

private:
    static BIDOO_DSP_INLINE uint32_t rotl (uint32_t x, int k) noexcept { return (x << k) | (x >> (32 - k)); }
    uint32_t s[4] {};
};
} // namespace bidoo::dsp

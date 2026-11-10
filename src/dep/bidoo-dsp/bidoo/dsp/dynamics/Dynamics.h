#pragma once

// Building blocks for the dynamics plugins: time constants, envelope followers, the static
// gain curve with a soft knee, a delay line for lookahead and a sliding-window maximum.

#include <cmath>
#include <vector>
#include <algorithm>

namespace bidoo::dsp
{
// Coefficient of a one-pole smoother reaching ~63 % in `ms`.
inline float timeCoeff (float ms, double sampleRate)
{
    if (ms <= 0.0f)
        return 0.0f;
    return (float) std::exp (-1.0 / (0.001 * ms * sampleRate));
}

// Attack / release follower on a positive signal (a level in linear or dB).
class Ballistics
{
public:
    void set (float attackMs, float releaseMs, double sr)
    {
        a = timeCoeff (attackMs, sr);
        r = timeCoeff (releaseMs, sr);
    }

    void reset (float v = 0.0f) { y = v; }

    inline float process (float x)
    {
        const float c = x > y ? a : r;
        y = x + c * (y - x);
        return y;
    }

    float value() const { return y; }

private:
    float a = 0.0f, r = 0.0f, y = 0.0f;
};

// Gain reduction (dB, <= 0) for a level in dB: threshold, ratio (>= 1, 100 = limiting),
// soft knee width in dB. With `expand`, it works downward (ratio applies below the threshold).
inline float gainComputer (float levelDb, float thresholdDb, float ratio, float kneeDb, bool expand = false)
{
    ratio = std::max (1.0f, ratio);
    const float slope = expand ? -(ratio - 1.0f) : 1.0f / ratio - 1.0f; // <= 0
    const float over = expand ? thresholdDb - levelDb : levelDb - thresholdDb;
    if (kneeDb > 0.0f && over > -kneeDb * 0.5f && over < kneeDb * 0.5f)
    {
        const float t = over + kneeDb * 0.5f;
        return slope * t * t / (2.0f * kneeDb);
    }
    return over <= 0.0f ? 0.0f : slope * over;
}

// A fixed-size delay line (lookahead, delay-matched dry paths).
class DelayLine
{
public:
    void prepare (int maxDelay)
    {
        buf.assign ((size_t) std::max (1, maxDelay + 1), 0.0f);
        w = 0;
    }

    void setDelay (int samples) { d = std::clamp (samples, 0, (int) buf.size() - 1); }
    int getDelay() const { return d; }

    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); }

    inline float process (float x)
    {
        buf[(size_t) w] = x;
        int r = w - d;
        if (r < 0)
            r += (int) buf.size();
        const float y = buf[(size_t) r];
        if (++w >= (int) buf.size())
            w = 0;
        return y;
    }

    // Fill the whole line with a value (an envelope at rest is 0 dB, a gain at rest unity).
    void fill (float v) { std::fill (buf.begin(), buf.end(), v); }

    // process() over a block (in and out may be the same); same results, sample for sample.
    void processBlock (const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i)
            out[i] = process (in[i]);
    }

private:
    std::vector<float> buf { 0.0f };
    int w = 0, d = 0;
};

// The same delay (same results, sample for sample) on a power-of-two ring: a mask instead of a
// compare and a size read per step, for the many delays that run per sample. The ring starts at
// `initial` (prepare), fill(v) and reset() reset it; setDelay clamps to 0 .. maxDelay.
class Pow2DelayLine
{
public:
    void prepare (int maxDelay, float initial = 0.0f)
    {
        int size = 1;
        while (size < maxDelay + 1)
            size *= 2;
        buf.assign ((size_t) size, initial);
        mask = size - 1;
        limit = std::max (0, maxDelay);
        w = 0;
        d = std::min (d, limit);
    }

    void setDelay (int samples) { d = std::clamp (samples, 0, limit); }
    int getDelay() const { return d; }

    void reset() { fill (0.0f); }
    void fill (float v) { std::fill (buf.begin(), buf.end(), v); }

    inline float process (float x)
    {
        buf[(size_t) w] = x;
        const float y = buf[(size_t) ((w - d) & mask)];
        w = (w + 1) & mask;
        return y;
    }

    // process() over a block (in and out may be the same); same results, sample for sample.
    void processBlock (const float* in, float* out, int n)
    {
        float* b = buf.data();
        int wp = w;
        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];
            b[wp] = x;
            out[i] = b[(wp - d) & mask];
            wp = (wp + 1) & mask;
        }
        w = wp;
    }

    // Writes a block without reading (the line keeps running while its output is not needed):
    // the state processBlock would leave.
    void write (const float* in, int n)
    {
        float* b = buf.data();
        int wp = w;
        for (int i = 0; i < n; ++i)
        {
            b[wp] = in[i];
            wp = (wp + 1) & mask;
        }
        w = wp;
    }

private:
    std::vector<float> buf = std::vector<float> (1, 0.0f);
    int w = 0, d = 0, mask = 0, limit = 0;
};

// Maximum of the last `length` samples in O(1) amortised: a monotonic queue in a ring buffer
// sized in prepare(), so nothing allocates on the audio thread.
class SlidingMax
{
public:
    void prepare (int maxLength)
    {
        cap = std::max (1, maxLength);
        items.assign ((size_t) cap + 1, {});
        reset();
    }

    void setLength (int n) { len = std::clamp (n, 1, cap); }

    void reset()
    {
        head = tail = 0;
        t = 0;
    }

    inline float process (float x)
    {
        const int last = (int) items.size() - 1; // (wraps by comparison: no division per step)
        while (head != tail && items[(size_t) (tail == 0 ? last : tail - 1)].v <= x)
            tail = tail == 0 ? last : tail - 1;
        items[(size_t) tail] = { x, t };
        tail = tail == last ? 0 : tail + 1;
        while (items[(size_t) head].t <= t - len)
            head = head == last ? 0 : head + 1;
        ++t;
        return items[(size_t) head].v;
    }

private:
    struct Item
    {
        float v = 0.0f;
        long long t = 0;
    };
    std::vector<Item> items = std::vector<Item> (2);
    int head = 0, tail = 0;
    long long t = 0;
    int len = 1, cap = 1;
};

// Peak level with instant attack and an exponential release (linear in, linear out).
class PeakFollower
{
public:
    void setRelease (float ms, double sr) { r = timeCoeff (ms, sr); }
    inline float process (float x)
    {
        x = std::abs (x);
        y = x > y ? x : x + r * (y - x);
        return y;
    }
    void reset() { y = 0.0f; }

private:
    float r = 0.0f, y = 0.0f;
};

// Mean square over a time constant (RMS = sqrt of the result).
class RmsFollower
{
public:
    void set (float ms, double sr) { c = timeCoeff (ms, sr); }
    inline float process (float x)
    {
        y = x * x + c * (y - x * x);
        return y;
    }
    void reset() { y = 0.0f; }

private:
    float c = 0.0f, y = 0.0f;
};
} // namespace bidoo::dsp

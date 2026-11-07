#pragma once

// HistoryBuffer and CrossfadeTap, from Mangle's glitch engine. The history is a stereo power-of-two
// buffer that is always recording, addressed by a running sample counter, so a slice "one beat
// ago" is just an address and a capture effect engaged before audio arrives is never silent. A
// CrossfadeTap is a read head on it that advances by `rate` per sample (any rate, forwards or
// backwards: repeat, reverser, tape stop, grain) and crossfades (equal power, a few ms) from
// where it was whenever it jumps, so slice edges never click.
//
// Three reads:
//  - read: cubic (Hermite), clamped to what is held. For heads at one sample per sample or slower.
//  - readKernel: the resampler's windowed-sinc kernel (synth/KernelRead.h) instead. Faster than one
//    sample per sample Hermite's interpolation images fold back as inharmonic aliases (a 5 kHz sine
//    read 1.9x faster at 48 kHz: spurs 49 dB down), the kernel's are 90 dB down (Quality Good). It
//    does not band-limit the read itself: what lies above sr / (2 |rate|) still folds.
//  - readFast: band-limited for the speed the head really moves at. It needs the decimated copies
//    (prepare's `decimatedCopies`, Mangle uses 2): copy 1 is the history half-band low-passed at
//    half the rate, copy 2 the same again at a quarter (the MipChain's half-band for the Good kernel,
//    90 dB, written as the history is: copy k's sample j sits at counter j * 2^k), shared by every
//    reader whatever its speed. A read at a step s picks the copy where s / 2^k is at most 2, then
//    synth/StepKernels.h's kernel for s / 2^k (stopband from 0.5 / step; on a copy, from 0.41 of its
//    rate at the latest). The output is flat to about 0.5 - 0.09 s of the rate up to 2x (0.41 at
//    1x, 0.32 at 2x), then 0.16 s past each copy change (0.32 to 0.39), then 0.5 - 0.045 s again:
//    always 0.32 to 0.41 of the rate (15 to 20 kHz at 48 kHz), every alias 90 dB down, up to 2^(c+1)
//    times real time with c copies (8x with two). At |step| <= 1 it is readKernel with the Good
//    kernel, bit for bit.
//
// Why shared decimated copies and not a write-side low pass tuned to the read speed (Chourmo's pitch
// shifter, delay/BandLimitedWrite.h): that works for a line with one reader at one known speed. A
// history has many readers at different, changing speeds, reading audio written before their speed
// was known (a freeze, a tape lag). A read-side kernel stretched by s would cost s times the taps per
// read, per reader; the copies cost one small FIR per written sample, shared, and 75 % more memory
// (two copies), and every read stays one kernel.
//
// Near the newest sample a copy's FIR has not caught up (copy 1 lags 33 samples, copy 2 99): a read
// whose kernel would reach past what that copy holds falls back to a finer one (the top band then
// folds for those few ms). A copy only uses input up to the reader's `newest`, so the output does not
// depend on the block size. With no copies (the default) prepare, write and every other read are
// what they always were, bit for bit.

#include "../oversampling/FirDesign.h"
#include "../synth/KernelRead.h"
#include "../synth/StepKernels.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>

namespace bidoo::dsp
{
class HistoryBuffer
{
public:
    static constexpr int kMaxCopies = 3; // half, quarter and eighth rate

    // Room for `seconds` at `sampleRate` (message thread, prepare); each decimated copy holds as
    // long (at least 256 samples). Builds the StepKernels when there are copies.
    void prepare (double sampleRate, double seconds, int decimatedCopies = 0)
    {
        size_t size = 1;
        while ((double) size < sampleRate * seconds)
            size <<= 1;
        levelCount = 1 + std::clamp (decimatedCopies, 0, kMaxCopies);
        for (int k = 0; k < kMaxCopies + 1; ++k)
        {
            auto& lv = levels[(size_t) k];
            const size_t n = k == 0 ? size : (k < levelCount ? std::max ((size_t) 256, size >> k) : 0);
            for (auto& c : lv.data)
                c.assign (n, 0.0f);
            lv.mask = (int64_t) n - 1;
            lv.written = 0;
        }
        for (auto& d : decimators)
            d = Decimator {};
        kernels = nullptr;
        if (levelCount == 1)
            return;

        // The half-band of Resampler.h's MipChain for the Good kernel (transition 0.09 wide, 90 dB):
        // its taps at odd distances from the centre, oldest first, after zeros up to kOddTaps.
        const auto& g = ResampleKernel::shared (ResampleQuality::Good);
        const auto h = fir::halfBand (StepKernels::halfBandWidth (g), g.stopbandDb());
        half = ((int) h.size() - 1) / 2;
        centreTap = (float) h[(size_t) half];
        const int odd = std::min (kOddTaps, half + 1); // distances -half, -half + 2 .. half (half is odd)
        std::fill (std::begin (oddTaps), std::end (oddTaps), 0.0f);
        for (int j = 0; j < odd; ++j)
            oddTaps[kOddTaps - odd + j] = (float) h[(size_t) (2 * j)];
        kernels = &StepKernels::shared();
        levelStop = StepKernels::levelStop();
    }

    void clear()
    {
        for (auto& lv : levels)
            for (auto& c : lv.data)
                std::fill (c.begin(), c.end(), 0.0f);
        for (auto& d : decimators)
            d = Decimator {};
    }

    void write (const float* l, const float* r, int n)
    {
        auto& lv = levels[0];
        for (int i = 0; i < n; ++i)
        {
            const auto at = (size_t) ((lv.written + i) & lv.mask);
            lv.data[0][at] = l[i];
            lv.data[1][at] = r[i];
        }
        if (levelCount == 1)
        {
            lv.written += n;
            return;
        }
        // each odd sample of a level completes a sample of the next
        int64_t from[kMaxCopies + 1];
        for (int k = 0; k < levelCount; ++k)
            from[k] = levels[(size_t) k].written;
        lv.written += n;
        for (int k = 0; k + 1 < levelCount; ++k)
            for (int64_t t = std::max ((int64_t) 0, from[k]) | 1; t < levels[(size_t) k].written; t += 2)
                push (k, t);
    }

    // Samples written so far: the counter of the next one.
    int64_t getWritten() const { return levels[0].written; }
    int64_t capacity() const { return levels[0].mask + 1; }
    int decimatedCopies() const { return levelCount - 1; }

    // At a fractional counter position, cubic (Hermite) interpolation. `newest` is the last counter
    // that may be read; positions outside what is held are clamped to it.
    void read (double pos, int64_t newest, float& l, float& r) const
    {
        const auto& lv = levels[0];
        const double oldest = (double) (newest - (lv.mask - 4));
        pos = std::clamp (pos, oldest, (double) newest);
        const auto i = (int64_t) std::floor (pos);
        const float f = (float) (pos - (double) i);
        l = interp (lv, lv.data[0], i, f, newest);
        r = interp (lv, lv.data[1], i, f, newest);
    }

    // The same read with a ResampleKernel (k.taps() samples around pos; ResampleKernel::shared,
    // called once off the audio thread). Positions are clamped as in read(), with room for the
    // kernel at the old end; taps past `newest` read `newest` (as read() does).
    void readKernel (const ResampleKernel& k, double pos, int64_t newest, float& l, float& r) const
    {
        const auto& lv = levels[0];
        const auto mask = lv.mask;
        const int taps = k.taps();
        const double oldest = (double) (newest - (mask - taps - 4));
        pos = std::clamp (pos, oldest, (double) newest);
        const auto w = kernel_read_detail::locate (k, pos);
        float s0, s1;
        const int64_t start = w.first & mask;
        if (w.first + taps - 1 <= newest && start + taps <= mask + 1)
        {
            resampler_detail::dot2 (w.a, w.b, lv.data[0].data() + start, taps, s0, s1);
            l = s0 + (s1 - s0) * w.frac;
            resampler_detail::dot2 (w.a, w.b, lv.data[1].data() + start, taps, s0, s1);
            r = s0 + (s1 - s0) * w.frac;
            return;
        }
        float tmp[2][64];
        for (int j = 0; j < taps; ++j)
        {
            const auto at = (size_t) (std::min (w.first + j, newest) & mask);
            tmp[0][j] = lv.data[0][at];
            tmp[1][j] = lv.data[1][at];
        }
        resampler_detail::dot2 (w.a, w.b, tmp[0], taps, s0, s1);
        l = s0 + (s1 - s0) * w.frac;
        resampler_detail::dot2 (w.a, w.b, tmp[1], taps, s0, s1);
        r = s0 + (s1 - s0) * w.frac;
    }

    // A read head moving `step` counters per sample (either way), band-limited for it: the copy and
    // the kernel for |step| (see the top of the file). Without decimated copies it is readKernel
    // with the Good kernel.
    void readFast (double pos, double step, int64_t newest, float& l, float& r) const
    {
        if (kernels == nullptr)
        {
            readKernel (ResampleKernel::shared (ResampleQuality::Good), pos, newest, l, r);
            return;
        }
        static constexpr double scale[kMaxCopies + 1] = { 1.0, 0.5, 0.25, 0.125 };
        double s = std::abs (step);
        int k = 0;
        while (s > StepKernels::kTop && k + 1 < levelCount)
        {
            s *= 0.5;
            ++k;
        }
        int64_t top = newest;
        if (k > 0)
        {
            // what each level holds from input up to `newest`; too close to it, a finer level
            int64_t lim[kMaxCopies + 1] = { newest, 0, 0, 0 };
            for (int j = 1; j <= k; ++j)
                lim[j] = (lim[j - 1] - half) >> 1; // floor
            while (k > 0 && (int64_t) std::floor (pos * scale[k]) + StepKernels::kWideTaps / 2 > lim[k])
            {
                --k;
                s *= 2.0;
            }
            top = lim[k];
        }
        const auto kernel = kernels->forStep (std::min (s, StepKernels::kTop), k > 0, levelStop);
        if (kernel.taps == 32)
            readLevel<32> (levels[(size_t) k], kernel.table, pos * scale[k], top, l, r);
        else
            readLevel<StepKernels::kWideTaps> (levels[(size_t) k], kernel.table, pos * scale[k], top, l, r);
    }

private:
    static constexpr int kOddTaps = 40; // the half-band's taps at odd distances (34), padded to a multiple of 8
    static constexpr int kRing = 64;    // the last odd samples of a level, doubled so the newest kOddTaps are contiguous

    struct Level
    {
        std::vector<float> data[2];
        int64_t mask = 0, written = 0;
    };

    // The odd samples of one level, for the half-band that makes the next.
    struct Decimator
    {
        float odd[2][2 * kRing] {};
        int at = 0; // where the next odd sample goes
    };

    static float at (const Level& lv, const std::vector<float>& c, int64_t i, int64_t newest)
    {
        return c[(size_t) (std::min (i, newest) & lv.mask)];
    }

    static float interp (const Level& lv, const std::vector<float>& c, int64_t i, float f, int64_t newest)
    {
        const float y0 = at (lv, c, i - 1, newest), y1 = at (lv, c, i, newest), y2 = at (lv, c, i + 1, newest),
                    y3 = at (lv, c, i + 2, newest);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    // readKernel on one level, with a kernel table (ResampleKernel's layout, StepKernels::kPhases
    // phases): positions clamped to what is held, taps past `newest` read `newest`.
    template <int taps>
    static void readLevel (const Level& lv, const float* table, double pos, int64_t newest, float& l, float& r)
    {
        constexpr int phases = StepKernels::kPhases;
        const auto mask = lv.mask;
        const double oldest = (double) (newest - (mask - taps - 4));
        pos = std::clamp (pos, oldest, (double) newest);
        // kernel_read_detail::locate
        const double fl = std::floor (pos);
        const int64_t i = (int64_t) fl;
        const double fp = (pos - fl) * phases;
        int p = (int) fp;
        if (p >= phases)
            p = phases - 1;
        const int64_t first = i - (taps / 2 - 1);
        const float* a = table + (size_t) p * taps;
        const float* b = a + taps;
        const float frac = (float) (fp - (double) p);

        const int64_t start = first & mask;
        if constexpr (taps == StepKernels::kWideTaps)
        {
            if (first + taps - 1 <= newest && start + taps <= mask + 1)
            {
                step_kernels_detail::blendDot2 (a, b, frac, lv.data[0].data() + start, lv.data[1].data() + start, taps, l, r);
                return;
            }
            float tmp[2][taps];
            for (int j = 0; j < taps; ++j)
            {
                const auto at = (size_t) (std::min (first + j, newest) & mask);
                tmp[0][j] = lv.data[0][at];
                tmp[1][j] = lv.data[1][at];
            }
            step_kernels_detail::blendDot2 (a, b, frac, tmp[0], tmp[1], taps, l, r);
            return;
        }
        float s0, s1;
        if (first + taps - 1 <= newest && start + taps <= mask + 1)
        {
            resampler_detail::dot2 (a, b, lv.data[0].data() + start, taps, s0, s1);
            l = s0 + (s1 - s0) * frac;
            resampler_detail::dot2 (a, b, lv.data[1].data() + start, taps, s0, s1);
            r = s0 + (s1 - s0) * frac;
            return;
        }
        float tmp[2][taps];
        for (int j = 0; j < taps; ++j)
        {
            const auto at = (size_t) (std::min (first + j, newest) & mask);
            tmp[0][j] = lv.data[0][at];
            tmp[1][j] = lv.data[1][at];
        }
        resampler_detail::dot2 (a, b, tmp[0], taps, s0, s1);
        l = s0 + (s1 - s0) * frac;
        resampler_detail::dot2 (a, b, tmp[1], taps, s0, s1);
        r = s0 + (s1 - s0) * frac;
    }

    // Level k holds its odd sample t: that completes level k + 1's sample (t - half) / 2, the
    // half-band of level k centred on its sample t - half (linear phase, so level k's sample j sits
    // at counter j * 2^k).
    void push (int k, int64_t t)
    {
        const Level& src = levels[(size_t) k];
        Level& dst = levels[(size_t) k + 1];
        auto& d = decimators[(size_t) k];
        const int64_t o = (t - half) >> 1;
        const auto in = (size_t) (t & src.mask);
        d.odd[0][d.at] = d.odd[0][d.at + kRing] = src.data[0][in];
        d.odd[1][d.at] = d.odd[1][d.at + kRing] = src.data[1][in];
        const int newest = d.at + kRing;
        d.at = (d.at + 1) & (kRing - 1);
        if (o < 0)
            return;
        const auto centre = (size_t) ((t - half) & src.mask), out = (size_t) (o & dst.mask);
        float sl, sr;
        step_kernels_detail::dot2ch (oddTaps, d.odd[0] + newest - (kOddTaps - 1), d.odd[1] + newest - (kOddTaps - 1), kOddTaps, sl,
                                     sr);
        dst.data[0][out] = centreTap * src.data[0][centre] + sl;
        dst.data[1][out] = centreTap * src.data[1][centre] + sr;
        dst.written = o + 1;
    }

    std::array<Level, kMaxCopies + 1> levels;
    std::array<Decimator, kMaxCopies> decimators;
    float oddTaps[kOddTaps] {}; // the half-band's taps at odd distances, oldest first
    float centreTap = 0.5f;
    int half = 0, levelCount = 1;
    double levelStop = 0.41;
    const StepKernels* kernels = nullptr;
};

// ---------------------------------------------------------------------------------------------
// A read head on the history: it advances by `rate` per sample and crossfades (equal power) from
// where it was whenever it jumps.
class CrossfadeTap
{
public:
    void reset() { valid = false; xf = 0; }

    void jump (double newPos, double newRate, int crossfade)
    {
        if (valid && crossfade > 0)
        {
            oldPos = pos;
            oldRate = rate;
            xf = xfLen = crossfade;
        }
        pos = newPos;
        rate = newRate;
        valid = true;
    }

    bool isValid() const { return valid; }
    double position() const { return pos; }

    void read (const HistoryBuffer& h, int64_t now, float& l, float& r)
    {
        h.read (pos, now, l, r);
        if (xf > 0)
        {
            float ol, orr;
            h.read (oldPos, now, ol, orr);
            const float g = (float) xf / (float) xfLen; // old's share
            const float gOld = std::sin (g * 1.57079632679489661923f);
            const float gNew = std::cos (g * 1.57079632679489661923f);
            l = l * gNew + ol * gOld;
            r = r * gNew + orr * gOld;
            oldPos += oldRate;
            --xf;
        }
        pos += rate;
    }

private:
    double pos = 0.0, rate = 1.0, oldPos = 0.0, oldRate = 1.0;
    int xf = 0, xfLen = 1;
    bool valid = false;
};
} // namespace bidoo::dsp

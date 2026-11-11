#pragma once

// GranularPitchShifter: Retour's pitch shifter for quality. Two grains reading a 50 ms window
// with complementary sin^2 gains. Each new grain starts where its signal lines up best with the
// grain playing at full level (a short cross-correlation search, SSE2 for the coarse scores), so
// the crossfades don't beat or comb; reads are windowed sinc (SincDelayLine). Transparent when
// the shift is 0 (bypassed, no added delay); latency() is the average delay it adds when active.
// For delays with a Doppler-style head and no added latency use PitchHeads (PitchHeads.h).

#include "SincDelayLine.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace bidoo::dsp
{
class GranularPitchShifter
{
public:
    void prepare (double sr)
    {
        window = (float) (0.05 * sr);
        search = std::max (8, (int) (0.012 * sr));    // lags tried: one period down to 83 Hz
        corrLen = std::max (16, (int) (0.008 * sr));  // samples compared per lag
        base = SincDelayLine::kMinDelay + 1.0f;
        buffer.prepare ((int) (window + (float) (search + corrLen)) + SincTable::kTaps + 16);
        step = 2 * std::max (1, (int) std::lround (sr / 48000.0)); // a 24 kHz grid for the coarse search
        candidate.assign ((size_t) (search + corrLen), 0.0f);
        coarseCandidate.assign ((size_t) ((search + corrLen + step - 1) / step), 0.0f);
        reference.assign ((size_t) corrLen, 0.0f);
        coarseScore.assign ((size_t) (search / step + 1), 0.0f);
        reset();
    }

    void reset()
    {
        buffer.reset();
        mix = 0.0f;
        running = false;
        grain[0] = { base + window * 0.5f, 0.5f };
        grain[1] = { base, 0.0f };
    }

    // The average delay it adds.
    float latency() const { return base + window * 0.5f + (float) search * 0.5f; }

    void setSemitones (float semitones)
    {
        on = std::abs (semitones) > 0.01f;
        ratio = std::pow (2.0f, semitones / 12.0f);
    }

    float process (float x)
    {
        buffer.write (x);
        mix += ((on ? 1.0f : 0.0f) - mix) * 0.002f;
        if (! on && mix < 0.0005f)
        {
            mix = 0.0f;
            running = false;
            return x; // bypassed: no added delay
        }
        const float speed = 1.0f - ratio;          // delay change per sample
        if (! running)
        {
            // starting: one grain at full level mid-window, the other lined up with it
            running = true;
            grain[0] = { base + window * 0.5f + (float) search * 0.5f, 0.5f };
            grain[1].phase = 0.0f;
            restart (1, speed);
        }
        const float inc = std::abs (speed) / window; // grain phase per sample
        const float hi = base + window + (float) search;
        float out = 0.0f;
        for (int g = 0; g < 2; ++g)
        {
            auto& gr = grain[g];
            gr.phase += inc;
            gr.delay += speed;
            if (gr.phase >= 1.0f || gr.delay < base || gr.delay > hi)
                restart (g, speed);
            const float s = std::sin (3.14159265358979f * gr.phase);
            out += s * s * buffer.read (gr.delay);
        }
        return x + mix * (out - x);
    }

    // process (0) on an empty buffer: the grains move on (and restart) as they would, nothing is read
    void idle()
    {
        buffer.write (0.0f);
        mix += ((on ? 1.0f : 0.0f) - mix) * 0.002f;
        if (! on && mix < 0.0005f)
        {
            mix = 0.0f;
            running = false;
            return;
        }
        const float speed = 1.0f - ratio;
        if (! running)
        {
            running = true;
            grain[0] = { base + window * 0.5f + (float) search * 0.5f, 0.5f };
            grain[1].phase = 0.0f;
            restart (1, speed);
        }
        const float inc = std::abs (speed) / window;
        const float hi = base + window + (float) search;
        for (int g = 0; g < 2; ++g)
        {
            auto& gr = grain[g];
            gr.phase += inc;
            gr.delay += speed;
            if (gr.phase >= 1.0f || gr.delay < base || gr.delay > hi)
                restart (g, speed);
        }
    }
    void clearAudio() { buffer.reset(); }

    bool isActive() const { return mix > 0.0f; }

private:
    struct Grain
    {
        float delay, phase;
    };

    // A new grain: from the far end of the window it will sweep, offset by the lag whose
    // signal matches the other grain best. The lags are tried from the middle of the search range
    // outwards and ties keep the earlier one, so a periodic signal settles on the period nearest
    // the middle and the average delay stays at latency() whatever the material.
    //
    // Cost: the search range and the compared length both grow with the sample rate, so the search
    // grows with its square. The coarse pass therefore works on a fixed time grid whatever the rate:
    // lags `step` samples apart, each compared on every `step`-th sample (step 2 up to 48 kHz,
    // 4 at 88.2 / 96 kHz, 8 at 192 kHz: the same 24 kHz grid), then the lags between the best one
    // and its coarse neighbours are scored at full resolution (every other sample, as up to 48 kHz).
    // Up to 48 kHz this is exactly the search it always was (same lags, same sums, same result).
    // When either grain's samples are all zero every score is 0 and the middle wins: no search.
    void restart (int g, float speed)
    {
        auto& gr = grain[g];
        const auto& other = grain[1 - g];
        gr.phase -= std::floor (gr.phase);
        const float start = speed < 0.0f ? base + window : base; // pitch up: the delay shrinks
        const int od = (int) other.delay;
        const int centre = (search / 2) / step * step;
        // keep the fractional part of the other grain, so the pair stays sample-aligned
        const float frac = other.delay - (float) od;

        // the samples every lag compares, unwrapped once: a lag's grain from candidate[lag], the
        // other grain from reference (every other sample)
        const int first = (int) start;
        bool candidateSilent = true, referenceSilent = true;
        for (int k = 0; k < search + corrLen; ++k)
        {
            const float v = buffer.sampleAgo (first + k);
            candidate[(size_t) k] = v;
            candidateSilent = candidateSilent && v == 0.0f;
        }
        for (int i = 0; i < corrLen; i += 2)
        {
            const float v = buffer.sampleAgo (od + i);
            reference[(size_t) i] = v;
            referenceSilent = referenceSilent && v == 0.0f;
        }
        if (candidateSilent || referenceSilent) // every correlation is exactly 0: the middle, as the search finds
        {
            gr.delay = start + (float) centre + frac;
            return;
        }
        for (int k = 0; k < (search + corrLen + step - 1) / step; ++k)
            coarseCandidate[(size_t) k] = candidate[(size_t) (step * k)];

        // a lag's match at full resolution: its grain against the other one, every other sample
        auto score = [&] (int lag)
        {
            const float* pa = candidate.data() + lag;
            const float* pb = reference.data();
            float c = 0.0f, e = 1.0e-9f;
            for (int i = 0; i < corrLen; i += 2)
            {
                const float a = pa[i], b = pb[i];
                c += a * b;
                e += a * a;
            }
            return c / std::sqrt (e);
        };
        // the coarse search tries the lags 0, step, .. min (2 centre, search): all scored up front,
        // four lags side by side per SSE register and four registers at once (independent sums, so
        // the adds overlap instead of waiting for each other); each lane sums its own lag in the
        // order score() does
        const int numCoarse = std::min (2 * centre, search) / step + 1;
        {
            const float* ref = reference.data();
            int m = 0;
#if BIDOO_DSP_SSE
            for (; m + 16 <= numCoarse; m += 16)
            {
                __m128 c0 = _mm_setzero_ps(), c1 = c0, c2 = c0, c3 = c0;
                __m128 e0 = _mm_set1_ps (1.0e-9f), e1 = e0, e2 = e0, e3 = e0;
                const float* pa = coarseCandidate.data() + m; // lane j of register r: coarse lag m + 4 r + j
                for (int i = 0, k = 0; i < corrLen; i += step, ++k)
                {
                    const __m128 r = _mm_set1_ps (ref[i]);
                    const __m128 a0 = _mm_loadu_ps (pa + k), a1 = _mm_loadu_ps (pa + k + 4);
                    const __m128 a2 = _mm_loadu_ps (pa + k + 8), a3 = _mm_loadu_ps (pa + k + 12);
                    c0 = _mm_add_ps (c0, _mm_mul_ps (a0, r));
                    c1 = _mm_add_ps (c1, _mm_mul_ps (a1, r));
                    c2 = _mm_add_ps (c2, _mm_mul_ps (a2, r));
                    c3 = _mm_add_ps (c3, _mm_mul_ps (a3, r));
                    e0 = _mm_add_ps (e0, _mm_mul_ps (a0, a0));
                    e1 = _mm_add_ps (e1, _mm_mul_ps (a1, a1));
                    e2 = _mm_add_ps (e2, _mm_mul_ps (a2, a2));
                    e3 = _mm_add_ps (e3, _mm_mul_ps (a3, a3));
                }
                float* out = coarseScore.data() + m;
                _mm_storeu_ps (out, _mm_div_ps (c0, _mm_sqrt_ps (e0)));
                _mm_storeu_ps (out + 4, _mm_div_ps (c1, _mm_sqrt_ps (e1)));
                _mm_storeu_ps (out + 8, _mm_div_ps (c2, _mm_sqrt_ps (e2)));
                _mm_storeu_ps (out + 12, _mm_div_ps (c3, _mm_sqrt_ps (e3)));
            }
            for (; m + 4 <= numCoarse; m += 4)
            {
                __m128 c = _mm_setzero_ps(), e = _mm_set1_ps (1.0e-9f);
                const float* pa = coarseCandidate.data() + m;
                for (int i = 0, k = 0; i < corrLen; i += step, ++k)
                {
                    const __m128 a = _mm_loadu_ps (pa + k);
                    c = _mm_add_ps (c, _mm_mul_ps (a, _mm_set1_ps (ref[i])));
                    e = _mm_add_ps (e, _mm_mul_ps (a, a));
                }
                _mm_storeu_ps (coarseScore.data() + m, _mm_div_ps (c, _mm_sqrt_ps (e)));
            }
#endif
            for (; m < numCoarse; ++m)
            {
                const float* pa = coarseCandidate.data() + m;
                float c = 0.0f, e = 1.0e-9f;
                for (int i = 0, k = 0; i < corrLen; i += step, ++k)
                {
                    const float a = pa[k];
                    c += a * ref[i];
                    e += a * a;
                }
                coarseScore[(size_t) m] = c / std::sqrt (e);
            }
        }
        int best = centre;
        float bestScore = coarseScore[(size_t) (centre / step)];
        // coarse: every step-th lag, alternately below and above the middle
        for (int k = step; k <= centre; k += step)
            for (int lag : { centre - k, centre + k })
                if (lag >= 0 && lag <= search)
                {
                    const float sc = coarseScore[(size_t) (lag / step)];
                    if (sc > bestScore)
                    {
                        bestScore = sc;
                        best = lag;
                    }
                }
        // fine: the lags between the best coarse one and its coarse neighbours, nearest first, at
        // full resolution. Above 48 kHz the coarse winner is scored again at full resolution first
        // (its coarse score summed fewer samples, so it is on another scale).
        const int coarse = best;
        auto tryLag = [&] (int lag)
        {
            const float sc = score (lag);
            if (sc > bestScore)
            {
                bestScore = sc;
                best = lag;
            }
        };
        if (step == 2)
        {
            // as it always was, bit for bit: lags coarse - 1 and coarse + 1 (at lag 0 this tries 0
            // and 2, both already scored, and never 1: kept so the sound up to 48 kHz is unchanged)
            for (int lag = std::max (0, coarse - 1); lag <= std::min (search, coarse + 1); lag += 2)
                tryLag (lag);
        }
        else
        {
            bestScore = score (coarse);
            for (int d = 1; d < step; ++d)
                for (int lag : { coarse - d, coarse + d })
                    if (lag >= 0 && lag <= search)
                        tryLag (lag);
        }
        gr.delay = start + (float) best + frac;
    }

    SincDelayLine buffer;
    // restart()'s scratch (sized by prepare): the samples the lags compare (all, and every step-th),
    // the other grain's, and the coarse lags' scores
    std::vector<float> candidate, coarseCandidate, reference, coarseScore;
    Grain grain[2] {};
    float window = 2400.0f, base = 10.0f, mix = 0.0f, ratio = 1.0f;
    int search = 576, corrLen = 384;
    int step = 2; // the coarse search's grid in samples: 2 up to 48 kHz, more above (restart())
    bool on = false, running = false;
};
} // namespace bidoo::dsp

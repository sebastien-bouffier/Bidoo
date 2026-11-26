#pragma once

// PhaseVocoderShifter: a pitch shifter for dense, polyphonic sound (a reverb tail, pads, a shimmer's
// feedback), by a phase vocoder with identity phase locking (Laroche & Dolson, "Improved phase vocoder
// time-scale modification of audio", 1999, written from the paper). Each frame's spectral peaks move to
// ratio x their frequency, their phase advanced at the new frequency; the bins around a peak keep their
// phase offset to it (times the ratio), so a partial's bins stay coherent and don't beat.
// Frames of 8192 samples at 44.1 / 48 kHz (16384 above 64 kHz: the same resolution in hertz), 8 x overlap.
// Measured on an octave up (repos\reverb-bench\shifters\shootout.py): the energy off the shifted partials is
// -41 dB on a pad chord and -21 dB on 30 random sines, against -10 / -8 dB for GranularPitchShifter and
// -8.5 / -14 dB for Prism's splice shifter; no splice roughness. The cost: a latency of one frame (170 ms)
// and transients smeared over a frame. For shimmers and pads; for drums, voices or a delay's head use
// GranularPitchShifter, PsolaStream or PitchHeads. Mono: one per channel. prepare() allocates; process() does
// not (two FFTs every hop, 1024 samples at 48 kHz).

#include "Fft.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace bidoo::dsp
{
class PhaseVocoderShifter
{
public:
    void prepare (double sampleRate)
    {
        const int order = sampleRate > 64000.0 ? 14 : 13;
        n = 1 << order;
        hop = n / 8;
        bins = n / 2 + 1;
        fft = std::make_unique<Fft> (order);
        window.resize ((size_t) n);
        double sumSq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / n));
            sumSq += (double) window[(size_t) i] * window[(size_t) i];
        }
        outScale = (float) (hop / sumSq); // the windowed overlap-add sums to one
        input.assign ((size_t) n, 0.0f);
        output.assign ((size_t) n, 0.0f);
        frame.assign ((size_t) (2 * n), 0.0f);
        mag.assign ((size_t) bins, 0.0f);
        phase.assign ((size_t) bins, 0.0f);
        lastPhase.assign ((size_t) bins, 0.0f);
        advance.assign ((size_t) bins, 0.0f);
        outPhase.assign ((size_t) bins, 0.0);
        outRe.assign ((size_t) bins, 0.0f);
        outIm.assign ((size_t) bins, 0.0f);
        peaks.assign ((size_t) bins, 0);
        reset();
    }

    void reset()
    {
        std::fill (input.begin(), input.end(), 0.0f);
        std::fill (output.begin(), output.end(), 0.0f);
        std::fill (lastPhase.begin(), lastPhase.end(), 0.0f);
        std::fill (outPhase.begin(), outPhase.end(), 0.0);
        writePos = 0;
        count = 0;
    }

    void setRatio (float r) { ratio = std::clamp (r, 0.25f, 4.0f); }
    void setSemitones (float semitones) { setRatio (std::pow (2.0f, semitones / 12.0f)); }

    // samples between an input and its shifted output
    int latency() const { return n; }

    float process (float x)
    {
        input[(size_t) writePos] = x;
        writePos = (writePos + 1) & (n - 1);
        const float y = output[(size_t) count];
        if (++count == hop)
        {
            count = 0;
            runFrame();
        }
        return y;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    void runFrame()
    {
        // the last n inputs, oldest first, windowed
        for (int i = 0; i < n; ++i)
            frame[(size_t) i] = input[(size_t) ((writePos + i) & (n - 1))] * window[(size_t) i];
        fft->performRealOnlyForwardTransform (frame.data(), true);

        const double hopPhase = 2.0 * kPi * hop / n;
        float maxMag = 0.0f;
        for (int k = 0; k < bins; ++k)
        {
            const float re = frame[(size_t) (2 * k)], im = frame[(size_t) (2 * k + 1)];
            const float m = std::sqrt (re * re + im * im);
            const float ph = std::atan2 (im, re);
            mag[(size_t) k] = m;
            maxMag = std::max (maxMag, m);
            // the bin's true phase advance over a hop: the expected one plus the wrapped deviation
            const double expected = hopPhase * k;
            double dev = (double) ph - lastPhase[(size_t) k] - expected;
            dev -= 2.0 * kPi * std::floor ((dev + kPi) / (2.0 * kPi));
            advance[(size_t) k] = (float) (expected + dev);
            lastPhase[(size_t) k] = ph;
            phase[(size_t) k] = ph;
        }

        // the peaks: local maxima over two bins each side, above -80 dB of the loudest
        int numPeaks = 0;
        const float floor = maxMag * 1.0e-4f;
        for (int k = 2; k < bins - 2; ++k)
        {
            const float m = mag[(size_t) k];
            if (m > floor && m > mag[(size_t) (k - 1)] && m > mag[(size_t) (k - 2)] && m >= mag[(size_t) (k + 1)] && m >= mag[(size_t) (k + 2)])
                peaks[(size_t) numPeaks++] = k;
        }

        std::fill (outRe.begin(), outRe.end(), 0.0f);
        std::fill (outIm.begin(), outIm.end(), 0.0f);
        for (int i = 0; i < numPeaks; ++i)
        {
            const int p = peaks[(size_t) i];
            const int q = (int) std::lround (p * ratio);
            if (q >= bins)
                break;
            // the peak's phase advances at its new frequency; its region (halfway to its neighbours) follows it
            outPhase[(size_t) q] = std::fmod (outPhase[(size_t) q] + (double) ratio * advance[(size_t) p], 2.0 * kPi);
            const int lo = i == 0 ? 0 : (peaks[(size_t) (i - 1)] + p) / 2;
            const int hi = i == numPeaks - 1 ? bins : (p + peaks[(size_t) (i + 1)]) / 2;
            for (int b = lo; b < hi; ++b)
            {
                const int t = (int) std::lround (b * ratio);
                if (t >= bins)
                    break;
                const double a = outPhase[(size_t) q] + (double) ratio * ((double) phase[(size_t) b] - phase[(size_t) p]);
                outRe[(size_t) t] += mag[(size_t) b] * (float) std::cos (a);
                outIm[(size_t) t] += mag[(size_t) b] * (float) std::sin (a);
            }
        }
        for (int k = 0; k < bins; ++k)
        {
            frame[(size_t) (2 * k)] = outRe[(size_t) k];
            frame[(size_t) (2 * k + 1)] = outIm[(size_t) k];
        }
        frame[1] = 0.0f; // DC and Nyquist are real
        frame[(size_t) (2 * (bins - 1) + 1)] = 0.0f;
        fft->performRealOnlyInverseTransform (frame.data());

        // overlap-add: the next hop's samples move to the front
        std::copy (output.begin() + hop, output.end(), output.begin());
        std::fill (output.end() - hop, output.end(), 0.0f);
        for (int i = 0; i < n; ++i)
            output[(size_t) i] += frame[(size_t) i] * window[(size_t) i] * outScale;
    }

    int n = 8192, hop = 1024, bins = 4097, writePos = 0, count = 0;
    float ratio = 2.0f, outScale = 1.0f;
    std::unique_ptr<Fft> fft;
    std::vector<float> window, input, output, frame, mag, phase, lastPhase, advance, outRe, outIm;
    std::vector<double> outPhase;
    std::vector<int> peaks;
};
} // namespace bidoo::dsp

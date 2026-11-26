#pragma once

// Real FFT backed by pffft (SIMD on x86 and ARM), with the buffer layouts and scaling of
// juce::dsp::FFT's real-only transforms, so code written for JUCE's FFT works unchanged
// (this is Fab's FabFft, without JUCE):
//  - performRealOnlyForwardTransform: N real samples in, interleaved complex bins 0..N/2 out
//    (and the mirrored negative frequencies up to 2N floats unless onlyNonNegative); unscaled;
//  - performRealOnlyInverseTransform: interleaved complex bins 0..N/2 in, N real samples out,
//    scaled by 1/N;
//  - performFrequencyOnlyForwardTransform: magnitudes of bins 0..N/2 (mirrored unless asked not).
// The caller's buffer must hold 2N floats, as with JUCE. Sizes too small for pffft (below 32) are
// computed directly (a plain DFT, cheap at those sizes).
//
// For partitioned convolution, use the raw pffft API (pffft_transform + pffft_zconvolve_accumulate
// in pffft's internal order) through getSetup(), with buffers from pffft_aligned_malloc. It is
// null for the small sizes.
//
// Construct on the message thread or in prepare(): it allocates.

#include "pffft.h"
#include <vector>

namespace bidoo::dsp
{
class Fft
{
public:
    explicit Fft (int order);
    ~Fft();

    Fft (const Fft&) = delete;
    Fft& operator= (const Fft&) = delete;

    int getSize() const noexcept { return size; }
    const PFFFT_Setup* getSetup() const noexcept { return setup; }

    void performRealOnlyForwardTransform (float* data, bool onlyNonNegative = false) const noexcept;
    void performRealOnlyInverseTransform (float* data) const noexcept;
    void performFrequencyOnlyForwardTransform (float* data, bool ignoreNegative = false) const noexcept;

private:
    void smallForward (float* data) const noexcept;
    void smallInverse (float* data) const noexcept;

    int size;
    PFFFT_Setup* setup = nullptr;
    float* buf = nullptr;
    float* work = nullptr;
    std::vector<double> smallCos, smallSin; // the small-size path: cos/sin(2 pi k / N)
    mutable std::vector<float> smallTmp;
};
} // namespace bidoo::dsp

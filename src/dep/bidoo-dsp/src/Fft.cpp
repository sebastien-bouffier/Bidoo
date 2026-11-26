#include "bidoo/dsp/fft/Fft.h"
#include "bidoo/dsp/core/Math.h"
#include <algorithm>
#include <cmath>

namespace bidoo::dsp
{
Fft::Fft (int order) : size (1 << order)
{
    // the sizes are powers of two, valid from pffft's smallest real size (2 x the SIMD width squared): the classic
    // pffft API only, so the file also builds on the older pffft the VCV Rack plugin ships (no pffft_min_fft_size
    // / pffft_is_valid_size there); the same sizes as before on bidoo-dsp's own pffft
    const int simd = pffft_simd_size();
    if (size >= 2 * simd * simd)
    {
        setup = pffft_new_setup (size, PFFFT_REAL);
        buf = static_cast<float*> (pffft_aligned_malloc (sizeof (float) * (size_t) size));
        work = static_cast<float*> (pffft_aligned_malloc (sizeof (float) * (size_t) size));
    }
    if (setup == nullptr)
    {
        smallCos.resize ((size_t) size);
        smallSin.resize ((size_t) size);
        for (int k = 0; k < size; ++k)
        {
            smallCos[(size_t) k] = std::cos (kTwoPi * k / size);
            smallSin[(size_t) k] = std::sin (kTwoPi * k / size);
        }
        smallTmp.resize ((size_t) size + 2);
    }
}

Fft::~Fft()
{
    if (setup != nullptr)
        pffft_destroy_setup (setup);
    pffft_aligned_free (buf);
    pffft_aligned_free (work);
}

void Fft::performRealOnlyForwardTransform (float* data, bool onlyNonNegative) const noexcept
{
    const int half = size / 2;
    if (setup == nullptr)
        smallForward (data);
    else
    {
        std::copy (data, data + size, buf);
        pffft_transform_ordered (setup, buf, buf, work, PFFFT_FORWARD);
        // pffft: [DC, Nyquist, re1, im1, ...]; JUCE: [re0, im0, re1, im1, ..., reN/2, imN/2]
        std::copy (buf + 2, buf + size, data + 2);
        data[0] = buf[0];
        data[1] = 0.0f;
        data[size] = buf[1];
        data[size + 1] = 0.0f;
    }
    if (! onlyNonNegative)
        for (int k = half + 1; k < size; ++k)
        {
            data[2 * k] = data[2 * (size - k)];
            data[2 * k + 1] = -data[2 * (size - k) + 1];
        }
}

void Fft::performRealOnlyInverseTransform (float* data) const noexcept
{
    if (setup == nullptr)
    {
        smallInverse (data);
        return;
    }
    buf[0] = data[0];
    buf[1] = data[size];
    std::copy (data + 2, data + size, buf + 2);
    pffft_transform_ordered (setup, buf, buf, work, PFFFT_BACKWARD);
    const float scale = 1.0f / (float) size;
    for (int i = 0; i < size; ++i)
        data[i] = buf[i] * scale;
}

void Fft::performFrequencyOnlyForwardTransform (float* data, bool ignoreNegative) const noexcept
{
    performRealOnlyForwardTransform (data, true);
    const int half = size / 2;
    for (int k = 0; k <= half; ++k)
        data[k] = std::hypot (data[2 * k], data[2 * k + 1]);
    if (! ignoreNegative)
        for (int k = half + 1; k < size; ++k)
            data[k] = data[size - k];
    std::fill (data + size, data + 2 * size, 0.0f);
}

// X[k] = sum x[n] e^(-2 pi i k n / N), bins 0..N/2, in double.
void Fft::smallForward (float* data) const noexcept
{
    const int half = size / 2;
    std::copy (data, data + size, smallTmp.begin());
    for (int k = 0; k <= half; ++k)
    {
        double re = 0.0, im = 0.0;
        for (int n = 0; n < size; ++n)
        {
            const auto idx = (size_t) ((k * n) % size);
            re += smallTmp[(size_t) n] * smallCos[idx];
            im -= smallTmp[(size_t) n] * smallSin[idx];
        }
        data[2 * k] = (float) re;
        data[2 * k + 1] = (float) im;
    }
}

// x[n] = 1/N sum X[k] e^(2 pi i k n / N), from bins 0..N/2 (the rest mirrored).
void Fft::smallInverse (float* data) const noexcept
{
    const int half = size / 2;
    std::copy (data, data + size + 2, smallTmp.begin());
    for (int n = 0; n < size; ++n)
    {
        double sum = 0.0;
        for (int k = 0; k <= half; ++k)
        {
            const auto idx = (size_t) ((k * n) % size);
            const double re = smallTmp[(size_t) (2 * k)], im = smallTmp[(size_t) (2 * k + 1)];
            const double term = re * smallCos[idx] - im * smallSin[idx];
            sum += (k == 0 || k == half) ? term : 2.0 * term;
        }
        data[n] = (float) (sum / size);
    }
}
} // namespace bidoo::dsp

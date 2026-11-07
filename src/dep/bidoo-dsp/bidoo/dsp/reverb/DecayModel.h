#pragma once

// DecayModel: the decay time (RT60) per frequency of a reverb, from Grotte. A base time, a Decay
// Rate EQ of up to six bands (bell, shelves, notch: each a multiplier on the time, in log2 so they
// add), a Brightness that makes high frequencies die sooner in darker spaces, and a style's own
// tilt. The curves are analog prototypes (s = jf/fc), so they don't depend on the sample rate.
// Feed timeAt (f) to AttenuationFit to get the absorption filters of the loop, and to the display.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace bidoo::dsp::reverb
{
constexpr int kDecayBands = 6;
enum Style { Modern, Vintage, Plate };
enum DecayShape { DecayBell, DecayLowShelf, DecayHighShelf, DecayNotch };

//  Curve shapes. Analog prototypes (s = jf/fc) so the curves don't depend on the sample rate.
// ---------------------------------------------------------------------------------------------
// kind 0 bell, 1 low shelf, 2 high shelf: dB at f.
inline double analogDb (int kind, double f, double fc, double q, double gainDb)
{
    const std::complex<double> s (0.0, f / fc);
    const double A = std::pow (10.0, gainDb / 40.0);
    std::complex<double> num, den;
    if (kind == 0)
    {
        num = s * s + s * (A / q) + 1.0;
        den = s * s + s / (A * q) + 1.0;
    }
    else
    {
        const double k = std::sqrt (A) / q;
        if (kind == 1)
        {
            num = A * (s * s + k * s + A);
            den = A * s * s + k * s + 1.0;
        }
        else
        {
            num = A * (A * s * s + k * s + 1.0);
            den = s * s + k * s + A;
        }
    }
    return 20.0 * std::log10 (std::max (1.0e-12, std::abs (num / den)));
}

// How much of a band's rate applies at f (0..1).
inline double decayWeight (int shape, double f, double fc, double q)
{
    switch (shape)
    {
        case DecayLowShelf:  return analogDb (1, f, fc, q * 0.7071, 12.0) / 12.0;
        case DecayHighShelf: return analogDb (2, f, fc, q * 0.7071, 12.0) / 12.0;
        case DecayNotch:
        {
            const std::complex<double> s (0.0, f / fc);
            return 1.0 - std::abs ((s * s + 1.0) / (s * s + s / std::max (0.05, q * 2.0) + 1.0));
        }
        case DecayBell:
        default:             return analogDb (0, f, fc, q, 12.0) / 12.0;
    }
}

struct DecayBand
{
    bool active = false;
    int shape = DecayBell;
    double freq = 1000.0, rate = 1.0, q = 1.0; // rate: decay time multiplier (0.25..4)
};

struct DecayModel
{
    double baseTime = 2.0;   // Space x Decay Rate, seconds
    double brightness = 0.5; // 0..1
    int style = Modern;
    std::array<DecayBand, kDecayBands> bands;

    // The Decay Rate EQ alone, as log2 of the multiplier.
    double bandsLog2 (double f) const
    {
        double v = 0.0;
        for (const auto& b : bands)
            if (b.active)
                v += std::log2 (std::max (0.01, b.rate)) * decayWeight (b.shape, f, b.freq, b.q);
        return v;
    }

    // Brightness (high frequencies die sooner in darker spaces) and the style's own tilt.
    double toneLog2 (double f) const
    {
        double v = (0.2 - 2.6 * (1.0 - brightness)) * analogDb (2, f, 4000.0, 0.5, 12.0) / 12.0;
        if (style == Vintage)
            v += 0.35 * analogDb (1, f, 220.0, 0.6, 12.0) / 12.0 - 0.3 * analogDb (2, f, 7000.0, 0.6, 12.0) / 12.0;
        else if (style == Plate)
            v += -0.35 * analogDb (1, f, 160.0, 0.6, 12.0) / 12.0 + 0.2 * analogDb (2, f, 5000.0, 0.6, 12.0) / 12.0;
        return v;
    }

    double timeAt (double f) const { return std::clamp (baseTime * std::exp2 (bandsLog2 (f) + toneLog2 (f)), 0.05, 150.0); }
};

// ---------------------------------------------------------------------------------------------
//  Small building blocks
} // namespace bidoo::dsp::reverb

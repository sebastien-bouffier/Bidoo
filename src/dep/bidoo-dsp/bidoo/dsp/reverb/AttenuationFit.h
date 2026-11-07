#pragma once

// AttenuationFit: absorption filters fitted to a target attenuation curve, from Grotte. Turns a
// curve (dB of loss per pass at 48 frequencies) into the gains of 11 filters (a broadband gain, a
// low shelf, eight octave peaks and a high shelf) by regularised least squares, refined twice
// against the filters' real responses, then nudged down so the cascade never exceeds the target's
// smallest loss (the loop stays strictly passive). A reverb line of loop length L then loses
// -60 dB * L / RT60 (f) per pass at every frequency, so its tail's RT60 follows the curve.
// Also: DBiquad, a double-precision transposed direct form II (at high sample rates the low
// stages have poles so close to 1 that float coefficients can't hold a loss of a few hundredths
// of a dB per pass).

#include "../delay/GuardDelayLine.h"
#include "../filters/Filters.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace bidoo::dsp::reverb
{
using bidoo::dsp::BiquadCoeffs;
namespace design = bidoo::dsp::design;
constexpr double kPi = 3.14159265358979323846;

// Double precision: at high sample rates the low stages have poles so close to 1 that float
// coefficients can't hold a loss of a few hundredths of a dB per pass.
struct DBiquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, s1 = 0.0, s2 = 0.0;

    void set (const BiquadCoeffs& c)
    {
        b0 = c.b0;
        b1 = c.b1;
        b2 = c.b2;
        a1 = c.a1;
        a2 = c.a2;
    }

    void reset() { s1 = s2 = 0.0; }

    inline double process (double x)
    {
        const double y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
};

// ---------------------------------------------------------------------------------------------
//  Absorption filters fitted to a target attenuation curve
// ---------------------------------------------------------------------------------------------
class AttenuationFit
{
public:
    static constexpr int kPoints = 48;
    static constexpr int kFilters = 11; // broadband, low shelf, 8 octave peaks, high shelf
    static constexpr int kStages = kFilters - 1;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // up to near Nyquist: nothing above the grid would hold the filters down otherwise
        const double fTop = 0.46 * sr;
        for (int k = 0; k < kPoints; ++k)
            freqs[(size_t) k] = 20.0 * std::pow (fTop / 20.0, (double) k / (kPoints - 1));

        centre[0] = 0.0;
        centre[1] = 46.0;
        for (int j = 0; j < 8; ++j)
            centre[(size_t) (2 + j)] = 62.5 * std::pow (2.0, j);
        centre[10] = std::min (11300.0, 0.4 * sr);

        // what designStage() and stageDb() need that doesn't depend on the gain, worked out once
        // with the very expressions of the shared designs (same coefficients, bit for bit)
        for (int j = 1; j < kFilters; ++j)
        {
            const double q = (j == 1 || j == kFilters - 1) ? 0.7071 : kPeakQ;
            const double w = 2.0 * kPi * design::clampFreq (centre[(size_t) j], sr) / sr;
            stageCos[(size_t) j] = std::cos (w);
            stageAlpha[(size_t) j] = std::sin (w) / (2.0 * q);
        }
        for (int k = 0; k < kPoints; ++k)
        {
            const double w = 2.0 * kPi * freqs[(size_t) k] / sr;
            z1s[(size_t) k] = std::polar (1.0, -w);
            z2s[(size_t) k] = z1s[(size_t) k] * z1s[(size_t) k];
        }

        // the mean loss of the line read per pass (the fractional part sweeps under modulation)
        for (int k = 0; k < kPoints; ++k)
        {
            const double w = 2.0 * kPi * freqs[(size_t) k] / sr;
            double sum = 0.0;
            const int nt = 32;
            for (int i = 0; i < nt; ++i)
            {
                const double t = (i + 0.5) / nt;
                double h[6];
                GuardDelayLine::lagrange6 (t, h);
                std::complex<double> H = 0.0;
                for (int m = 0; m < 6; ++m)
                    H += h[m] * std::polar (1.0, -w * (m - 2));
                sum += 20.0 * std::log10 (std::max (1.0e-9, std::abs (H)));
            }
            interpLoss[(size_t) k] = sum / nt;
        }

        // basis: the dB response of each filter at the prototype gain, per dB of gain
        std::array<std::array<double, kFilters>, kPoints> B {};
        for (int j = 0; j < kFilters; ++j)
        {
            const auto c = designStage (j, kProtoDb);
            for (int k = 0; k < kPoints; ++k)
                B[(size_t) k][(size_t) j] = j == 0 ? 1.0 : c.magnitudeDb (freqs[(size_t) k], sr) / kProtoDb;
        }

        // P = (B'B + lambda I)^-1 B'
        double M[kFilters][2 * kFilters] = {};
        for (int a = 0; a < kFilters; ++a)
        {
            for (int b = 0; b < kFilters; ++b)
            {
                double s = 0.0;
                for (int k = 0; k < kPoints; ++k)
                    s += B[(size_t) k][(size_t) a] * B[(size_t) k][(size_t) b];
                M[a][b] = s + (a == b ? 1.0e-3 : 0.0);
            }
            M[a][kFilters + a] = 1.0;
        }
        for (int col = 0; col < kFilters; ++col)
        {
            int piv = col;
            for (int r = col + 1; r < kFilters; ++r)
                if (std::abs (M[r][col]) > std::abs (M[piv][col]))
                    piv = r;
            for (int c = 0; c < 2 * kFilters; ++c)
                std::swap (M[col][c], M[piv][c]);
            const double d = M[col][col];
            for (int c = 0; c < 2 * kFilters; ++c)
                M[col][c] /= d;
            for (int r = 0; r < kFilters; ++r)
                if (r != col)
                {
                    const double f = M[r][col];
                    for (int c = 0; c < 2 * kFilters; ++c)
                        M[r][c] -= f * M[col][c];
                }
        }
        for (int a = 0; a < kFilters; ++a)
            for (int k = 0; k < kPoints; ++k)
            {
                double s = 0.0;
                for (int b = 0; b < kFilters; ++b)
                    s += M[a][kFilters + b] * B[(size_t) k][(size_t) b];
                P[(size_t) a][(size_t) k] = s;
            }
    }

    double freqAt (int k) const { return freqs[(size_t) k]; }
    double interpLossAt (int k) const { return interpLoss[(size_t) k]; }

    // Stage j (1..10) at a gain; j = 0 is the broadband gain (identity here).
    // (design::lowShelf, highShelf and bell with their frequency terms precomputed: this runs
    // hundreds of times per sub-block while the decay glides)
    BiquadCoeffs designStage (int j, double gainDb) const
    {
        if (j == 0)
            return BiquadCoeffs::identity();
        const double A = std::pow (10.0, gainDb / 40.0);
        const double c = stageCos[(size_t) j], a = stageAlpha[(size_t) j];
        if (j == 1 || j == kFilters - 1)
        {
            const double sa = 2.0 * std::sqrt (A) * a;
            if (j == 1)
                return design::normalise (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
                                          (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
            return design::normalise (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
                                      (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
        }
        return design::normalise (1.0 + a * A, -2.0 * c, 1.0 - a * A, 1.0 + a / A, -2.0 * c, 1.0 - a / A);
    }

    // |H|^2 of c at point k (as c.magnitude() squared, floored like magnitudeDb()), in real
    // arithmetic with the point's e^-jw precomputed: no complex division, no square root
    double stagePower (const BiquadCoeffs& c, int k) const
    {
        const auto& z1 = z1s[(size_t) k];
        const auto& z2 = z2s[(size_t) k];
        const double nr = c.b0 + c.b1 * z1.real() + c.b2 * z2.real(), ni = c.b1 * z1.imag() + c.b2 * z2.imag();
        const double dr = 1.0 + c.a1 * z1.real() + c.a2 * z2.real(), di = c.a1 * z1.imag() + c.a2 * z2.imag();
        return std::max (1.0e-24, (nr * nr + ni * ni) / (dr * dr + di * di));
    }

    // Least-squares gains (dB) for a target attenuation (dB per point), refined twice against the
    // filters' real responses, then nudged down so the cascade never exceeds the target's
    // smallest loss (the loop stays strictly passive).
    void fit (const double* target, double* gains, bool passive = true) const
    {
        solve (target, gains);
        double actual[kPoints], err[kPoints], delta[kFilters];
        for (int pass = 0; pass < 2; ++pass)
        {
            response (gains, actual);
            for (int k = 0; k < kPoints; ++k)
                err[k] = target[k] - actual[k];
            solve (err, delta);
            for (int j = 0; j < kFilters; ++j)
                gains[j] += delta[j];
        }
        if (! passive)
            return;
        response (gains, actual);
        double excess = -1.0e9, leastLoss = -1.0e9;
        for (int k = 0; k < kPoints; ++k)
        {
            excess = std::max (excess, actual[k]);
            leastLoss = std::max (leastLoss, target[k]);
        }
        const double ceiling = 0.5 * leastLoss; // negative
        if (excess > ceiling)
            gains[0] -= excess - ceiling;
    }

    void response (const double* gains, double* out) const
    {
        // the stages' power gains multiplied, then one logarithm per point (not one per stage)
        double power[kPoints];
        for (int k = 0; k < kPoints; ++k)
            power[k] = 1.0;
        for (int j = 1; j < kFilters; ++j)
        {
            if (std::abs (gains[j]) < 1.0e-6)
                continue;
            const auto c = designStage (j, gains[j]);
            for (int k = 0; k < kPoints; ++k)
                power[k] *= stagePower (c, k);
        }
        for (int k = 0; k < kPoints; ++k)
            out[k] = gains[0] + 10.0 * std::log10 (power[k]);
    }

private:
    void solve (const double* t, double* g) const
    {
        for (int j = 0; j < kFilters; ++j)
        {
            double s = 0.0;
            for (int k = 0; k < kPoints; ++k)
                s += P[(size_t) j][(size_t) k] * t[k];
            g[j] = s;
        }
    }

    static constexpr double kProtoDb = -6.0;
    static constexpr double kPeakQ = 1.1;
    double sr = 48000.0;
    std::array<double, kPoints> freqs {};
    std::array<double, kFilters> centre {};
    std::array<double, kPoints> interpLoss {};
    std::array<std::array<double, kPoints>, kFilters> P {};
    std::array<double, kFilters> stageCos {}, stageAlpha {};
    std::array<std::complex<double>, kPoints> z1s {}, z2s {};
};

// ---------------------------------------------------------------------------------------------
} // namespace bidoo::dsp::reverb

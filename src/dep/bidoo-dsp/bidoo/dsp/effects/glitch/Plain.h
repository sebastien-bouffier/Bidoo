#pragma once

// JUCE's small maths helpers as plain C++, with JUCE's exact arithmetic, so the engine (Engine.h,
// Effects.h, Pattern.h, Macros.h, SpectralDelay.h) builds and sounds the same without JUCE.

#include <cmath>

namespace bidoo::dsp::glitch
{
template <typename T> constexpr T jmin (T a, T b) { return b < a ? b : a; }
template <typename T> constexpr T jmax (T a, T b) { return a < b ? b : a; }
template <typename T> constexpr T jmax (T a, T b, T c) { return a < b ? (b < c ? c : b) : (a < c ? c : a); }
template <typename T> constexpr T jlimit (T lo, T hi, T v) { return v < lo ? lo : (hi < v ? hi : v); }

template <typename F>
struct Pi
{
    static constexpr F pi = static_cast<F> (3.141592653589793238L);
    static constexpr F twoPi = static_cast<F> (2 * 3.141592653589793238L);
    static constexpr F halfPi = static_cast<F> (3.141592653589793238L / 2);
};

// To nearest, ties to even (juce::roundToInt)
inline int roundToInt (double x)
{
    union { int asInt[2]; double asDouble; } n;
    n.asDouble = x + 6755399441055744.0;
    return n.asInt[0];
}

// juce::Decibels with its -100 dB floor
inline float decibelsToGain (float db) { return db > -100.0f ? std::pow (10.0f, db * 0.05f) : 0.0f; }
inline float gainToDecibels (float g) { return g > 0.0f ? jmax (-100.0f, static_cast<float> (std::log10 (g)) * 20.0f) : -100.0f; }

// 0..9, a..f, A..F -> 0..15, anything else -1
inline int hexDigitValue (char c)
{
    auto d = (unsigned int) (c - '0');
    if (d < 10u)
        return (int) d;
    d += (unsigned int) ('0' - 'a');
    if (d < 6u)
        return (int) d + 10;
    d += (unsigned int) ('a' - 'A');
    if (d < 6u)
        return (int) d + 10;
    return -1;
}

// A skewed range (juce::NormalisableRange with setSkewForCentre, no interval), its arithmetic exactly
struct SkewRange
{
    float start, end, skew;
    SkewRange (float lo, float hi, float centre)
        : start (lo), end (hi), skew (std::log (0.5f) / std::log ((centre - lo) / (hi - lo))) {}
    float convertTo0to1 (float v) const { return std::pow (jlimit (0.0f, 1.0f, (v - start) / (end - start)), skew); }
    float convertFrom0to1 (float p) const
    {
        p = jlimit (0.0f, 1.0f, p);
        if (skew != 1.0f && p > 0.0f)
            p = std::exp (std::log (p) / skew);
        return start + (end - start) * p;
    }
};
} // namespace bidoo::dsp::glitch

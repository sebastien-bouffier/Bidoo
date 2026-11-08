#pragma once

// The voice filter: stereo, set up at control rate, run per sample.
// State-variable types use the trapezoidal (TPT) SVF; 24 dB types cascade a second stage without
// resonance. The ladder is four one-pole TPT stages in a zero-delay feedback loop (ZDF, Zavalishin's
// "The Art of VA Filter Design", ch. 5) with tanh on the loop's input, so its cutoff and its
// self-oscillation stay in tune up to near Nyquist. Combs are a tuned delay with feedback, read by
// Hermite interpolation. Drive is tanh with first-order antiderivative anti-aliasing (ADAA).
// Added in 0.9.0, with a Morph setting (0..1):
//   Morph 12/24  low-pass, band-pass, high-pass and notch in one, Morph sweeping between them.
//   Formant      three band-passes at the formants of a sung vowel, Morph going A, E, I, O, U;
//                the cutoff shifts the formants (1 kHz leaves them where they are).
//   Phaser       six all-pass stages around the cutoff, Morph spreading them apart; Res feeds back.
//   Flanger      the dry sound plus a delay of one period of the cutoff; Res feeds back, Morph
//                turns the feedback from positive (0) through none (0.5) to negative (1).
// Added in 0.56 (all zero-delay where they resonate, tuned so their peak and their self-oscillation
// sit on the cutoff up to near Nyquist; nonlinearities inside a loop are solved by the secant method of
// the ladder, so every loop is unconditionally stable whatever the cutoff does, even at audio rate):
//   Diode 24     a diode ladder (TB-303 style): four one-poles each pulled back by the next (coupled
//                at half strength, solved as one tridiagonal system), so its knee is soft and the
//                resonance takes more bass than the ladder's; the resonance peak is placed on the
//                cutoff (the loop's 180-degree point sits 1.055 x above the stage corner), tanh on the
//                loop's input; Morph (Bass) feeds back a low-passed copy of the input so the bass the
//                resonance takes away comes back (0: thin, the 303's squelch; 1: full).
//   OTA 24       four OTA one-poles each saturating on its own input difference (tanh per stage,
//                each solved by its secant) and a soft clip in the feedback that holds the
//                self-oscillation in tune: the resonance rounds off rather than the ladder's
//                input-clipped bark, and Drive makes the stages bend; Morph (Poles) taps 4, 3, 2, 1 poles.
//   SK Low 12, SK High 12  a Sallen-Key two-pole (Korg MS-20 style), resonance by positive feedback
//                through a clipper: it screams and self-oscillates at the cutoff; Morph (Bite) and
//                Drive lower the clipper's threshold, so the resonance turns gritty.
//   Band Reject  a 24 dB low-pass below and a 24 dB high-pass above the cutoff, summed (Linkwitz-Riley,
//                so a width of 0 is flat); Morph (Width) opens the gap from half an octave to 6.
//   Low Shelf, High Shelf  the TPT SVF's shelves; Morph (Gain) from -24 dB through 0 (50 %) to
//                +24 dB, Res sharpens the corner into a bump.
//   Dist Comb    a comb whose feedback goes through a damping low-pass and a tanh (ADAA, its half
//                sample and the low-pass's phase taken off the delay so it stays in tune); Drive
//                drives the loop's tanh, Morph (Damp) darkens the loop, full Res self-oscillates.
//   Diffuser     eight Schroeder all-passes in series (different lengths left and right, each a whole
//                delay plus a first-order all-pass for the fraction, so the top stays flat), their
//                total length one period of the cutoff (up to 50 ms); Morph (Density) is the
//                all-passes' gain, Res feeds the chain's output back (soft-limited) into a small room.
//   Downsample   a sample and hold at twice the cutoff (polyBLEP steps, so the hold times are not
//                stuck to the sample grid), Morph (Smooth) from steps to straight lines; Res feeds
//                the held value back negated, a peak at the cutoff that sings from about 95 %.
// Added in 0.59, from bidoo-dsp's voice filter families (filters/VoiceFilters.h, where each is
// described): Low 6 to Formant F, mapped below in LibOf. Each family keeps one set of coefficients
// and a state per channel; the comb, chord comb and reverb families share one block of memory per
// channel (libMem), cleared when the type changes to one of them.

#include "Params.h"
#include <bidoo/dsp/filters/TptSvf.h>
#include <bidoo/dsp/filters/VoiceFilters.h>
#include <bidoo/dsp/shaping/AdaaTanhTable.h>
#include <bidoo/dsp/shaping/TanhPade.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace potion
{
// The bidoo-dsp family that plays each 0.59 filter type.
namespace vfl
{
namespace vf = bidoo::dsp::voicefilters;
using R = vf::Resp;
constexpr int kFirst = (int) FilterType::Low6;
constexpr int kCount = kNumFilterTypes - kFirst;
template <FilterType T>
struct LibOf;
#define POTION_LIB(T, ...)            \
    template <>                       \
    struct LibOf<FilterType::T>       \
    {                                 \
        using type = __VA_ARGS__;     \
    };
POTION_LIB (Low6, vf::Slope<vf::SlopeKind::Low6>)
POTION_LIB (Low18, vf::Slope<vf::SlopeKind::Low18>)
POTION_LIB (High6, vf::Slope<vf::SlopeKind::High6>)
POTION_LIB (High18, vf::Slope<vf::SlopeKind::High18>)
POTION_LIB (Peak24, vf::Slope<vf::SlopeKind::Peak24>)
POTION_LIB (DualLH, vf::Dual<R::Low, R::High>)
POTION_LIB (DualLB, vf::Dual<R::Low, R::Band>)
POTION_LIB (DualLP, vf::Dual<R::Low, R::Peak>)
POTION_LIB (DualLN, vf::Dual<R::Low, R::Notch>)
POTION_LIB (DualHB, vf::Dual<R::Band, R::High>) // the band below, the high-pass above
POTION_LIB (DualHP, vf::Dual<R::Peak, R::High>)
POTION_LIB (DualHN, vf::Dual<R::Notch, R::High>)
POTION_LIB (DualBP, vf::Dual<R::Band, R::Peak>)
POTION_LIB (DualPP, vf::Dual<R::Peak, R::Peak>)
POTION_LIB (DualPN, vf::Dual<R::Peak, R::Notch>)
POTION_LIB (DualNN, vf::Dual<R::Notch, R::Notch>)
POTION_LIB (MorphLph12, vf::Morph3<R::Low, R::Peak, R::High, false>)
POTION_LIB (MorphLph24, vf::Morph3<R::Low, R::Peak, R::High, true>)
POTION_LIB (MorphLnh12, vf::Morph3<R::Low, R::Notch, R::High, false>)
POTION_LIB (MorphLnh24, vf::Morph3<R::Low, R::Notch, R::High, true>)
POTION_LIB (MorphBpn12, vf::Morph3<R::Band, R::Peak, R::Notch, false>)
POTION_LIB (MorphBpn24, vf::Morph3<R::Band, R::Peak, R::Notch, true>)
POTION_LIB (CombLowPlus, vf::FilteredComb<vf::CombLoop::Low, false>)
POTION_LIB (CombLowMinus, vf::FilteredComb<vf::CombLoop::Low, true>)
POTION_LIB (CombHighPlus, vf::FilteredComb<vf::CombLoop::High, false>)
POTION_LIB (CombHighMinus, vf::FilteredComb<vf::CombLoop::High, true>)
POTION_LIB (CombBandPlus, vf::FilteredComb<vf::CombLoop::Band, false>)
POTION_LIB (CombBandMinus, vf::FilteredComb<vf::CombLoop::Band, true>)
POTION_LIB (Phaser4Plus, vf::Phaser<4, false>)
POTION_LIB (Phaser4Minus, vf::Phaser<4, true>)
POTION_LIB (Phaser8Plus, vf::Phaser<8, false>)
POTION_LIB (Phaser8Minus, vf::Phaser<8, true>)
POTION_LIB (Phaser16Plus, vf::Phaser<16, false>)
POTION_LIB (Phaser16Minus, vf::Phaser<16, true>)
POTION_LIB (BandEq, vf::BandEq)
POTION_LIB (RingMod, vf::RingMod)
POTION_LIB (ChordComb, vf::ChordComb)
POTION_LIB (Reverb, vf::SmallReverb)
POTION_LIB (AddBass, vf::AddBass)
POTION_LIB (ScreamLow, vf::Scream<false>)
POTION_LIB (ScreamBand, vf::Scream<true>)
POTION_LIB (Cmos12, vf::Cmos12)
POTION_LIB (DjFilter, vf::DjFilter)
POTION_LIB (FormantF, vf::FormantBank<vf::kFemaleVowels>)
#undef POTION_LIB
template <int I>
using Family = typename LibOf<(FilterType) (kFirst + I)>::type;
// one family's coefficients and its two channels' states
template <int I>
struct Slot
{
    static constexpr int index = I;
    typename Family<I>::Coeffs c;
    typename Family<I>::State s[2];
};
template <typename F>
concept HasMemory = requires (double fs) { F::memoryNeeded (fs); };
template <int... I>
std::tuple<Slot<I>...> slotsOf (std::integer_sequence<int, I...>);
using Slots = decltype (slotsOf (std::make_integer_sequence<int, kCount> {}));
} // namespace vfl

class VoiceFilter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const size_t len = (size_t) (fs / 20.0) + 4;
        for (auto& b : comb)
            b.assign (len, 0.0f);
        // the Diffuser shares the comb's memory: each stage gets a slice in proportion to its length
        for (int ch = 0; ch < 2; ++ch)
        {
            int start = 0;
            for (int i = 0; i < kDiffStages; ++i)
            {
                const int n = (int) ((double) (len - 1) * kDiffWeights[ch][i]);
                diffStart[ch][i] = start;
                diffLen[ch][i] = n;
                start += n;
            }
        }
        prepareLib();
        reset();
    }

    void reset()
    {
        for (auto& c : svf)
            for (auto& s : c)
                s[0] = s[1] = 0.0f;
        for (auto& c : ladder)
            for (auto& p : c)
                p.reset();
        ladderSlope[0] = ladderSlope[1] = 1.0f;
        driveState[0].reset();
        driveState[1].reset();
        for (auto& b : comb)
            std::fill (b.begin(), b.end(), 0.0f);
        combPos = 0;
        for (auto& c : formant)
            for (auto& s : c)
                s[0] = s[1] = 0.0f;
        for (auto& c : allpass)
            for (auto& p : c)
                p.reset();
        phaserLast[0] = phaserLast[1] = 0.0f;
        resetAdded();
        resetLib (type);
    }

    void setup (FilterType t, float cutoffHz, float res, float driveAmount, float morphAmount = 0.0f)
    {
        type = t;
        morph = std::clamp (morphAmount, 0.0f, 1.0f);
        cutoffHz = std::clamp (cutoffHz, 15.0f, (float) (fs * 0.45));
        res = std::clamp (res, 0.0f, 1.0f);
        drive = 1.0f + driveAmount * 8.0f;
        driveOut = 1.0f / std::sqrt (drive);
        const bool wasDriven = useDrive;
        useDrive = driveAmount > 0.001f;
        if (useDrive && ! wasDriven) // the drive's memory starts from silence, not from an old note
        {
            driveState[0].reset();
            driveState[1].reset();
        }
        chooseKernels();
        if ((int) type >= vfl::kFirst && (int) type < kNumFilterTypes)
        {
            setupLib (cutoffHz, res, driveAmount);
            return;
        }
        if (type >= FilterType::Diode24 && type <= FilterType::Downsample)
        {
            setupAdded (cutoffHz, res, driveAmount);
            return;
        }

        if (type == FilterType::CombPlus || type == FilterType::CombMinus || type == FilterType::Flanger)
        {
            combDelay = std::clamp ((float) (fs / cutoffHz), 2.0f, (float) comb[0].size() - 3.0f);
            combFb = type == FilterType::Flanger ? res * 0.95f * (1.0f - 2.0f * morph)
                                                 : (type == FilterType::CombPlus ? 1.0f : -1.0f) * res * 0.97f;
            return;
        }
        if (type == FilterType::Formant)
        {
            setupFormants (cutoffHz, res);
            return;
        }
        if (type == FilterType::Phaser)
        {
            // stages spread over up to 4 octaves around the cutoff
            for (int i = 0; i < kPhaserStages; ++i)
            {
                const float hz = std::clamp (cutoffHz * std::exp2 (morph * 4.0f * ((float) i / (kPhaserStages - 1) - 0.5f)), 15.0f,
                                             (float) (fs * 0.45));
                const float gi = std::tan (3.14159265f * hz / (float) fs);
                apG[i] = gi / (1.0f + gi);
            }
            phaserFb = res * 0.9f;
            return;
        }

        g = std::tan (3.14159265f * cutoffHz / (float) fs);
        if (type == FilterType::Ladder24)
        {
            G = g / (1.0f + g);
            G4 = G * G * G * G;
            // 3.9 x res as before up to res 0.9, then up to 4.15 at full resonance: the zero-delay loop
            // self-oscillates from k = 4 at every cutoff (the old one-sample-late loop did above ~200 Hz
            // from k ~3.5, its delay adding loop gain), so full resonance still sings.
            ladderK = res * 3.9f + std::max (0.0f, res - 0.9f) * 2.5f;
            return;
        }
        k = 2.0f - 1.98f * res;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        const float k2 = 1.41421356f;
        b1 = 1.0f / (1.0f + g * (g + k2));
        b2 = g * b1;
        b3 = g * b2;
    }

    // One sample of both channels (the type's kernel, inlined here).
    void process (float& l, float& r)
    {
        if (useDrive)
            processPair<true> (l, r);
        else
            processPair<false> (l, r);
    }

    // A run of samples moved through the filter and mixed with what went in:
    // x += mix * (filtered - x), per channel, in place. The same numbers as process() per sample, from
    // a loop made for the type and drive (chosen in setup): no branching on the type per sample.
    void processMix (float* l, float* r, int n, float mix) { (this->*mixKernel) (l, r, n, mix); }

private:
    template <FilterType T, bool Drive>
    void pair (float& l, float& r)
    {
        l = processChannel<T, Drive> (0, l);
        r = processChannel<T, Drive> (1, r);
    }

    template <bool Drive>
    void processPair (float& l, float& r)
    {
        switch (type)
        {
            case FilterType::Low12: pair<FilterType::Low12, Drive> (l, r); break;
            case FilterType::Low24: pair<FilterType::Low24, Drive> (l, r); break;
            case FilterType::High12: pair<FilterType::High12, Drive> (l, r); break;
            case FilterType::High24: pair<FilterType::High24, Drive> (l, r); break;
            case FilterType::Band12: pair<FilterType::Band12, Drive> (l, r); break;
            case FilterType::Band24: pair<FilterType::Band24, Drive> (l, r); break;
            case FilterType::Notch12: pair<FilterType::Notch12, Drive> (l, r); break;
            case FilterType::Notch24: pair<FilterType::Notch24, Drive> (l, r); break;
            case FilterType::Ladder24: pair<FilterType::Ladder24, Drive> (l, r); break;
            case FilterType::CombPlus: pair<FilterType::CombPlus, Drive> (l, r); break;
            case FilterType::CombMinus: pair<FilterType::CombMinus, Drive> (l, r); break;
            case FilterType::Morph12: pair<FilterType::Morph12, Drive> (l, r); break;
            case FilterType::Morph24: pair<FilterType::Morph24, Drive> (l, r); break;
            case FilterType::Formant: pair<FilterType::Formant, Drive> (l, r); break;
            case FilterType::Phaser: pair<FilterType::Phaser, Drive> (l, r); break;
            case FilterType::Flanger: pair<FilterType::Flanger, Drive> (l, r); break;
            case FilterType::Diode24: pair<FilterType::Diode24, Drive> (l, r); break;
            case FilterType::Ota24: pair<FilterType::Ota24, Drive> (l, r); break;
            case FilterType::SkLow12: pair<FilterType::SkLow12, Drive> (l, r); break;
            case FilterType::SkHigh12: pair<FilterType::SkHigh12, Drive> (l, r); break;
            case FilterType::BandReject: pair<FilterType::BandReject, Drive> (l, r); break;
            case FilterType::LowShelf: pair<FilterType::LowShelf, Drive> (l, r); break;
            case FilterType::HighShelf: pair<FilterType::HighShelf, Drive> (l, r); break;
            case FilterType::DistComb: pair<FilterType::DistComb, Drive> (l, r); break;
            case FilterType::Diffuser: pair<FilterType::Diffuser, Drive> (l, r); break;
            case FilterType::Downsample: pair<FilterType::Downsample, Drive> (l, r); break;
            default:
                if ((int) type >= vfl::kFirst && (int) type < kNumFilterTypes)
                {
                    static constexpr auto pairs = libPairs<Drive> (std::make_integer_sequence<int, vfl::kCount> {});
                    (this->*pairs[(size_t) ((int) type - vfl::kFirst)]) (l, r);
                }
                else
                    pair<FilterType::Peak, Drive> (l, r); // Peak, and a number past the list
                break;
        }
    }

    using PairKernel = void (VoiceFilter::*) (float&, float&);
    template <bool Drive, int... I>
    static constexpr std::array<PairKernel, sizeof...(I)> libPairs (std::integer_sequence<int, I...>)
    {
        return { &VoiceFilter::pair<(FilterType) (vfl::kFirst + I), Drive>... };
    }

    // ---- the 0.59 types (bidoo-dsp voice filter families) ----
    template <typename Fn>
    void forEachLib (Fn&& fn)
    {
        std::apply ([&] (auto&... slot) { (fn (slot), ...); }, lib);
    }
    void prepareLib()
    {
        int need = 0;
        forEachLib ([&] (auto& slot)
                    {
                        using F = vfl::Family<std::remove_reference_t<decltype (slot)>::index>;
                        if constexpr (vfl::HasMemory<F>)
                            need = std::max (need, F::memoryNeeded (fs));
                    });
        for (auto& m : libMem)
            m.assign ((size_t) need, 0.0f);
        forEachLib ([&] (auto& slot)
                    {
                        using F = vfl::Family<std::remove_reference_t<decltype (slot)>::index>;
                        if constexpr (std::is_same_v<F, vfl::vf::SmallReverb>)
                            for (int ch = 0; ch < 2; ++ch)
                                F::attach (slot.s[ch], libMem[ch].data(), fs, ch == 1);
                        else if constexpr (vfl::HasMemory<F>)
                            for (int ch = 0; ch < 2; ++ch)
                                F::attach (slot.s[ch], libMem[ch].data(), fs);
                    });
        libType = -1;
    }
    // A type's states back to silence (with its memory: the families sharing libMem clear it).
    void resetLib (FilterType t)
    {
        const int i = (int) t - vfl::kFirst;
        if (i < 0 || i >= vfl::kCount)
            return;
        forEachLib ([&] (auto& slot)
                    {
                        if (std::remove_reference_t<decltype (slot)>::index == i)
                            for (auto& st : slot.s)
                                st.reset();
                    });
    }
    void setupLib (float cutoffHz, float res, float driveAmount)
    {
        if ((int) type != libType) // a new family starts from silence, not from another's memory
        {
            libType = (int) type;
            resetLib (type);
        }
        const bidoo::dsp::voicefilters::Settings st { fs, cutoffHz, res, morph, driveAmount };
        const int i = (int) type - vfl::kFirst;
        forEachLib ([&] (auto& slot)
                    {
                        constexpr int I = std::remove_reference_t<decltype (slot)>::index;
                        if (I == i)
                            vfl::Family<I>::design (slot.c, st);
                    });
    }

    template <FilterType T, bool Drive>
    void mixRun (float* l, float* r, int n, float mix)
    {
        for (int i = 0; i < n; ++i)
        {
            const float yl = processChannel<T, Drive> (0, l[i]);
            const float yr = processChannel<T, Drive> (1, r[i]);
            l[i] += mix * (yl - l[i]);
            r[i] += mix * (yr - r[i]);
        }
    }

    using MixKernel = void (VoiceFilter::*) (float*, float*, int, float);
    static constexpr int kNumTypes = kNumFilterTypes;
    template <int... T>
    static constexpr std::array<MixKernel, 2 * sizeof...(T)> mixTable (std::integer_sequence<int, T...>)
    {
        return { &VoiceFilter::mixRun<(FilterType) T, false>..., &VoiceFilter::mixRun<(FilterType) T, true>... };
    }
    void chooseKernels()
    {
        static constexpr auto mixes = mixTable (std::make_integer_sequence<int, kNumTypes> {});
        // a number past the list plays as Peak (the old switch's default)
        const int t = (int) type >= 0 && (int) type < kNumTypes ? (int) type : (int) FilterType::Peak;
        mixKernel = mixes[(size_t) (t + (useDrive ? kNumTypes : 0))];
    }

    // The comb's delayed sample, `delay` samples before the write position `pos` (2 <= delay <=
    // size - 3), by 4-point 3rd-order Hermite: a flatter top than a linear read, so high cutoffs
    // with feedback keep their brightness and the pitch sweeps without the linear read's dull,
    // fraction-dependent low-pass. At delay 2 the fraction is 0 and the slot about to be written
    // (the oldest) gets no weight.
    static float combRead (const std::vector<float>& buf, int pos, float delay)
    {
        const int n = (int) buf.size();
        float readPos = (float) pos - delay;
        if (readPos < 0.0f)
            readPos += (float) n;
        int i0 = (int) readPos;
        const float t = readPos - (float) i0;
        if (i0 >= n)
            i0 -= n;
        const int im1 = i0 > 0 ? i0 - 1 : n - 1;
        const int i1 = i0 + 1 < n ? i0 + 1 : i0 + 1 - n;
        const int i2 = i0 + 2 < n ? i0 + 2 : i0 + 2 - n;
        const float xm1 = buf[(size_t) im1], x0 = buf[(size_t) i0], x1 = buf[(size_t) i1], x2 = buf[(size_t) i2];
        const float c = 0.5f * (x1 - xm1);
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + 0.5f * (x2 - x0);
        const float b = w + a;
        return ((a * t - b) * t + c) * t + x0;
    }

    template <FilterType T, bool Drive>
    float processChannel (int ch, float x)
    {
        if constexpr (Drive && T != FilterType::DistComb) // tanh with first-order ADAA (half a sample late, far less aliasing)
            x = (float) bidoo::dsp::adaa<1> (driveState[ch], (double) (x * drive), bidoo::dsp::AdaaTanhTable<64> {}) * driveOut;

        if constexpr ((int) T >= vfl::kFirst)
        {
            auto& slot = std::get<(int) T - vfl::kFirst> (lib);
            return vfl::Family<(int) T - vfl::kFirst>::process (slot.s[ch], slot.c, x);
        }
        else if constexpr (T >= FilterType::Diode24)
            return processAdded<T> (ch, x);
        else if constexpr (T == FilterType::Formant)
        {
            float y = 0.0f;
            for (int b = 0; b < 3; ++b)
            {
                auto& st = formant[ch][b];
                const auto& c = fc[b];
                y += bidoo::dsp::tptSvfStep (st[0], st[1], x, c.a1, c.a2, c.a3, c.k).bp * c.k * c.gain;
            }
            return y;
        }
        else if constexpr (T == FilterType::Phaser)
        {
            float u = x + phaserFb * phaserLast[ch];
            for (int i = 0; i < kPhaserStages; ++i)
                u = allpass[ch][i].allPass (u, apG[i]);
            phaserLast[ch] = std::tanh (u); // soft-limit the feedback
            return 0.5f * (x + u);
        }
        else if constexpr (T == FilterType::Flanger)
        {
            auto& buf = comb[ch];
            const int n = (int) buf.size();
            const float d = combRead (buf, combPos, combDelay);
            buf[(size_t) combPos] = x + combFb * d;
            if (ch == 1)
                combPos = (combPos + 1) % n;
            return 0.5f * (x + d) * (1.0f - std::abs (combFb) * 0.4f);
        }
        else if constexpr (T == FilterType::Ladder24)
        {
            // Zero-delay feedback: each TPT stage is y = G x + (1 - G) s, so the fourth stage's output is
            // y4 = G^4 u + S (S from the states), and the loop's input v = x - k y4 with u = tanh (v)
            // is implicit. tanh is replaced by its secant through the origin at the previous sample's
            // v (u / v, in (0, 1]), which makes the loop linear for this sample and solvable at once:
            // v = (x - k S) / (1 + k G^4 t). One tanh a sample, unconditionally stable (the
            // denominator is >= 1), and the cutoff and resonance peak sit where g = tan (pi fc / fs)
            // puts them (the bilinear transform's prewarping), up to near Nyquist.
            auto& s = ladder[ch];
            const float h = 1.0f - G;
            const float S = h * (((s[0].s * G + s[1].s) * G + s[2].s) * G + s[3].s);
            const float v = (x - ladderK * S) / (1.0f + ladderK * G4 * ladderSlope[ch]);
            float u = std::tanh (v);
            ladderSlope[ch] = std::abs (v) > 1.0e-4f ? u / v : 1.0f;
            for (int i = 0; i < 4; ++i)
                u = s[i].lowPass (u, G);
            return u * (1.0f + ladderK * 0.5f);
        }
        else if constexpr (T == FilterType::CombPlus || T == FilterType::CombMinus)
        {
            auto& buf = comb[ch];
            const int n = (int) buf.size();
            const float d = combRead (buf, combPos, combDelay);
            const float y = x + combFb * d;
            buf[(size_t) combPos] = y;
            if (ch == 1)
                combPos = (combPos + 1) % n;
            return y * (1.0f - std::abs (combFb) * 0.5f);
        }
        else
        {
            // first stage, with resonance
            auto& s = svf[ch][0];
            const auto o = bidoo::dsp::tptSvfStep (s[0], s[1], x, a1, a2, a3, k);
            const float low = o.lp, band = o.bp, high = o.hp;

            float y;
            if constexpr (T == FilterType::Morph12 || T == FilterType::Morph24)
                y = morphed (low, band * k, high);
            else if constexpr (T == FilterType::Low12 || T == FilterType::Low24)
                y = low;
            else if constexpr (T == FilterType::High12 || T == FilterType::High24)
                y = high;
            else if constexpr (T == FilterType::Band12 || T == FilterType::Band24)
                y = band * k;
            else if constexpr (T == FilterType::Notch12 || T == FilterType::Notch24)
                y = low + high;
            else
                y = low - high; // peak

            constexpr bool twoStage = T == FilterType::Low24 || T == FilterType::High24 || T == FilterType::Band24
                                      || T == FilterType::Notch24 || T == FilterType::Morph24;
            if constexpr (! twoStage)
                return y;
            else
            {
                auto& t = svf[ch][1];
                const auto o2 = bidoo::dsp::tptSvfStep (t[0], t[1], y, b1, b2, b3, 1.41421356f);
                if constexpr (T == FilterType::Morph24)
                    return morphed (o2.lp, o2.bp * 1.41421356f, o2.hp);
                else if constexpr (T == FilterType::Low24)
                    return o2.lp;
                else if constexpr (T == FilterType::High24)
                    return o2.hp;
                else if constexpr (T == FilterType::Band24)
                    return o2.bp * 1.41421356f;
                else
                    return y - 1.41421356f * o2.bp; // notch: low + high of the second stage
            }
        }
    }

    // ---- the 0.56 types ----

    // The secant of a saturator through the origin, u / v (in (0, 1]), for the next sample's solve.
    static float secant (float u, float v) { return std::abs (v) > 1.0e-4f ? u / v : 1.0f; }

    void resetAdded()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < 4; ++i)
            {
                diodeS[ch][i] = otaS[ch][i] = 0.0f;
                otaT[ch][i] = 1.0f;
                for (auto& s : rejectS[ch][i])
                    s = 0.0f;
            }
            diodeT[ch] = skT[ch] = otaFbT[ch] = 1.0f;
            diodeLp[ch].reset();
            skS[ch][0] = skS[ch][1] = 0.0f;
            dampLp[ch].reset();
            dampHp[ch].reset();
            for (auto& z : diffZ[ch])
                z = 0.0f;
            loopState[ch].reset();
            held[ch] = prevHeld[ch] = lastIn[ch] = outPrev[ch] = diffLast[ch] = 0.0f;
        }
        for (auto& c : diffPos)
            for (auto& p : c)
                p = 0;
        dsPhase = dsDelta = 0.0f;
        dsStep = false;
    }

    void setupAdded (float cutoffHz, float res, float driveAmount)
    {
        constexpr float pi = 3.14159265f;
        const float tw = std::tan (pi * cutoffHz / (float) fs);
        switch (type)
        {
            case FilterType::Diode24:
            {
                // Each stage also pulled by the next one at half strength (the diode ladder's coupling):
                // (I - gA) y = s + g e1 u, A tridiagonal (-1.5 0.5 / 1 -1.5 0.5 / 1 -1.5 0.5 / 1 -1),
                // solved by Thomas' algorithm. Its 180-degree point is at 1.05529 x the stage corner and
                // needs a loop gain of 8.61777 to oscillate: g puts that point on the cutoff
                // (prewarped), k is scaled to it.
                const float gd = tw / 1.05529f;
                const float b[4] { 1.0f + 1.5f * gd, 1.0f + 1.5f * gd, 1.0f + 1.5f * gd, 1.0f + gd };
                diodeDen[0] = 1.0f / b[0];
                diodeGe[0] = gd * diodeDen[0];
                for (int i = 1; i < 4; ++i)
                {
                    diodeDen[i] = 1.0f / (b[i] - 0.5f * gd * diodeGe[i - 1]);
                    diodeGe[i] = gd * diodeDen[i];
                }
                // the response of the states to the loop's input u: (I - gA)^-1 g e1
                float f[4];
                f[0] = gd * diodeDen[0];
                for (int i = 1; i < 4; ++i)
                    f[i] = diodeGe[i] * f[i - 1];
                diodeM[3] = f[3];
                for (int i = 2; i >= 0; --i)
                    diodeM[i] = f[i] + 0.5f * diodeGe[i] * diodeM[i + 1];
                diodeK = 8.61777f * (0.975f * res + 1.25f * std::max (0.0f, res - 0.9f));
                diodeBass = morph;
                diodeG1 = tw / (1.0f + tw);
                diodeOut = std::sqrt (1.0f + diodeK * (1.0f - morph)) * (1.0f + 0.1f * diodeK); // half the lost bass back in level
                return;
            }
            case FilterType::Ota24:
                otaG = tw;
                otaK = res * 3.9f + std::max (0.0f, res - 0.9f) * 2.5f;
                otaSat = 0.3f * (1.0f + 3.0f * driveAmount); // the stages' tanh: gentle, then rounder with Drive
                otaOut = 1.0f + otaK * 0.5f;
                return;
            case FilterType::SkLow12:
            case FilterType::SkHigh12:
                skG = tw;
                // 2 is the edge of self-oscillation (Q = 1 / (2 - K)); the clipper holds it from there
                skK = 2.0f * (0.975f * res + 0.5f * std::max (0.0f, res - 0.9f));
                skBeta = (0.5f + 4.5f * morph) * (1.0f + driveAmount);
                skOut = std::sqrt (2.0f * skBeta); // the clipper's level made up, so Bite changes the colour more than the level
                return;
            case FilterType::BandReject:
            {
                const float w = 0.5f + 5.5f * morph; // octaves
                const float lo = std::clamp (cutoffHz * std::exp2 (-0.5f * w), 15.0f, (float) (fs * 0.45));
                const float hi = std::clamp (cutoffHz * std::exp2 (0.5f * w), 15.0f, (float) (fs * 0.45));
                const float gl = std::tan (pi * lo / (float) fs), gh = std::tan (pi * hi / (float) fs);
                const float kr = 1.41421356f * (1.0f - 0.93f * res); // resonance on the first stage of each side
                rejectC[0] = Coeffs::make (gl, kr);
                rejectC[1] = Coeffs::make (gl, 1.41421356f);
                rejectC[2] = Coeffs::make (gh, kr);
                rejectC[3] = Coeffs::make (gh, 1.41421356f);
                return;
            }
            case FilterType::LowShelf:
            case FilterType::HighShelf:
            {
                // Simper's SVF shelves: A = 10^(dB / 40), the corner g moved by sqrt (A)
                const float A = std::pow (10.0f, (morph - 0.5f) * 48.0f / 40.0f);
                const float sa = std::sqrt (A);
                k = 1.41421356f - 1.2f * res;
                g = type == FilterType::LowShelf ? tw / sa : tw * sa;
                a1 = 1.0f / (1.0f + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
                if (type == FilterType::LowShelf)
                {
                    shelfM0 = 1.0f;
                    shelfM1 = k * (A - 1.0f);
                    shelfM2 = A * A - 1.0f;
                }
                else
                {
                    shelfM0 = A * A;
                    shelfM1 = k * (1.0f - A) * A;
                    shelfM2 = 1.0f - A * A;
                }
                return;
            }
            case FilterType::DistComb:
            {
                // The loop: delay, damping low-pass (from 0.45 fs at Morph 0 down to 1.5 x the cutoff
                // at 1, exponentially), a high-pass three octaves under the cutoff (no DC builds up in
                // the loop), tanh with ADAA. The filters' phase delay at the cutoff and ADAA's half
                // sample come off the delay, and the feedback is divided by their largest gain over the
                // first three harmonics, so the comb stays on the cutoff, Res sets the decay whatever
                // the damping, and no overtone outgrows the fundamental.
                const float top = (float) (fs * 0.45);
                const float fl = std::min (top * std::pow (std::min (1.5f * cutoffHz, top) / top, morph), top);
                const float gl = std::tan (pi * fl / (float) fs), gh = std::tan (pi * 0.125f * cutoffHz / (float) fs);
                dampG = gl / (1.0f + gl);
                dampHpG = gh / (1.0f + gh);
                const float w = 2.0f * pi * cutoffHz / (float) fs;
                auto loopAt = [&] (float ww)
                {
                    const std::complex<float> z1 = std::polar (1.0f, -ww);
                    auto onePoleLow = [&] (float gg) { return gg * (1.0f + z1) / ((1.0f + gg) - (1.0f - gg) * z1); };
                    return onePoleLow (gl) * (1.0f - onePoleLow (gh));
                };
                const auto loop = loopAt (w);
                const float most = std::max (std::abs (loop), std::max (std::abs (loopAt (std::min (2.0f * w, pi))), std::abs (loopAt (std::min (3.0f * w, pi)))));
                const float lag = -std::arg (loop) / w; // samples
                combDelay = std::clamp ((float) (fs / cutoffHz) - lag - 0.5f, 2.0f, (float) comb[0].size() - 3.0f);
                const float fb = 0.99f * res + 0.5f * std::max (0.0f, res - 0.9f); // past 1 near the top: it sings
                combFb = fb / std::max (most, 0.1f);
                loopDrive = 1.0f + 8.0f * driveAmount;
                loopMakeup = std::sqrt (loopDrive);
                distOut = 1.0f - 0.5f * std::min (fb, 1.0f);
                return;
            }
            case FilterType::Diffuser:
            {
                const float period = (float) fs / cutoffHz;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < kDiffStages; ++i)
                    {
                        // N whole samples, then a first-order all-pass for the fraction in [0.5, 1.5):
                        // flat in level, unlike an interpolated read (eight stages of those dull the top)
                        const float d = std::clamp (period * (float) kDiffWeights[ch][i], 1.5f, (float) diffLen[ch][i] - 3.0f);
                        const int whole = (int) (d - 0.5f);
                        const float frac = d - (float) whole;
                        diffN[ch][i] = whole;
                        diffEta[ch][i] = (1.0f - frac) / (1.0f + frac);
                    }
                diffG = 0.2f + 0.55f * morph;
                diffFb = 0.92f * res;
                return;
            }
            default: // Downsample
                dsInc = std::min (2.0f * cutoffHz / (float) fs, 0.95f);
                dsFb = 1.05f * res;
                dsGain = 1.0f + 0.5f * dsFb; // half the level the negative feedback takes from the bass
                return;
        }
    }

    template <FilterType T>
    float processAdded (int ch, float x)
    {
        if constexpr (T == FilterType::Diode24)
        {
            // the states' part (I - gA)^-1 s by the tridiagonal solve, then the loop as the ladder's
            auto& s = diodeS[ch];
            const float f0 = s[0] * diodeDen[0];
            const float f1 = s[1] * diodeDen[1] + diodeGe[1] * f0;
            const float f2 = s[2] * diodeDen[2] + diodeGe[2] * f1;
            const float z3 = s[3] * diodeDen[3] + diodeGe[3] * f2;
            const float z2 = f2 + 0.5f * diodeGe[2] * z3;
            const float z1 = f1 + 0.5f * diodeGe[1] * z2;
            const float z0 = f0 + 0.5f * diodeGe[0] * z1;
            const float xl = diodeLp[ch].lowPass (x, diodeG1);
            const float v = (x - diodeK * (z3 - diodeBass * xl)) / (1.0f + diodeK * diodeM[3] * diodeT[ch]);
            const float u = bidoo::dsp::tanhPade (v);
            diodeT[ch] = secant (u, v);
            const float y0 = z0 + diodeM[0] * u, y1 = z1 + diodeM[1] * u, y2 = z2 + diodeM[2] * u, y3 = z3 + diodeM[3] * u;
            s[0] = 2.0f * y0 - s[0];
            s[1] = 2.0f * y1 - s[1];
            s[2] = 2.0f * y2 - s[2];
            s[3] = 2.0f * y3 - s[3];
            return y3 * diodeOut;
        }
        else if constexpr (T == FilterType::Ota24)
        {
            // each stage dy = g tanh (a (in - y)) / a, its tanh replaced by the last sample's secant t:
            // y = G in + (1 - G) s with G = g t / (1 + g t); the loop then solved as the ladder's
            auto& s = otaS[ch];
            auto& t = otaT[ch];
            float Gs[4];
            for (int i = 0; i < 4; ++i)
            {
                const float gt = otaG * t[i];
                Gs[i] = gt / (1.0f + gt);
            }
            // the feedback through its own soft clip (secant too): it holds the self-oscillation at a
            // level where the stages stay nearly linear, so it stays in tune
            const float kf = otaK * otaFbT[ch];
            const float S = (((1.0f - Gs[0]) * s[0] * Gs[1] + (1.0f - Gs[1]) * s[1]) * Gs[2] + (1.0f - Gs[2]) * s[2]) * Gs[3] + (1.0f - Gs[3]) * s[3];
            float in = (x - kf * S) / (1.0f + kf * Gs[0] * Gs[1] * Gs[2] * Gs[3]);
            float y[4];
            for (int i = 0; i < 4; ++i)
            {
                y[i] = Gs[i] * in + (1.0f - Gs[i]) * s[i];
                const float d = otaSat * (in - y[i]);
                t[i] = secant (bidoo::dsp::tanhPade (d), d);
                s[i] = 2.0f * y[i] - s[i];
                in = y[i];
            }
            const float c = 3.0f * y[3];
            otaFbT[ch] = secant (bidoo::dsp::tanhPade (c), c);
            // Poles: 4 (0), 3 (1/3), 2 (2/3), 1 (1); each pole fewer 1.5 dB down, so the resonance
            // (3 dB more a pole fewer) grows a little and the pass band drops a little
            const float p = morph * 3.0f;
            const int i0 = std::min (2, (int) p);
            const float a = y[3 - i0] * kTapGain[i0], b = y[2 - i0] * kTapGain[i0 + 1];
            return (a + (p - (float) i0) * (b - a)) * otaOut;
        }
        else if constexpr (T == FilterType::SkLow12 || T == FilterType::SkHigh12)
        {
            // y1 = s1 + g (x - y1 + K t b), y2 = s2 + g b, b = y1 - y2 (the band-pass): b at once
            auto& s = skS[ch];
            const float gk = skG;
            const float fb = skK * skT[ch];
            const float b = (s[0] - (1.0f + gk) * s[1] + gk * x) / ((1.0f + gk) * (1.0f + gk) - gk * fb);
            const float y2 = s[1] + gk * b;
            const float y1 = b + y2;
            s[0] = 2.0f * y1 - s[0];
            s[1] = 2.0f * y2 - s[1];
            const float c = skBeta * b;
            skT[ch] = secant (bidoo::dsp::tanhPade (c), c);
            if constexpr (T == FilterType::SkLow12)
                return y2 * skOut;
            else
                return (x - y2 - 2.0f * b + fb * b) * skOut; // the band-pass's derivative: s^2 / (s^2 + (2 - K) s + 1)
        }
        else if constexpr (T == FilterType::BandReject)
        {
            auto& st = rejectS[ch];
            const auto& c = rejectC;
            const float lo1 = bidoo::dsp::tptSvfStep (st[0][0], st[0][1], x, c[0].a1, c[0].a2, c[0].a3, c[0].k).lp;
            const float lo = bidoo::dsp::tptSvfStep (st[1][0], st[1][1], lo1, c[1].a1, c[1].a2, c[1].a3, c[1].k).lp;
            const float hi1 = bidoo::dsp::tptSvfStep (st[2][0], st[2][1], x, c[2].a1, c[2].a2, c[2].a3, c[2].k).hp;
            const float hi = bidoo::dsp::tptSvfStep (st[3][0], st[3][1], hi1, c[3].a1, c[3].a2, c[3].a3, c[3].k).hp;
            return lo + hi;
        }
        else if constexpr (T == FilterType::LowShelf || T == FilterType::HighShelf)
        {
            auto& s = svf[ch][0];
            const auto o = bidoo::dsp::tptSvfStep (s[0], s[1], x, a1, a2, a3, k);
            return shelfM0 * x + shelfM1 * o.bp + shelfM2 * o.lp;
        }
        else if constexpr (T == FilterType::DistComb)
        {
            auto& buf = comb[ch];
            const int n = (int) buf.size();
            const float d = dampHp[ch].highPass (dampLp[ch].lowPass (combRead (buf, combPos, combDelay), dampG), dampHpG);
            const float sat = (float) bidoo::dsp::adaa<1> (loopState[ch], (double) (d * loopDrive), bidoo::dsp::AdaaTanhTable<64> {}) / loopDrive;
            const float y = x + combFb * sat;
            buf[(size_t) combPos] = y;
            if (ch == 1)
                combPos = (combPos + 1) % n;
            // the loop's tanh holds the echoes under 1 / Drive: half of that (in dB) made up on the way out
            return (x + combFb * sat * loopMakeup) * distOut;
        }
        else if constexpr (T == FilterType::Diffuser)
        {
            float* base = comb[ch].data();
            const float dg = diffG;
            float u = x + diffFb * bidoo::dsp::tanhPade (diffLast[ch]); // soft-limited: a moving cutoff is not an all-pass
            for (int i = 0; i < kDiffStages; ++i)
            {
                float* ring = base + diffStart[ch][i];
                const int n = diffLen[ch][i];
                int& p = diffPos[ch][i];
                const int r0 = p - diffN[ch][i] < 0 ? p - diffN[ch][i] + n : p - diffN[ch][i];
                const int r1 = r0 > 0 ? r0 - 1 : n - 1;
                const float eta = diffEta[ch][i];
                float& z = diffZ[ch][i];
                const float dl = eta * (ring[r0] - z) + ring[r1];
                z = dl;
                const float w = u + dg * dl;
                ring[p] = w;
                p = p + 1 < n ? p + 1 : 0;
                u = dl - dg * w;
            }
            diffLast[ch] = u;
            return u;
        }
        else // Downsample
        {
            if (ch == 0)
            {
                dsPhase += dsInc;
                dsStep = dsPhase >= 1.0f;
                if (dsStep)
                {
                    dsPhase -= 1.0f;
                    dsDelta = std::min (dsPhase / dsInc, 0.999f); // samples since the hold point
                }
            }
            const float m = morph;
            float corr = 0.0f;
            if (dsStep)
            {
                const float xs = x - dsDelta * (x - lastIn[ch]); // the input at the hold point
                const float h = xs - dsFb * bidoo::dsp::tanhPade (held[ch]);
                const float jump = (1.0f - m) * (h - held[ch]);
                prevHeld[ch] = held[ch];
                held[ch] = h;
                // 2-point polyBLEP around the step: one sample late, so the sample before gets its half
                outPrev[ch] += 0.5f * dsGain * jump * dsDelta * dsDelta;
                corr = -0.5f * dsGain * jump * (1.0f - dsDelta) * (1.0f - dsDelta);
            }
            lastIn[ch] = x;
            const float ramp = prevHeld[ch] + (held[ch] - prevHeld[ch]) * dsPhase;
            const float now = ((1.0f - m) * held[ch] + m * ramp) * dsGain + corr;
            const float out = outPrev[ch];
            outPrev[ch] = now;
            return out;
        }
    }

    // Morph: low (0), band (1/3), high (2/3), notch (1), blended in between.
    float morphed (float low, float band, float high) const
    {
        const float m = morph * 3.0f;
        if (m < 1.0f)
            return low + m * (band - low);
        if (m < 2.0f)
            return band + (m - 1.0f) * (high - band);
        return high + (m - 2.0f) * low; // high -> high + low (notch)
    }

    void setupFormants (float cutoffHz, float res)
    {
        // formant frequencies (Hz) and levels (dB) of A, E, I, O, U (a male voice)
        static constexpr float kVowels[5][3][2] {
            { { 800.0f, 0.0f }, { 1150.0f, -6.0f }, { 2900.0f, -32.0f } },
            { { 350.0f, 0.0f }, { 2000.0f, -20.0f }, { 2800.0f, -15.0f } },
            { { 270.0f, 0.0f }, { 2140.0f, -12.0f }, { 2950.0f, -26.0f } },
            { { 450.0f, 0.0f }, { 800.0f, -11.0f }, { 2830.0f, -22.0f } },
            { { 325.0f, 0.0f }, { 700.0f, -16.0f }, { 2700.0f, -35.0f } },
        };
        const float pos = morph * 4.0f;
        const int v0 = std::min (3, (int) pos);
        const float t = pos - (float) v0;
        const float shift = std::clamp (cutoffHz / 1000.0f, 0.25f, 4.0f);
        const float q = 4.0f + res * 16.0f;
        for (int b = 0; b < 3; ++b)
        {
            const float hz = std::exp (std::log (kVowels[v0][b][0]) * (1.0f - t) + std::log (kVowels[v0 + 1][b][0]) * t) * shift;
            const float db = kVowels[v0][b][1] * (1.0f - t) + kVowels[v0 + 1][b][1] * t;
            auto& c = fc[b];
            const float gb = std::tan (3.14159265f * std::clamp (hz, 30.0f, (float) (fs * 0.45)) / (float) fs);
            c.k = 1.0f / q;
            c.a1 = 1.0f / (1.0f + gb * (gb + c.k));
            c.a2 = gb * c.a1;
            c.a3 = gb * c.a2;
            c.gain = std::pow (10.0f, db / 20.0f) * 2.0f;
        }
    }

    static constexpr int kPhaserStages = 6;
    struct Band
    {
        float k = 0.25f, a1 = 0, a2 = 0, a3 = 0, gain = 1.0f;
    };
    Band fc[3];
    float formant[2][3][2] {};
    float apG[kPhaserStages] {};
    bidoo::dsp::TptOnePole<float> allpass[2][kPhaserStages];
    float phaserLast[2] {}, phaserFb = 0.0f;
    float morph = 0.0f;

    double fs = 48000.0;
    FilterType type = FilterType::Low24;
    float g = 0.1f, k = 1.0f, a1 = 0, a2 = 0, a3 = 0, b1 = 0, b2 = 0, b3 = 0;
    float G = 0.1f, G4 = 1.0e-4f, ladderK = 0.0f;
    float drive = 1.0f, driveOut = 1.0f;
    bool useDrive = false;
    MixKernel mixKernel = &VoiceFilter::mixRun<FilterType::Low24, false>;
    float svf[2][2][2] {};
    bidoo::dsp::TptOnePole<float> ladder[2][4];
    float ladderSlope[2] { 1.0f, 1.0f }; // the loop tanh's secant u / v at the last sample
    bidoo::dsp::AdaaState driveState[2];
    std::vector<float> comb[2];
    int combPos = 0;
    float combDelay = 100.0f, combFb = 0.0f;

    // the 0.56 types
    using Coeffs = bidoo::dsp::TptSvfCoeffs<float>;
    float diodeS[2][4] {}, diodeT[2] { 1.0f, 1.0f }, diodeDen[4] {}, diodeGe[4] {}, diodeM[4] {};
    float diodeK = 0.0f, diodeBass = 0.0f, diodeG1 = 0.1f, diodeOut = 1.0f;
    bidoo::dsp::TptOnePole<float> diodeLp[2];
    static constexpr float kTapGain[4] { 1.0f, 0.841f, 0.708f, 0.596f };
    float otaS[2][4] {}, otaT[2][4] {}, otaG = 0.1f, otaK = 0.0f, otaSat = 1.0f, otaOut = 1.0f;
    float skS[2][2] {}, skT[2] { 1.0f, 1.0f }, skG = 0.1f, skK = 0.0f, skBeta = 1.0f, skOut = 1.0f;
    float otaFbT[2] { 1.0f, 1.0f };
    float rejectS[2][4][2] {};
    Coeffs rejectC[4];
    float shelfM0 = 1.0f, shelfM1 = 0.0f, shelfM2 = 0.0f;
    bidoo::dsp::TptOnePole<float> dampLp[2], dampHp[2];
    bidoo::dsp::AdaaState loopState[2];
    float dampG = 0.5f, dampHpG = 0.01f, loopDrive = 1.0f, loopMakeup = 1.0f, distOut = 1.0f;
    static constexpr int kDiffStages = 8;
    // the Diffuser's stage lengths, as fractions of the comb's memory and of one period of the cutoff,
    // a little different on each side so the two decorrelate
    static constexpr double kDiffWeights[2][kDiffStages] { { 0.1266, 0.0813, 0.1731, 0.0690, 0.1477, 0.0936, 0.1988, 0.1099 },
                                                           { 0.1180, 0.0871, 0.1650, 0.0745, 0.1530, 0.0890, 0.2060, 0.1074 } };
    int diffStart[2][kDiffStages] {}, diffLen[2][kDiffStages] {}, diffPos[2][kDiffStages] {};
    int diffN[2][kDiffStages] {};
    float diffEta[2][kDiffStages] {}, diffZ[2][kDiffStages] {}, diffG = 0.5f, diffFb = 0.0f, diffLast[2] {};
    float held[2] {}, prevHeld[2] {}, lastIn[2] {}, outPrev[2] {};
    float dsPhase = 0.0f, dsDelta = 0.0f, dsInc = 0.1f, dsFb = 0.0f, dsGain = 1.0f;
    bool dsStep = false;

    // the 0.59 types
    vfl::Slots lib;
    std::vector<float> libMem[2];
    int libType = -1;
};
} // namespace potion

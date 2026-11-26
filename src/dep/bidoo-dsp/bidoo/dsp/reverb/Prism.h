#pragma once

// PrismReverb: eight reverb algorithms with an optional shimmer, from fadabox's Prism. HALL, ROOM,
// CHAMBER, PLATE and CLOUD run on an eight-line feedback delay network (Hadamard mixing, modulated
// Hermite-interpolated reads, an allpass and frequency-dependent damping in every line); SPRING runs
// two dispersive spring tanks (chains of allpasses in a delay loop, the chirp of a spring); NON-LIN
// and REVERSE are dense tapped bursts with a gated or rising envelope. The shimmer is a pitch
// shifter in the feedback path (since 2026-11-26 two phase vocoders, PhaseVocoderShifter, around the whole
// reverb; before, a delay-line shifter that splices at the best correlation point, also inside three lines).
// The 2026-11-26 voicing (repos/reverb-bench, measured against Valhalla): a modulation floor (no metallic ROOM
// and PLATE, no still tail at MOD 0), dense mode no longer doubling the modulation, darker early reflections
// (DAMP), body under 300 Hz where a snare's came back thin, and the shimmer's octaves fading pass after pass.
// Moved from fadabox unchanged (bit-identical); header-only. It is a second, lighter network next to
// the Grotte core (Fdn16 / FdnReverb): 8 lines with one-pole damping instead of 16 with fitted
// absorption, but with the plate, spring, burst and shimmer voicings. Wet output only.
// Since 2026-11-09 (audit Q3.10, Q3.11) a moved pre-delay crossfades instead of jumping, and the shimmer's
// splice search and the lines' DC guard keep their 48 kHz times at any rate: still settings at 48 kHz bit
// for bit as before, other rates not. Since the same day (from the Bidoo VCV copy, which had them first):
// configure() returns at once when the settings did not change, the per-block coefficients are worked out
// once in prepare(), and a change of algorithm fades the output out (10 ms), clears the buffers while
// silent and fades the new algorithm in (10 ms) instead of clearing them under a playing tail (after
// reset(), when there is no tail, it still switches at once). Settings held: bit for bit as before.
// And, as FdnReverb / FdnEcoReverb: a NaN or an infinity coming in is heard as silence, and an output that
// goes non-finite resets the reverb (that block silent); finite audio bit for bit as before.
// Dense mode (prepare (rate, true); Bidoo VCV REI, 2026-11-25): the FDN algorithms run 16 lines instead of 8
// (log spaced between 0.75 x the shortest and the longest of the eight; PLATE's 2.2 x longer), with deeper,
// slower modulation and 24 diffused early reflections instead of 8 discrete ones. Measured on impulse
// responses: the tail's colour (deviation of its spectrum from a smooth one) down from 1.2-3.3 to 1.1-1.5
// times that of noise, echo density reached in 20-115 ms instead of 20-165 ms, the same RT60 and energy
// (within 0.5 dB). About twice the CPU of the FDN algorithms. Off by default: FadaBox and Potion bit for bit.
// Stretch (Settings::stretch, for cabanon's Grotte, 2026-11-26; off by default, bit for bit): a decay time per frequency
// on the network algorithms, Grotte's Stretch EQ: every line gets absorption filters (AttenuationFit, Grotte's: a
// broadband gain, two shelves and eight octave bells, in double) fitted so the line loses what its own gain and damping
// lose divided by the factor asked at each frequency. One fit at the mean loop, scaled per line; worked out again only
// when the decay, the damping, the lengths or the curve change.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>
#include "../core/Random.h"
#include "../core/Trig.h" // sinF: per-sample sines, cheap on MinGW (VCV) too
#include "../shaping/TanhPade.h"
#include "../fft/PhaseVocoderShifter.h"
#include "AttenuationFit.h"

namespace bidoo::dsp::reverb
{
namespace prism_detail
{
constexpr float kPiF = 3.14159265358979f, kTwoPiF = 2.0f * kPiF;
inline float clampf (float x, float lo, float hi) { return std::min (hi, std::max (lo, x)); }

// tanh as a rational function: within 2e-4 of std::tanh and exactly +-1 from |x| = 4.97 on. The
// shared tanhPade, with the lower clamp first so a NaN reads -4.97 as this copy always did (bit for
// bit on every input)
inline float prismFastTanh (float x) { return tanhPade (std::max (-4.97f, x)); }

inline float prismSoftClip (float x)
{
    const float a = std::abs (x);
    if (a < 0.9f)
        return x;
    const float y = 0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f);
    return x < 0 ? -y : y;
}

// xorshift32, for the fixed random patterns of the burst algorithms
using PrismRng = XorShift32;

// in-place 8-point Hadamard transform, normalised (lossless)
template <size_t N>
inline void hadamard (std::array<float, N>& x)
{
    for (int h = 1; h < (int) N; h *= 2)
        for (int i = 0; i < (int) N; i += 2 * h)
            for (int j = i; j < i + h; ++j)
            {
                const float a = x[(size_t) j], b = x[(size_t) (j + h)];
                x[(size_t) j] = a + b;
                x[(size_t) (j + h)] = a - b;
            }
    constexpr float k = N == 8 ? 0.35355339f : 0.25f; // 1 / sqrt (N)
    for (auto& v : x)
        v *= k;
}

constexpr float kSignL[8] { 1, -1, 1, 1, -1, 1, -1, -1 };
constexpr float kSignR[8] { 1, 1, -1, -1, 1, 1, -1, -1 }; // orthogonal to kSignL: uncorrelated sides
constexpr float kErMs[8] { 7.1f, 11.3f, 17.9f, 23.3f, 31.7f, 41.9f, 53.3f, 67.1f };
constexpr float kErGain[8] { 0.85f, -0.7f, 0.62f, -0.55f, 0.46f, -0.38f, 0.3f, -0.24f };
// dense: the outputs' signs, two orthogonal Walsh rows of 16 (the parity of i & 5 and of i & 10)
constexpr float kSign16L[16] { 1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, 1 };
constexpr float kSign16R[16] { 1, 1, -1, -1, 1, 1, -1, -1, -1, -1, 1, 1, -1, -1, 1, 1 };
// dense: the eight lengths spread to sixteen (log spaced, a little jitter so no two share a ratio)
constexpr float kJitter16[16] { 0.0f, 0.021f, -0.017f, 0.013f, -0.023f, 0.011f, -0.009f, 0.019f, -0.014f, 0.024f, -0.02f, 0.008f, -0.012f, 0.016f, -0.006f, 0.0f };
} // namespace prism_detail

// PRISM REVERB: eight reverb algorithms with an optional shimmer (a pitch shifter in the feedback path).
// HALL, ROOM, CHAMBER, PLATE and CLOUD run on an eight-line feedback delay network (Hadamard mixing,
// modulated Hermite-interpolated reads, an allpass and frequency-dependent damping in every line);
// SPRING runs two dispersive spring tanks (chains of allpasses in a delay loop, the chirp of a
// spring); NON-LIN and REVERSE are dense tapped bursts with a gated or rising envelope.
class PrismReverb
{
public:
    enum Algo { Hall, Room, Chamber, Plate, Spring, NonLin, Reverse, Cloud, NumAlgos };
    static const std::vector<std::string>& algoNames();
    static const std::vector<std::string>& intervalNames(); // shimmer pitch
    static float intervalSemis (int index, int voice); // voice 0 or 1: the stacked intervals differ
    // Settings::stretchLog2's points: 20 Hz to 20 kHz, a third of an octave apart
    static constexpr int kStretchPoints = 31;
    static double stretchFreq (int k) { return 20.0 * std::pow (1000.0, (double) k / (double) (kStretchPoints - 1)); }

    struct Settings
    {
        int algo = Hall;
        float preMs = 10;
        float decay = 0.5f;   // 0..1: decay time (gate length for NON-LIN / REVERSE); the top = FREEZE
        float damp = 0.4f;    // 0..1: 0 bright, 1 dark
        float mod = 0.3f;     // 0..1: line modulation (chorus in the tail, spring wobble)
        float shimmer = 0;    // 0..1: amount of pitched feedback
        int interval = 4;     // index into intervalNames()
        // (added for Potion; the defaults keep the sound bit for bit) FREEZE without DECAY at its top, so the
        // space does not grow when the tail is held (the FDN algorithms); and a glide of the FDN lines'
        // lengths when DECAY moves (one-pole, ms): 0 jumps to the new lengths, a few hundred ms make DECAY
        // smooth to modulate (a short pitch bend in the tail instead of clicks)
        bool freeze = false;
        float glideMs = 0;
        // (added for Bidoo VCV REI, 2026-11-26, from Grotte; the defaults keep the sound bit for bit) Build Up, 0..1:
        // sparse, distinct early reflections that thicken into the tail (less input diffusion, less smearing of the
        // dense mode's reflections, a weaker allpass in the FDN lines). shimmerTone, 0..1: the shimmer loop's high
        // cut, 1 kHz (dark) to 12 kHz (bright), two poles, so each pass is darker; below 0 it follows DAMP as before
        float buildUp = 0;
        float shimmerTone = -1;
        // (also for REI, Grotte's Size / Density / Depth; below 0, the default, each is as before, bit for bit)
        // size 0..1: the FDN lines' lengths x 0.5 to x 1.5 (0.5: as the algorithm made them), no longer grown by DECAY.
        // density 0..1: the input diffusers' and the lines' allpass gains x 0.5 to x 1.5 (0.5: the algorithm's).
        // depth 0..1: the early reflections from close (louder, 9 kHz) to far (quieter, 4 kHz); 0.3 their usual level
        float size = -1;
        float density = -1;
        float depth = -1;
        // (for cabanon's Grotte, 2026-11-26; off, the default, bit for bit) a decay time per frequency on the network
        // algorithms (HALL, ROOM, CHAMBER, PLATE, CLOUD), Grotte's Stretch EQ: stretchLog2[k] is the log2 of the factor on
        // the decay time at stretchFreq (k), on top of DECAY and DAMP (1: twice as long, -1: half; held within -2..2).
        // Nothing while frozen, nor on SPRING, NON-LIN or REVERSE (no decaying loop to stretch).
        bool stretch = false;
        std::array<float, kStretchPoints> stretchLog2 {};
    };

    // dense: the FDN algorithms' 16-line voicing (see the top of this file), fixed until the next prepare()
    void prepare (float sampleRate, bool dense = false);
    // clears every buffer (no allocation); the next change of algorithm then switches at once (no fade)
    void reset();
    // wet output only; non-finite input samples are heard as silence, a non-finite output resets the reverb
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const Settings& s);
    // seconds for the decay knob of an algorithm, for the screen and tests
    static float decaySeconds (int algo, float decay01);
    static constexpr float kFreezeAt = 126.5f / 127.0f;

private:
    friend struct PrismTestAccess; // the tests reach the shimmer's shifter alone
    static constexpr int kLines = 16; // storage; 8 run unless dense
    static constexpr int kEr = 24;    // storage; 8 reflections unless dense
    static constexpr int kSpringStages = 20;
    static constexpr int kTaps = 36;

    struct Allpass
    {
        std::vector<float> buf;
        int pos = 0, len = 1;
        void prepare (int maxLen);
        void reset();
        void setLength (int l) { len = std::clamp (l, 1, (int) buf.size()); }
        inline float process (float x, float g)
        {
            int r = pos - len;
            if (r < 0)
                r += (int) buf.size();
            const float d = buf[(size_t) r];
            const float v = x - g * d;
            buf[(size_t) pos] = v;
            if (++pos >= (int) buf.size())
                pos = 0;
            return d + g * v;
        }
    };

    // a mono ring buffer with a power-of-two size, read with Hermite interpolation
    struct Line
    {
        std::vector<float> buf;
        int mask = 0, write = 0;
        void prepare (int minLen);
        void reset();
        inline void push (float x)
        {
            buf[(size_t) write] = x;
            write = (write + 1) & mask;
        }
        float read (float delay) const; // samples, >= 1
        inline float readInt (int delay) const { return buf[(size_t) ((write - delay) & mask)]; }
    };

    // a delay-line pitch shifter that splices like a tape loop: the read point slides at the pitch ratio
    // and, when it runs out of room, jumps by about a window to the point where the signal best matches
    // (a correlation search), then crossfades; aligned splices keep notes clean instead of rough
    struct Shifter
    {
        Line line;
        float d = 0, d2 = 0, fade = 1, fadeInc = 0;
        // the splice search in samples: set by ear at 48 kHz (384, 3, 6, 5, 480), the same times at any rate
        int matchLen = 384, matchStep = 3, coarseStep = 6, fineReach = 5, search = 480;
        float searchF = 480; // (search as a float, for the per-sample bounds)
        void prepare (float sr);
        void reset();
        float process (float x, float ratio, float windowSamples);
        int bestJump (int from, int lo, int hi) const;
        // the search's numbers: as constants at 48 kHz (so the loops compile as they always did), or scaled
        struct At48k
        {
            static constexpr int len = 384, step = 3, coarse = 6, reach = 5;
        };
        struct Scaled
        {
            int len, step, coarse, reach;
        };
        template <class Numbers>
        int bestJumpWith (int from, int lo, int hi, Numbers numbers) const;
    };

    static bool sameSettings (const Settings& a, const Settings& b)
    {
        return a.algo == b.algo && a.preMs == b.preMs && a.decay == b.decay && a.damp == b.damp && a.mod == b.mod && a.shimmer == b.shimmer
               && a.interval == b.interval && a.freeze == b.freeze && a.glideMs == b.glideMs && a.buildUp == b.buildUp
               && a.shimmerTone == b.shimmerTone && a.size == b.size && a.density == b.density && a.depth == b.depth
               && a.stretch == b.stretch && (! a.stretch || a.stretchLog2 == b.stretchLog2);
    }
    void clear(); // the buffers and the running state, for reset() and a change of algorithm
    void processFinite (const float* inL, const float* inR, float* outL, float* outR, int n, const Settings& s);
    void configure (const Settings& s);
    // transient: a pre-delay crossfading or the output fading for a change of algorithm (the still loop
    // stays what it always was); with fadingOut, returns as soon as the output has faded out
    template <bool transient>
    int run (const float* inL, const float* inR, float* outL, float* outR, int n, bool fadingOut);
    template <int N>
    void fdn (float inL, float inR, float& outL, float& outR);
    void spring (float inL, float inR, float& outL, float& outR);
    void burst (float inL, float inR, float& outL, float& outR);
    void designStretch (const Settings& s, float t60, float hfRatio);

    float sr = 48000;
    int algo = -1;
    // input
    Line preL, preR;
    std::array<Allpass, 4> inDiffL, inDiffR;
    float inDiffGain = 0.6f, inDiffBase = 0.6f; // the gain used, and the algorithm's (Build Up lowers it)
    // early reflections (from the predelayed input)
    std::array<int, kEr> erTimeL {}, erTimeR {};
    std::array<float, kEr> erGain {};
    std::array<Allpass, 2> erApL, erApR; // dense: the reflections smeared by two allpasses a side
    float erApG = 1;                     // their gains' scale (Build Up: less smearing)
    float erLevelBase = 0;               // the algorithm's reflections level (Settings::depth scales it)
    bool dense = false;
    int nLines = 8, nEr = 8;
    float erLevel = 0;
    // FDN
    std::array<Line, kLines> lines;
    std::array<Allpass, kLines> lineAp, lineAp2; // two allpasses per line: a denser, smoother tail
    std::array<float, kLines> baseLen {}, gain {}, dampB {}, dampZ {}, dcZ {}, lfoPhase {}, lfoInc {}, lfoNow {};
    std::array<float, kLines> curLen {}; // the lengths read now: baseLen, or gliding towards it (Settings::glideMs)
    std::array<float, kLines> lens16 {}; // dense: the sixteen lengths (ms) of the algorithm
    float apGain = 0.3f, apBase = 0.3f, dampA = 0.5f, modDepth = 0, lenGlide = 0;
    bool snapLen = true;
    // Settings::stretch: each line's absorption filters (double) and broadband gain; what they were designed from
    AttenuationFit stretchFit;
    std::array<std::array<DBiquad, AttenuationFit::kStages>, kLines> lineEq {};
    std::array<double, kLines> lineEqGain {};
    bool stretchOn = false;
    struct StretchKey
    {
        float t60 = -1, hfRatio = -1;
        std::array<float, kLines> len {};
        std::array<float, kStretchPoints> curve {};
        bool operator== (const StretchKey&) const = default;
    } stretchKey;
    // spring tanks
    struct Tank
    {
        Line line;
        std::vector<float> apX, apY; // the allpass stages' K-sample memories
        int apPos = 0, stretch = 1;  // K
        float lp = 0, hp = 0, out = 0;
        float length = 2000;
        double lfo = 0;
    };
    std::array<Tank, 2> tanks;
    float springA = 0.6f, springFb = 0.8f, springLpA = 0.5f, springHpA = 0.01f;
    // NON-LIN / REVERSE burst
    Line burstL, burstR;
    std::array<int, kTaps> tapTime {};
    std::array<float, kTaps> tapGain {};
    float lastBurstDecay = -1;
    std::array<Allpass, 2> outDiffL, outDiffR;
    float burstDampZL = 0, burstDampZR = 0;
    // shimmer: around the whole reverb, every algorithm (since 2026-11-26 two phase vocoders, and no more
    // shifters inside the FDN's lines: their splices left junk between the octaves and their loss was made up,
    // so the octaves outlived the note)
    PhaseVocoderShifter shiftL, shiftR;
    float shimL = 0, shimR = 0, shimEnv = 0, inEnv = 0, shimHpL = 0, shimHpR = 0, shimLpL = 0, shimLpR = 0;
    float shimLp2L = 0, shimLp2R = 0;
    // the early reflections' high cut (since 2026-11-26: walls take the highs off; it followed DAMP nowhere)
    float erLpA = 1, erLpL = 0, erLpR = 0;
    // body: the wet lows lifted under about 300 Hz where an algorithm gave a snare's body back thinner than its top
    // (since 2026-11-26; dB per algorithm in configure)
    float warmGain = 0, warmA = 0, warmL = 0, warmR = 0;
    float shimRatio = 2, shimRatio2 = 2, shimLpA = 0.5f, inGate = 1, shimGain = 0, shimDirect = 0;
    float outGain = 1;
    int preSamples = 0, preTarget = 0, preOld = 0; // the pre-delay read now, the one wanted, the one fading out
    float preFade = 1, preFadeInc = 0;             // below 1: a moved pre-delay crossfading (10 ms)
    bool snapPre = true;
    float dcA = 0.0008f; // the lines' DC guard: 0.0008 at 48 kHz (about 6 Hz), the same corner at any rate
    // per-block coefficients of run(), from the rate (prepare())
    float shimHpA = 0, gateA = 0, envAtk = 0, envRel = 0, inRelA = 0;
    // the settings last configured: configure() returns at once when they are the same
    Settings lastSettings;
    static constexpr float kModFloorMs = 0.3f; // the lines' least modulation depth (ROOM and PLATE 0.42: their lines are short)
    static constexpr float kShimGain = 1.4f;   // the shimmer loop's gain at SHIMMER 1 (3.2 before the phase vocoders:
                                               // at SHIMMER 0.5 the octave rang 5.7 dB over the note in the tail;
                                               // now -3.6 / -10 / -31 dB for the first three octaves, dense -10 / -24 / -35)
    static constexpr float kShimLpHz = 6000.0f; // its high cut with DAMP at 0
    bool configured = false;
    // a change of algorithm: the output fades out (the old algorithm playing on), the buffers are cleared
    // while silent, the new algorithm fades in; snapAlgo: just reset, nothing to fade
    float switchGain = 1;
    bool snapAlgo = true;
    bool freeze = false;
    unsigned counter = 0;
};

namespace prism_detail
{
// The FDN algorithms: line lengths (ms), decay range (s), input diffusers (ms) and gain, the allpass
// in each line (ms) and gain, modulation depth (ms), early reflections level and spacing
struct FdnDef
{
    std::array<float, 8> lens;
    float tLo, tHi;
    std::array<float, 4> diff;
    float diffGain;
    float apMs, apGain;
    float modMs;
    float er, erScale;
    float sizeFromDecay; // how much DECAY also grows the space
    float level;
};

inline const FdnDef& fdnDef (int algo)
{
    static const FdnDef hall { { 37.1f, 41.9f, 47.3f, 53.9f, 61.7f, 71.3f, 83.1f, 97.7f }, 0.8f, 14.0f, { 4.77f, 3.59f, 12.73f, 9.3f }, 0.62f,
                               5.3f, 0.4f, 1.1f, 0.32f, 1.0f, 0.3f, 1.0f };
    static const FdnDef room { { 11.3f, 13.7f, 16.1f, 18.9f, 21.7f, 24.3f, 27.1f, 30.7f }, 0.2f, 3.0f, { 1.9f, 2.7f, 4.1f, 3.3f }, 0.5f,
                               2.1f, 0.3f, 0.15f, 0.6f, 0.35f, 0.4f, 1.0f };
    static const FdnDef chamber { { 19.3f, 23.1f, 26.9f, 31.3f, 35.9f, 40.1f, 45.7f, 51.1f }, 0.4f, 6.0f, { 3.1f, 4.3f, 7.7f, 5.9f }, 0.62f,
                                  3.7f, 0.45f, 0.6f, 0.45f, 0.6f, 0.3f, 1.0f };
    static const FdnDef plate { { 9.1f, 11.9f, 14.3f, 17.7f, 20.9f, 24.7f, 28.1f, 33.3f }, 0.4f, 9.0f, { 4.77f, 3.59f, 12.73f, 9.3f }, 0.72f,
                                6.1f, 0.6f, 0.25f, 0.0f, 1.0f, 0.0f, 0.85f };
    static const FdnDef cloud { { 67.3f, 79.1f, 93.7f, 108.9f, 127.3f, 143.1f, 161.9f, 183.7f }, 2.0f, 40.0f, { 11.3f, 17.9f, 23.7f, 29.1f }, 0.7f,
                                9.7f, 0.55f, 1.6f, 0.0f, 1.0f, 0.15f, 0.9f };
    switch (algo)
    {
        case PrismReverb::Room: return room;
        case PrismReverb::Chamber: return chamber;
        case PrismReverb::Plate: return plate;
        case PrismReverb::Cloud: return cloud;
        default: return hall;
    }
}

inline bool isFdn (int algo)
{
    return algo == PrismReverb::Hall || algo == PrismReverb::Room || algo == PrismReverb::Chamber || algo == PrismReverb::Plate
           || algo == PrismReverb::Cloud;
}
} // namespace prism_detail

inline const std::vector<std::string>& PrismReverb::algoNames()
{
    static const std::vector<std::string> n { "HALL", "ROOM", "CHAMBER", "PLATE", "SPRING", "NON-LIN", "REVERSE", "CLOUD" };
    return n;
}

inline const std::vector<std::string>& PrismReverb::intervalNames()
{
    // the last three stack two intervals: one on each side of the shimmer, and on alternate tank lines
    static const std::vector<std::string> n { "-OCT", "-5TH", "+4TH", "+5TH", "+OCT", "+OCT+5", "+2OCT", "+OCT/+5TH", "+OCT/-OCT",
                                              "+OCT/+2OCT" };
    return n;
}

inline float PrismReverb::intervalSemis (int index, int voice)
{
    static constexpr float a[] { -12, -7, 5, 7, 12, 19, 24, 12, 12, 12 };
    static constexpr float b[] { -12, -7, 5, 7, 12, 19, 24, 7, -12, 24 };
    const int i = std::clamp (index, 0, 9);
    return voice == 0 ? a[i] : b[i];
}

inline float PrismReverb::decaySeconds (int a, float d)
{
    d = prism_detail::clampf (d, 0.0f, 1.0f);
    float lo = 0.5f, hi = 6.0f;
    if (prism_detail::isFdn (a))
    {
        lo = prism_detail::fdnDef (a).tLo;
        hi = prism_detail::fdnDef (a).tHi;
    }
    else if (a == NonLin)
    {
        lo = 0.08f;
        hi = 0.6f;
    }
    else if (a == Reverse)
    {
        lo = 0.1f;
        hi = 0.8f;
    }
    return lo * std::pow (hi / lo, d);
}

// --- building blocks
inline void PrismReverb::Allpass::prepare (int maxLen)
{
    buf.assign ((size_t) std::max (2, maxLen), 0.0f);
    len = std::min (len, (int) buf.size());
    pos = 0;
}

inline void PrismReverb::Allpass::reset()
{
    std::fill (buf.begin(), buf.end(), 0.0f);
}

inline void PrismReverb::Line::prepare (int minLen)
{
    int size = 16;
    while (size < minLen + 8)
        size *= 2;
    buf.assign ((size_t) size, 0.0f);
    mask = size - 1;
    write = 0;
}

inline void PrismReverb::Line::reset()
{
    std::fill (buf.begin(), buf.end(), 0.0f);
}

inline float PrismReverb::Line::read (float delay) const
{
    delay = std::max (1.0f, std::min (delay, (float) (mask - 4)));
    const int i = (int) delay;
    const float t = delay - (float) i;
    // Hermite between the samples delay - 1 .. delay + 2 (older samples are further back)
    const float x0 = buf[(size_t) ((write - i) & mask)];
    const float x1 = buf[(size_t) ((write - i - 1) & mask)];
    const float xm1 = buf[(size_t) ((write - i + 1) & mask)];
    const float x2 = buf[(size_t) ((write - i - 2) & mask)];
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

inline void PrismReverb::Shifter::prepare (float rate)
{
    line.prepare ((int) (rate * 0.25f));
    const float toRef = rate / 48000.0f;
    matchLen = (int) std::lround (384.0f * toRef);
    matchStep = std::max (1, (int) std::lround (3.0f * toRef));
    coarseStep = std::max (1, (int) std::lround (6.0f * toRef));
    fineReach = std::max (1, (int) std::lround (5.0f * toRef));
    search = (int) std::lround (480.0f * toRef);
    searchF = (float) search;
    reset();
}

inline void PrismReverb::Shifter::reset()
{
    line.reset();
    d = 0;
    d2 = 0;
    fade = 1;
    fadeInc = 0;
}

// the jump (in samples, lo..hi) where the signal best matches the signal at delay 'from' (both compared
// over the stretch just before them, normalised so loudness doesn't decide)
template <class Numbers>
inline int PrismReverb::Shifter::bestJumpWith (int from, int lo, int hi, Numbers numbers) const
{
    constexpr bool fixed = std::is_same_v<Numbers, At48k>;
    const int kLen = fixed ? At48k::len : numbers.len, kStep = fixed ? At48k::step : numbers.step;
    const int coarse = fixed ? At48k::coarse : numbers.coarse, reach = fixed ? At48k::reach : numbers.reach;
    float e0 = 1.0e-9f;
    for (int j = 0; j < kLen; j += kStep)
    {
        const float a = line.readInt (from + j);
        e0 += a * a;
    }
    auto score = [&] (int k)
    {
        float c = 0, e1 = 1.0e-9f;
        for (int j = 0; j < kLen; j += kStep)
        {
            const float b = line.readInt (from + k + j);
            c += line.readInt (from + j) * b;
            e1 += b * b;
        }
        return c / std::sqrt (e0 * e1);
    };
    // coarse (every 6 samples at 48 kHz: no note below 4 kHz slips between), then the best neighbourhood finely
    int best = (lo + hi) / 2;
    float bestScore = -2;
    for (int k = lo; k <= hi; k += coarse)
        if (const float sc = score (k); sc > bestScore)
        {
            bestScore = sc;
            best = k;
        }
    const int centre = best;
    for (int k = std::max (lo, centre - reach); k <= std::min (hi, centre + reach); ++k)
        if (k != centre)
            if (const float sc = score (k); sc > bestScore)
            {
                bestScore = sc;
                best = k;
            }
    return best;
}

inline int PrismReverb::Shifter::bestJump (int from, int lo, int hi) const
{
    // at 48 kHz with the numbers as constants, as it always was (the compiler unrolls them)
    if (matchLen == At48k::len && matchStep == At48k::step && coarseStep == At48k::coarse && fineReach == At48k::reach)
        return bestJumpWith (from, lo, hi, At48k {});
    return bestJumpWith (from, lo, hi, Scaled { matchLen, matchStep, coarseStep, fineReach });
}

inline float PrismReverb::Shifter::process (float x, float ratio, float window)
{
    line.push (x);
    const float slide = ratio - 1.0f; // how fast the read point moves towards the present
    // (searchF: 10 ms, enough to find a period of any note above 100 Hz)
    // the old read point must not reach the present while it fades out
    const float fadeLen = std::max (64.0f, std::min (window * 0.35f, (window - searchF) / (std::abs (slide) + 1.0f)));
    const float lowest = std::max (0.0f, slide) * fadeLen + 4.0f;
    const float highest = window + searchF + 8.0f;
    if (d <= 0.0f)
        d = slide > 0.0f ? lowest + window : 8.0f;
    d -= slide;
    if (fade < 1.0f)
    {
        d2 -= slide;
        fade = std::min (1.0f, fade + fadeInc);
    }
    else if (slide > 0.0f ? d < lowest : (slide < 0.0f && d > highest))
    {
        // splice: the old read point fades out, the new one fades in about a window away
        const int w = (int) window;
        const int jump = slide > 0.0f ? bestJump ((int) d, w - search, w + search) : bestJump ((int) d, -w - search, -w + search);
        d2 = d;
        d += (float) jump;
        fade = 0;
        fadeInc = 1.0f / fadeLen;
    }
    if (fade >= 1.0f)
        return line.read (d);
    const float g = sinF (0.5f * prism_detail::kPiF * fade);
    return g * g * line.read (d) + (1.0f - g * g) * line.read (d2);
}

// --- setup
inline void PrismReverb::prepare (float sampleRate, bool denseMode)
{
    sr = sampleRate;
    dense = denseMode;
    nLines = dense ? 16 : 8;
    nEr = dense ? kEr : 8;
    dcA = 0.0008f * (48000.0f / sr);
    preFadeInc = 1.0f / (0.01f * sr);
    shimHpA = 1.0f - std::exp (-prism_detail::kTwoPiF * 160.0f / sr);
    gateA = 1.0f - std::exp (-1.0f / (0.02f * sr));
    envAtk = 1.0f - std::exp (-1.0f / (0.005f * sr));
    envRel = 1.0f - std::exp (-1.0f / (0.3f * sr));
    inRelA = 1.0f - std::exp (-1.0f / (2.5f * sr));
    preL.prepare ((int) (sr * 0.5f));
    preR.prepare ((int) (sr * 0.5f));
    for (int i = 0; i < 4; ++i)
    {
        inDiffL[(size_t) i].prepare ((int) (sr * 0.035f));
        inDiffR[(size_t) i].prepare ((int) (sr * 0.035f));
    }
    for (int i = 0; i < kLines; ++i) // the eight lines dense mode adds stay tiny without it
    {
        lines[(size_t) i].prepare (i < nLines ? (int) (sr * 0.3f) : 0);
        lineAp[(size_t) i].prepare (i < nLines ? (int) (sr * 0.02f) : 0);
        lineAp2[(size_t) i].prepare (i < nLines ? (int) (sr * 0.02f) : 0);
    }
    for (int i = 0; i < 2; ++i)
    {
        erApL[(size_t) i].prepare (dense ? (int) (sr * 0.01f) : 0);
        erApR[(size_t) i].prepare (dense ? (int) (sr * 0.01f) : 0);
    }
    const int k = std::max (1, (int) std::lround (sr / (2.0f * 4400.0f)));
    for (auto& t : tanks)
    {
        t.line.prepare ((int) (sr * 0.1f));
        t.apX.assign ((size_t) (kSpringStages * k), 0.0f);
        t.apY.assign ((size_t) (kSpringStages * k), 0.0f);
        t.stretch = k;
    }
    burstL.prepare ((int) (sr * 0.85f));
    burstR.prepare ((int) (sr * 0.85f));
    for (int i = 0; i < 2; ++i)
    {
        outDiffL[(size_t) i].prepare ((int) (sr * 0.01f));
        outDiffR[(size_t) i].prepare ((int) (sr * 0.01f));
    }
    shiftL.prepare (sr);
    shiftR.prepare (sr);
    stretchFit.prepare (sr);
    for (int i = 0; i < nLines; ++i)
    {
        lfoPhase[(size_t) i] = (float) i / (float) nLines;
        // advanced every 8 samples; dense: sixteen rates over about the same span
        lfoInc[(size_t) i] = (0.11f + (dense ? 0.1f : 0.137f) * (float) i) / sr * 8.0f;
    }
    algo = -1;
    reset();
}

inline void PrismReverb::reset()
{
    clear();
    switchGain = 1;
    snapAlgo = true;    // empty: a new algorithm may start at once
    configured = false; // the next configure() works everything out again (snapPre, snapLen)
}

inline void PrismReverb::clear()
{
    preL.reset();
    preR.reset();
    for (auto& a : inDiffL)
        a.reset();
    for (auto& a : inDiffR)
        a.reset();
    for (auto& l : lines)
        l.reset();
    for (auto& a : lineAp2)
        a.reset();
    for (auto& a : lineAp)
        a.reset();
    for (auto& a : erApL)
        a.reset();
    for (auto& a : erApR)
        a.reset();
    dampZ.fill (0);
    dcZ.fill (0);
    for (auto& line : lineEq)
        for (auto& bq : line)
            bq.reset();
    for (auto& t : tanks)
    {
        t.line.reset();
        std::fill (t.apX.begin(), t.apX.end(), 0.0f);
        std::fill (t.apY.begin(), t.apY.end(), 0.0f);
        t.apPos = 0;
        t.lp = t.hp = t.out = 0;
    }
    burstL.reset();
    burstR.reset();
    for (auto& a : outDiffL)
        a.reset();
    for (auto& a : outDiffR)
        a.reset();
    burstDampZL = burstDampZR = 0;
    shiftL.reset();
    shiftR.reset();
    shimLp2L = shimLp2R = erLpL = erLpR = warmL = warmR = 0;
    inGate = 1;
    snapLen = true;
    snapPre = true;
    preFade = 1;
    shimL = shimR = shimEnv = inEnv = shimHpL = shimHpR = shimLpL = shimLpR = 0;
    counter = 0;
    // the modulation starts where prepare() started it, so a reset (or a change of algorithm) plays
    // exactly as a fresh reverb (it went on from the last user's phases, 2026-11-25)
    for (int i = 0; i < nLines; ++i)
        lfoPhase[(size_t) i] = (float) i / (float) nLines;
    lfoNow.fill (0);
    for (auto& t : tanks)
        t.lfo = 0;
}

inline void PrismReverb::configure (const Settings& s)
{
    // the same settings give the same coefficients (everything below depends on them and the rate only)
    if (configured && sameSettings (s, lastSettings))
        return;
    configured = true;
    lastSettings = s;
    const int a = std::clamp (s.algo, 0, (int) NumAlgos - 1);
    if (a != algo)
    {
        algo = a;
        clear();
        // fixed per algorithm: diffuser lengths, early reflections, burst taps
        const float ms = sr * 0.001f;
        if (prism_detail::isFdn (a))
        {
            const auto& d = prism_detail::fdnDef (a);
            for (int i = 0; i < 4; ++i)
            {
                inDiffL[(size_t) i].setLength ((int) (d.diff[(size_t) i] * ms));
                inDiffR[(size_t) i].setLength ((int) (d.diff[(size_t) i] * 1.13f * ms));
            }
            inDiffBase = d.diffGain;
            for (int i = 0; i < 8; ++i)
            {
                if (dense)
                    continue;
                erTimeL[(size_t) i] = (int) (prism_detail::kErMs[i] * d.erScale * ms);
                erTimeR[(size_t) i] = (int) (prism_detail::kErMs[i] * d.erScale * 1.09f * ms + 0.7f * ms);
                erGain[(size_t) i] = prism_detail::kErGain[i] * (i % 3 == 1 ? -1.0f : 1.0f);
                lineAp[(size_t) i].setLength ((int) (d.apMs * (0.62f + 0.11f * (float) i) * ms));
                lineAp2[(size_t) i].setLength ((int) (d.apMs * (0.31f + 0.067f * (float) ((i * 3) % 8)) * ms));
            }
            erLevelBase = d.er;
            apBase = d.apGain;
            outGain = d.level * 0.55f;
            if (dense)
            {
                // sixteen lines: lengths log spaced over the eight's span (PLATE's 2.2 x longer: its short
                // lines rang on a few modes), allpasses spread the same way
                const float stretch = a == Plate ? 2.2f : 1.0f;
                const float lo = d.lens[0] * 0.75f * stretch, hi = d.lens[7] * stretch;
                for (int i = 0; i < kLines; ++i)
                {
                    lens16[(size_t) i] = lo * std::pow (hi / lo, (float) i / 15.0f) * (1.0f + prism_detail::kJitter16[i]);
                    lineAp[(size_t) i].setLength ((int) (d.apMs * (0.62f + 0.055f * (float) i) * ms));
                    lineAp2[(size_t) i].setLength ((int) (d.apMs * (0.31f + 0.0335f * (float) ((i * 7) % 16)) * ms));
                }
                // 24 reflections over the eight's span (5-67 ms x erScale), denser late, gains falling with
                // time and the energy of the eight, then smeared by two allpasses: no discrete flams
                prism_detail::PrismRng rr { 0xe41u };
                float sum = 0;
                for (int k = 0; k < kEr; ++k)
                {
                    const float x = ((float) k + 0.5f + 0.35f * rr.bipolar()) / (float) kEr;
                    const float t = 5.0f + 62.0f * std::pow (x, 1.4f);
                    const float tR = 5.0f + 62.0f * std::pow (std::min (1.0f, x + 0.3f * rr.bipolar() / (float) kEr), 1.4f);
                    erTimeL[(size_t) k] = std::max (1, (int) (t * d.erScale * ms));
                    erTimeR[(size_t) k] = std::max (1, (int) (tR * d.erScale * 1.07f * ms + 0.4f * ms));
                    const float g = std::exp (-t / 45.0f) * (rr.uniform() < 0.5f ? -1.0f : 1.0f) * (0.6f + 0.4f * rr.uniform());
                    erGain[(size_t) k] = g;
                    sum += g * g;
                }
                const float norm = std::sqrt (2.75f / std::max (1.0e-6f, sum));
                for (auto& g : erGain)
                    g *= norm;
                erApL[0].setLength ((int) (1.3f * ms));
                erApL[1].setLength ((int) (2.9f * ms));
                erApR[0].setLength ((int) (1.7f * ms));
                erApR[1].setLength ((int) (3.4f * ms));
                // the same energy as the eight-line voicing (measured)
                outGain *= a == Plate ? 1.75f : 1.29f;
            }
        }
        else
        {
            // spring and bursts: light input diffusion
            static constexpr float diff[4] { 1.3f, 2.1f, 3.7f, 5.3f };
            for (int i = 0; i < 4; ++i)
            {
                inDiffL[(size_t) i].setLength ((int) (diff[i] * ms));
                inDiffR[(size_t) i].setLength ((int) (diff[i] * 1.17f * ms));
            }
            inDiffBase = a == Spring ? 0.3f : 0.6f;
            erLevelBase = 0;
            outGain = a == Spring ? 0.7f : 0.7f;
            outDiffL[0].setLength ((int) (1.7f * ms));
            outDiffL[1].setLength ((int) (3.3f * ms));
            outDiffR[0].setLength ((int) (2.1f * ms));
            outDiffR[1].setLength ((int) (3.9f * ms));
        }
        lastBurstDecay = -1;
    }

    {
        // Build Up (Grotte's, 2026-11-26): less input diffusion and less smearing of the reflections, so the first
        // echoes stay distinct, and a weaker allpass in the lines, so density grows pass after pass through the
        // matrix instead of at once (the same energy: allpasses only). 0 multiplies by exactly 1: as before
        const float bu = prism_detail::clampf (s.buildUp, 0.0f, 1.0f);
        inDiffGain = inDiffBase * (1.0f - 0.8f * bu);
        apGain = apBase * (1.0f - 0.7f * bu);
        erApG = 1.0f - bu;
        // Density: more or less diffusion around the algorithm's (kept under the gains where allpasses ring)
        if (s.density >= 0.0f)
        {
            const float fd = 0.5f + prism_detail::clampf (s.density, 0.0f, 1.0f);
            inDiffGain = std::min (0.8f, inDiffGain * fd);
            apGain = std::min (0.75f, apGain * fd);
        }
        // Depth: the reflections quieter as the source moves away (Grotte's 1.15 - 0.85 x distance, 1 at 0.3)
        erLevel = erLevelBase;
        if (s.depth >= 0.0f)
            erLevel *= (1.15f - 0.85f * prism_detail::clampf (s.depth, 0.0f, 1.0f)) / (1.15f - 0.85f * 0.3f);
    }

    preTarget = std::max (1, (int) (prism_detail::clampf (s.preMs, 0.0f, 300.0f) * sr * 0.001f));
    if (snapPre) // after a reset the lines are empty: nothing to fade
    {
        preSamples = preTarget;
        snapPre = false;
    }
    // DEC at its top: FREEZE (the FDN algorithms): the tail stops decaying and new input is shut out
    freeze = prism_detail::isFdn (a) && (s.decay >= kFreezeAt || s.freeze);
    const float t60 = decaySeconds (a, s.decay);
    // damping: 0 = open (18 kHz), 1 = dark (1 kHz)
    const float dampHz = 18000.0f * std::pow (1000.0f / 18000.0f, prism_detail::clampf (s.damp, 0.0f, 1.0f));
    dampA = 1.0f - std::exp (-prism_detail::kTwoPiF * std::min (dampHz, sr * 0.45f) / sr);
    const float hfRatio = std::pow (0.07f, prism_detail::clampf (s.damp, 0.0f, 1.0f)); // high frequencies' share of the decay time

    if (! prism_detail::isFdn (a))
        stretchOn = false; // (Settings::stretch: the network algorithms only)
    if (prism_detail::isFdn (a))
    {
        const auto& d = prism_detail::fdnDef (a);
        const float size = s.size >= 0.0f ? 0.5f + prism_detail::clampf (s.size, 0.0f, 1.0f)
                                          : 1.0f - d.sizeFromDecay * 0.15f + d.sizeFromDecay * 0.3f * s.decay;
        for (int i = 0; i < nLines; ++i)
        {
            baseLen[(size_t) i] = (dense ? lens16[(size_t) i] : d.lens[(size_t) i]) * size * sr * 0.001f;
            const float loop = baseLen[(size_t) i] + (float) (lineAp[(size_t) i].len + lineAp2[(size_t) i].len);
            // frequency-dependent decay (Jot): the low end decays in T60, the top in T60 * hf, for
            // any line length, through a one-pole whose DC gain is g and Nyquist gain g_hf
            const float g = std::pow (10.0f, -3.0f * loop / (sr * t60));
            const float gHf = std::pow (10.0f, -3.0f * loop / (sr * t60 * hfRatio));
            gain[(size_t) i] = freeze ? 1.0f : g;
            dampB[(size_t) i] = freeze ? 0.0f : (g - gHf) / (g + gHf);
        }
        designStretch (s, t60, hfRatio);
        lenGlide = s.glideMs > 0.0f ? 1.0f - std::exp (-1.0f / (s.glideMs * 0.001f * sr)) : 0.0f;
        if (snapLen || lenGlide == 0.0f)
            curLen = baseLen;
        snapLen = false;
        // MOD sets the depth over a floor (since 2026-11-26): below about 0.3 ms the modes stand still and the tail
        // rings like metal (ROOM and PLATE gave 0.045 / 0.075 ms at MOD 0.3: 8.5 / 7.6 dB of ripple in the tail's
        // spectrum against 5.6 for noise and Valhalla's). Dense mode no longer doubles it (up to 6 x for PLATE): a
        // held note spread 51 Hz in HALL; its sixteen lines are smooth enough on the floor.
        modDepth = std::max (kModFloorMs * (a == Room || a == Plate ? 1.4f : 1.0f), d.modMs * prism_detail::clampf (s.mod, 0.0f, 1.0f)) * sr * 0.001f;
    }
    else if (a == Spring)
    {
        tanks[0].length = 0.0413f * sr;
        tanks[1].length = 0.0467f * sr;
        springA = -0.62f;
        const float loop = 0.044f * sr + (float) (kSpringStages * tanks[0].stretch) * 4.0f;
        springFb = std::pow (10.0f, -3.0f * loop / (sr * t60));
        const float lpHz = 6500.0f * std::pow (2500.0f / 6500.0f, prism_detail::clampf (s.damp, 0.0f, 1.0f));
        springLpA = 1.0f - std::exp (-prism_detail::kTwoPiF * lpHz / sr);
        springHpA = 1.0f - std::exp (-prism_detail::kTwoPiF * 120.0f / sr);
        modDepth = prism_detail::clampf (s.mod, 0.0f, 1.0f) * 0.0012f * sr;
    }
    else if (std::abs (s.decay - lastBurstDecay) > 0.002f)
    {
        // NON-LIN: flat then a short fade (a gate); REVERSE: rising, then cut
        lastBurstDecay = s.decay;
        const float gate = t60 * sr;
        prism_detail::PrismRng r { 0x5eedu };
        float sum = 0;
        for (int k = 0; k < kTaps; ++k)
        {
            const float x = ((float) k + 0.5f + 0.4f * r.bipolar()) / (float) kTaps;
            tapTime[(size_t) k] = std::max (1, (int) (x * gate));
            float g = 1.0f;
            if (a == NonLin)
                g = x < 0.75f ? 1.0f : 1.0f - (x - 0.75f) / 0.25f * 0.85f;
            else
                g = x * x;
            g *= (r.uniform() < 0.5f ? -1.0f : 1.0f) * (0.8f + 0.2f * r.uniform());
            tapGain[(size_t) k] = g;
            sum += g * g;
        }
        const float norm = 1.0f / std::sqrt (std::max (1.0e-6f, sum));
        for (auto& g : tapGain)
            g *= norm;
    }

    // shimmer
    shimRatio = std::pow (2.0f, intervalSemis (s.interval, 0) / 12.0f);
    shimRatio2 = std::pow (2.0f, intervalSemis (s.interval, 1) / 12.0f);
    {
        // one interval: the two sides a few cents apart (two voices, wide and soft)
        const float detune = shimRatio == shimRatio2 ? std::pow (2.0f, 0.04f / 12.0f) : 1.0f;
        shiftL.setRatio (shimRatio * detune);
        shiftR.setRatio (shimRatio2 / detune);
    }
    // DAMP darkens the climbing octaves (two poles in the loop, each pass darker): 6 kHz open, 1.8 kHz dark; or
    // shimmerTone sets it on its own, 1 to 12 kHz (Grotte's TONE)
    const float shimLpHz = s.shimmerTone >= 0.0f ? 1000.0f * std::pow (12.0f, prism_detail::clampf (s.shimmerTone, 0.0f, 1.0f))
                                                 : kShimLpHz * std::pow (0.3f, prism_detail::clampf (s.damp, 0.0f, 1.0f));
    shimLpA = 1.0f - std::exp (-prism_detail::kTwoPiF * std::min (shimLpHz, sr * 0.45f) / sr);
    // the early reflections: 9 kHz open, 4 kHz dark (DAMP), or 9 kHz close, 4 kHz far (Settings::depth)
    const float erHz = s.depth >= 0.0f ? 9000.0f - 5000.0f * prism_detail::clampf (s.depth, 0.0f, 1.0f)
                                       : 9000.0f * std::pow (4000.0f / 9000.0f, prism_detail::clampf (s.damp, 0.0f, 1.0f));
    erLpA = 1.0f - std::exp (-prism_detail::kTwoPiF * std::min (erHz, sr * 0.45f) / sr);
    {
        // measured on a snare against ValhallaRoom / VintageVerb (repos/reverb-bench, balance.py): 125 / 250 Hz back
        // from -0.3..-2.8 dB to about +0.5..+2; HALL and ROOM (eight lines) were already full
        static constexpr float warmDb[2][NumAlgos] { { 0, 0, 2.0f, 2.0f, 0, 0, 0, 1.5f }, { 3.0f, 1.5f, 2.0f, 1.0f, 0, 0, 0, 0.5f } };
        const float db = warmDb[dense ? 1 : 0][a];
        warmGain = std::pow (10.0f, db / 20.0f) - 1.0f;
        warmA = 1.0f - std::exp (-prism_detail::kTwoPiF * 300.0f / sr);
    }
    // frozen, the shimmer would climb forever: it holds what it has
    const float shim = freeze ? 0.0f : prism_detail::clampf (s.shimmer, 0.0f, 1.0f);
    shimGain = shim * kShimGain;
    shimDirect = shim * (prism_detail::isFdn (a) ? 0.5f : 0.7f);
}

// --- the algorithms, one stereo sample each
template <int N>
inline void PrismReverb::fdn (float inL, float inR, float& outL, float& outR)
{
    // N = 8: the original voicing, unchanged; 16: dense
    constexpr const float* signL = N == 8 ? prism_detail::kSignL : prism_detail::kSign16L;
    constexpr const float* signR = N == 8 ? prism_detail::kSignR : prism_detail::kSign16R;
    if ((counter++ & 7) == 0)
        for (int i = 0; i < N; ++i)
        {
            auto& ph = lfoPhase[(size_t) i];
            ph += lfoInc[(size_t) i];
            ph -= std::floor (ph);
            lfoNow[(size_t) i] = 1.0f + sinF (prism_detail::kTwoPiF * ph); // the read modulation, held for 8 samples
        }
    std::array<float, N> y {};
    float l = 0, r = 0;
    for (int i = 0; i < N; ++i)
    {
        if (lenGlide > 0.0f)
            curLen[(size_t) i] += lenGlide * (baseLen[(size_t) i] - curLen[(size_t) i]);
        const float d = curLen[(size_t) i] + modDepth * lfoNow[(size_t) i];
        y[(size_t) i] = lines[(size_t) i].read (d);
        l += signL[i] * y[(size_t) i];
        r += signR[i] * y[(size_t) i];
        // damping (high frequencies decay faster) and a DC guard, then the decay gain
        auto& z = dampZ[(size_t) i];
        const float b = dampB[(size_t) i];
        z = (1.0f - b) * y[(size_t) i] + b * z;
        auto& dc = dcZ[(size_t) i];
        dc += dcA * (z - dc);
        y[(size_t) i] = (z - dc) * gain[(size_t) i];
        if (stretchOn) // Settings::stretch: the line's absorption filters
        {
            double v = (double) y[(size_t) i] * lineEqGain[(size_t) i];
            for (auto& bq : lineEq[(size_t) i])
                v = bq.process (v);
            y[(size_t) i] = (float) v;
        }
    }
    prism_detail::hadamard (y);
    constexpr float inK = N == 8 ? 0.5f : 0.3535534f; // the same input power into twice the lines
    for (int i = 0; i < N; ++i)
    {
        const float in = (i % 2 == 0 ? inL : inR) * (i % 4 < 2 ? inK : -inK);
        lines[(size_t) i].push (lineAp2[(size_t) i].process (lineAp[(size_t) i].process (y[(size_t) i] + in, apGain), apGain * 0.8f));
    }
    constexpr float outK = N == 8 ? 0.35355339f : 0.25f;
    outL = l * outK;
    outR = r * outK;
}

// Settings::stretch: the lines' absorption filters for the curve asked, from configure () once the lines' gains and
// damping are set (their loss is what the filters scale).
inline void PrismReverb::designStretch (const Settings& s, float t60, float hfRatio)
{
    if (! s.stretch || freeze)
    {
        stretchOn = false;
        stretchKey = {};
        return;
    }
    StretchKey key;
    key.t60 = t60;
    key.hfRatio = hfRatio;
    for (int i = 0; i < nLines; ++i)
        key.len[(size_t) i] = baseLen[(size_t) i];
    for (int k = 0; k < kStretchPoints; ++k)
        key.curve[(size_t) k] = prism_detail::clampf (s.stretchLog2[(size_t) k], -2.0f, 2.0f);
    if (stretchOn && key == stretchKey)
        return;
    stretchKey = key;
    // the mean loop, its gain and damping as configure () gives a line (Jot: DC g, Nyquist g_hf)
    double ref = 0.0;
    std::array<double, kLines> loop {};
    for (int i = 0; i < nLines; ++i)
    {
        loop[(size_t) i] = (double) baseLen[(size_t) i] + (double) (lineAp[(size_t) i].len + lineAp2[(size_t) i].len);
        ref += loop[(size_t) i];
    }
    ref /= nLines;
    const double g = std::pow (10.0, -3.0 * ref / (sr * t60)), gHf = std::pow (10.0, -3.0 * ref / (sr * t60 * hfRatio));
    const double b = (g - gHf) / (g + gHf);
    constexpr int kPoints = AttenuationFit::kPoints;
    double base[kPoints], target[kPoints], wanted[kPoints], gains[AttenuationFit::kFilters], got[kPoints];
    for (int k = 0; k < kPoints; ++k)
    {
        const double f = stretchFit.freqAt (k), w = 2.0 * 3.14159265358979323846 * f / sr;
        // the line's loss per pass now (dB, negative): its gain and its one-pole damping (1 - b) / (1 - b z^-1)
        base[k] = 20.0 * std::log10 (g) + 20.0 * std::log10 ((1.0 - b) / std::sqrt (1.0 - 2.0 * b * std::cos (w) + b * b));
        const double x = prism_detail::clampf ((float) ((kStretchPoints - 1) * std::log (f / 20.0) / std::log (1000.0)), 0.0f,
                                               (float) (kStretchPoints - 1));
        const int i0 = std::min ((int) x, kStretchPoints - 2);
        const double l2 = key.curve[(size_t) i0] + (x - i0) * (key.curve[(size_t) (i0 + 1)] - key.curve[(size_t) i0]);
        wanted[k] = base[k] * std::pow (2.0, -l2); // the time x 2^l2: the loss / 2^l2
        target[k] = wanted[k] - base[k];
    }
    stretchFit.fit (target, gains, false);
    // the loop stays lossy: no point closer to 0 dB than half the smallest loss asked
    stretchFit.response (gains, got);
    double excess = -1.0e9, leastLoss = -1.0e9;
    for (int k = 0; k < kPoints; ++k)
    {
        excess = std::max (excess, base[k] + got[k]);
        leastLoss = std::max (leastLoss, wanted[k]);
    }
    if (excess > 0.5 * leastLoss)
        gains[0] -= excess - 0.5 * leastLoss;
    for (int i = 0; i < nLines; ++i)
    {
        const double r = loop[(size_t) i] / ref; // a longer line loses more per pass
        lineEqGain[(size_t) i] = std::pow (10.0, gains[0] * r / 20.0);
        for (int st = 0; st < AttenuationFit::kStages; ++st)
        {
            const double gs = gains[st + 1] * r;
            lineEq[(size_t) i][(size_t) st].set (std::abs (gs) < 1.0e-7 ? BiquadCoeffs::identity() : stretchFit.designStage (st + 1, gs));
        }
    }
    if (! stretchOn)
        for (auto& line : lineEq)
            for (auto& bq : line)
                bq.reset();
    stretchOn = true;
}

inline void PrismReverb::spring (float inL, float inR, float& outL, float& outR)
{
    const float in[2] { inL, inR };
    float out[2] {};
    for (int t = 0; t < 2; ++t)
    {
        auto& k = tanks[(size_t) t];
        k.lfo += (0.6 + 0.23 * t) / sr;
        k.lfo -= std::floor (k.lfo);
        const float d = k.line.read (k.length + modDepth * (1.0f + sinF (prism_detail::kTwoPiF * (float) k.lfo)));
        out[t] = d;
        // loop filters: the spring's bandwidth
        k.lp += springLpA * (d - k.lp);
        k.hp += springHpA * (k.lp - k.hp);
        const float fbSig = (k.lp - k.hp) * springFb;
        const float cross = tanks[(size_t) (1 - t)].out * 0.12f;
        float x = in[t] * 0.6f + fbSig * 0.94f + cross * springFb;
        // dispersion: stretched first-order allpasses (a + z^-K) / (1 + a z^-K), the chirp
        const int K = k.stretch;
        const int pos = k.apPos;
        for (int s = 0; s < kSpringStages; ++s)
        {
            float* bx = k.apX.data() + s * K;
            float* by = k.apY.data() + s * K;
            const float xd = bx[pos], yd = by[pos];
            const float yv = springA * x + xd - springA * yd;
            bx[pos] = x;
            by[pos] = yv;
            x = yv;
        }
        k.line.push (prism_detail::prismSoftClip (x));
    }
    for (auto& k : tanks)
        k.apPos = (k.apPos + 1) % k.stretch;
    tanks[0].out = out[0];
    tanks[1].out = out[1];
    // the two springs leak into each other's side a little, like one tank with two pickups
    outL = out[0] + 0.3f * out[1];
    outR = out[1] + 0.3f * out[0];
}

inline void PrismReverb::burst (float inL, float inR, float& outL, float& outR)
{
    burstL.push (inL);
    burstR.push (inR);
    float l = 0, r = 0;
    for (int k = 0; k < kTaps; ++k)
    {
        const int t = tapTime[(size_t) k];
        l += tapGain[(size_t) k] * burstL.readInt (t);
        r += tapGain[(size_t) (kTaps - 1 - k)] * (k % 2 == 0 ? 1.0f : -1.0f) * burstR.readInt (t + (k % 2 == 0 ? 37 : 71));
    }
    l = outDiffL[1].process (outDiffL[0].process (l, 0.6f), 0.55f);
    r = outDiffR[1].process (outDiffR[0].process (r, 0.6f), 0.55f);
    burstDampZL += dampA * (l - burstDampZL);
    burstDampZR += dampA * (r - burstDampZR);
    outL = burstDampZL;
    outR = burstDampZR;
}

inline void PrismReverb::process (const float* inL, const float* inR, float* outL, float* outR, int n, const Settings& s)
{
    // A NaN or an infinity would stay in the lines for good: the reverb hears silence in its place. A sum
    // per block, so finite audio goes through untouched (bit for bit) for about one add per sample.
    float inSum = 0.0f;
    for (int i = 0; i < n; ++i)
        inSum += inL[i] + inR[i];
    if (std::isfinite (inSum))
        processFinite (inL, inR, outL, outR, n, s);
    else
    {
        constexpr int kChunk = 64;
        float cleanL[kChunk], cleanR[kChunk];
        for (int done = 0; done < n; done += kChunk)
        {
            const int m = std::min (kChunk, n - done);
            for (int i = 0; i < m; ++i)
            {
                cleanL[i] = std::isfinite (inL[done + i]) ? inL[done + i] : 0.0f;
                cleanR[i] = std::isfinite (inR[done + i]) ? inR[done + i] : 0.0f;
            }
            processFinite (cleanL, cleanR, outL + done, outR + done, m, s);
        }
    }
    // the state overflowed (finite input, but far too loud) or something else went wrong: start again,
    // this block silent (reset() only clears: no allocation)
    float outSum = 0.0f;
    for (int i = 0; i < n; ++i)
        outSum += outL[i] + outR[i];
    if (! std::isfinite (outSum))
    {
        reset();
        std::fill (outL, outL + n, 0.0f);
        std::fill (outR, outR + n, 0.0f);
    }
}

inline void PrismReverb::processFinite (const float* inL, const float* inR, float* outL, float* outR, int n, const Settings& s)
{
    const int want = std::clamp (s.algo, 0, (int) NumAlgos - 1);
    for (int done = 0; done < n;)
    {
        // a new algorithm while a tail plays: the old one plays on (with the other settings new) while the
        // output fades out; once silent, configure() switches (clearing the buffers) and the new one fades in
        const bool fadingOut = algo >= 0 && want != algo && ! snapAlgo && switchGain > 0.0f;
        if (fadingOut)
        {
            Settings held = s;
            held.algo = algo;
            configure (held);
        }
        else
            configure (s);
        snapAlgo = false;
        // the crossfades only while something moves: still, the loop is what it always was
        if (! fadingOut && switchGain >= 1.0f && preFade >= 1.0f && preTarget == preSamples)
            done += run<false> (inL + done, inR + done, outL + done, outR + done, n - done, false);
        else
            done += run<true> (inL + done, inR + done, outL + done, outR + done, n - done, fadingOut);
    }
}

template <bool transient>
inline int PrismReverb::run (const float* inL, const float* inR, float* outL, float* outR, int n, bool fadingOut)
{
    const bool shimmerOn = shimGain > 0.0f;
    if (! shimmerOn)
        shimL = shimR = 0;
    const float hpA = shimHpA, lpA = shimLpA;
    const float gateTarget = freeze ? 0.0f : 1.0f;
    for (int i = 0; i < n; ++i)
    {
        preL.push (inL[i]);
        preR.push (inR[i]);
        inGate += gateA * (gateTarget - inGate);
        // A moved pre-delay would jump to another place in the input (a click): 10 ms of crossfade
        // instead, to where it is wanted when the fade starts, so a knob on the move is chased.
        float preG = 1, preInL = 0, preInR = 0;
        if constexpr (transient)
        {
            if (preFade >= 1.0f && preTarget != preSamples)
            {
                preOld = preSamples;
                preSamples = preTarget;
                preFade = 0;
            }
            preInL = preL.readInt (preSamples);
            preInR = preR.readInt (preSamples);
            if (preFade < 1.0f)
            {
                preFade = std::min (1.0f, preFade + preFadeInc);
                preG = preFade * preFade * (3.0f - 2.0f * preFade);
                preInL = preG * preInL + (1.0f - preG) * preL.readInt (preOld);
                preInR = preG * preInR + (1.0f - preG) * preR.readInt (preOld);
            }
        }
        else
        {
            preInL = preL.readInt (preSamples);
            preInR = preR.readInt (preSamples);
        }
        float xL = inGate * preInL + shimL, xR = inGate * preInR + shimR;
        for (int k = 0; k < 4; ++k)
        {
            xL = inDiffL[(size_t) k].process (xL, inDiffGain);
            xR = inDiffR[(size_t) k].process (xR, inDiffGain);
        }
        float oL = 0, oR = 0;
        if (prism_detail::isFdn (algo))
        {
            if (dense)
                fdn<16> (xL, xR, oL, oR);
            else
                fdn<8> (xL, xR, oL, oR);
        }
        else if (algo == Spring)
            spring (xL, xR, oL, oR);
        else
            burst (xL, xR, oL, oR);
        if (erLevel > 0.0f)
        {
            float eL = 0, eR = 0;
            for (int k = 0; k < nEr; ++k)
            {
                eL += erGain[(size_t) k] * preL.readInt (preSamples + erTimeL[(size_t) k]);
                eR += erGain[(size_t) k] * preR.readInt (preSamples + erTimeR[(size_t) k]);
            }
            if (transient && preG < 1.0f) // the pre-delay on the move: the old reflections fade out with it
            {
                float oldL = 0, oldR = 0;
                for (int k = 0; k < nEr; ++k)
                {
                    oldL += erGain[(size_t) k] * preL.readInt (preOld + erTimeL[(size_t) k]);
                    oldR += erGain[(size_t) k] * preR.readInt (preOld + erTimeR[(size_t) k]);
                }
                eL = preG * eL + (1.0f - preG) * oldL;
                eR = preG * eR + (1.0f - preG) * oldR;
            }
            erLpL += erLpA * (eL - erLpL);
            erLpR += erLpA * (eR - erLpR);
            eL = erLpL;
            eR = erLpR;
            if (dense)
            {
                eL = erApL[1].process (erApL[0].process (eL, 0.6f * erApG), 0.55f * erApG);
                eR = erApR[1].process (erApR[0].process (eR, 0.6f * erApG), 0.55f * erApG);
            }
            oL += inGate * erLevel * 0.5f * eL;
            oR += inGate * erLevel * 0.5f * eR;
        }
        if (warmGain > 0.0f)
        {
            warmL += warmA * (oL - warmL);
            warmR += warmA * (oR - warmR);
            oL += warmGain * warmL;
            oR += warmGain * warmR;
        }
        oL *= outGain;
        oR *= outGain;

        // shimmer: the wet signal pitch shifted and band limited, heard directly and fed back into the
        // input so every pass climbs (or falls) another interval; a level follower and a soft clip keep
        // the pitched tail from running away
        if (shimmerOn)
        {
            const float inLvl = 0.5f * (std::abs (inL[i]) + std::abs (inR[i]));
            inEnv += (inLvl > inEnv ? envAtk : inRelA) * (inLvl - inEnv);
            const float lvl = 0.5f * (std::abs (shimLpL) + std::abs (shimLpR));
            shimEnv += (lvl > shimEnv ? envAtk : envRel) * (lvl - shimEnv);
            // the pitched tail may grow up to about the input's level, never past it
            const float agc = std::min (1.0f, (0.35f * inEnv + 1.0e-5f) / (shimEnv + 1.0e-5f));
            float sL = shiftL.process (oL), sR = shiftR.process (oR);
            shimHpL += hpA * (sL - shimHpL);
            shimHpR += hpA * (sR - shimHpR);
            sL -= shimHpL;
            sR -= shimHpR;
            shimLp2L += lpA * (sL - shimLp2L);
            shimLp2R += lpA * (sR - shimLp2R);
            shimLpL += lpA * (shimLp2L - shimLpL);
            shimLpR += lpA * (shimLp2R - shimLpR);
            shimL = prism_detail::prismFastTanh (shimLpL * shimGain * agc);
            shimR = prism_detail::prismFastTanh (shimLpR * shimGain * agc);
            oL += shimDirect * shimLpL;
            oR += shimDirect * shimLpR;
        }
        if constexpr (transient)
        {
            // a change of algorithm: 10 ms out, 10 ms in (smoothstep; exactly 1 when not switching)
            switchGain = fadingOut ? std::max (0.0f, switchGain - preFadeInc) : std::min (1.0f, switchGain + preFadeInc);
            const float g = switchGain * switchGain * (3.0f - 2.0f * switchGain);
            outL[i] = oL * g;
            outR[i] = oR * g;
            if (fadingOut && switchGain <= 0.0f)
                return i + 1; // silent: process() switches and goes on with the new algorithm
        }
        else
        {
            outL[i] = oL;
            outR[i] = oR;
        }
    }
    return n;
}
} // namespace bidoo::dsp::reverb

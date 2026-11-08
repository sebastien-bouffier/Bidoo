// The only file that sees Potion's Filter.h: it needs C++20, the plugin builds as C++17 (see the Makefile).
#include "FiltreEngine.hpp"
#include "dep/potion/Filter.h"
#include "dep/pffft/pffft.h"

namespace filtre {

using FT = potion::FilterType;
using CK = CurveKind;

static constexpr double kPi = 3.14159265358979323846;

static const char *const kGroups[] = {"Low-pass", "High-pass", "Band and peak", "Notch, shelf and EQ", "Morphing", "Dual", "Comb and flanger", "Phaser", "Formant", "Effects"};
enum { G_LOW, G_HIGH, G_BAND, G_NOTCH, G_MORPH, G_DUAL, G_COMB, G_PHASER, G_FORMANT, G_FX };

struct Row {
	FT type;
	const char *name;
	int group;
	const char *morph;
	CK curve;
};

static const char *const kSpread = "Spread";
static const char *const kLbhn = "Low > band > high > notch";
static const char *const kLph = "Low > peak > high";
static const char *const kLnh = "Low > notch > high";
static const char *const kBpn = "Band > peak > notch";
static const char *const kVowel = "Vowel A > E > I > O > U";

// Names as in Potion's kFilterNames; Morph as in the comments of Filter.h and VoiceFilters.h.
static const Row kRows[kNumTypes] = {
	{FT::Low6, "Low 6", G_LOW, nullptr, CK::Linear},
	{FT::Low12, "Low 12", G_LOW, nullptr, CK::Linear},
	{FT::Low18, "Low 18", G_LOW, nullptr, CK::Linear},
	{FT::Low24, "Low 24", G_LOW, nullptr, CK::Linear},
	{FT::Ladder24, "Ladder 24", G_LOW, nullptr, CK::SmallSignal},
	{FT::Diode24, "Diode 24", G_LOW, "Bass", CK::SmallSignal},
	{FT::Ota24, "OTA 24", G_LOW, "Poles 4 > 3 > 2 > 1", CK::SmallSignal},
	{FT::SkLow12, "SK Low 12", G_LOW, "Bite", CK::SmallSignal},
	{FT::AddBass, "Add Bass", G_LOW, "Bass, up to +12 dB", CK::SmallSignal},
	{FT::ScreamLow, "Scream Low", G_LOW, "Scream", CK::SmallSignal},

	{FT::High6, "High 6", G_HIGH, nullptr, CK::Linear},
	{FT::High12, "High 12", G_HIGH, nullptr, CK::Linear},
	{FT::High18, "High 18", G_HIGH, nullptr, CK::Linear},
	{FT::High24, "High 24", G_HIGH, nullptr, CK::Linear},
	{FT::SkHigh12, "SK High 12", G_HIGH, "Bite", CK::SmallSignal},

	{FT::Band12, "Band 12", G_BAND, nullptr, CK::Linear},
	{FT::Band24, "Band 24", G_BAND, nullptr, CK::Linear},
	{FT::Peak, "Peak", G_BAND, nullptr, CK::Linear},
	{FT::Peak24, "Peak 24", G_BAND, nullptr, CK::Linear},
	{FT::ScreamBand, "Scream Band", G_BAND, "Scream", CK::SmallSignal},

	{FT::Notch12, "Notch 12", G_NOTCH, nullptr, CK::Linear},
	{FT::Notch24, "Notch 24", G_NOTCH, nullptr, CK::Linear},
	{FT::BandReject, "Band Reject", G_NOTCH, "Width, 0.5 to 6 octaves", CK::Linear},
	{FT::LowShelf, "Low Shelf", G_NOTCH, "Gain, -24 to +24 dB", CK::Linear},
	{FT::HighShelf, "High Shelf", G_NOTCH, "Gain, -24 to +24 dB", CK::Linear},
	{FT::BandEq, "Band EQ", G_NOTCH, "Gain, -24 to +24 dB", CK::Linear},
	{FT::DjFilter, "DJ Filter", G_NOTCH, "Width of the flat zone", CK::Linear},

	{FT::Morph12, "Morph 12", G_MORPH, kLbhn, CK::Linear},
	{FT::Morph24, "Morph 24", G_MORPH, kLbhn, CK::Linear},
	{FT::MorphLph12, "L/P/H 12", G_MORPH, kLph, CK::Linear},
	{FT::MorphLph24, "L/P/H 24", G_MORPH, kLph, CK::Linear},
	{FT::MorphLnh12, "L/N/H 12", G_MORPH, kLnh, CK::Linear},
	{FT::MorphLnh24, "L/N/H 24", G_MORPH, kLnh, CK::Linear},
	{FT::MorphBpn12, "B/P/N 12", G_MORPH, kBpn, CK::Linear},
	{FT::MorphBpn24, "B/P/N 24", G_MORPH, kBpn, CK::Linear},
	{FT::Cmos12, "CMOS 12", G_MORPH, "Low > band > high", CK::SmallSignal},

	{FT::DualLH, "Dual L+H", G_DUAL, kSpread, CK::Linear},
	{FT::DualLB, "Dual L+B", G_DUAL, kSpread, CK::Linear},
	{FT::DualLP, "Dual L+P", G_DUAL, kSpread, CK::Linear},
	{FT::DualLN, "Dual L+N", G_DUAL, kSpread, CK::Linear},
	{FT::DualHB, "Dual H+B", G_DUAL, kSpread, CK::Linear},
	{FT::DualHP, "Dual H+P", G_DUAL, kSpread, CK::Linear},
	{FT::DualHN, "Dual H+N", G_DUAL, kSpread, CK::Linear},
	{FT::DualBP, "Dual B+P", G_DUAL, kSpread, CK::Linear},
	{FT::DualPP, "Dual P+P", G_DUAL, kSpread, CK::Linear},
	{FT::DualPN, "Dual P+N", G_DUAL, kSpread, CK::Linear},
	{FT::DualNN, "Dual N+N", G_DUAL, kSpread, CK::Linear},

	{FT::CombPlus, "Comb +", G_COMB, nullptr, CK::Linear},
	{FT::CombMinus, "Comb -", G_COMB, nullptr, CK::Linear},
	{FT::CombLowPlus, "Comb LP +", G_COMB, "Damp", CK::SmallSignal},
	{FT::CombLowMinus, "Comb LP -", G_COMB, "Damp", CK::SmallSignal},
	{FT::CombHighPlus, "Comb HP +", G_COMB, "Thin", CK::SmallSignal},
	{FT::CombHighMinus, "Comb HP -", G_COMB, "Thin", CK::SmallSignal},
	{FT::CombBandPlus, "Comb BP +", G_COMB, "Focus", CK::SmallSignal},
	{FT::CombBandMinus, "Comb BP -", G_COMB, "Focus", CK::SmallSignal},
	{FT::ChordComb, "Chord Comb", G_COMB, "Unison > chord", CK::SmallSignal},
	{FT::DistComb, "Dist Comb", G_COMB, "Damp", CK::SmallSignal},
	{FT::Flanger, "Flanger", G_COMB, "Feedback + > none > -", CK::Linear},

	{FT::Phaser, "Phaser", G_PHASER, kSpread, CK::SmallSignal},
	{FT::Phaser4Plus, "Phaser 4 +", G_PHASER, kSpread, CK::SmallSignal},
	{FT::Phaser4Minus, "Phaser 4 -", G_PHASER, kSpread, CK::SmallSignal},
	{FT::Phaser8Plus, "Phaser 8 +", G_PHASER, kSpread, CK::SmallSignal},
	{FT::Phaser8Minus, "Phaser 8 -", G_PHASER, kSpread, CK::SmallSignal},
	{FT::Phaser16Plus, "Phaser 16 +", G_PHASER, kSpread, CK::SmallSignal},
	{FT::Phaser16Minus, "Phaser 16 -", G_PHASER, kSpread, CK::SmallSignal},

	{FT::Formant, "Formant", G_FORMANT, kVowel, CK::Linear},
	{FT::FormantF, "Formant F", G_FORMANT, kVowel, CK::Linear},

	{FT::Diffuser, "Diffuser", G_FX, "Density", CK::SmallSignal},
	{FT::Reverb, "Reverb", G_FX, "Damping", CK::Linear},
	{FT::Downsample, "Downsample", G_FX, "Steps > lines", CK::Noise},
	{FT::RingMod, "Ring Mod", G_FX, "Sine > square", CK::Noise},
};

// Rows are in one table so the names, groups and Morph meanings can be read together; the lookups are built here.
static TypeInfo gInfos[kNumTypes];
static int gPositions[kNumTypes];
static const bool gInfosReady = [] {
	for (int i = 0; i < kNumTypes; i++) {
		const Row &r = kRows[i];
		gInfos[i] = {(int)r.type, r.name, kGroups[r.group], r.morph, r.curve};
		gPositions[(int)r.type] = i;
	}
	return true;
}();

const TypeInfo &typeAt(int position) {
	return gInfos[std::clamp(position, 0, kNumTypes - 1)];
}
int positionOfId(int id) {
	return id >= 0 && id < kNumTypes ? gPositions[id] : -1;
}
int groupCount() {
	return (int)(sizeof(kGroups) / sizeof(kGroups[0]));
}
const char *groupName(int group) {
	return kGroups[std::clamp(group, 0, groupCount() - 1)];
}
int groupOfPosition(int position) {
	return kRows[std::clamp(position, 0, kNumTypes - 1)].group;
}

// ---- the filter

struct FilterCore::Impl {
	potion::VoiceFilter f;
	size_t bytes = 0;
};

FilterCore::FilterCore() : impl(new Impl) {
}
FilterCore::~FilterCore() {
	delete impl;
}
void FilterCore::prepare(double sampleRate) {
	impl->f.prepare(sampleRate);
	namespace vf = bidoo::dsp::voicefilters;
	// its two 50 ms delay lines, and per channel the memory shared by the comb, chord comb and reverb families
	impl->bytes = sizeof(Impl) + 2 * ((size_t)(sampleRate / 20.0) + 4) * sizeof(float)
	              + 2 * (size_t)std::max({vf::ChordComb::memoryNeeded(sampleRate), vf::SmallReverb::memoryNeeded(sampleRate),
	                                      vf::FilteredComb<vf::CombLoop::Low, false>::memoryNeeded(sampleRate)})
	                  * sizeof(float);
}
void FilterCore::reset() {
	impl->f.reset();
}
void FilterCore::setup(int id, float cutoffHz, float res, float drive, float morph) {
	impl->f.setup((FT)id, cutoffHz, res, drive, morph);
}
void FilterCore::process(float &l, float &r) {
	impl->f.process(l, r);
}
size_t FilterCore::memoryBytes() const {
	return impl->bytes;
}

// ---- FFT helper

namespace {
struct Fft {
	PFFFT_Setup *setup = nullptr;
	float *in = nullptr, *out = nullptr;
	int n = 0;
	~Fft() {
		clear();
	}
	void clear() {
		if (setup)
			pffft_destroy_setup(setup);
		if (in)
			pffft_aligned_free(in);
		if (out)
			pffft_aligned_free(out);
		setup = nullptr;
		in = out = nullptr;
		n = 0;
	}
	void make(int size) {
		if (size == n)
			return;
		clear();
		n = size;
		setup = pffft_new_setup(n, PFFFT_REAL);
		in = (float *)pffft_aligned_malloc(sizeof(float) * (size_t)n);
		out = (float *)pffft_aligned_malloc(sizeof(float) * (size_t)n);
	}
	// power[0..n/2] of in[0..n-1]
	void power(float *dst) {
		pffft_transform_ordered(setup, in, out, nullptr, PFFFT_FORWARD);
		dst[0] = out[0] * out[0];
		dst[n / 2] = out[1] * out[1];
		for (int k = 1; k < n / 2; k++)
			dst[k] = out[2 * k] * out[2 * k] + out[2 * k + 1] * out[2 * k + 1];
	}
};

inline float toDb(float p) {
	return 10.f * std::log10(std::max(p, 1.0e-20f));
}
}  // namespace

// ---- the curve

struct CurveMeter::Impl {
	double fs = 48000.0;
	FilterCore core;
	Fft fft;
	std::vector<float> irL, irR, powerL, powerR, sum;
	int minLength = 8192, maxLength = 65536;

	void impulse(const CurveSettings &s, Curve &out, int &samples);
	void noise(const CurveSettings &s, Curve &out, int &samples);
	void bands(int length, Curve &out);
};

CurveMeter::CurveMeter() : impl(new Impl) {
}
CurveMeter::~CurveMeter() {
	delete impl;
}

void CurveMeter::prepare(double sampleRate) {
	Impl &m = *impl;
	m.fs = sampleRate;
	m.core.prepare(sampleRate);
	m.minLength = 8192;
	while (m.minLength * 48000.0 < 8192.0 * sampleRate)
		m.minLength *= 2;
	m.maxLength = 8 * m.minLength;
	m.irL.assign((size_t)m.maxLength, 0.f);
	m.irR.assign((size_t)m.maxLength, 0.f);
	m.powerL.assign((size_t)m.maxLength / 2 + 1, 0.f);
	m.powerR.assign((size_t)m.maxLength / 2 + 1, 0.f);
	m.sum.assign((size_t)m.maxLength / 2 + 1, 0.f);
}

void CurveMeter::measure(const CurveSettings &s, Curve &out) {
	out.kind = typeAt(positionOfId(s.id)).curve;
	if (out.kind == CK::Noise)
		impl->noise(s, out, lastSamples);
	else
		impl->impulse(s, out, lastSamples);
}

// An impulse, low enough to stay out of the nonlinearities, until the tail has died out.
void CurveMeter::Impl::impulse(const CurveSettings &s, Curve &out, int &samples) {
	static constexpr float kLevel = 0.01f;
	static constexpr int kChunk = 2048;
	core.setup(s.id, s.cutoff, s.res, s.drive, s.morph);
	core.reset();
	float total = 0.f;
	int n = 0;
	bool dead = false;
	while (n < maxLength && !dead) {
		float chunk = 0.f;
		for (int i = 0; i < kChunk; i++) {
			float l = n == 0 ? kLevel : 0.f, r = l;
			core.process(l, r);
			irL[(size_t)n] = l;
			irR[(size_t)n] = r;
			chunk += l * l + r * r;
			n++;
		}
		total += chunk;
		dead = n >= minLength && chunk <= 1.0e-8f * total;
	}
	samples = n;
	int length = minLength;
	while (length < n)
		length *= 2;
	// cut short (a long comb, a self-oscillating loop): taper the end rather than a step
	if (!dead) {
		const int taper = n / 4;
		for (int i = 0; i < taper; i++) {
			const float w = 0.5f + 0.5f * std::cos((float)kPi * (float)(i + 1) / (float)taper);
			irL[(size_t)(n - taper + i)] *= w;
			irR[(size_t)(n - taper + i)] *= w;
		}
	}
	fft.make(length);
	const float mix = std::clamp(s.mix, 0.f, 1.f), gain = mix / kLevel;
	for (int ch = 0; ch < 2; ch++) {
		const std::vector<float> &ir = ch == 0 ? irL : irR;
		for (int i = 0; i < n; i++)
			fft.in[i] = gain * ir[(size_t)i];
		for (int i = n; i < length; i++)
			fft.in[i] = 0.f;
		fft.in[0] += 1.f - mix;  // the dry path
		fft.power((ch == 0 ? powerL : powerR).data());
	}
	for (int k = 0; k <= length / 2; k++)  // the two channels' mean (they differ in Diffuser and Reverb)
		sum[(size_t)k] = 0.5f * (powerL[(size_t)k] + powerR[(size_t)k]);
	bands(length, out);
}

// From the power of the bins in sum[] to the points of the screen.
void CurveMeter::Impl::bands(int length, Curve &out) {
	const double hzPerBin = fs / length;
	const int bins = length / 2;
	for (int p = 0; p < kPoints; p++) {
		const double lo = bandEdgeHz(p, fs) / hzPerBin, hi = bandEdgeHz(p + 1, fs) / hzPerBin;
		const int k0 = (int)std::ceil(lo), k1 = std::min((int)std::floor(hi), bins);
		if (k1 - k0 < 1) {
			// narrower than a bin: between the two nearest
			const double at = pointHz(p, fs) / hzPerBin;
			const int k = std::min((int)at, bins - 1);
			const float t = (float)(at - k);
			const float v = toDb(sum[(size_t)k] + t * (sum[(size_t)k + 1] - sum[(size_t)k]));
			out.mean[p] = out.low[p] = out.high[p] = v;
		}
		else {
			double acc = 0.0;
			float lowest = sum[(size_t)k0], highest = lowest;
			for (int k = k0; k <= k1; k++) {
				const float v = sum[(size_t)k];
				acc += v;
				lowest = std::min(lowest, v);
				highest = std::max(highest, v);
			}
			out.mean[p] = toDb((float)(acc / (k1 - k0 + 1)));
			out.low[p] = toDb(lowest);
			out.high[p] = toDb(highest);
		}
	}
}

// The types that are not time-invariant (they move energy to other frequencies, so a gain "at this frequency" means
// nothing): a fixed white noise through the filter, output power per bin against the noise's mean power. That is the
// spectrum white noise comes out with, on the same axis as the FFT of the screen.
void CurveMeter::Impl::noise(const CurveSettings &s, Curve &out, int &samples) {
	static constexpr float kLevel = 0.3f;  // uniform, -15 dB rms
	static constexpr int kFrames = 30;
	const int length = minLength, hop = length / 2;
	const float mix = std::clamp(s.mix, 0.f, 1.f);
	core.setup(s.id, s.cutoff, s.res, s.drive, s.morph);
	core.reset();
	fft.make(length);
	std::fill(sum.begin(), sum.begin() + length / 2 + 1, 0.f);
	uint32_t seed = 22222;
	auto next = [&seed] {
		seed = seed * 1664525u + 1013904223u;
		return kLevel * (float)(int32_t)seed / 2147483648.f;
	};
	for (int i = 0; i < hop; i++) {  // settle
		float l = next(), r = l;
		core.process(l, r);
	}
	// irL and irR hold the two channels' last frame
	std::fill(irL.begin(), irL.begin() + length, 0.f);
	std::fill(irR.begin(), irR.begin() + length, 0.f);
	double input = 0.0;
	for (int frame = 0; frame < kFrames; frame++) {
		std::copy(irL.begin() + hop, irL.begin() + length, irL.begin());
		std::copy(irR.begin() + hop, irR.begin() + length, irR.begin());
		for (int i = length - hop; i < length; i++) {
			const float x = next();
			float l = x, r = x;
			core.process(l, r);
			irL[(size_t)i] = x + mix * (l - x);
			irR[(size_t)i] = x + mix * (r - x);
			input += (double)x * x;
		}
		if (frame < 1)
			continue;
		for (int ch = 0; ch < 2; ch++) {
			const std::vector<float> &y = ch == 0 ? irL : irR;
			for (int i = 0; i < length; i++)
				fft.in[i] = y[(size_t)i] * (0.5f - 0.5f * std::cos(2.f * (float)kPi * (float)i / (float)length));
			fft.power(powerL.data());
			for (int k = 0; k <= length / 2; k++)
				sum[(size_t)k] += powerL[(size_t)k];
		}
	}
	samples = hop * (kFrames + 1);
	// white noise of power v through a Hann window: v * length * 3/8 per bin
	const double v = input / (hop * kFrames);
	const float scale = (float)(1.0 / (2.0 * (kFrames - 1) * v * length * 0.375));
	for (int k = 0; k <= length / 2; k++)
		sum[(size_t)k] *= scale;
	bands(length, out);
	std::copy(out.mean, out.mean + kPoints, out.low);
	std::copy(out.mean, out.mean + kPoints, out.high);
}

// ---- the spectrum

struct SpectrumMeter::Impl {
	Fft fft;
	std::vector<float> window, power;
	float smooth[kPoints];
	float scale = 1.f;  // from FFT power to band power, a sine of amplitude 1 = 1
	float k0[kPoints], k1[kPoints];
};

SpectrumMeter::SpectrumMeter() : impl(new Impl) {
	std::fill(impl->smooth, impl->smooth + kPoints, kFloor);
}
SpectrumMeter::~SpectrumMeter() {
	delete impl;
}
void SpectrumMeter::prepare(double sampleRate) {
	Impl &m = *impl;
	const int n = sampleRate > 70000.0 ? 8192 : 4096;
	m.fft.make(n);
	m.window.resize((size_t)n);
	double area = 0.0;
	for (int i = 0; i < n; i++) {
		m.window[(size_t)i] = 0.5f - 0.5f * std::cos(2.f * (float)kPi * (float)i / (float)n);
		area += m.window[(size_t)i];
	}
	// A sine of amplitude 1 gives power (area / 2)^2 at its peak, and a Hann window's main lobe sums to 1.5 times
	// that: taken off, so the sine reads 0 dB once a band has summed its lobe.
	m.scale = (float)(4.0 / (area * area) / 1.5);
	m.power.assign((size_t)n / 2 + 1, 0.f);
	const double hzPerBin = sampleRate / n;
	for (int p = 0; p < kPoints; p++) {
		m.k0[p] = (float)(bandEdgeHz(p, sampleRate) / hzPerBin);
		m.k1[p] = (float)(bandEdgeHz(p + 1, sampleRate) / hzPerBin);
	}
}
int SpectrumMeter::windowSize() const {
	return impl->fft.n;
}
void SpectrumMeter::update(const float *samples, float *db) {
	Impl &m = *impl;
	const int n = m.fft.n, bins = n / 2;
	for (int i = 0; i < n; i++)
		m.fft.in[i] = samples[i] * m.window[(size_t)i];
	m.fft.power(m.power.data());
	for (int p = 0; p < kPoints; p++) {
		const float a = m.k0[p], b = m.k1[p];
		float acc = 0.f;
		const int ka = (int)std::ceil(a), kb = std::min((int)std::floor(b), bins);
		if (kb >= ka) {
			for (int k = ka; k <= kb; k++)
				acc += m.power[(size_t)k];
		}
		else {
			// narrower than a bin (the low end): the bin's power, shared out over the band's width
			const float at = 0.5f * (a + b);
			const int k = std::min((int)at, bins - 1);
			const float t = at - (float)k;
			acc = (m.power[(size_t)k] + t * (m.power[(size_t)k + 1] - m.power[(size_t)k])) * (b - a);
		}
		const float d = std::max(toDb(acc * m.scale), kFloor);
		float &z = m.smooth[p];
		z += (d > z ? 0.4f : 0.12f) * (d - z);
		db[p] = z;
	}
}

}  // namespace filtre

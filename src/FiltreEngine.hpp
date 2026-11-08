// FiLTRe's engine, free of Rack: Potion's VoiceFilter behind a small C++17 interface (Filter.h needs C++20 and is
// compiled alone in FiltreEngine.cpp), a voice that crossfades on a type change, the capture ring between the
// audio and UI threads, the measured curve of the selected filter and the spectrum on the screen.
//
// Levels: the filter works at Potion's level, 1.0 = full scale; the module maps 5 V to 1.0.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace filtre {

constexpr int kNumTypes = 71;
constexpr int kPoints = 256;      // points of the screen's curve and spectra, log spaced

// How the curve of a type is measured, and so what it means.
enum class CurveKind : uint8_t {
	Linear,       // an impulse response: the true transfer function (with Drive at 0)
	SmallSignal,  // an impulse response at a low level: true for quiet signals, loud ones bend it
	Noise         // the type is not time-invariant: the spectrum white noise comes out with
};

struct TypeInfo {
	int id;              // potion::FilterType
	const char *name;    // Potion's name
	const char *group;
	const char *morph;   // what Morph does, nullptr: the type does not use it
	CurveKind curve;
};

// The types in the order of the knob and the menu: related types next to each other. Not Potion's order, which
// is the order they were added in.
const TypeInfo &typeAt(int position);
int positionOfId(int id);  // -1 if unknown
int groupCount();
const char *groupName(int group);
int groupOfPosition(int position);

// One stereo Potion VoiceFilter.
class FilterCore {
public:
	FilterCore();
	~FilterCore();
	FilterCore(const FilterCore &) = delete;
	FilterCore &operator=(const FilterCore &) = delete;
	void prepare(double sampleRate);  // allocates
	void reset();
	void setup(int id, float cutoffHz, float res, float drive, float morph);
	void process(float &l, float &r);
	size_t memoryBytes() const;
private:
	struct Impl;
	Impl *impl;
};

// A voice: the filter, plus a second one to crossfade to when the type changes (the new type starts from silence
// while the old one fades out, which also limits how fast a CV can change the type: one change per crossfade).
// With a steady type the output is exactly the VoiceFilter's.
class Voice {
public:
	void prepare(double sampleRate) {
		fadeLength = std::max(1, (int)(sampleRate * 0.008));
		f[0].prepare(sampleRate);
		f[1].prepare(sampleRate);
		active = 0;
		fade = 0;
		id = -1;
	}
	// to silence, for a voice that was not playing
	void reset() {
		f[active].reset();
		if (fade > 0)
			f[1 - active].reset();
		fade = 0;
	}
	// at control rate
	void set(int newId, float cutoffHz, float res, float drive, float morph) {
		if (newId == id && fade == 0 && cutoffHz == lastCut && res == lastRes && drive == lastDrive && morph == lastMorph)
			return;
		lastCut = cutoffHz;
		lastRes = res;
		lastDrive = drive;
		lastMorph = morph;
		if (id < 0) {
			id = newId;
		}
		else if (newId != id && fade == 0) {
			oldId = id;
			id = newId;
			active = 1 - active;
			f[active].setup(id, cutoffHz, res, drive, morph);
			f[active].reset();
			fade = fadeLength;
		}
		f[active].setup(id, cutoffHz, res, drive, morph);
		if (fade > 0)
			f[1 - active].setup(oldId, cutoffHz, res, drive, morph);
	}
	void process(float &l, float &r) {
		if (fade > 0) {
			float ol = l, or_ = r;
			f[1 - active].process(ol, or_);
			f[active].process(l, r);
			const float a = (float)fade / (float)fadeLength;  // 1 -> 0: how much of the old type
			l += a * (ol - l);
			r += a * (or_ - r);
			fade--;
		}
		else {
			f[active].process(l, r);
		}
	}
	size_t memoryBytes() const {
		return f[0].memoryBytes() + f[1].memoryBytes();
	}
private:
	FilterCore f[2];
	int active = 0, id = -1, oldId = 0, fade = 0, fadeLength = 384;
	float lastCut = 0.f, lastRes = 0.f, lastDrive = 0.f, lastMorph = 0.f;
};

// The latest samples, written by the audio thread and read by the UI: no locks, nothing waits. A reader that is
// lapped gets a torn window for one frame of the display, nothing worse.
class CaptureRing {
public:
	static constexpr uint32_t kSize = 1u << 14;
	CaptureRing() {
		for (auto &s : buf)
			s.store(0.f, std::memory_order_relaxed);
	}
	void push(float x) {
		const uint32_t w = pos.load(std::memory_order_relaxed);
		buf[w & (kSize - 1)].store(x, std::memory_order_relaxed);
		pos.store(w + 1, std::memory_order_release);
	}
	uint32_t written() const {
		return pos.load(std::memory_order_acquire);
	}
	void latest(float *dst, int n) const {
		const uint32_t w = written();
		for (int i = 0; i < n; i++)
			dst[i] = buf[(w - (uint32_t)n + (uint32_t)i) & (kSize - 1)].load(std::memory_order_relaxed);
	}
private:
	std::atomic<float> buf[kSize];
	std::atomic<uint32_t> pos {0};
};

// The screen's frequency axis.
inline double topHz(double fs) {
	return std::min(0.49 * fs, 24000.0);
}
inline double pointHz(int i, double fs) {
	return 20.0 * std::pow(topHz(fs) / 20.0, (double)i / (kPoints - 1));
}
inline double bandEdgeHz(int i, double fs) {  // the low edge of point i's band
	return 20.0 * std::pow(topHz(fs) / 20.0, (i - 0.5) / (kPoints - 1));
}

struct CurveSettings {
	int id = 1;
	float cutoff = 1000.f, res = 0.f, drive = 0.f, morph = 0.f, mix = 1.f;
	// close enough not to measure again
	bool same(const CurveSettings &o) const {
		return id == o.id && std::abs(cutoff - o.cutoff) <= 0.002f * o.cutoff && std::abs(res - o.res) <= 0.002f
		       && std::abs(drive - o.drive) <= 0.002f && std::abs(morph - o.morph) <= 0.002f && std::abs(mix - o.mix) <= 0.002f;
	}
};

// What the screen draws of the curve, in dB per point: the mean power in the point's band, and the lowest and
// highest bin in it (a comb's teeth or a sharp peak can be narrower than a point).
struct Curve {
	float mean[kPoints], low[kPoints], high[kPoints];
	CurveKind kind = CurveKind::Linear;
	Curve() {
		std::fill(mean, mean + kPoints, 0.f);
		std::fill(low, low + kPoints, 0.f);
		std::fill(high, high + kPoints, 0.f);
	}
};

// Measures the curve on a private filter. Runs on the UI's worker thread, never the audio thread.
class CurveMeter {
public:
	struct Impl;
	CurveMeter();
	~CurveMeter();
	CurveMeter(const CurveMeter &) = delete;
	CurveMeter &operator=(const CurveMeter &) = delete;
	void prepare(double sampleRate);  // allocates
	void measure(const CurveSettings &s, Curve &out);
	int lastSamples = 0;  // how long the last measure ran the filter
private:
	Impl *impl;
};

// The band powers of a window of samples, as dB per point: band power, so pink noise reads flat and a sine reads
// its peak level (1.0 = 0 dB).
class SpectrumMeter {
public:
	static constexpr float kFloor = -120.f;
	SpectrumMeter();
	~SpectrumMeter();
	SpectrumMeter(const SpectrumMeter &) = delete;
	SpectrumMeter &operator=(const SpectrumMeter &) = delete;
	void prepare(double sampleRate);  // allocates
	int windowSize() const;
	// samples: windowSize() of them; db: kPoints, smoothed from what they held (fast up, slow down)
	void update(const float *samples, float *db);
private:
	struct Impl;
	Impl *impl;
};

}  // namespace filtre

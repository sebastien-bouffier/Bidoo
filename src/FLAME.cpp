#include "plugin.hpp"
#include "BidooTheme.hpp"
#include "dsp/resampler.hpp"
#include "dsp/filter.hpp"
#include "dep/pffft/pffft.h"
#include "bidoo-dsp/bidoo/dsp/filters/Filters.h"
#include <atomic>
#include <cstring>
#include <iostream>
#include <vector>
#include <algorithm>
#include <limits>
#include "dsp/ringbuffer.hpp"

using namespace std;

// A spectrogram of a stereo input. Drawing a box on it picks a band and a time span. AMP L and R give
// the amplitude of what is inside, per side: a 5 V sine in the box gives 5 V. BAND L and R give the
// input audio filtered to the box's band. The display shows both sides at once: per bin, the RMS of
// left and right, so a mono input looks the same as on one side.

// The box's band as audio: a 48 dB/oct Butterworth high-pass at the box's low edge and low-pass at
// its high edge, each four TPT state variable sections from bidoo-dsp, which stay stable while the
// edges move. No latency. The edges glide (in octaves) so that dragging the box doesn't click; an
// edge that is off (the box touches 0 Hz or Nyquist) glides out of the way and fades to the dry
// signal, then stops computing.
struct FlameBand {
	static constexpr int kSections = 4;      // order 8: 48 dB/oct
	static constexpr double kGlide = 0.020;  // s, time constant of the edges' glide
	static constexpr double kFade = 0.015;   // s, an edge turning on or off, the box appearing or going
	static constexpr int kCoefEvery = 8;     // samples between coefficient updates while an edge moves

	struct Edge {
		bool high = false;                   // high-pass (low edge) or low-pass (high edge)
		double logF = 0.0, logTarget = 0.0;  // log2 of the cutoff
		double mix = 0.0, mixTarget = 0.0;   // 1: filtering, 0: dry
		bool running = false;                // its sections are computed
		bidoo::dsp::TptSvfCoeffs<double> c[kSections];
		bidoo::dsp::TptSvfState<double> s[2][kSections];

		void setCoefs(double sr) {
			const double f = clamp(std::exp2(logF), 1.0, 0.49 * sr);
			const double g = std::tan(M_PI * f / sr);
			for (int i = 0; i < kSections; i++)
				c[i] = bidoo::dsp::TptSvfCoeffs<double>::make(g, 1.0 / bidoo::dsp::design::butterworthQ(2 * kSections, i));
		}
		void reset() {
			for (auto &ch : s)
				for (auto &st : ch)
					st.reset();
		}
		void resetSide(int ch) {
			for (auto &st : s[ch])
				st.reset();
		}
		inline double process(int ch, double x) {
			if (!running)
				return x;
			double y = x;
			for (int i = 0; i < kSections; i++) {
				const bidoo::dsp::TptSvfOut<double> o = s[ch][i].tick(y, c[i]);
				y = high ? o.hp : o.lp;
			}
			return x + mix * (y - x);
		}
	};

	Edge lo, hi;
	double gain = 0.0, gainTarget = 0.0;   // 0 without a box
	double sr = 44100.0, glideA = 0.0, fadeA = 0.0;
	int coefCount = 0;
	bool snap = true;   // the first target is taken at once

	FlameBand() {
		lo.high = true;
		setSampleRate(sr);
	}

	void setSampleRate(double s) {
		sr = s;
		glideA = 1.0 - std::exp(-1.0 / (kGlide * sr));
		fadeA = 1.0 - std::exp(-1.0 / (kFade * sr));
		lo.setCoefs(sr);
		hi.setCoefs(sr);
	}

	// the box's edges in Hz; an edge that is off parks out of the way (5 Hz, or near Nyquist)
	void setTarget(bool on, bool loOn, double fLo, bool hiOn, double fHi) {
		gainTarget = on ? 1.0 : 0.0;
		lo.mixTarget = loOn ? 1.0 : 0.0;
		lo.logTarget = std::log2(loOn ? clamp(fLo, 5.0, 0.49 * sr) : 5.0);
		hi.mixTarget = hiOn ? 1.0 : 0.0;
		hi.logTarget = std::log2(hiOn ? clamp(fHi, 5.0, 0.49 * sr) : 0.49 * sr);
		if (snap) {
			snap = false;
			gain = gainTarget;
			for (Edge *e : {&lo, &hi}) {
				e->logF = e->logTarget;
				e->mix = e->mixTarget;
				e->running = e->mix > 0.0;
				e->setCoefs(sr);
			}
		}
	}

	static inline void follow(double &v, double target, double a, double eps) {
		v += (target - v) * a;
		if (std::fabs(target - v) < eps)
			v = target;
	}

	// one sample of the glides, then the edges' coefficients when they moved
	void step() {
		follow(gain, gainTarget, fadeA, 1e-5);
		bool moved = false;
		for (Edge *e : {&lo, &hi}) {
			if (e->logF != e->logTarget) {
				follow(e->logF, e->logTarget, glideA, 1e-4);
				moved = true;
			}
			follow(e->mix, e->mixTarget, fadeA, 1e-5);
			if (e->mix > 0.0 && !e->running) {
				e->reset();
				e->running = true;
			}
			else if (e->mix == 0.0 && e->running && e->logF == e->logTarget)
				e->running = false;
		}
		// while moving, every kCoefEvery samples; once arrived, the exact target
		if (moved ? ++coefCount >= kCoefEvery : coefCount > 0) {
			coefCount = 0;
			lo.setCoefs(sr);
			hi.setCoefs(sr);
		}
	}

	bool silent() const {
		return gain == 0.0 && gainTarget == 0.0;
	}

	inline float process(int ch, float x) {
		double y = hi.process(ch, lo.process(ch, (double)x)) * gain;
		if (!std::isfinite(y)) {
			lo.reset();
			hi.reset();
			y = 0.0;
		}
		return (float)y;
	}

	void reset() {
		lo.reset();
		hi.reset();
	}

	// a side that was not computed starts from silence
	void resetSide(int ch) {
		lo.resetSide(ch);
		hi.resetSide(ch);
	}
};

struct FLAME : BidooModule {
	enum ParamIds {
		MIN_PARAM,
		MED_PARAM,
		MAX_PARAM,
		YELLOW_PARAM,
		BLUE_PARAM,
		GREEN_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_L_INPUT,     // id 0, the old mono input: older patches keep their cable
		IN_R_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		AMP_L_OUTPUT,   // id 0, the old amplitude output: older patches keep their cable
		AMP_R_OUTPUT,
		BAND_L_OUTPUT,
		BAND_R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		MIN_LIGHT,
		MED_LIGHT,
		MAX_LIGHT,
		YELLOW_LIGHT,
		BLUE_LIGHT,
		GREEN_LIGHT,
		NUM_LIGHTS
	};

	static constexpr int H = 200;       // rows of history, one per hop; the display is H px tall
	static constexpr int MAXN = 2048;
	static constexpr float WIDTH = 130.0f;  // display width in px

	int N = 1024;
	int N2 = 512;

	// everything allocated once: nothing is allocated or freed while running,
	// and the display always reads valid memory
	PFFFT_Setup *setups[3] = {};
	float *work = nullptr, *workOut = nullptr;
	float hann[MAXN];
	float input[2][MAXN] = {};          // left, right (right = left when IN R is not connected)
	int inPos = 0, hop = 0;
	bool stereo = false;                // IN R connected: the right side is analysed on its own
	float mag[2][MAXN / 2 + 2] = {};    // one analysis per side, before they are combined
	// the display's rows: per bin, the RMS of left and right, a ring with the newest at head
	float rows[H][MAXN / 2 + 2] = {};
	// per side, running sums of the squared magnitudes of each row (cum[c][r][k]: bins
	// below k), so a band's energy is one subtraction (double: a quiet band next to a
	// loud one keeps its precision)
	double cum[2][H][MAXN / 2 + 3] = {};
	float rowAmp[2][H] = {};            // per side, amplitude (V) of the band in each row
	int head = 0, filled = 0;
	std::atomic<uint32_t> rowCount {0};  // rows written so far, for the display
	// The live spectrum, for the display's front curve: the newest analysis,
	// refreshed every half frame but at least every LIVE_HOP samples (at size
	// 2048 the history keeps its 1024-sample rows, the live curve gets twice
	// as many). Two buffers: the audio thread writes the one the display is
	// not pointed at, then points the display at it.
	static constexpr int LIVE_HOP = 512;
	float live[2][MAXN / 2 + 2] = {};
	std::atomic<int> liveIdx {0};
	std::atomic<uint32_t> liveCount {0};
	int liveHop = 0;
	// a size asked for by a loaded patch, applied by the audio thread
	std::atomic<int> pendingSize {0};

	// the box, in display px (set by the display)
	float xBox = 0.f, yBox = 0.f, wBox = 0.f, hBox = 0.f;
	std::atomic<bool> boxChanged {true};
	int binLo = 0, binHi = -1;
	float out[2] = {};
	float outSmooth[2] = {};  // out through a 5 ms one-pole: no staircase at the row rate

	FlameBand band;
	float sampleRate = 0.f;

	dsp::SchmittTrigger sizeTriggers[3];
	dsp::SchmittTrigger yellowTrigger, blueTrigger, greenTrigger;
	int colorScheme = 0;

	FLAME() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(MIN_PARAM, "FFT size 512");
		configButton(MED_PARAM, "FFT size 1024");
		configButton(MAX_PARAM, "FFT size 2048");
		configButton(YELLOW_PARAM, "Yellow display");
		configButton(BLUE_PARAM, "Blue display");
		configButton(GREEN_PARAM, "Green display");
		configInput(IN_L_INPUT, "Left audio");
		configInput(IN_R_INPUT, "Right audio (normalled to left)");
		configOutput(BAND_L_OUTPUT, "Left: audio in the box's band");
		configOutput(BAND_R_OUTPUT, "Right: audio in the box's band");
		configOutput(AMP_L_OUTPUT, "Left: amplitude inside the box (a 5 V sine gives 5 V)");
		configOutput(AMP_R_OUTPUT, "Right: amplitude inside the box (a 5 V sine gives 5 V)");
		for (int i = 0; i < 3; i++)
			setups[i] = pffft_new_setup(512 << i, PFFFT_REAL);
		work = (float *)pffft_aligned_malloc(MAXN * sizeof(float));
		workOut = (float *)pffft_aligned_malloc(MAXN * sizeof(float));
		setSize(1024);
	}

	~FLAME() {
		for (PFFFT_Setup *s : setups)
			pffft_destroy_setup(s);
		pffft_aligned_free(work);
		pffft_aligned_free(workOut);
	}

	void setSize(int n) {
		N = n;
		N2 = n / 2;
		for (int k = 0; k < N; k++)
			hann[k] = 0.5f - 0.5f * std::cos(2.0f * M_PI * k / N);
		std::memset(input, 0, sizeof(input));
		std::memset(rows, 0, sizeof(rows));
		std::memset(cum, 0, sizeof(cum));
		std::memset(rowAmp, 0, sizeof(rowAmp));
		std::memset(live, 0, sizeof(live));
		inPos = hop = liveHop = head = filled = 0;
		boxChanged = true;
	}

	bool anyInput() {
		return inputs[IN_L_INPUT].isConnected() || inputs[IN_R_INPUT].isConnected();
	}

	// row of the given age (0: newest), for the display
	const float *row(int age) const {
		return rows[(head - age + H) % H];
	}
	// the band's amplitude of both sides (their RMS), for the display
	float amp(int age) const {
		const int r = (head - age + H) % H;
		return std::sqrt(0.5f * (rowAmp[0][r] * rowAmp[0][r] + rowAmp[1][r] * rowAmp[1][r]));
	}

	// the display's warped frequency axis: px -> bin
	float pxToBin(float px) const {
		return (1.0f - std::pow(1.0f - clamp(px, 0.f, WIDTH) / WIDTH, 0.1f)) * N2;
	}

	// For a Hann window the energy of a sine of amplitude A, summed over its
	// bins, is (3/8) N^2 A^2 / 2 in squared magnitudes 2|X|: A = sqrt(8 E / 3) / N.
	// The input is read at 1/10 (for the display), hence the 10.
	float bandAmp(int c, int row) const {
		if (binHi < binLo)
			return 0.f;
		double e = cum[c][row][binHi + 1] - cum[c][row][binLo];
		return 10.0f * std::sqrt(8.0f * (float)std::max(e, 0.0) / 3.0f) / N;
	}

	// the magnitudes of side c's last N samples, oldest first, into r
	void transform(int c, float *r) {
		const float *in = input[c];
		for (int k = 0; k < N; k++)
			work[k] = in[(inPos + k) % N] * hann[k];
		pffft_transform_ordered(setups[N == 512 ? 0 : (N == 1024 ? 1 : 2)], work, workOut, nullptr, PFFFT_FORWARD);
		r[0] = 2.0f * std::fabs(workOut[0]);
		for (int k = 1; k < N2; k++)
			r[k] = 2.0f * std::sqrt(workOut[2 * k] * workOut[2 * k] + workOut[2 * k + 1] * workOut[2 * k + 1]);
		r[N2] = 2.0f * std::fabs(workOut[1]);  // Nyquist, packed in [1]
	}

	// both sides into mag, and their per-bin RMS into r (mono: the left side as it is)
	void transformBoth(float *r) {
		transform(0, mag[0]);
		if (!stereo) {
			std::memcpy(r, mag[0], (N2 + 1) * sizeof(float));
			return;
		}
		transform(1, mag[1]);
		for (int k = 0; k <= N2; k++)
			r[k] = std::sqrt(0.5f * (mag[0][k] * mag[0][k] + mag[1][k] * mag[1][k]));
	}

	// a new history row (which is also the live spectrum)
	void analyse() {
		head = (head + 1) % H;
		float *r = rows[head];
		transformBoth(r);
		for (int c = 0; c < (stereo ? 2 : 1); c++) {
			double *s = cum[c][head];
			s[0] = 0.0;
			for (int k = 0; k <= N2; k++)
				s[k + 1] = s[k] + (double)mag[c][k] * mag[c][k];
		}
		if (!stereo)   // the right side is the left one
			std::memcpy(cum[1][head], cum[0][head], (N2 + 2) * sizeof(double));
		rowAmp[0][head] = bandAmp(0, head);
		rowAmp[1][head] = stereo ? bandAmp(1, head) : rowAmp[0][head];
		filled = std::min(filled + 1, H);
		rowCount.fetch_add(1, std::memory_order_release);
		publishLive(r);
	}

	// the live spectrum only, between two rows (size 2048)
	void analyseLive() {
		int w = 1 - liveIdx.load(std::memory_order_relaxed);
		transformBoth(live[w]);
		liveIdx.store(w, std::memory_order_release);
		liveCount.fetch_add(1, std::memory_order_release);
	}

	void publishLive(const float *r) {
		int w = 1 - liveIdx.load(std::memory_order_relaxed);
		std::memcpy(live[w], r, (N2 + 1) * sizeof(float));
		liveIdx.store(w, std::memory_order_release);
		liveCount.fetch_add(1, std::memory_order_release);
	}

	// the box moved: a new band for every stored row, and new edges for BAND
	void updateBand() {
		float x0 = std::min(xBox, xBox + wBox), x1 = std::max(xBox, xBox + wBox);
		binLo = clamp((int)std::floor(pxToBin(x0)), 0, N2);
		binHi = clamp((int)std::ceil(pxToBin(x1)), 0, N2);
		if (std::fabs(wBox) < 1.f)
			binHi = binLo - 1;
		for (int c = 0; c < 2; c++)
			for (int i = 0; i < H; i++)
				rowAmp[c][i] = bandAmp(c, i);
		// BAND's edges: the frequencies of the box's first and last bins. A box from bin 0 has
		// no high-pass, one up to Nyquist no low-pass, no box is silence
		const double binHz = (double)sampleRate / N;
		band.setTarget(binHi >= binLo, binLo > 0, binLo * binHz, binHi < N2, binHi * binHz);
	}

	// the box's time span: rows are 1 px tall, the newest at the bottom
	void updateOutput() {
		out[0] = out[1] = 0.f;
		if (std::fabs(wBox) < 1.f || std::fabs(hBox) < 1.f || binHi < binLo)
			return;
		float y0 = std::min(yBox, yBox + hBox), y1 = std::max(yBox, yBox + hBox);
		int ageLo = clamp((int)std::floor(H - y1), 0, H - 1);
		int ageHi = clamp((int)std::ceil(H - y0), 0, H - 1);
		ageHi = std::min(ageHi, filled - 1);
		if (ageHi < ageLo)
			return;
		for (int c = 0; c < 2; c++) {
			float e = 0.f;
			for (int a = ageLo; a <= ageHi; a++) {
				const float v = rowAmp[c][(head - a + H) % H];
				e += v * v;
			}
			out[c] = std::sqrt(e / (ageHi - ageLo + 1));
		}
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "xBox", json_real(xBox));
		json_object_set_new(rootJ, "yBox", json_real(yBox));
		json_object_set_new(rootJ, "wBox", json_real(wBox));
		json_object_set_new(rootJ, "hBox", json_real(hBox));
		json_object_set_new(rootJ, "frameSize", json_integer(N));
		json_object_set_new(rootJ, "colorScheme", json_integer(colorScheme));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		if (json_t *j = json_object_get(rootJ, "xBox")) xBox = json_number_value(j);
		if (json_t *j = json_object_get(rootJ, "yBox")) yBox = json_number_value(j);
		if (json_t *j = json_object_get(rootJ, "wBox")) wBox = json_number_value(j);
		if (json_t *j = json_object_get(rootJ, "hBox")) hBox = json_number_value(j);
		if (json_t *j = json_object_get(rootJ, "colorScheme")) colorScheme = json_number_value(j);
		// the box stays on the display
		xBox = clamp(xBox, 0.f, WIDTH);
		wBox = clamp(xBox + wBox, 0.f, WIDTH) - xBox;
		yBox = clamp(yBox, 0.f, (float)H);
		hBox = clamp(yBox + hBox, 0.f, (float)H) - yBox;
		int n = 1024;
		if (json_t *j = json_object_get(rootJ, "frameSize")) n = json_number_value(j);
		// the audio thread may be running: it makes the change itself
		pendingSize = n == 512 || n == 2048 ? n : 1024;
		boxChanged = true;
	}

	void process(const ProcessArgs &args) override {
		if (pendingSize.load(std::memory_order_relaxed)) {
			int n = pendingSize.exchange(0);
			if (n && n != N)
				setSize(n);
		}
		for (int i = 0; i < 3; i++) {
			if (sizeTriggers[i].process(params[MIN_PARAM + i].getValue()) && N != (512 << i))
				setSize(512 << i);
		}
		lights[MIN_LIGHT].setBrightness(N == 512 ? 1.0f : 0.0f);
		lights[MED_LIGHT].setBrightness(N == 1024 ? 1.0f : 0.0f);
		lights[MAX_LIGHT].setBrightness(N == 2048 ? 1.0f : 0.0f);

		if (yellowTrigger.process(params[YELLOW_PARAM].getValue()))
			colorScheme = 0;
		if (blueTrigger.process(params[BLUE_PARAM].getValue()))
			colorScheme = 1;
		if (greenTrigger.process(params[GREEN_PARAM].getValue()))
			colorScheme = 2;
		lights[YELLOW_LIGHT].setBrightness(colorScheme == 0 ? 1.0f : 0.0f);
		lights[BLUE_LIGHT].setBrightness(colorScheme == 1 ? 1.0f : 0.0f);
		lights[GREEN_LIGHT].setBrightness(colorScheme == 2 ? 1.0f : 0.0f);

		bool changed = boxChanged.load(std::memory_order_relaxed) && boxChanged.exchange(false);
		if (args.sampleRate != sampleRate) {
			sampleRate = args.sampleRate;
			band.setSampleRate(sampleRate);
			changed = true;
		}

		// IN R is normalled to IN L
		const float inL = inputs[IN_L_INPUT].getVoltageSum();
		const bool wasStereo = stereo;
		stereo = inputs[IN_R_INPUT].isConnected();
		if (stereo && !wasStereo)   // the right side's filters were not running: no old state
			band.resetSide(1);
		const float inR = stereo ? inputs[IN_R_INPUT].getVoltageSum() : inL;
		input[0][inPos] = inL * 0.1f;
		input[1][inPos] = inR * 0.1f;
		inPos = (inPos + 1) % N;
		if (changed)
			updateBand();
		// a new row every half frame, the live spectrum at least every LIVE_HOP
		++liveHop;
		if (++hop >= N2) {
			hop = liveHop = 0;
			analyse();
			updateOutput();
		}
		else {
			if (liveHop >= LIVE_HOP) {
				liveHop = 0;
				analyseLive();
			}
			if (changed)
				updateOutput();
		}
		const float a = std::min(1.f, 1.f / (0.005f * args.sampleRate));
		for (int c = 0; c < 2; c++) {
			outSmooth[c] += (out[c] - outSmooth[c]) * a;
			outputs[AMP_L_OUTPUT + c].setVoltage(outSmooth[c]);
		}

		// BAND, computed only while one of its jacks is used
		float bandL = 0.f, bandR = 0.f;
		if (outputs[BAND_L_OUTPUT].isConnected() || outputs[BAND_R_OUTPUT].isConnected()) {
			band.step();
			if (!band.silent()) {
				bandL = band.process(0, inL);
				bandR = stereo ? band.process(1, inR) : bandL;
			}
			else
				band.reset();
		}
		outputs[BAND_L_OUTPUT].setVoltage(bandL);
		outputs[BAND_R_OUTPUT].setVoltage(bandR);
	}
};

// The display's magnitude scale: dB, from FLAME_FLOOR_DB (0) to 0 dBFS (1),
// where 0 dBFS is a 10 V sine (full scale for Rack's audio interface). The
// input is read at 1/10 and Hann-windowed, so such a sine peaks at
// 2|X| = N/2 = N2.
static constexpr float FLAME_FLOOR_DB = -90.0f;
static constexpr float FLAME_ROW_PX = 32.0f;    // a history row's height at 0 dBFS
static constexpr float FLAME_LIVE_PX = 96.0f;   // the live curve's height at 0 dBFS
static constexpr int FLAME_COLS = 130;

// display column -> bin edges (warped axis), for a size
static void flameColumns(float *colBin, int n2) {
	for (int i = 0; i <= FLAME_COLS; i++)
		colBin[i] = (1.0f - std::pow(1.0f - (float)i / FLAME_COLS, 0.1f)) * n2;
}

// one row of magnitudes -> one value per column on the dB scale (0..1): the
// loudest bin under each column, so a partial stays visible where columns
// span many bins
static void flameRowToColumns(const float *r, const float *colBin, int n2, float *out) {
	const float scale = 1.0f / (float)n2;
	for (int i = 0; i < FLAME_COLS; i++) {
		float lo = colBin[i], hi = colBin[i + 1];
		float m = interpolateLinear(r, std::min(lo, (float)n2 - 1.0f));
		for (int k = (int)std::ceil(lo); k < hi && k <= n2; k++)
			m = std::max(m, r[k]);
		float db = 20.0f * std::log10(std::max(m * scale, 1e-9f));
		out[i] = clamp(1.0f - db / FLAME_FLOOR_DB, 0.f, 1.f);
	}
}

// the three schemes, as they always were: a row's colours whiten with age
static void flameColors(int scheme, int age, float alpha, NVGcolor &stroke, NVGcolor &fill) {
	auto c = [age](int v) { return std::min(v + age, 255); };
	int a = (int)clamp(alpha, 0.f, 255.f);
	if (scheme == 1) {
		stroke = nvgRGBA(c(0), c(233), c(255), a);
		fill = nvgRGBA(c(46), c(87), c(228), a);
	}
	else if (scheme == 2) {
		stroke = nvgRGBA(c(150), c(255), c(150), a);
		fill = nvgRGBA(c(46), c(228), c(46), a);
	}
	else {
		stroke = nvgRGBA(c(255), c(233), c(0), a);
		fill = nvgRGBA(c(228), c(87), c(46), a);
	}
}

// The history (a waterfall, newest at the bottom), drawn into a framebuffer
// that is redrawn only when a new row has come in (or the size, colours or
// input change). The newest row is not drawn here: the live curve, drawn on
// top every frame, stands in for it.
struct FLAMESpectrum : Widget {
	FLAME *module = nullptr;
	float colBin[FLAME_COLS + 1];
	int colN2 = -1;
	// each row on the dB scale, per column, worked out once when it comes in
	float cols[FLAME::H][FLAME_COLS] = {};
	uint32_t cachedRows = 0;

	void draw(const DrawArgs &args) override {
		if (!module || !module->anyInput())
			return;
		const int n2 = module->N2;
		uint32_t count = module->rowCount.load(std::memory_order_acquire);
		int fresh = (int)std::min<uint32_t>(count - cachedRows, FLAME::H);
		if (colN2 != n2) {
			colN2 = n2;
			flameColumns(colBin, n2);
			fresh = FLAME::H;
		}
		cachedRows = count;
		const int head = module->head;
		for (int a = 0; a < fresh; a++) {
			int idx = (head - a + FLAME::H) % FLAME::H;
			flameRowToColumns(module->rows[idx], colBin, n2, cols[idx]);
		}

		nvgSave(args.vg);
		nvgShapeAntiAlias(args.vg, false);
		nvgStrokeWidth(args.vg, 1);
		for (int j = FLAME::H - 1; j > 0; j--) {
			const float *c = cols[(head - j + FLAME::H) % FLAME::H];
			nvgBeginPath(args.vg);
			float y = box.size.y * (1.0f - (float)j / (float)FLAME::H);
			nvgMoveTo(args.vg, 0, y);
			for (int i = 0; i < FLAME_COLS; i++)
				nvgLineTo(args.vg, i, y - c[i] * FLAME_ROW_PX);
			nvgLineTo(args.vg, FLAME::WIDTH, y);
			nvgLineTo(args.vg, 0, y);
			nvgClosePath(args.vg);
			// fades faster than it used to: the present dominates, the past is context
			float alpha = 22.0f + 150.0f * std::exp(-(float)j / 32.0f);
			NVGcolor stroke, fill;
			flameColors(module->colorScheme, j, alpha, stroke, fill);
			nvgStrokeColor(args.vg, stroke);
			nvgFillColor(args.vg, fill);
			nvgStroke(args.vg);
			nvgFill(args.vg);
		}
		nvgRestore(args.vg);
	}
};

// Shown in the light layer (it glows in a dark room, as it always did).
struct FLAMEFramebuffer : FramebufferWidget {
	void draw(const DrawArgs &args) override {}
	void drawLayer(const DrawArgs &args, int layer) override {
		if (layer == 1)
			FramebufferWidget::draw(args);
	}
};

struct FLAMEDisplay : OpaqueWidget {
	FLAME *module;
	const float width = 130.0f;
	FLAMEFramebuffer *fb = nullptr;
	FLAMESpectrum *spectrum = nullptr;
	uint32_t lastRows = 0;
	int lastN = -1, lastScheme = -1;
	bool lastConnected = false;

	// the live curve, updated every UI frame: the newest spectrum per column
	// (target), followed with a fast attack and a slower release (level), and
	// a peak that holds, then falls slowly
	float colBin[FLAME_COLS + 1];
	int colN2 = -1;
	float target[FLAME_COLS] = {}, level[FLAME_COLS] = {}, peak[FLAME_COLS] = {}, peakHold[FLAME_COLS] = {};
	uint32_t lastLive = 0;
	double lastTime = -1.0;
	static constexpr float ATTACK = 0.010f;    // s
	static constexpr float RELEASE = 0.140f;   // s
	static constexpr float HOLD = 0.6f;        // s
	static constexpr float FALL = 0.18f;       // of the scale per s (16 dB/s)

	FLAMEDisplay() {

	}

	// called once the display has its size and module
	void setup() {
		fb = new FLAMEFramebuffer();
		fb->box.size = box.size;
		addChild(fb);
		spectrum = new FLAMESpectrum();
		spectrum->module = module;
		spectrum->box.size = box.size;
		fb->addChild(spectrum);
	}

	void advanceLive(float dt) {
		if (!module || !module->anyInput()) {
			std::fill(level, level + FLAME_COLS, 0.f);
			std::fill(peak, peak + FLAME_COLS, 0.f);
			return;
		}
		const int n2 = module->N2;
		uint32_t count = module->liveCount.load(std::memory_order_acquire);
		if (colN2 != n2 || count != lastLive) {
			if (colN2 != n2) {
				colN2 = n2;
				flameColumns(colBin, n2);
			}
			lastLive = count;
			flameRowToColumns(module->live[module->liveIdx.load(std::memory_order_acquire)], colBin, n2, target);
		}
		const float att = 1.0f - std::exp(-dt / ATTACK), rel = 1.0f - std::exp(-dt / RELEASE);
		for (int i = 0; i < FLAME_COLS; i++) {
			level[i] += (target[i] - level[i]) * (target[i] > level[i] ? att : rel);
			if (level[i] >= peak[i]) {
				peak[i] = level[i];
				peakHold[i] = HOLD;
			}
			else if ((peakHold[i] -= dt) < 0.f)
				peak[i] = std::max(level[i], peak[i] - FALL * dt);
		}
	}

	void step() override {
		if (module && fb) {
			bool connected = module->anyInput();
			uint32_t rows = module->rowCount.load(std::memory_order_relaxed);
			if (rows != lastRows || module->N != lastN || module->colorScheme != lastScheme || connected != lastConnected) {
				lastRows = rows;
				lastN = module->N;
				lastScheme = module->colorScheme;
				lastConnected = connected;
				fb->setDirty();
			}
			double now = system::getTime();
			float dt = lastTime < 0.0 ? 0.f : (float)clamp(now - lastTime, 0.0, 0.1);
			lastTime = now;
			advanceLive(dt);
		}
		OpaqueWidget::step();
	}

	// the live curve: a thin peak-hold line, a filled area at full strength
	// and a bright line on it
	void drawLive(const DrawArgs &args) {
		const float bottom = box.size.y;
		NVGcolor stroke, fill;
		flameColors(module->colorScheme, 0, 255.f, stroke, fill);
		nvgShapeAntiAlias(args.vg, true);

		// the peak first: where it meets the curve, the curve stays on top
		nvgBeginPath(args.vg);
		for (int i = 0; i < FLAME_COLS; i++) {
			float y = bottom - peak[i] * FLAME_LIVE_PX - 0.5f;
			if (i == 0)
				nvgMoveTo(args.vg, i + 0.5f, y);
			else
				nvgLineTo(args.vg, i + 0.5f, y);
		}
		nvgStrokeColor(args.vg, nvgRGBA(0, 0, 0, 90));
		nvgStrokeWidth(args.vg, 2.4f);
		nvgStroke(args.vg);
		nvgStrokeColor(args.vg, nvgLerpRGBA(stroke, nvgRGB(255, 255, 255), 0.35f));
		nvgStrokeWidth(args.vg, 1.0f);
		nvgStroke(args.vg);

		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0, bottom);
		for (int i = 0; i < FLAME_COLS; i++)
			nvgLineTo(args.vg, i + 0.5f, bottom - level[i] * FLAME_LIVE_PX);
		nvgLineTo(args.vg, width, bottom);
		nvgClosePath(args.vg);
		NVGcolor fillTop = fill;
		fillTop.a = 0.55f;
		nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0, bottom, 0, bottom - FLAME_LIVE_PX, fill, fillTop));
		nvgFill(args.vg);

		nvgBeginPath(args.vg);
		for (int i = 0; i < FLAME_COLS; i++) {
			float y = bottom - level[i] * FLAME_LIVE_PX;
			if (i == 0)
				nvgMoveTo(args.vg, i + 0.5f, y);
			else
				nvgLineTo(args.vg, i + 0.5f, y);
		}
		// a dark edge under the line keeps it apart from the history behind
		nvgLineJoin(args.vg, NVG_ROUND);
		nvgStrokeColor(args.vg, nvgRGBA(0, 0, 0, 110));
		nvgStrokeWidth(args.vg, 3.6f);
		nvgStroke(args.vg);
		nvgStrokeColor(args.vg, stroke);
		nvgStrokeWidth(args.vg, 1.8f);
		nvgStroke(args.vg);

		nvgShapeAntiAlias(args.vg, false);
	}

	void onButton(const event::Button &e) override {
		if (module && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			e.setTarget(this);
			module->xBox=e.pos.x;
			module->yBox=e.pos.y;
			module->hBox=0;
			module->wBox=0;
			module->boxChanged = true;
		}
	}

	void onDragStart(const event::DragStart &e) override {
		APP->window->cursorLock();
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(const event::DragMove &e) override {
		if (!module)
			return;
		module->hBox+=e.mouseDelta.y;
		if (module->hBox+module->yBox>=module->H) module->hBox=module->H-module->yBox;
		if (module->hBox+module->yBox<=0) module->hBox=-1*module->yBox;
		module->wBox+=e.mouseDelta.x;
		if (module->wBox+module->xBox>=width) module->wBox=width-module->xBox;
		if (module->wBox+module->xBox<=0) module->wBox=-1*module->xBox;
		module->boxChanged = true;
		OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const event::DragEnd &e) override {
		APP->window->cursorUnlock();
		if (module)
			module->boxChanged = true;
		OpaqueWidget::onDragEnd(e);
	}

	// the axes' ink: yellow over a dark halo, so it reads over a bright spectrum too
	void axisTick(const DrawArgs &args, float x0, float y0, float x1, float y1) {
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, x0, y0);
		nvgLineTo(args.vg, x1, y1);
		nvgStrokeWidth(args.vg, 3.f);
		nvgStrokeColor(args.vg, nvgRGBA(0, 0, 0, 200));
		nvgStroke(args.vg);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStrokeColor(args.vg, theme::kScreenYellow);
		nvgStroke(args.vg);
	}

	void axisLabel(const DrawArgs &args, float x, float y, const char *s, int align) {
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (font)
			nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 7.f);
		nvgTextAlign(args.vg, align);
		nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 230));
		nvgFontBlur(args.vg, 1.5f);
		for (int k = 0; k < 3; k++)
			nvgText(args.vg, x, y, s, nullptr);
		nvgFontBlur(args.vg, 0.f);
		nvgFillColor(args.vg, theme::kScreenYellow);
		nvgText(args.vg, x, y, s, nullptr);
	}

	float axisSampleRate() const {
		return module->sampleRate > 0.f ? module->sampleRate : APP->engine->getSampleRate();
	}

	// the history's time scale along the left edge: a row of age j is j hops old, at
	// y = height (1 - j / H); ticks at a round step giving at most eight of them
	void drawTimeAxis(const DrawArgs &args) {
		const float span = (float)module->N2 / axisSampleRate() * FLAME::H;
		static const float steps[] = {0.05f, 0.1f, 0.2f, 0.25f, 0.5f, 1.f, 2.f, 5.f};
		float step = steps[0];
		for (float s : steps) {
			step = s;
			if (span / s <= 8.f)
				break;
		}
		for (int t = 0; t * step <= span; t++) {
			const float y = box.size.y * (1.f - t * step / span);
			axisTick(args, 0.f, y, t == 0 ? 6.f : 4.f, y);
			if (t == 0 || y < 6.f || y > box.size.y - 12.f)
				continue;   // no label for the present, at the very top, or over the frequency scale
			char s[16];
			snprintf(s, sizeof(s), step < 1.f ? "-%.2gs" : "-%.0fs", t * step);
			axisLabel(args, 6.f, y, s, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		}
	}

	// the frequency scale along the bottom: the inverse of pxToBin, where a column x
	// shows the frequency (1 - (1 - x / W)^0.1) sr / 2; round frequencies at least
	// 16 px apart, none against the edges
	void drawFreqAxis(const DrawArgs &args) {
		const float sr = axisSampleRate();
		static const float freqs[] = {50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 3000.f, 5000.f, 10000.f, 20000.f};
		const float bottom = box.size.y;
		float lastX = -1e9f;
		for (float f : freqs) {
			if (f >= 0.5f * sr)
				break;
			const float x = width * (1.f - std::pow(1.f - 2.f * f / sr, 10.f));
			if (x < 14.f || x > width - 8.f || x - lastX < 16.f)
				continue;
			lastX = x;
			axisTick(args, x, bottom, x, bottom - 4.f);
			char s[16];
			if (f >= 1000.f)
				snprintf(s, sizeof(s), "%gk", f / 1000.f);
			else
				snprintf(s, sizeof(s), "%g", f);
			axisLabel(args, x, bottom - 5.f, s, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
		}
	}

	// the live curve's level scale along the right edge: dB from FLAME_FLOOR_DB at the
	// bottom to 0 dBFS (a 10 V sine) FLAME_LIVE_PX above it, every 20 dB; the lowest
	// tick has no label, the frequency scale is there
	void drawDbAxis(const DrawArgs &args) {
		const float bottom = box.size.y;
		for (float db = 0.f; db > FLAME_FLOOR_DB; db -= 20.f) {
			const float y = bottom - (1.f - db / FLAME_FLOOR_DB) * FLAME_LIVE_PX;
			axisTick(args, width - 4.f, y, width, y);
			if (y > bottom - 14.f)
				continue;
			char s[16];
			snprintf(s, sizeof(s), db == 0.f ? "0dB" : "%.0f", db);
			axisLabel(args, width - 6.f, y, s, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		// the spectrogram (framebuffer) first
		Widget::drawLayer(args, layer);
		if (layer == 1 && module) {
			nvgSave(args.vg);
			nvgScissor(args.vg,0.2f,00.2f,width-0.4f,box.size.y-0.4f);
			nvgShapeAntiAlias(args.vg,false);

			if (module->anyInput()) {
				// the live curve over the history, then the box's shading over both
				drawLive(args);
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, width, 0);
				// the band's amplitude per row: 3 px per volt
				for (size_t j=FLAME::H-1; j>0; j--) {
					float y = box.size.y*(1.0f - (float)j/(float)module->H);
					nvgLineTo(args.vg, width - std::min(module->amp(j)*3.0f, width), y);
				}
				nvgLineTo(args.vg, width, box.size.y);
				nvgLineTo(args.vg, width, 0);
				nvgClosePath(args.vg);
				nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 120));
				nvgFill(args.vg);
			}

			nvgBeginPath(args.vg);
			nvgRect(args.vg, module->xBox, module->yBox, module->wBox, module->hBox);
			nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 80));
			nvgFill(args.vg);

			drawTimeAxis(args);
			drawFreqAxis(args);
			drawDbAxis(args);

			nvgResetScissor(args.vg);
			nvgRestore(args.vg);
		}
	}

};


struct FLAMEWidget : BidooWidget {
	FLAMEWidget(FLAME *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/FLAME.svg"));

		theme::addScrews(this);

		// The flame screen keeps the old panel's top left corner, made shorter (FLAME::H) to leave room for six jacks.
		FLAMEDisplay *display = new FLAMEDisplay();
		display->module = module;
		display->box = theme::specificRect(10, 28, 130, FLAME::H);
		display->setup();
		addChild(display);

		// The six buttons in one row on the 6-column grid, the FFT sizes then the colours, each under its label, with
		// its light at the old offset inside. The row sits one box gap above the jack boxes.
		const float w = box.size.x;
		const theme::Size kButtonSize = {17.9996f, 18.f};                 // LEDButton
		const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};     // SmallLight
		const float kLightIn = 6.f;      // from a button's corner to its light's
		const float kButtonRowY = theme::boxTop(1) - theme::kBoxGap - kButtonSize.h * 0.5f;
		const int buttonIds[6] = {FLAME::MIN_PARAM, FLAME::MED_PARAM, FLAME::MAX_PARAM, FLAME::YELLOW_PARAM, FLAME::BLUE_PARAM, FLAME::GREEN_PARAM};
		const int lightIds[6] = {FLAME::MIN_LIGHT, FLAME::MED_LIGHT, FLAME::MAX_LIGHT, FLAME::YELLOW_LIGHT, FLAME::BLUE_LIGHT, FLAME::GREEN_LIGHT};
		for (int i = 0; i < 6; i++) {
			const theme::Pos button = theme::sameCol(theme::at(i, 6, 0, w), kButtonRowY);
			const theme::Pos light = theme::specificCorner(button.x - kButtonSize.w * 0.5f + kLightIn, button.y - kButtonSize.h * 0.5f + kLightIn, kSmallLightSize);
			addParam(theme::param<LEDButton>(button, module, buttonIds[i]));
			addChild(theme::light<SmallLight<BlueLight>>(light, module, lightIds[i]));
		}

		// The jacks: three boxed columns on the grid (IN, BAND, AMP), left above right in one tall box per column
		// (rows 1 to 0). The two captions and two jacks share the box's height with five equal gaps, so
		// the rows are specific (the panel's spread.py uses the same arithmetic).
		const float kCapHeight = 1493.f / 2048.f * theme::kBoxLabelSize;   // DejaVu's capitals
		const float kGap = (theme::boxBottom(0) - theme::boxTop(1) - 2.f * (kCapHeight + theme::kJackSize.h)) / 5.f;
		const float kLeftY = theme::boxTop(1) + 2.f * kGap + kCapHeight + theme::kJackSize.h * 0.5f;
		const float kRightY = kLeftY + 2.f * kGap + kCapHeight + theme::kJackSize.h;
		auto at = [&](float i, float y) { return theme::sameCol(theme::jackAt(i, 3, 0, w), y); };   // each in the middle of its box
		addInput(theme::input<PJ301MPort>(at(0, kLeftY), module, FLAME::IN_L_INPUT));
		addInput(theme::input<PJ301MPort>(at(0, kRightY), module, FLAME::IN_R_INPUT));
		addOutput(theme::output<PJ301MPort>(at(1, kLeftY), module, FLAME::BAND_L_OUTPUT));
		addOutput(theme::output<PJ301MPort>(at(1, kRightY), module, FLAME::BAND_R_OUTPUT));
		addOutput(theme::output<PJ301MPort>(at(2, kLeftY), module, FLAME::AMP_L_OUTPUT));
		addOutput(theme::output<PJ301MPort>(at(2, kRightY), module, FLAME::AMP_R_OUTPUT));
	}
};

Model *modelFLAME = createModel<FLAME, FLAMEWidget>("fLAME");

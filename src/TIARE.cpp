#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/filter.hpp"
#include "dsp/fir.hpp"
#include "dsp/window.hpp"

using namespace std;

using simd::float_4;

// Oscillator running at 2x the sample rate. The phase is distorted by a
// two-segment line through (X, Y); every jump of the saw and square (and of all
// waves on hard sync) gets a minBLEP at its exact time, found on the
// undistorted phase. Oversampling takes care of the corners phase distortion
// puts in the waves.

// Accurate only on [0, 1]
template <typename T>
T sin2pi_pade_05_5_4(T x) {
	x -= 0.5f;
	return (T(-6.283185307) * x + T(33.19863968) * simd::pow(x, 3) - T(32.44191367) * simd::pow(x, 5))
	       / (1 + T(1.296008659) * simd::pow(x, 2) + T(0.7028072946) * simd::pow(x, 4));
}

template <typename T>
T expCurve(T x) {
	return (3 + x * (-13 + 5 * x)) / (3 + 2 * x);
}

enum TiareWave { TIARE_SIN, TIARE_TRI, TIARE_SAW, TIARE_SQR, TIARE_WAVES };

// Waves of a distorted phase in [0, 1), for one lane (float) or four (float_4).
template <typename T>
T tiareWave(int wave, T phase, bool analog, T pulseWidth) {
	switch (wave) {
		case TIARE_SIN: {
			if (analog) {
				// Quadratic approximation of sine, slightly richer harmonics
				auto halfPhase = (phase < 0.5f);
				T x = phase - simd::ifelse(halfPhase, T(0.25f), T(0.75f));
				return (1.f - 16.f * x * x) * simd::ifelse(halfPhase, T(1.f), T(-1.f));
			}
			return sin2pi_pade_05_5_4(phase);
		}
		case TIARE_TRI: {
			if (analog) {
				T x = phase + 0.25f;
				x -= simd::trunc(x);
				auto halfX = (x >= 0.5f);
				x *= 2;
				x -= simd::trunc(x);
				return expCurve(x) * simd::ifelse(halfX, T(1.f), T(-1.f));
			}
			return 1 - 4 * simd::fmin(simd::fabs(phase - 0.25f), simd::fabs(phase - 1.25f));
		}
		case TIARE_SAW: {
			T x = phase + 0.5f;
			x -= simd::trunc(x);
			return analog ? -expCurve(x) : 2 * x - 1;
		}
		default:
			return simd::ifelse(phase < pulseWidth, T(1.f), T(-1.f));
	}
}

// Distorted phase: a line from (0, 0) to (x, y), then to (1, 1).
inline float tiareDistort(float p, float x, float y) {
	return p <= x ? p * y / x : y + (p - x) * (1.f - y) / (1.f - x);
}

// Inverse of tiareDistort.
inline float tiareUndistort(float d, float x, float y) {
	return d <= y ? d * x / y : x + (d - y) * (1.f - x) / (1.f - y);
}

// Same, for four lanes at once.
inline float_4 tiareDistort(float_4 p, float_4 x, float_4 y) {
	return simd::ifelse(p <= x, p * y / x, y + (p - x) * (1.f - y) / (1.f - x));
}

inline float_4 tiareUndistort(float_4 d, float_4 x, float_4 y) {
	return simd::ifelse(d <= y, d * x / y, x + (d - y) * (1.f - x) / (1.f - y));
}

// 2x decimator: windowed-sinc FIR on a doubled ring buffer, no modulo per tap.
template <typename T>
struct TiareDecimator {
	static const int TAPS = 32;
	float kernel[TAPS];
	T buffer[2 * TAPS];
	int pos = 0;

	TiareDecimator() {
		dsp::boxcarLowpassIR(kernel, TAPS, 0.9f * 0.5f / 2.f);
		dsp::blackmanHarrisWindow(kernel, TAPS);
		float sum = 0.f;
		for (float k : kernel) sum += k;
		for (float &k : kernel) k /= sum;
		for (T &b : buffer) b = 0.f;
	}

	void push(T x) {
		buffer[pos] = x;
		buffer[pos + TAPS] = x;
		pos = (pos + 1) % TAPS;
	}

	T process(T a, T b) {
		push(a);
		push(b);
		const T *newest = &buffer[pos + TAPS - 1];
		T out = 0.f;
		for (int i = 0; i < TAPS; i++)
			out += kernel[i] * newest[-i];
		return out;
	}
};

template <typename T>
struct TiareOscillator {
	bool analog = false;
	bool soft = false;
	bool syncEnabled = false;
	bool lfo = false;
	int channels = 0;

	T phase = 0.f;
	T freq = 0.f;
	T pulseWidth = 0.5f;
	T distX = 0.5f;
	T distY = 0.5f;
	T syncDirection = 1.f;
	T lastSync = 0.f;

	dsp::MinBlepGenerator<16, 16, T> blep[TIARE_WAVES];
	TiareDecimator<T> decimator[TIARE_WAVES];
	dsp::TRCFilter<T> sqrFilter;
	T out[TIARE_WAVES];

	void setPitch(T pitch, int factor) {
		lfo = factor != 1;
		freq = dsp::FREQ_C4 * dsp::approxExp2_taylor5(pitch + 30) / 1073741824 / factor;
	}

	void setPulseWidth(T pw) {
		pulseWidth = simd::clamp(pw, 0.01f, 0.99f);
	}

	float laneWave(int wave, int i, float p) {
		p -= std::floor(p);
		return tiareWave<float>(wave, tiareDistort(p, distX[i], distY[i]), analog, pulseWidth[i]);
	}

	// minBLEPs for the jumps met while lane i moves from p over span (one
	// direction, no wrap of more than one cycle). t0 and tLen place this
	// segment inside the oversampled step.
	void scanJumps(int i, float p, float span, float t0, float tLen, int waves) {
		if (span == 0.f)
			return;
		const float eps = 1e-5f;
		T mask = simd::movemaskInverse<T>(1 << i);
		// Phase of each jump: saw at distorted 0.5, square at pulse width and at 0.
		float thresholds[3] = {tiareUndistort(0.5f, distX[i], distY[i]), tiareUndistort(pulseWidth[i], distX[i], distY[i]), 0.f};
		int owners[3] = {TIARE_SAW, TIARE_SQR, TIARE_SQR};
		for (int k = 0; k < 3; k++) {
			int w = owners[k];
			if (!(waves & (1 << w)))
				continue;
			float th = thresholds[k];
			float delta = span > 0.f ? th - p : p - th;
			delta -= std::floor(delta);
			if (delta == 0.f || delta > std::fabs(span))
				continue;
			float before = laneWave(w, i, th - (span > 0.f ? eps : -eps));
			float after = laneWave(w, i, th + (span > 0.f ? eps : -eps));
			float when = t0 + tLen * delta / std::fabs(span);
			blep[w].insertDiscontinuity(when - 1.f, mask & T(after - before));
		}
	}

	// Same as scanJumps for the lanes in laneMask, all moving by their whole
	// deltaPhase: crossings are found for the 4 lanes at once, and only the
	// lanes that do jump are handled one by one.
	void scanJumpsLanes(T p, T span, int laneMask, int waves) {
		const float eps = 1e-5f;
		T forward = span > 0.f;
		T dist = simd::fabs(span);
		T thresholds[3] = {tiareUndistort(T(0.5f), distX, distY), tiareUndistort(pulseWidth, distX, distY), T(0.f)};
		int owners[3] = {TIARE_SAW, TIARE_SQR, TIARE_SQR};
		for (int k = 0; k < 3; k++) {
			int w = owners[k];
			if (!(waves & (1 << w)))
				continue;
			T delta = simd::ifelse(forward, thresholds[k] - p, p - thresholds[k]);
			delta -= simd::floor(delta);
			int hits = simd::movemask((delta > 0.f) & (delta <= dist)) & laneMask;
			for (int i = 0; hits; i++, hits >>= 1) {
				if (!(hits & 1))
					continue;
				float th = thresholds[k][i];
				float e = span[i] > 0.f ? eps : -eps;
				float jump = laneWave(w, i, th + e) - laneWave(w, i, th - e);
				blep[w].insertDiscontinuity(delta[i] / dist[i] - 1.f, simd::movemaskInverse<T>(1 << i) & T(jump));
			}
		}
	}

	// waves: bit mask of the outputs in use.
	void process(float sampleTime, T syncValue, int waves) {
		float dt = sampleTime * 0.5f;
		T sub[TIARE_WAVES][2];
		T syncStart = lastSync;
		lastSync = syncValue;

		for (int s = 0; s < 2; s++) {
			T syncFrom = syncStart + (syncValue - syncStart) * (s * 0.5f);
			T syncTo = syncStart + (syncValue - syncStart) * ((s + 1) * 0.5f);

			T deltaPhase = simd::clamp(freq * dt, 1e-7f, 0.35f);
			if (soft)
				deltaPhase *= syncDirection;
			else
				syncDirection = 1.f;

			int syncMask = 0;
			T syncCrossing = 0.f;
			if (syncEnabled) {
				// Rising through 0 V, including from a gate resting at exactly 0 V.
				T sync = (syncFrom <= 0.f) & (syncTo > 0.f);
				syncMask = simd::movemask(sync) & ((1 << channels) - 1);
				syncCrossing = simd::clamp(-syncFrom / simd::fmax(syncTo - syncFrom, 1e-9f), 1e-6f, 1.f);
			}

			T newPhase = phase + deltaPhase;
			int laneMask = (1 << channels) - 1;
			scanJumpsLanes(phase, deltaPhase, laneMask & ~syncMask, waves);
			for (int i = 0; syncMask >> i; i++) {
				if (!(syncMask & (1 << i)))
					continue;
				float p = phase[i];
				float d = deltaPhase[i];
				{
					float sc = syncCrossing[i];
					if (soft) {
						// Soft sync: the phase turns back at the crossing.
						scanJumps(i, p, d * sc, 0.f, sc, waves);
						scanJumps(i, p + d * sc, -d * (1.f - sc), sc, 1.f - sc, waves);
						syncDirection[i] = -syncDirection[i];
						newPhase[i] = p + d * sc - d * (1.f - sc);
					}
					else {
						// Hard sync: jump to the start of the cycle.
						scanJumps(i, p, d * sc, 0.f, sc, waves);
						float at = p + d * sc;
						float restart = (1.f - sc) * d;
						T mask = simd::movemaskInverse<T>(1 << i);
						for (int w = 0; w < TIARE_WAVES; w++) {
							if (waves & (1 << w))
								blep[w].insertDiscontinuity(sc - 1.f, mask & T(laneWave(w, i, 0.f) - laneWave(w, i, at)));
						}
						scanJumps(i, 0.f, restart, sc, 1.f - sc, waves);
						newPhase[i] = restart;
					}
				}
			}
			phase = newPhase - simd::floor(newPhase);

			T distorted = tiareDistort(phase, distX, distY);

			for (int w = 0; w < TIARE_WAVES; w++) {
				if (waves & (1 << w))
					sub[w][s] = tiareWave<T>(w, distorted, analog, pulseWidth) + blep[w].process();
			}
		}

		for (int w = 0; w < TIARE_WAVES; w++) {
			if (waves & (1 << w))
				out[w] = decimator[w].process(sub[w][0], sub[w][1]);
		}

		// Analog square: AC-coupled like a real VCO, but not in LFO mode.
		if (analog && !lfo && (waves & (1 << TIARE_SQR))) {
			sqrFilter.setCutoffFreq(20.f * sampleTime);
			sqrFilter.process(out[TIARE_SQR]);
			out[TIARE_SQR] = sqrFilter.highpass() * 0.95f;
		}
	}
};

struct TIARE : BidooModule {
	enum ParamIds {
		FREQ_PARAM,
		FINE_PARAM,
		MODE_PARAM,
		SYNC_PARAM,
		FM_PARAM,
		PW_PARAM,
		PWM_PARAM,
		DIST_X_PARAM,
		DIST_Y_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PITCH_INPUT,
		SYNC_INPUT,
		DIST_X_INPUT,
		DIST_Y_INPUT,
		FM_INPUT,
		PW_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		SIN_OUTPUT,
		TRI_OUTPUT,
		SAW_OUTPUT,
		SQR_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	static constexpr float DIST_MIN = 0.01f;
	static constexpr float DIST_MAX = 0.99f;

	int freqFactor = 1;
	// Phase distortion point of channel 1, for the display.
	float displayX = 0.5f;
	float displayY = 0.5f;
	TiareOscillator<float_4> oscillators[4];

	TIARE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(MODE_PARAM, 0.f, 1.f, 1.f, "Engine mode", {"Digital", "Analog"});
		configSwitch(SYNC_PARAM, 0.f, 1.f, 1.f, "Sync mode", {"Soft", "Hard"});
		configParam(FREQ_PARAM, -54.f, 54.f, 0.f, "Frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
		configParam(FINE_PARAM, -1.f, 1.f, 0.f, "Fine frequency");
		configParam(FM_PARAM, 0.f, 1.f, 0.f, "Frequency modulation", "%", 0.f, 100.f);
		configParam(PW_PARAM, 0.01f, 0.99f, 0.5f, "Pulse width", "%", 0.f, 100.f);
		configParam(PWM_PARAM, 0.f, 1.f, 0.f, "Pulse width modulation", "%", 0.f, 100.f);
		configParam(DIST_X_PARAM, DIST_MIN, DIST_MAX, 0.5f, "Phase distortion X", "%", 0.f, 100.f);
		configParam(DIST_Y_PARAM, DIST_MIN, DIST_MAX, 0.5f, "Phase distortion Y", "%", 0.f, 100.f);
		configInput(PITCH_INPUT, "1V/oct pitch");
		configInput(FM_INPUT, "Frequency modulation");
		configInput(SYNC_INPUT, "Sync");
		configInput(PW_INPUT, "Pulse width modulation (scaled by PWM, 10 V: the whole range)");
		configInput(DIST_X_INPUT, "Phase distortion X (0-10 V, replaces the slider)");
		configInput(DIST_Y_INPUT, "Phase distortion Y (0-10 V, replaces the slider)");
		configOutput(SIN_OUTPUT, "Sine");
		configOutput(TRI_OUTPUT, "Triangle");
		configOutput(SAW_OUTPUT, "Sawtooth");
		configOutput(SQR_OUTPUT, "Square");
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "freqFactor", json_integer(freqFactor));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *freqFactorJ = json_object_get(rootJ, "freqFactor");
		if (freqFactorJ) {
			freqFactor = json_integer_value(freqFactorJ) == 100 ? 100 : 1;
		}
	}

	float_4 distortion(int input, int param, int c) {
		if (inputs[input].isConnected()) {
			float_4 v = simd::clamp(inputs[input].getPolyVoltageSimd<float_4>(c), 0.f, 10.f);
			return DIST_MIN + v * ((DIST_MAX - DIST_MIN) / 10.f);
		}
		return params[param].getValue();
	}

	void process(const ProcessArgs &args) override {
		float freqParam = params[FREQ_PARAM].getValue() / 12.f;
		freqParam += dsp::quadraticBipolar(params[FINE_PARAM].getValue()) * 3.f / 12.f;
		float fmParam = dsp::quadraticBipolar(params[FM_PARAM].getValue());

		int channels = std::max(inputs[PITCH_INPUT].getChannels(), 1);
		int waves = 0;
		for (int w = 0; w < TIARE_WAVES; w++)
			if (outputs[SIN_OUTPUT + w].isConnected())
				waves |= 1 << w;

		for (int c = 0; c < channels; c += 4) {
			auto* oscillator = &oscillators[c / 4];
			oscillator->channels = std::min(channels - c, 4);
			oscillator->analog = params[MODE_PARAM].getValue() > 0.f;
			oscillator->soft = params[SYNC_PARAM].getValue() <= 0.f;

			float_4 pitch = freqParam;
			pitch += inputs[PITCH_INPUT].getVoltageSimd<float_4>(c);
			if (inputs[FM_INPUT].isConnected()) {
				pitch += fmParam * inputs[FM_INPUT].getPolyVoltageSimd<float_4>(c);
			}
			oscillator->setPitch(pitch, freqFactor);
			oscillator->setPulseWidth(params[PW_PARAM].getValue() + params[PWM_PARAM].getValue() * inputs[PW_INPUT].getPolyVoltageSimd<float_4>(c) / 10.f);
			oscillator->distX = distortion(DIST_X_INPUT, DIST_X_PARAM, c);
			oscillator->distY = distortion(DIST_Y_INPUT, DIST_Y_PARAM, c);
			if (c == 0) {
				displayX = oscillator->distX[0];
				displayY = oscillator->distY[0];
			}

			oscillator->syncEnabled = inputs[SYNC_INPUT].isConnected();
			oscillator->process(args.sampleTime, inputs[SYNC_INPUT].getPolyVoltageSimd<float_4>(c), waves);

			for (int w = 0; w < TIARE_WAVES; w++) {
				if (waves & (1 << w))
					outputs[SIN_OUTPUT + w].setVoltageSimd(5.f * oscillator->out[w], c);
			}
		}

		for (int w = 0; w < TIARE_WAVES; w++)
			outputs[SIN_OUTPUT + w].setChannels(channels);
	}
};

struct TIAREDisplay : TransparentWidget {
	TIARE *module;
	const float size = 140.f;

	TIAREDisplay() {}

	void setFromMouse(Vec pos) {
		if (!module->inputs[TIARE::DIST_X_INPUT].isConnected())
			module->paramQuantities[TIARE::DIST_X_PARAM]->setValue(clamp(pos.x / size, 0.f, 1.f));
		if (!module->inputs[TIARE::DIST_Y_INPUT].isConnected())
			module->paramQuantities[TIARE::DIST_Y_PARAM]->setValue(clamp(1.f - pos.y / size, 0.f, 1.f));
	}

	// Mouse position in the display, followed here rather than read back from
	// the module, whose display values only update on the next audio sample.
	Vec dragPos;

	void onButton(const event::Button &e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			e.consume(this);
			dragPos = e.pos;
			setFromMouse(dragPos);
		}
	}

	void onDragMove(const event::DragMove &e) override {
		// Follow the mouse, whatever the zoom.
		dragPos = dragPos.plus(e.mouseDelta.div(getAbsoluteZoom()));
		setFromMouse(dragPos);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x80));
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, 0, size / 2);
			nvgLineTo(args.vg, size, size / 2);
			nvgMoveTo(args.vg, size / 2, 0);
			nvgLineTo(args.vg, size / 2, size);
			nvgStroke(args.vg);

			// Distortion curve through (X, Y)
			float px = module->displayX * size;
			float py = (1.f - module->displayY) * size;
			nvgStrokeColor(args.vg, nvgRGBA(42, 87, 117, 255));
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, 0, size);
			nvgLineTo(args.vg, px, py);
			nvgLineTo(args.vg, size, 0);
			nvgStroke(args.vg);
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, px, py, 2.5f);
			nvgFillColor(args.vg, nvgRGBA(42, 87, 117, 255));
			nvgFill(args.vg);
		}
		Widget::drawLayer(args, layer);
	}
};

// Thin slider along an edge of the display: a real param widget, so X and Y
// can be typed in, MIDI-mapped and automated. Its marker is placed as the
// display places the curve point (the value across the display's length), so
// both line up whatever the slider's own length and offset.
struct TiareAxisSlider : app::SliderKnob {
	float span = 140.f;   // the display's length
	float offset = 0.f;   // where the slider starts, from the display's start

	void draw(const DrawArgs& args) override {
		engine::ParamQuantity *pq = getParamQuantity();
		float v = pq ? pq->getValue() : 0.5f;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 0x40));
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		if (horizontal)
			nvgRect(args.vg, v * span - offset - 1.5f, 0, 3.f, box.size.y);
		else
			nvgRect(args.vg, 0, (1.f - v) * span - offset - 1.5f, box.size.x, 3.f);
		nvgFillColor(args.vg, nvgRGBA(42, 87, 117, 255));
		nvgFill(args.vg);
	}
};

struct moduleModeItem : MenuItem {
	TIARE *module;
	void onAction(const event::Action &e) override {
		module->freqFactor = module->freqFactor == 1 ? 100 : 1;
	}
};

struct TIAREWidget : BidooWidget {

	TIAREWidget(TIARE *module) {

		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/TIARE.svg"));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		if (module) {
			TIAREDisplay *display = new TIAREDisplay();
			display->module = module;
			display->box.pos = Vec(5, 119);
			display->box.size = Vec(140, 140);
			addChild(display);
		}

		TiareAxisSlider *sliderX = createParam<TiareAxisSlider>(Vec(5 + 8, 119 + 140 - 7), module, TIARE::DIST_X_PARAM);
		sliderX->box.size = Vec(140 - 8, 7);
		sliderX->horizontal = true;
		sliderX->offset = 8.f;
		addParam(sliderX);
		TiareAxisSlider *sliderY = createParam<TiareAxisSlider>(Vec(5, 119), module, TIARE::DIST_Y_PARAM);
		sliderY->box.size = Vec(7, 140 - 8);
		addParam(sliderY);

		addParam(createParam<CKSS>(Vec(30, 80), module, TIARE::MODE_PARAM));
		addParam(createParam<CKSS>(Vec(134, 80), module, TIARE::SYNC_PARAM));

		addParam(createParam<BidooLargeBlueKnob>(Vec(72, 45), module, TIARE::FREQ_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(129, 45), module, TIARE::FINE_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(33, 45), module, TIARE::FM_PARAM));

		addInput(createInput<TinyPJ301MPort>(Vec(53, 83), module, TIARE::FM_INPUT));
		// pulse width, for the square: PW, its modulation amount and input
		addParam(createParam<BidooBlueTrimpot>(Vec(153.5f, 135), module, TIARE::PW_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(153.5f, 174), module, TIARE::PWM_PARAM));
		addInput(createInput<PJ301MPort>(Vec(150.5f, 228), module, TIARE::PW_INPUT));

		addInput(createInput<PJ301MPort>(Vec(15.36f, 283), module, TIARE::PITCH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(56.79f, 283), module, TIARE::SYNC_INPUT));
		addInput(createInput<PJ301MPort>(Vec(98.71f, 283), module, TIARE::DIST_X_INPUT));
		addInput(createInput<PJ301MPort>(Vec(140.14f, 283), module, TIARE::DIST_Y_INPUT));

		addOutput(createOutput<PJ301MPort>(Vec(15.36f, 330), module, TIARE::SIN_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(56.79f, 330), module, TIARE::TRI_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(98.71f, 330), module, TIARE::SAW_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(140.14f, 330), module, TIARE::SQR_OUTPUT));
	}

	void appendContextMenu(Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		menu->addChild(new MenuSeparator());
		moduleModeItem *modeItem = new moduleModeItem;
		modeItem->text = "Mode: ";
		modeItem->rightText = dynamic_cast<TIARE*>(this->module)->freqFactor == 1 ? "OSC✔ LFO" : "OSC  LFO✔";
		modeItem->module = dynamic_cast<TIARE*>(this->module);
		menu->addChild(modeItem);
	}
};

Model *modelTIARE = createModel<TIARE, TIAREWidget>("TiARE");

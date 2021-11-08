#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/resampler.hpp"
#include "dsp/filter.hpp"

using namespace std;

using simd::float_4;


// Accurate only on [0, 1]
template <typename T>
T sin2pi_pade_05_7_6(T x) {
	x -= 0.5f;
	return (T(-6.28319) * x + T(35.353) * simd::pow(x, 3) - T(44.9043) * simd::pow(x, 5) + T(16.0951) * simd::pow(x, 7))
	       / (1 + T(0.953136) * simd::pow(x, 2) + T(0.430238) * simd::pow(x, 4) + T(0.0981408) * simd::pow(x, 6));
}

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


template <int OVERSAMPLE, int QUALITY, typename T>
struct Oscillator {
	bool analog = false;
	bool soft = false;
	bool syncEnabled = false;
	// For optimizing in serial code
	int channels = 0;

	T lastSyncValue = 0.f;
	T phase = 0.f;
	T phaseDist = 0.f;
	T freq;
	T pulseWidth = 0.5f;
	T syncDirection = 1.f;
	int lfoFactor = 1;

	dsp::TRCFilter<T> sqrFilter;

	dsp::MinBlepGenerator<QUALITY, OVERSAMPLE, T> sqrMinBlep;
	dsp::MinBlepGenerator<QUALITY, OVERSAMPLE, T> sawMinBlep;
	dsp::MinBlepGenerator<QUALITY, OVERSAMPLE, T> triMinBlep;
	dsp::MinBlepGenerator<QUALITY, OVERSAMPLE, T> sinMinBlep;

	T sqrValue = 0.f;
	T lastSqrValue = 0.f;
	T sawValue = 0.f;
	T lastSawValue = 0.f;
	T triValue = 0.f;
	T lastTriValue = 0.f;
	T sinValue = 0.f;
	T lastSinValue = 0.f;

	void setPitch(T pitch, int factor) {
		lfoFactor = factor;
		freq = dsp::FREQ_C4 * dsp::approxExp2_taylor5(pitch + 30) / 1073741824 / factor;
	}

	void setPulseWidth(T pulseWidth) {
		const float pwMin = 0.01f;
		this->pulseWidth = simd::clamp(pulseWidth, pwMin, 1.f - pwMin);
	}

	void process(float deltaTime, T syncValue, float phaseDistX, float phaseDistY) {
		// Advance phase
		T deltaPhase = simd::clamp(freq * deltaTime, 1e-6f, 0.35f);

		if (soft) {
			// Reverse direction
			deltaPhase *= syncDirection;
		}
		else {
			// Reset back to forward
			syncDirection = 1.f;
		}

		// Wrap phase
		phase += deltaPhase;
		phase -= simd::floor(phase);

		for (int i=0; i<channels; i++) {
			if (phase[i] <= phaseDistX)
					phaseDist[i] = phase[i] * phaseDistY / phaseDistX;
				else
					phaseDist[i] = phaseDistY + (phase[i]-phaseDistX) * (1.0f - phaseDistY) / (1.0f - phaseDistX);
		}


		// Wrap phase
		phaseDist -= simd::floor(phaseDist);


		if (lfoFactor == 1) {

			T wrapPhase = (syncDirection == -1.f) & 1.f;
			T wrapCrossing = (wrapPhase - (phaseDist - deltaPhase)) / deltaPhase;
			int wrapMask = simd::movemask((0 < wrapCrossing) & (wrapCrossing <= 1.f));
			if (wrapMask) {
				for (int i = 0; i < channels; i++) {
					if (wrapMask & (1 << i)) {
						T mask = simd::movemaskInverse<T>(1 << i);
						float p = wrapCrossing[i] - 1.f;
						T x = mask & (2.f * syncDirection);
						sqrMinBlep.insertDiscontinuity(p, x);
					}
				}
			}

			T pulseCrossing = (pulseWidth - (phaseDist - deltaPhase)) / deltaPhase;
			int pulseMask = simd::movemask((0 < pulseCrossing) & (pulseCrossing <= 1.f));
			if (pulseMask) {
				for (int i = 0; i < channels; i++) {
					if (pulseMask & (1 << i)) {
						T mask = simd::movemaskInverse<T>(1 << i);
						float p = pulseCrossing[i] - 1.f;
						T x = mask & (-2.f * syncDirection);
						sqrMinBlep.insertDiscontinuity(p, x);
					}
				}
			}

				T halfCrossing = (0.5f - (phaseDist - deltaPhase)) / deltaPhase;
			int halfMask = simd::movemask((0 < halfCrossing) & (halfCrossing <= 1.f));
			if (halfMask) {
				for (int i = 0; i < channels; i++) {
					if (halfMask & (1 << i)) {
						T mask = simd::movemaskInverse<T>(1 << i);
						float p = halfCrossing[i] - 1.f;
						T x = mask & (-2.f * syncDirection);
						sawMinBlep.insertDiscontinuity(p, x);
					}
				}
			}

			if (syncEnabled) {
				T deltaSync = syncValue - lastSyncValue;
				T syncCrossing = -lastSyncValue / deltaSync;
				lastSyncValue = syncValue;
				T sync = (0.f < syncCrossing) & (syncCrossing <= 1.f) & (syncValue >= 0.f);
				int syncMask = simd::movemask(sync);
				if (syncMask) {
					if (soft) {
						syncDirection = simd::ifelse(sync, -syncDirection, syncDirection);
					}
					else {
						T newPhase = simd::ifelse(sync, (1.f - syncCrossing) * deltaPhase, phase);
						// Insert minBLEP for sync
						for (int i = 0; i < channels; i++) {
							if (syncMask & (1 << i)) {
								T mask = simd::movemaskInverse<T>(1 << i);
								float p = syncCrossing[i] - 1.f;
								T x;
								x = mask & (sqr(newPhase) - sqr(phaseDist));
								sqrMinBlep.insertDiscontinuity(p, x);
								x = mask & (saw(newPhase) - saw(phaseDist));
								sawMinBlep.insertDiscontinuity(p, x);
								x = mask & (tri(newPhase) - tri(phaseDist));
								triMinBlep.insertDiscontinuity(p, x);
								x = mask & (sin(newPhase) - sin(phaseDist));
								sinMinBlep.insertDiscontinuity(p, x);
							}
						}
						phase = newPhase;
					}
				}
			}
		}

		sqrValue = sqr(phaseDist);

		if (lfoFactor == 1) {
			T deltaOut = sqrValue - lastSqrValue;
	    T outCrossing = -lastSqrValue / deltaOut;
	    lastSqrValue = sqrValue;
	    int outMask =  simd::movemask((0.f < outCrossing) & (outCrossing <= 1.f) & (sqrValue >= 0.f));
	    if (outMask) {
	      for (int i = 0; i < channels; i++) {
	        if (outMask & (1 << i)) {
	          T mask = simd::movemaskInverse<T>(1 << i);
	          float p = outCrossing[i] - 1.f;
	          T x;
	          x = mask & (sqr(phaseDist+deltaPhase-simd::floor(phaseDist+deltaPhase)) - sqr(phaseDist));
	          sqrMinBlep.insertDiscontinuity(p, x);
	        }
	      }
	    }
		}

		sqrValue += sqrMinBlep.process();

		if (analog) {
			sqrFilter.setCutoffFreq(20.f * deltaTime);
			sqrFilter.process(sqrValue);
			sqrValue = sqrFilter.highpass() * 0.95f;
		}

		sawValue = saw(phaseDist);

		if (lfoFactor == 1) {
			T deltaOut = sawValue - lastSawValue;
	    T outCrossing = -lastSawValue / deltaOut;
	    lastSawValue = sawValue;
	    int outMask =  simd::movemask((0.f < outCrossing) & (outCrossing <= 1.f) & (sawValue >= 0.f));
	    if (outMask) {
	      for (int i = 0; i < channels; i++) {
	        if (outMask & (1 << i)) {
	          T mask = simd::movemaskInverse<T>(1 << i);
	          float p = outCrossing[i] - 1.f;
	          T x;
	          x = mask & (saw(phaseDist+deltaPhase-simd::floor(phaseDist+deltaPhase)) - saw(phaseDist));
	          sawMinBlep.insertDiscontinuity(p, x);
	        }
	      }
	    }
		}

		sawValue += sawMinBlep.process();

		triValue = tri(phaseDist);

		if (lfoFactor == 1) {
			T deltaOut = triValue - lastTriValue;
			T outCrossing = -lastTriValue / deltaOut;
			lastTriValue = triValue;
			int outMask =  simd::movemask((0.f < outCrossing) & (outCrossing <= 1.f) & (triValue >= 0.f));
			if (outMask) {
				for (int i = 0; i < channels; i++) {
					if (outMask & (1 << i)) {
						T mask = simd::movemaskInverse<T>(1 << i);
						float p = outCrossing[i] - 1.f;
						T x;
						x = mask & (tri(phaseDist+deltaPhase-simd::floor(phaseDist+deltaPhase)) - tri(phaseDist));
						triMinBlep.insertDiscontinuity(p, x);
					}
				}
			}
		}

		triValue += triMinBlep.process();

		sinValue = sin(phaseDist);

		if (lfoFactor == 1) {
			T deltaOut = sinValue - lastSinValue;
			T outCrossing = -lastSinValue / deltaOut;
			lastSinValue = sinValue;
			int outMask =  simd::movemask((0.f < outCrossing) & (outCrossing <= 1.f) & (sinValue >= 0.f));
			if (outMask) {
				for (int i = 0; i < channels; i++) {
					if (outMask & (1 << i)) {
						T mask = simd::movemaskInverse<T>(1 << i);
						float p = outCrossing[i] - 1.f;
						T x;
						x = mask & (sin(phaseDist+deltaPhase-simd::floor(phaseDist+deltaPhase)) - sin(phaseDist));
						sinMinBlep.insertDiscontinuity(p, x);
					}
				}
			}
		}

		sinValue += sinMinBlep.process();
	}

	T sin(T phase) {
		T v;
		if (analog) {
			// Quadratic approximation of sine, slightly richer harmonics
			T halfPhase = (phase < 0.5f);
			T x = phase - simd::ifelse(halfPhase, 0.25f, 0.75f);
			v = 1.f - 16.f * simd::pow(x, 2);
			v *= simd::ifelse(halfPhase, 1.f, -1.f);
		}
		else {
			v = sin2pi_pade_05_5_4(phase);
			// v = sin2pi_pade_05_7_6(phase);
			// v = simd::sin(2 * T(M_PI) * phase);
		}
		return v;
	}
	T sin() {
		return sinValue;
	}

	T tri(T phase) {
		T v;
		if (analog) {
			T x = phase + 0.25f;
			x -= simd::trunc(x);
			T halfX = (x >= 0.5f);
			x *= 2;
			x -= simd::trunc(x);
			v = expCurve(x) * simd::ifelse(halfX, 1.f, -1.f);
		}
		else {
			v = 1 - 4 * simd::fmin(simd::fabs(phase - 0.25f), simd::fabs(phase - 1.25f));
		}
		return v;
	}
	T tri() {
		return triValue;
	}

	T saw(T phase) {
		T v;
		T x = phase + 0.5f;
		x -= simd::trunc(x);
		if (analog) {
			v = -expCurve(x);
		}
		else {
			v = 2 * x - 1;
		}
		return v;
	}
	T saw() {
		return sawValue;
	}

	T sqr(T phase) {
		T v = simd::ifelse(phase < pulseWidth, 1.f, -1.f);
		return v;
	}
	T sqr() {
		return sqrValue;
	}

	T light() {
		return simd::sin(2 * T(M_PI) * phase);
	}
};

struct TIARE : Module {
	enum ParamIds {
		FREQ_PARAM,
		FINE_PARAM,
		MODE_PARAM,
		SYNC_PARAM,
		FM_PARAM,
		PW_PARAM,
		PWM_PARAM,
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

	float phaseDist = 0.0f;
	float phaseDistX = 0.5f, phaseDistY = 0.5f;
	int freqFactor = 1;
	Oscillator<16, 16, float_4> oscillators[4];

	TIARE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(MODE_PARAM, 0.f, 1.f, 1.f, "Engine mode", {"Digital", "Analog"});
		configSwitch(SYNC_PARAM, 0.f, 1.f, 1.f, "Sync mode", {"Soft", "Hard"});
		configParam(FREQ_PARAM, -54.f, 54.f, 0.f, "Frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
		configParam(FINE_PARAM, -1.f, 1.f, 0.f, "Fine frequency");
		configParam(FM_PARAM, 0.f, 1.f, 0.f, "Frequency modulation", "%", 0.f, 100.f);
		configParam(PW_PARAM, 0.01f, 0.99f, 0.5f, "Pulse width", "%", 0.f, 100.f);
		configParam(PWM_PARAM, 0.f, 1.f, 0.f, "Pulse width modulation", "%", 0.f, 100.f);
		configInput(PITCH_INPUT, "1V/oct pitch");
		configInput(FM_INPUT, "Frequency modulation");
		configInput(SYNC_INPUT, "Sync");
		configInput(PW_INPUT, "Pulse width modulation");
		configOutput(SIN_OUTPUT, "Sine");
		configOutput(TRI_OUTPUT, "Triangle");
		configOutput(SAW_OUTPUT, "Sawtooth");
		configOutput(SQR_OUTPUT, "Square");
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "phaseDistX", json_real(phaseDistX));
		json_object_set_new(rootJ, "phaseDistY", json_real(phaseDistY));
		json_object_set_new(rootJ, "freqFactor", json_integer(freqFactor));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *phaseDistXJ = json_object_get(rootJ, "phaseDistX");
		if (phaseDistXJ) {
			phaseDistX = json_number_value(phaseDistXJ);
		}
		json_t *phaseDistYJ = json_object_get(rootJ, "phaseDistY");
		if (phaseDistYJ) {
			phaseDistY = json_number_value(phaseDistYJ);
		}
		json_t *freqFactorJ = json_object_get(rootJ, "freqFactor");
		if (freqFactorJ) {
			freqFactor = json_integer_value(freqFactorJ);
		}
	}

	void onRandomize() override {
		if (!inputs[TIARE::DIST_X_INPUT].isConnected()) {
			phaseDistX = random::uniform();
		}
		if (!inputs[TIARE::DIST_Y_INPUT].isConnected()) {
			phaseDistY = random::uniform();
		}
	}

	void onReset() override {
		if (!inputs[TIARE::DIST_X_INPUT].isConnected()) {
			phaseDistX = 0.5f;
		}
		if (!inputs[TIARE::DIST_Y_INPUT].isConnected()) {
			phaseDistY = 0.5f;
		}
	}

	void process(const ProcessArgs &args) override {
		if (inputs[DIST_X_INPUT].isConnected())
			phaseDistX = rescale(clamp(inputs[DIST_X_INPUT].getVoltage(), 0.0f, 10.0f), 0.0f, 10.0f, 0.01f, 0.98f);

		if (inputs[DIST_Y_INPUT].isConnected())
			phaseDistY = rescale(clamp(inputs[DIST_Y_INPUT].getVoltage(), 0.0f, 10.0f), 0.0f, 10.0f, 0.01f, 0.98f);

		float freqParam = params[FREQ_PARAM].getValue() / 12.f;
		freqParam += dsp::quadraticBipolar(params[FINE_PARAM].getValue()) * 3.f / 12.f;
		float fmParam = dsp::quadraticBipolar(params[FM_PARAM].getValue());

		int channels = std::max(inputs[PITCH_INPUT].getChannels(), 1);

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

			oscillator->syncEnabled = inputs[SYNC_INPUT].isConnected();
			oscillator->process(args.sampleTime, inputs[SYNC_INPUT].getPolyVoltageSimd<float_4>(c), phaseDistX, phaseDistY);

			// Set output
			if (outputs[SIN_OUTPUT].isConnected())
				outputs[SIN_OUTPUT].setVoltageSimd(5.f * oscillator->sin(), c);
			if (outputs[TRI_OUTPUT].isConnected())
				outputs[TRI_OUTPUT].setVoltageSimd(5.f * oscillator->tri(), c);
			if (outputs[SAW_OUTPUT].isConnected())
				outputs[SAW_OUTPUT].setVoltageSimd(5.f * oscillator->saw(), c);
			if (outputs[SQR_OUTPUT].isConnected())
				outputs[SQR_OUTPUT].setVoltageSimd(5.f * oscillator->sqr(), c);
		}

		outputs[SIN_OUTPUT].setChannels(channels);
		outputs[TRI_OUTPUT].setChannels(channels);
		outputs[SAW_OUTPUT].setChannels(channels);
		outputs[SQR_OUTPUT].setChannels(channels);
	}
};

struct TIAREDisplay : TransparentWidget {
	TIARE *module;
	float initX = 0.0f;
	float initY = 0.0f;
	float dragX = 0.0f;
	float dragY = 0.0f;

	TIAREDisplay() {}

	void onDragStart(const event::DragStart &e) override {
		dragX = APP->scene->rack->getMousePos().x;
		dragY = APP->scene->rack->getMousePos().y;
	}

	void onDragMove(const event::DragMove &e) override {
		if (!module->inputs[TIARE::DIST_X_INPUT].isConnected()) {
			float newDragX = APP->scene->rack->getMousePos().x;
			module->phaseDistX = rescale(clamp(initX + (newDragX - dragX), 0.0f, 140.0f), 0.0f, 140.0f, 0.01f, 0.98f);
		}
		if (!module->inputs[TIARE::DIST_Y_INPUT].isConnected()) {
			float newDragY = APP->scene->rack->getMousePos().y;
			module->phaseDistY = rescale(clamp(initY - (newDragY - dragY), 0.0f, 140.0f), 0.0f, 140.0f, 0.01f, 1.0f);
		}
	}

	void onButton(const event::Button &e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			e.setTarget(this);
			initX = e.pos.x;
			initY = 140.0f - e.pos.y;
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x80));
			{
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0, 70);
				nvgLineTo(args.vg, 140, 70);
				nvgMoveTo(args.vg, 70, 0);
				nvgLineTo(args.vg, 70, 140);
				nvgClosePath(args.vg);
			}
			nvgStroke(args.vg);

			// Draw phase distortion
			nvgStrokeColor(args.vg, nvgRGBA(42, 87, 117, 255));
			{
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0, 140);
				nvgLineTo(args.vg, (int)(rescale(module->phaseDistX, 0, 1, 0, 140)), 140 - (int)(rescale(module->phaseDistY, 0, 1, 0.01, 140)));
				nvgMoveTo(args.vg, (int)(rescale(module->phaseDistX, 0, 1, 0, 140)), 140 - (int)(rescale(module->phaseDistY, 0, 1, 0.01, 140)));
				nvgLineTo(args.vg, 140, 0);
				nvgClosePath(args.vg);
			}
			nvgStroke(args.vg);
		}
		Widget::drawLayer(args, layer);
	}

};

struct moduleModeItem : MenuItem {
	TIARE *module;
	void onAction(const event::Action &e) override {
		module->freqFactor = module->freqFactor == 1 ? 100 : 1;
	}
};

struct TIAREWidget : ModuleWidget {

	TIAREWidget(TIARE *module) {

		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/TIARE.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		if (module) {
			{
				TIAREDisplay *display = new TIAREDisplay();
				display->module = module;
				display->box.pos = Vec(5, 119);
				display->box.size = Vec(140, 140);
				addChild(display);
			}
		}

		addParam(createParam<CKSS>(Vec(15, 80), module, TIARE::MODE_PARAM));
		addParam(createParam<CKSS>(Vec(119, 80), module, TIARE::SYNC_PARAM));

		addParam(createParam<BidooLargeBlueKnob>(Vec(57, 45), module, TIARE::FREQ_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(114, 45), module, TIARE::FINE_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(18, 45), module, TIARE::FM_PARAM));

		addInput(createInput<TinyPJ301MPort>(Vec(38, 83), module, TIARE::FM_INPUT));
		addInput(createInput<PJ301MPort>(Vec(7, 283), module, TIARE::PITCH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(44, 283), module, TIARE::SYNC_INPUT));
		addInput(createInput<PJ301MPort>(Vec(81.5f, 283), module, TIARE::DIST_X_INPUT));
		addInput(createInput<PJ301MPort>(Vec(118.5f, 283), module, TIARE::DIST_Y_INPUT));

		addOutput(createOutput<PJ301MPort>(Vec(7, 330), module, TIARE::SIN_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(44, 330), module, TIARE::TRI_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(81.5f, 330), module, TIARE::SAW_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(118.5f, 330), module, TIARE::SQR_OUTPUT));
	}

	void appendContextMenu(Menu *menu) override {
		menu->addChild(new MenuEntry);
		moduleModeItem *modeItem = new moduleModeItem;
		modeItem->text = "Mode: ";
		modeItem->rightText = dynamic_cast<TIARE*>(this->module)->freqFactor == 1 ? "OSC✔ LFO" : "OSC  LFO✔";
		modeItem->module = dynamic_cast<TIARE*>(this->module);
		menu->addChild(modeItem);
	}
};

Model *modelTIARE = createModel<TIARE, TIAREWidget>("TiARE");

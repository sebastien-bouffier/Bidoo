#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "Svf.hpp"
#include "dep/filters/pitchshifter.h"

#define BUFF_SIZE 2048

using namespace std;

struct HCTIP : BidooModule {
	enum ParamIds {
		PITCH_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		PITCH_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	PitchShifter *pShifter = nullptr;

	HCTIP() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PITCH_PARAM, -12.f, 12.f, 0.f, "Pitch", " semitones");
		configInput(INPUT, "Audio");
		configInput(PITCH_INPUT, "Pitch CV (1V/oct)");
		configOutput(OUTPUT, "Audio");
		configBypass(INPUT, OUTPUT);
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		delete pShifter;
		pShifter = new PitchShifter();
		pShifter->init(BUFF_SIZE, 8, e.sampleRate);
		pShifter->phaseLock = true;
	}

	// the phase-locked shifter's gain per semitone from -12 to +24, measured
	// on sines from 110 Hz to 1760 Hz (within 3% across them): divided out
	static float levelFix(float semis) {
		static const float gain[37] = {
			1.791f, 1.919f, 2.018f, 2.101f, 2.191f, 2.250f, 2.359f, 2.381f, 2.562f, 2.658f, 2.739f, 2.763f,
			2.997f,
			2.853f, 2.920f, 3.027f, 3.053f, 3.113f, 3.190f, 3.185f, 3.270f, 3.296f, 3.322f, 3.341f, 3.437f,
			3.393f, 3.399f, 3.424f, 3.452f, 3.455f, 3.472f, 3.494f, 3.494f, 3.501f, 3.511f, 3.531f, 3.563f
		};
		float x = clamp(semis, -12.f, 24.f) + 12.f;
		int i = std::min((int)x, 35);
		return 1.0f / (gain[i] + (gain[i + 1] - gain[i]) * (x - i));
	}

	void process(const ProcessArgs &args) override {
		if (!pShifter)
			return;
		float semis = params[PITCH_PARAM].getValue() + inputs[PITCH_INPUT].getVoltage() * 12.f;
		semis = clamp(semis, -12.f, 24.f);
		float ratio = dsp::exp2_taylor5(semis / 12.f);
		// one 256-sample hop of FFT work at a time instead of eight at once
		float y = pShifter->step(ratio, inputs[INPUT].getVoltage() * 0.2f);
		outputs[OUTPUT].setVoltage(bidooSoftLimit(y * 5.0f * levelFix(semis)));
	}

	~HCTIP() {
		delete pShifter;
	}
};

struct HCTIPWidget : BidooWidget {
	HCTIPWidget(HCTIP *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/HCTIP.svg"));

		addParam(createParam<BidooBlueKnob>(Vec(8, 70), module, HCTIP::PITCH_PARAM));
		addInput(createInput<PJ301MPort>(Vec(10, 130.f), module, HCTIP::PITCH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10, 283.0f), module, HCTIP::INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10, 330), module, HCTIP::OUTPUT));
	}
};

Model *modelHCTIP = createModel<HCTIP, HCTIPWidget>("HCTIP");

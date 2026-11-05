#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"
#include "gverb.h"
#include "dep/gverb/src/gverb.c"
#include "dep/gverb/src/gverbdsp.c"

using namespace std;

struct DFUZE : BidooModule {
	enum ParamIds {
		SIZE_PARAM,
		REVTIME_PARAM,
		DAMP_PARAM,
		FREEZE_PARAM,
		BANDWIDTH_PARAM,
		EARLYLEVEL_PARAM,
		TAIL_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_INPUT,
		SIZE_INPUT,
		REVTIME_INPUT,
		DAMP_INPUT,
		FREEZE_INPUT,
		BANDWIDTH_INPUT,
		EARLYLEVEL_INPUT,
		TAIL_INPUT,
		IN_R_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		FREEZE_LIGHT,
		NUM_LIGHTS
	};


	// one gverb per side
	ty_gverb *verb[2] = {nullptr, nullptr};
	float size = -1.f;
	float applied[6] = {-1.f, -1.f, -1.f, -1.f, -1.f, -1.f};

	// FREEZE: gverb has no freeze mode, so its tail is held by an endless
	// reverb time and no damping while the input fades out (10 ms)
	dsp::SchmittTrigger freezeTrigger;
	bool freeze = false;
	float inGain = 1.f;

	DFUZE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SIZE_PARAM, 1.0f, 300.0f, 30.0f, "Size", " m");
		configParam(REVTIME_PARAM, 0.0f, 50.0f, 0.5f, "Reverb time", " s");
		configParam(DAMP_PARAM, 0.0f, 0.9f, 0.5f, "Damping", "%", 0.f, 100.f / 0.9f);
		configParam(BANDWIDTH_PARAM, 0.0f, 1.0f, 0.5f, "Input bandwidth", "%", 0.f, 100.f);
		configParam(EARLYLEVEL_PARAM, 0.0f, 10.0f, 5.0f, "Early reflections level", "%", 0.f, 10.f);
		configParam(TAIL_PARAM, 0.0f, 10.0f, 5.0f, "Tail level", "%", 0.f, 10.f);
		configInput(IN_INPUT, "Left (polyphonic inputs are summed)");
		configInput(IN_R_INPUT, "Right (follows left when unpatched)");
		configInput(SIZE_INPUT, "Size CV (0-10 V: +0-300 m)");
		configInput(REVTIME_INPUT, "Reverb time CV (0-10 V: +0-50 s)");
		configInput(DAMP_INPUT, "Damping CV (0-10 V: full range)");
		configInput(BANDWIDTH_INPUT, "Input bandwidth CV (0-10 V: full range)");
		configInput(EARLYLEVEL_INPUT, "Early reflections level CV (0-10 V: full range)");
		configInput(TAIL_INPUT, "Tail level CV (0-10 V: full range)");
		configButton(FREEZE_PARAM, "Freeze");
		configInput(FREEZE_INPUT, "Freeze toggle trigger");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		build(48000.f);
	}

	~DFUZE() {
		for (ty_gverb *v : verb)
			if (v)
				gverb_free(v);
	}

	// gverb's delays are counted in samples: rebuilt for each sample rate
	void build(float sampleRate) {
		for (int i = 0; i < 2; i++) {
			if (verb[i])
				gverb_free(verb[i]);
			verb[i] = gverb_new((int)sampleRate, 300, 1, 1, 1, 1, 1, 1, 1);
		}
		size = -1.f;
		for (float &a : applied)
			a = -1.f;
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		build(e.sampleRate);
	}

	void onReset() override {
		freeze = false;
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "freeze", json_boolean(freeze));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		freeze = json_is_true(json_object_get(rootJ, "freeze"));
	}

	void process(const ProcessArgs &args) override;
};

void DFUZE::process(const ProcessArgs &args) {
	if (freezeTrigger.process(params[FREEZE_PARAM].getValue() + inputs[FREEZE_INPUT].getVoltage()))
		freeze = !freeze;
	lights[FREEZE_LIGHT].setBrightness(freeze ? 1.f : 0.f);
	float fade = 1.f / (0.01f * args.sampleRate);
	inGain = freeze ? std::max(inGain - fade, 0.f) : std::min(inGain + fade, 1.f);

	// the size glides (50 ms) so a moving size bends the tail instead of clicking
	float sizeTarget = clamp(params[SIZE_PARAM].getValue() + inputs[SIZE_INPUT].getVoltage() * 30.0f, 1.0f, 300.0f);
	size = size < 0.f ? sizeTarget : size + (sizeTarget - size) * std::min(1.0f, 1.0f / (0.05f * args.sampleRate));

	float values[6] = {
		size,
		freeze ? 1e6f : clamp(params[REVTIME_PARAM].getValue() + inputs[REVTIME_INPUT].getVoltage() * 5.0f, 0.0f, 50.0f),
		freeze ? 0.0f : clamp(params[DAMP_PARAM].getValue() + inputs[DAMP_INPUT].getVoltage() * 0.09f, 0.0f, 0.9f),
		clamp(params[BANDWIDTH_PARAM].getValue() + inputs[BANDWIDTH_INPUT].getVoltage() * 0.1f, 0.0f, 1.0f),
		clamp((params[EARLYLEVEL_PARAM].getValue() + inputs[EARLYLEVEL_INPUT].getVoltage()) * 0.1f, 0.0f, 1.0f),
		clamp((params[TAIL_PARAM].getValue() + inputs[TAIL_INPUT].getVoltage()) * 0.1f, 0.0f, 1.0f)
	};
	// coefficients worked out again only when a value moves
	for (int k = 0; k < 6; k++) {
		if (values[k] == applied[k] || (k == 0 && std::fabs(values[k] - applied[k]) < 1e-4f))
			continue;
		applied[k] = values[k];
		for (ty_gverb *v : verb) {
			switch (k) {
				case 0: gverb_set_roomsize(v, values[k]); break;
				case 1: gverb_set_revtime(v, values[k]); break;
				case 2: gverb_set_damping(v, values[k]); break;
				case 3: gverb_set_inputbandwidth(v, values[k]); break;
				case 4: gverb_set_earlylevel(v, values[k]); break;
				case 5: gverb_set_taillevel(v, values[k]); break;
			}
		}
	}

	float inL = inputs[IN_INPUT].getVoltageSum();
	float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltageSum() : inL;
	float ll, lr, rl, rr;
	gverb_do(verb[0], inL * 0.1f * inGain, &ll, &lr);
	gverb_do(verb[1], inR * 0.1f * inGain, &rl, &rr);
	outputs[OUT_L_OUTPUT].setVoltage(bidooSoftLimit(ll * 10.0f));
	outputs[OUT_R_OUTPUT].setVoltage(bidooSoftLimit(rr * 10.0f));
}

template <typename BASE>
struct DFUZELight : BASE {
	DFUZELight() {
		this->box.size = mm2px(Vec(6.f, 6.f));
	}
};

struct DFUZEWidget : BidooWidget {
	DFUZEWidget(DFUZE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/DFUZE.svg"));

		addParam(createParam<BidooBlueKnob>(Vec(13, 46), module, DFUZE::SIZE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 86), module, DFUZE::REVTIME_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 126), module, DFUZE::DAMP_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 166), module, DFUZE::BANDWIDTH_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 206), module, DFUZE::EARLYLEVEL_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 246), module, DFUZE::TAIL_PARAM));


		addInput(createInput<PJ301MPort>(Vec(65.0f, 48.0f), module, DFUZE::SIZE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 88.0f), module, DFUZE::REVTIME_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 128.0f), module, DFUZE::DAMP_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 168.0f), module, DFUZE::BANDWIDTH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 208.0f), module, DFUZE::EARLYLEVEL_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 248.0f), module, DFUZE::TAIL_INPUT));

		addParam(createParam<LEDBezel>(Vec(18.5f, 290.5f), module, DFUZE::FREEZE_PARAM));
		addChild(createLight<DFUZELight<BlueLight>>(Vec(20.3f, 292.3f), module, DFUZE::FREEZE_LIGHT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 288.0f), module, DFUZE::FREEZE_INPUT));

		addInput(createInput<TinyPJ301MPort>(Vec(8.0f, 340.0f), module, DFUZE::IN_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(8.0f+22.0f, 340.0f), module, DFUZE::IN_R_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(60.0f, 340.0f), module, DFUZE::OUT_L_OUTPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(60.0f+22.0f, 340.0f), module, DFUZE::OUT_R_OUTPUT));
	}
};

Model *modelDFUZE = createModel<DFUZE, DFUZEWidget>("dFUZE");

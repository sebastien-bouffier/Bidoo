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

	// the coefficients follow the knobs and CVs every 16 samples
	dsp::ClockDivider control;
	static constexpr int CONTROL = 16;
	// idle: nothing in and nothing left of the tail for longer than the
	// longest delay, then gverb is skipped until the input moves again
	int silent = 0;
	bool idle = false;

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
			// built as the original LADSPA design (a 50 m room, spread 15): the
			// diffusers are sized from the room given here, a 1 m room made them
			// about 50 times too short (a grainy, metallic tail)
			verb[i] = gverb_new((int)sampleRate, 300, 50, 1, 0.5f, 15, 0.5f, 0.5f, 0.5f);
		}
		size = -1.f;
		for (float &a : applied)
			a = -1.f;
		control.setDivision(CONTROL);
		control.reset();
		silent = 0;
		idle = false;
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
	float fade = 1.f / (0.01f * args.sampleRate);
	inGain = freeze ? std::max(inGain - fade, 0.f) : std::min(inGain + fade, 1.f);

	// control rate: the size glides (50 ms) so a moving size bends the tail
	// instead of clicking; coefficients worked out again only when a value moves
	if (size < 0.f || control.process()) {
		lights[FREEZE_LIGHT].setBrightness(freeze ? 1.f : 0.f);
		float sizeTarget = clamp(params[SIZE_PARAM].getValue() + inputs[SIZE_INPUT].getVoltage() * 30.0f, 1.0f, 300.0f);
		size = size < 0.f ? sizeTarget : size + (sizeTarget - size) * std::min(1.0f, (float)CONTROL / (0.05f * args.sampleRate));

		float values[6] = {
			size,
			freeze ? 1e6f : clamp(params[REVTIME_PARAM].getValue() + inputs[REVTIME_INPUT].getVoltage() * 5.0f, 0.0f, 50.0f),
			freeze ? 0.0f : clamp(params[DAMP_PARAM].getValue() + inputs[DAMP_INPUT].getVoltage() * 0.09f, 0.0f, 0.9f),
			clamp(params[BANDWIDTH_PARAM].getValue() + inputs[BANDWIDTH_INPUT].getVoltage() * 0.1f, 0.0f, 1.0f),
			clamp((params[EARLYLEVEL_PARAM].getValue() + inputs[EARLYLEVEL_INPUT].getVoltage()) * 0.1f, 0.0f, 1.0f),
			clamp((params[TAIL_PARAM].getValue() + inputs[TAIL_INPUT].getVoltage()) * 0.1f, 0.0f, 1.0f)
		};
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
	}

	bool outL = outputs[OUT_L_OUTPUT].isConnected(), outR = outputs[OUT_R_OUTPUT].isConnected();
	float inL = inputs[IN_INPUT].getVoltageSum();
	float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltageSum() : inL;
	float xL = inL * 0.1f * inGain, xR = inR * 0.1f * inGain;

	// nothing to hear, or nothing left of the tail: skip the reverb
	bool quiet = std::fabs(xL) < 1e-7f && std::fabs(xR) < 1e-7f;
	if (!quiet) {
		silent = 0;
		idle = false;
	}
	if (idle || (!outL && !outR)) {
		outputs[OUT_L_OUTPUT].setVoltage(0.f);
		outputs[OUT_R_OUTPUT].setVoltage(0.f);
		return;
	}

	// dual mono: the left reverb feeds the left output, the right one the right
	// (true stereo, ll + rl and lr + rr, doubled the cost for a narrower image);
	// a side nobody listens to is not worked out
	float yL = outL ? gverb_do_side(verb[0], xL, 0) : 0.f;
	float yR = outR ? gverb_do_side(verb[1], xR, 1) : 0.f;
	outputs[OUT_L_OUTPUT].setVoltage(bidooSoftLimit(yL * 10.0f));
	outputs[OUT_R_OUTPUT].setVoltage(bidooSoftLimit(yR * 10.0f));

	// the tail is gone once the output stays under -140 dB (re 10 V) for longer
	// than the longest delay line
	if (quiet && std::fabs(yL) < 1e-6f && std::fabs(yR) < 1e-6f) {
		if (++silent > (int)(verb[0]->largestdelay + 0.25f * args.sampleRate))
			idle = true;
	}
	else
		silent = 0;
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

		// No screws, as before. The original hand-drawn panel: six rows of a knob joined by a line to its CV jack, then
		// the freeze button and its jack, then the stereo in and out. Each knob, button and jack sits on the line the
		// panel draws (the old code had the jacks 0.9 to 1.5 px above it, the knobs up to 0.55, the button 1.1 below);
		// the button in the knobs' column (it was 1.4 px right); each stereo jack under its L or R label.
		const theme::Pos knobCol = theme::specificCorner(13, 46, theme::kBlueKnobSize);
		const theme::Pos cvCol = theme::specificCorner(65, 48, theme::kJackSize);
		const theme::Pos rowLine[7] = {theme::specific(0, 61.062f), theme::specific(0, 100.938f), theme::specific(0, 141.312f),
			theme::specific(0, 180.75f), theme::specific(0, 221.312f), theme::specific(0, 261.188f), theme::specific(0, 300.f)};
		const int knobIds[6] = {DFUZE::SIZE_PARAM, DFUZE::REVTIME_PARAM, DFUZE::DAMP_PARAM, DFUZE::BANDWIDTH_PARAM, DFUZE::EARLYLEVEL_PARAM, DFUZE::TAIL_PARAM};
		const int cvIds[6] = {DFUZE::SIZE_INPUT, DFUZE::REVTIME_INPUT, DFUZE::DAMP_INPUT, DFUZE::BANDWIDTH_INPUT, DFUZE::EARLYLEVEL_INPUT, DFUZE::TAIL_INPUT};
		for (int i = 0; i < 6; i++)
			addParam(theme::param<BidooBlueKnob>(theme::colRow(knobCol, rowLine[i]), module, knobIds[i]));
		for (int i = 0; i < 6; i++)
			addInput(theme::input<PJ301MPort>(theme::colRow(cvCol, rowLine[i]), module, cvIds[i]));

		const theme::Size kBezelSize = {21.26028f, 21.25987f};   // LEDBezel
		const float kLightIn = 1.8f;                             // from the bezel's corner to its light's
		const theme::Pos freeze = theme::colRow(knobCol, rowLine[6]);
		const theme::Pos freezeLight = theme::specificCorner(freeze.x - kBezelSize.w * 0.5f + kLightIn, freeze.y - kBezelSize.h * 0.5f + kLightIn,
			theme::Size{mm2px(6.f), mm2px(6.f)});   // DFUZELight
		addParam(theme::param<LEDBezel>(freeze, module, DFUZE::FREEZE_PARAM));
		addChild(theme::light<DFUZELight<BlueLight>>(freezeLight, module, DFUZE::FREEZE_LIGHT));
		addInput(theme::input<PJ301MPort>(theme::colRow(cvCol, rowLine[6]), module, DFUZE::FREEZE_INPUT));

		const theme::Pos stereoRow = theme::specificCorner(8, 340, theme::kTinyJackSize);
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(theme::specific(15.594f, 0), stereoRow), module, DFUZE::IN_INPUT));
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(theme::specific(37.125f, 0), stereoRow), module, DFUZE::IN_R_INPUT));
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(theme::specific(67.281f, 0), stereoRow), module, DFUZE::OUT_L_OUTPUT));
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(theme::specific(88.812f, 0), stereoRow), module, DFUZE::OUT_R_OUTPUT));
	}
};

Model *modelDFUZE = createModel<DFUZE, DFUZEWidget>("dFUZE");

#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "Svf.hpp"
#include "dep/bidoo-dsp/bidoo/dsp/reverb/Prism.h"

using namespace std;

using bidoo::dsp::reverb::PrismReverb;

// REI: the Prism reverb of FadaBox (eight algorithms and a shimmer), stereo.
// Prism works on blocks: the wet signal comes 32 samples (0.7 ms at 48 kHz) late.
struct REI : BidooModule {
	enum ParamIds {
		SIZE_PARAM,
		DAMP_PARAM,
		FREEZE_PARAM,
		WIDTH_PARAM,
		DRY_PARAM,
		WET_PARAM,
		SHIMM_PARAM,
		SHIMMPITCH_PARAM,
		CLIPPING_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_L_INPUT,
		IN_R_INPUT,
		SIZE_INPUT,
		DAMP_INPUT,
		FREEZE_INPUT,
		WIDTH_INPUT,
		SHIMM_INPUT,
		SHIMMPITCH_INPUT,
		DRY_INPUT,
		WET_INPUT,
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

	static constexpr int BLOCK = 32;
	static constexpr int NUM_INTERVALS = 10;
	static const std::vector<float> &preDelays() {
		static const std::vector<float> v {0.f, 10.f, 20.f, 40.f, 80.f, 150.f};
		return v;
	}
	static const std::vector<float> &mods() {
		static const std::vector<float> v {0.f, 0.15f, 0.3f, 0.5f, 0.75f, 1.f};
		return v;
	}

	PrismReverb prism;
	PrismReverb::Settings settings;
	float inL[BLOCK] = {}, inR[BLOCK] = {}, wetL[BLOCK] = {}, wetR[BLOCK] = {};
	int pos = 0;
	// idle: input and wet output under -100 dB for IDLE_SECONDS, then Prism is
	// skipped (and while no output is patched) until the input moves again
	static constexpr float IDLE_LEVEL = 1e-5f;  // Prism units, 1 = 5 V
	static constexpr float IDLE_SECONDS = 1.5f;
	int quietBlocks = 0;
	bool idle = false;
	float blockPeak = 0.f;

	dsp::SchmittTrigger freezeTrigger;
	bool freeze = false;

	// Prism freezes its network algorithms only. For SPRING, NON-LIN and
	// REVERSE, REI loops the last second of the wet output instead, with a
	// crossfade at the loop point, and shuts the input out, like Prism does.
	std::vector<float> histL, histR;  // the wet output, kept while not looping
	int histW = 0, loopLen = 1, loopX = 1, loopStart = 0, loopPos = 0;
	bool looping = false;
	float loopMix = 0.f, loopStep = 0.f;
	// context menu, saved with the patch
	int algo = PrismReverb::Hall;
	int preDelay = 1;
	int mod = 2;

	REI() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SIZE_PARAM, 0.f, 1.f, .5f, "Decay", "%", 0.f, 100.f);
		configParam(DAMP_PARAM, 0.f, 1.f, .4f, "Damping (dark)", "%", 0.f, 100.f);
		configParam(WIDTH_PARAM, 0.f, 1.f, .5f, "Stereo width of the reverb (50%: as it comes)", "%", 0.f, 100.f);
		configParam(DRY_PARAM, 0.f, 1.f, 1.f, "Dry", "%", 0.f, 100.f);
		configParam(WET_PARAM, 0.f, 1.f, .5f, "Wet", "%", 0.f, 100.f);
		configParam(SHIMM_PARAM, 0.f, 1.f, 0.f, "Shimmer", "%", 0.f, 100.f);
		std::vector<std::string> intervals;
		for (const std::string &n : PrismReverb::intervalNames())
			intervals.push_back(n);
		configSwitch(SHIMMPITCH_PARAM, 0.f, NUM_INTERVALS - 1, 4.f, "Shimmer interval", intervals);
		configButton(FREEZE_PARAM, "Freeze");
		configSwitch(CLIPPING_PARAM, 0, 1, 0, "Output limit", {"Soft (under 10 V)", "Hard clip at 10 V"});
		configInput(IN_L_INPUT, "Left");
		configInput(IN_R_INPUT, "Right (follows left when unpatched)");
		configInput(SIZE_INPUT, "Decay CV (0-10 V: full range)");
		configInput(DAMP_INPUT, "Damping CV (0-10 V: full range)");
		configInput(FREEZE_INPUT, "Freeze toggle trigger");
		configInput(WIDTH_INPUT, "Width CV (0-10 V: full range)");
		configInput(SHIMM_INPUT, "Shimmer CV (0-10 V: full range)");
		configInput(SHIMMPITCH_INPUT, "Shimmer interval CV (1 V per step)");
		configInput(DRY_INPUT, "Dry CV (0-10 V: full range)");
		configInput(WET_INPUT, "Wet CV (0-10 V: full range)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		// a short glide of the line lengths, so a moving DECAY bends instead of clicking
		settings.glideMs = 100.f;
		prism.prepare(48000.f);
		prepareLoop(48000.f);
	}

	void prepareLoop(float sr) {
		loopLen = (int)sr;            // 1 s loop
		loopX = (int)(0.05f * sr);    // 50 ms crossfades
		histL.assign(loopLen + loopX + 1, 0.f);
		histR.assign(loopLen + loopX + 1, 0.f);
		histW = loopPos = loopStart = 0;
		looping = false;
		loopMix = 0.f;
		loopStep = 1.f / loopX;
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		prism.prepare(e.sampleRate);
		prepareLoop(e.sampleRate);
		pos = 0;
	}

	// The wet output, through the loop when it freezes.
	void loopFreeze(float &oL, float &oR) {
		bool want = freeze && !bidoo::dsp::reverb::prism_detail::isFdn(algo);
		int n = histL.size();
		if (want && !looping) {
			// the last second plus a crossfade's length, from the history
			looping = true;
			loopStart = (histW - (loopLen + loopX) + n) % n;
			loopPos = 0;
		}
		if (!looping) {
			histL[histW] = oL;
			histR[histW] = oR;
			histW = (histW + 1) % n;
			return;
		}
		float lL = histL[(loopStart + loopPos) % n], lR = histR[(loopStart + loopPos) % n];
		if (loopPos < loopX) {
			// the loop's start fades in over the audio that followed its end
			float w = (float)loopPos / loopX * (float)M_PI * 0.5f;
			float gIn = std::sin(w), gOut = std::cos(w);
			int t = (loopStart + loopLen + loopPos) % n;
			lL = lL * gIn + histL[t] * gOut;
			lR = lR * gIn + histR[t] * gOut;
		}
		if (++loopPos >= loopLen)
			loopPos = 0;
		loopMix = want ? std::min(loopMix + loopStep, 1.f) : std::max(loopMix - loopStep, 0.f);
		oL += (lL - oL) * loopMix;
		oR += (lR - oR) * loopMix;
		if (!want && loopMix <= 0.f)
			looping = false;
	}

	void onReset() override {
		algo = PrismReverb::Hall;
		preDelay = 1;
		mod = 2;
		freeze = false;
		prism.reset();
		prepareLoop(APP && APP->engine ? APP->engine->getSampleRate() : 48000.f);
	}

	// FREEZE is not saved: a patch opens with an empty reverb, and a held
	// (empty) tail with the input shut out would only be silence
	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "algo", json_integer(algo));
		json_object_set_new(rootJ, "preDelay", json_integer(preDelay));
		json_object_set_new(rootJ, "mod", json_integer(mod));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		freeze = false;
		if (json_t *j = json_object_get(rootJ, "algo"))
			algo = clamp((int)json_integer_value(j), 0, PrismReverb::NumAlgos - 1);
		if (json_t *j = json_object_get(rootJ, "preDelay"))
			preDelay = clamp((int)json_integer_value(j), 0, (int)preDelays().size() - 1);
		if (json_t *j = json_object_get(rootJ, "mod"))
			mod = clamp((int)json_integer_value(j), 0, (int)mods().size() - 1);
	}

	void process(const ProcessArgs &args) override {
		if (freezeTrigger.process(params[FREEZE_PARAM].getValue() + inputs[FREEZE_INPUT].getVoltage()))
			freeze = !freeze;
		lights[FREEZE_LIGHT].setBrightness(freeze ? 1.f : 0.f);

		// Prism runs at +-1 for +-5 V
		float l = inputs[IN_L_INPUT].getVoltageSum();
		float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltageSum() : l;
		// a looping freeze shuts the input out, as Prism's own freeze does
		bool shut = freeze && !bidoo::dsp::reverb::prism_detail::isFdn(algo);
		inL[pos] = shut ? 0.f : l * 0.2f;
		inR[pos] = shut ? 0.f : r * 0.2f;
		blockPeak = std::max(blockPeak, std::max(std::max(std::fabs(inL[pos]), std::fabs(inR[pos])), std::max(std::fabs(wetL[pos]), std::fabs(wetR[pos]))));
		float oL = wetL[pos] * 5.0f, oR = wetR[pos] * 5.0f;
		loopFreeze(oL, oR);
		if (++pos >= BLOCK) {
			pos = 0;
			bool heard = outputs[OUT_L_OUTPUT].isConnected() || outputs[OUT_R_OUTPUT].isConnected();
			if (blockPeak >= IDLE_LEVEL || looping) {
				quietBlocks = 0;
				idle = false;
			}
			else if (!idle && ++quietBlocks * BLOCK > (int)(IDLE_SECONDS * args.sampleRate))
				idle = true;
			blockPeak = 0.f;
			if (idle || !heard) {
				std::fill(wetL, wetL + BLOCK, 0.f);
				std::fill(wetR, wetR + BLOCK, 0.f);
			}
			else {
				settings.algo = algo;
				settings.preMs = preDelays()[preDelay];
				settings.mod = mods()[mod];
				settings.decay = clamp(params[SIZE_PARAM].getValue() + inputs[SIZE_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
				settings.damp = clamp(params[DAMP_PARAM].getValue() + inputs[DAMP_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
				settings.shimmer = clamp(params[SHIMM_PARAM].getValue() + inputs[SHIMM_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
				settings.interval = clamp((int)std::round(params[SHIMMPITCH_PARAM].getValue() + inputs[SHIMMPITCH_INPUT].getVoltage()), 0, NUM_INTERVALS - 1);
				settings.freeze = freeze;
				prism.process(inL, inR, wetL, wetR, BLOCK, settings);
			}
		}

		// width on the wet only: 0 mono, 50% as Prism makes it, 100% twice the side
		float width = clamp(params[WIDTH_PARAM].getValue() + inputs[WIDTH_INPUT].getVoltage() * 0.1f, 0.f, 1.f) * 2.f;
		float mid = 0.5f * (oL + oR), side = 0.5f * (oL - oR) * width;
		float wet = clamp(params[WET_PARAM].getValue() + inputs[WET_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float dry = clamp(params[DRY_PARAM].getValue() + inputs[DRY_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float outL = dry * l + wet * (mid + side);
		float outR = dry * r + wet * (mid - side);
		if (params[CLIPPING_PARAM].getValue() > 0.5f) {
			outL = clamp(outL, -10.0f, 10.0f);
			outR = clamp(outR, -10.0f, 10.0f);
		}
		else {
			outL = bidooSoftLimit(outL);
			outR = bidooSoftLimit(outR);
		}
		outputs[OUT_L_OUTPUT].setVoltage(outL);
		outputs[OUT_R_OUTPUT].setVoltage(outR);
	}

};

template <typename BASE>
struct REILight : BASE {
	REILight() {
		this->box.size = mm2px(Vec(6.f, 6.f));
	}
};

struct REIWidget : BidooWidget {
	REIWidget(REI *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/REI.svg"));

		const float yAnchor = 38.0f;
		const float inputsOffset = 32.0f;
		const float groupsOffset = 64.0f;
		const float xInputsAnchor = 33.0f;
		const float xInputsOffset = 24.0f;

		addParam(createParam<BidooBlueKnob>(Vec(13, yAnchor), module, REI::SIZE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(63, yAnchor), module, REI::DAMP_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor, yAnchor+inputsOffset), module, REI::SIZE_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor+xInputsOffset, yAnchor+inputsOffset), module, REI::DAMP_INPUT));

		addParam(createParam<BidooBlueKnob>(Vec(13, yAnchor+groupsOffset), module, REI::WIDTH_PARAM));
		addParam(createParam<LEDBezel>(Vec(67, yAnchor+groupsOffset+4), module, REI::FREEZE_PARAM));
		addChild(createLight<REILight<BlueLight>>(Vec(68.8f, yAnchor+groupsOffset+5.8f), module, REI::FREEZE_LIGHT));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor, yAnchor+groupsOffset+inputsOffset), module, REI::WIDTH_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor+xInputsOffset, yAnchor+groupsOffset+inputsOffset), module, REI::FREEZE_INPUT));

		addParam(createParam<BidooBlueKnob>(Vec(13, yAnchor+2*groupsOffset), module, REI::SHIMM_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(63, yAnchor+2*groupsOffset), module, REI::SHIMMPITCH_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor, yAnchor+2*groupsOffset+inputsOffset), module, REI::SHIMM_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor+xInputsOffset, yAnchor+2*groupsOffset+inputsOffset), module, REI::SHIMMPITCH_INPUT));

		addParam(createParam<BidooBlueKnob>(Vec(13, yAnchor+3*groupsOffset), module, REI::DRY_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(63, yAnchor+3*groupsOffset), module, REI::WET_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor, yAnchor+3*groupsOffset+inputsOffset), module, REI::DRY_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor+xInputsOffset, yAnchor+3*groupsOffset+inputsOffset), module, REI::WET_INPUT));

		addParam(createParam<CKSS>(Vec(21.0f, 289.0f), module, REI::CLIPPING_PARAM));

		addInput(createInput<TinyPJ301MPort>(Vec(8.0f, 340.0f), module, REI::IN_L_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(8.0f+22.0f, 340.0f), module, REI::IN_R_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(60.0f, 340.0f), module, REI::OUT_L_OUTPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(60.0f+22.0f, 340.0f), module, REI::OUT_R_OUTPUT));
	}

	void appendContextMenu(Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		REI *module = dynamic_cast<REI *>(this->module);
		if (!module)
			return;
		menu->addChild(new MenuSeparator());
		std::vector<std::string> algos;
		for (const std::string &n : PrismReverb::algoNames())
			algos.push_back(n);
		menu->addChild(createIndexSubmenuItem("Algorithm", algos,
			[=]() { return module->algo; }, [=](size_t i) { module->algo = (int)i; }));
		std::vector<std::string> pre;
		for (float ms : REI::preDelays())
			pre.push_back(rack::string::f("%g ms", ms));
		menu->addChild(createIndexSubmenuItem("Pre-delay", pre,
			[=]() { return module->preDelay; }, [=](size_t i) { module->preDelay = (int)i; }));
		std::vector<std::string> mod;
		for (float m : REI::mods())
			mod.push_back(rack::string::f("%d%%", (int)std::round(m * 100.f)));
		menu->addChild(createIndexSubmenuItem("Modulation", mod,
			[=]() { return module->mod; }, [=](size_t i) { module->mod = (int)i; }));
	}
};

Model *modelREI = createModel<REI, REIWidget>("REI");

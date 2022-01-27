// Based on Will Pirkle's courses & Vadim Zavalishin's book

#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/resampler.hpp"

using namespace std;

#define pi 3.14159265359

struct FilterStage {
	float mem = 0.0;

	float Filter(float sample, float freq, float smpRate, float gain, int mode) {
		float g = std::tan(pi*freq / smpRate);
		float G = g / (1.0 + g);
		float out;
		if (mode == 0) {
			out = (sample - mem) * G + mem;
		} else {
			out = (std::tanh(sample*gain) / std::tanh(gain) - mem) * G + mem;
		}
		mem = out + (sample - mem) * G;
		return out;
	}
};

struct LadderFilter {
	FilterStage stage1;
	FilterStage stage2;
	FilterStage stage3;
	FilterStage stage4;
	float q;
	float freq;
	float smpRate;
	int mode = 0;
	float gain = 1.0f;

	void setParams(float freq, float q, float smpRate, float gain, int mode) {
		this->freq = freq;
		this->q = q;
		this->smpRate = smpRate;
		this->mode = mode;
		this->gain = gain;
	}

	float calcOutput(float sample) {
		float g = std::tan(pi*freq / smpRate);
		float G = g / (1.0f + g);
		G = G*G*G*G;
		float S1 = stage1.mem / (1.0f + g);
		float S2 = stage2.mem / (1.0f + g);
		float S3 = stage3.mem / (1.0f + g);
		float S4 = stage4.mem / (1.0f + g);
		float S = G*G*G*S1 + G*G*S2 + G*S3 + S4;
		return stage4.Filter(stage3.Filter(stage2.Filter(stage1.Filter((sample - q * S) / (1.0f + q * G),
			freq, smpRate, gain, mode), freq, smpRate, gain, mode), freq, smpRate, gain, mode), freq, smpRate, gain, mode);
	}
};

struct LIMBO : BidooModule {
	enum ParamIds {
		CUTOFF_PARAM,
		Q_PARAM,
		CMOD_PARAM,
		MUG_PARAM,
		MODE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_L,
		IN_R,
		CUTOFF_INPUT,
		Q_INPUT,
		MUG_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L,
		OUT_R,
		NUM_OUTPUTS
	};
	enum LightIds {
		LEARN_LIGHT,
		NUM_LIGHTS
	};

	LadderFilter lFilter, rFilter;

	///Tooltip
	struct tpOnOff : ParamQuantity {
		std::string getDisplayValueString() override {
			if (getValue() < 1.f)
				return "On";
			else
				return "Off";
		}
	};
	///Tooltip

	LIMBO() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CUTOFF_PARAM, 0.0f, 1.0f, 1.0f, "Frequency","Hz", std::pow(2, 10.f), dsp::FREQ_C4 / std::pow(2, 5.f));
		configParam(Q_PARAM, 0.0f, 1.0f, 0.0f, "Q", "%", 0.f, 100.f);
		configParam(MUG_PARAM, 0.0f, 1.0f, 0.0f, "Gain Boost", "%", 0.f, 100.f);
		configParam(CMOD_PARAM, -1.0f, 1.0f, 0.0f, "Freq. Mod", "%", 0.f, 100.f);
		configParam<tpOnOff>(MODE_PARAM, 0.0f, 1.0f, 0.0f, "Linear");
	}

	void process(const ProcessArgs &args) override {
		float cfreq = std::pow(2.0f, rescale(clamp(params[CUTOFF_PARAM].getValue() + params[CMOD_PARAM].getValue() * inputs[CUTOFF_INPUT].getVoltage() * 0.2f, 0.0f, 1.0f), 0.0f, 1.0f, 4.5f, 14.0f));
		float q = 3.5f * clamp(params[Q_PARAM].getValue() + inputs[Q_INPUT].getVoltage() * 0.2f, 0.0f, 1.0f);
		float g = pow(2.0f, rescale(clamp(params[MUG_PARAM].getValue() + inputs[MUG_INPUT].getVoltage() * 0.2f, 0.0f, 1.0f), 0.0f, 1.0f, 0.0f, 3.0f));
		int mode = (int)params[MODE_PARAM].getValue();
		lFilter.setParams(cfreq, q, args.sampleRate, g / 3, mode);
		rFilter.setParams(cfreq, q, args.sampleRate, g / 3, mode);
		float inL = inputs[IN_L].getVoltage() * 0.2f;
		float inR = inputs[IN_R].getVoltage() * 0.2f;
		inL = lFilter.calcOutput(inL)*5.0f*(mode == 0 ? g : 1);
		inR = rFilter.calcOutput(inR)*5.0f*(mode == 0 ? g : 1);
		outputs[OUT_L].setVoltage(inL);
		outputs[OUT_R].setVoltage(inR);
	}

};

struct LIMBOWidget : BidooWidget {
	LIMBOWidget(LIMBO *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/LIMBO.svg"));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<BidooHugeBlueKnob>(Vec(32, 61), module, LIMBO::CUTOFF_PARAM));
		addParam(createParam<BidooLargeBlueKnob>(Vec(12, 143), module, LIMBO::Q_PARAM));
		addParam(createParam<BidooLargeBlueKnob>(Vec(71, 143), module, LIMBO::MUG_PARAM));
		addParam(createParam<BidooLargeBlueKnob>(Vec(12, 208), module, LIMBO::CMOD_PARAM));
		addParam(createParam<CKSS>(Vec(82.5f, 217), module, LIMBO::MODE_PARAM));

		addInput(createInput<PJ301MPort>(Vec(7, 283), module, LIMBO::CUTOFF_INPUT));
		addInput(createInput<PJ301MPort>(Vec(48, 283), module, LIMBO::Q_INPUT));
		addInput(createInput<PJ301MPort>(Vec(88.5f, 283), module, LIMBO::MUG_INPUT));

		addInput(createInput<TinyPJ301MPort>(Vec(8.f, 340), module, LIMBO::IN_L));
		addInput(createInput<TinyPJ301MPort>(Vec(8.f+22.f, 340), module, LIMBO::IN_R));

		addOutput(createOutput<TinyPJ301MPort>(Vec(75.f, 340), module, LIMBO::OUT_L));
		addOutput(createOutput<TinyPJ301MPort>(Vec(75.f+22.f, 340), module, LIMBO::OUT_R));
	}
};

Model *modelLIMBO = createModel<LIMBO, LIMBOWidget>("lIMbO");

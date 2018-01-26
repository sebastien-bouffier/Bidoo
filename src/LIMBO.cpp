// Based on Will Pirkle's courses & Vadim Zavalishin's book

#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/decimator.hpp"

using namespace std;

#define pi 3.14159265359

struct FilterStage
{
	float mem = 0.0;

	float Filter(float sample, float freq, float smpRate)
	{
		float g = tan(pi*freq/smpRate);
		float G = g/(1.0 + g);
		float out = (sample - mem) * G + mem;
		mem = out + (sample - mem) * G	;
		return out;
	}
};

struct LadderFilter
{
	FilterStage stage1;
	FilterStage stage2;
	FilterStage stage3;
	FilterStage stage4;
	float q;
	float freq;
	float smpRate;

	void setParams(float freq, float q, float smpRate) {
		this->freq = freq;
		this->q=q;
		this->smpRate=smpRate;
	}

	float calcOutput(float sample)
	{
		float g = tan(pi*freq/smpRate);
		float G = g/(1 + g);
		G = G*G*G*G;
		float S1 = stage1.mem/(1 + g);
		float S2 = stage2.mem/(1 + g);
		float S3 = stage3.mem/(1 + g);
		float S4 = stage4.mem/(1 + g);
		float S = G*G*G*S1 + G*G*S2 + G*S3 + S4;
		return stage4.Filter(stage3.Filter(stage2.Filter(stage1.Filter((sample - q*S)/(1 + q*G),freq,smpRate),freq,smpRate),freq,smpRate),freq,smpRate);
	}

};

struct LIMBO : Module {
	enum ParamIds {
		CUTOFF_PARAM,
		Q_PARAM,
		CMOD_PARAM,
		MUG_PARAM,
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
	LadderFilter lFilter,rFilter;

	LIMBO() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
	}

	void step() override;

};

void LIMBO::step() {
	float cfreq = pow(2,rescalef(clampf(params[CUTOFF_PARAM].value + params[CMOD_PARAM].value * inputs[CUTOFF_INPUT].value / 5,0,1),0,1,4.5,13));
	float q = 3.5 * clampf(params[Q_PARAM].value + inputs[Q_INPUT].value / 5.0, 0.0, 1.0);
	lFilter.setParams(cfreq,q,engineGetSampleRate());
	rFilter.setParams(cfreq,q,engineGetSampleRate());
	float inL = inputs[IN_L].value/5; //normalise to -1/+1 we consider VCV Rack standard is #+5/-5V on VCO1
	float inR = inputs[IN_R].value/5;
	//filtering + makeup gain
	float g = clampf(params[MUG_PARAM].value + inputs[MUG_INPUT].value,1,10);
	inL = lFilter.calcOutput(inL)*5*g;
	inR = rFilter.calcOutput(inR)*5*g;
	outputs[OUT_L].value = inL;
	outputs[OUT_R].value = inR;
}

struct LIMBODisplay : TransparentWidget {
	LIMBO *module;
	std::shared_ptr<Font> font;
	LIMBODisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void draw(NVGcontext *vg) override {

	}
};

LIMBOWidget::LIMBOWidget() {
	LIMBO *module = new LIMBO();
	setModule(module);
	box.size = Vec(15*8, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/LIMBO.svg")));
		addChild(panel);
	}

	LIMBODisplay *display = new LIMBODisplay();
	display->module = module;
	display->box.pos = Vec(5, 40);
	display->box.size = Vec(110, 70);
	addChild(display);

	addParam(createParam<BidooHugeBlueKnob>(Vec(33, 61), module, LIMBO::CUTOFF_PARAM, 0, 1, 1));
	addParam(createParam<BidooLargeBlueKnob>(Vec(12, 143), module, LIMBO::Q_PARAM, 0, 1, 0));
	addParam(createParam<BidooLargeBlueKnob>(Vec(71, 143), module, LIMBO::MUG_PARAM, 1, 10, 1));
	addParam(createParam<BidooLargeBlueKnob>(Vec(12, 208), module, LIMBO::CMOD_PARAM, -1, 1, 0));

	addInput(createInput<PJ301MPort>(Vec(12, 280), module, LIMBO::CUTOFF_INPUT));
	addInput(createInput<PJ301MPort>(Vec(47, 280), module, LIMBO::Q_INPUT));
	addInput(createInput<PJ301MPort>(Vec(82, 280), module, LIMBO::MUG_INPUT));

	addInput(createInput<TinyPJ301MPort>(Vec(24, 319), module, LIMBO::IN_L));
	addInput(createInput<TinyPJ301MPort>(Vec(24, 339), module, LIMBO::IN_R));
	addOutput(createOutput<TinyPJ301MPort>(Vec(95, 319), module, LIMBO::OUT_L));
	addOutput(createOutput<TinyPJ301MPort>(Vec(95, 339), module, LIMBO::OUT_R));

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));
}

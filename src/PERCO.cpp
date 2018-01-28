// Based on Will Pirkle's courses & Vadim Zavalishin's book

#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/decimator.hpp"

using namespace std;

#define pi 3.14159265359


struct MultiFilter
{
	float q;
	float freq;
	float smpRate;
	float hp = 0,bp = 0,lp = 0,mem1 = 0,mem2 = 0;

	void setParams(float freq, float q, float smpRate) {
		this->freq = freq;
		this->q=q;
		this->smpRate=smpRate;
	}

	void calcOutput(float sample)
	{
		float g = tan(pi*freq/smpRate);
		float R = 1.0/(2.0*q);
		hp = (sample - (2.0*R + g)*mem1 - mem2)/(1.0 + 2.0*R*g + g*g);
		bp = g*hp + mem1;
		lp = g*bp +  mem2;
		mem1 = g*hp + bp;
		mem2 = g*bp + lp;
	}

};

struct PERCO : Module {
	enum ParamIds {
		CUTOFF_PARAM,
		Q_PARAM,
		CMOD_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN,
		CUTOFF_INPUT,
		Q_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_LP,
		OUT_BP,
		OUT_HP,
		NUM_OUTPUTS
	};
	enum LightIds {
		LEARN_LIGHT,
		NUM_LIGHTS
	};
	MultiFilter filter;

	PERCO() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
	}

	void step() override;

};

void PERCO::step() {
	float cfreq = pow(2,rescalef(clampf(params[CUTOFF_PARAM].value + params[CMOD_PARAM].value * inputs[CUTOFF_INPUT].value / 5,0,1),0,1,4.5,13));
	float q = 10 * clampf(params[Q_PARAM].value + inputs[Q_INPUT].value / 5.0, 0.1, 1.0);
	filter.setParams(cfreq,q,engineGetSampleRate());
	float in = inputs[IN].value/5; //normalise to -1/+1 we consider VCV Rack standard is #+5/-5V on VCO1
	//filtering
	filter.calcOutput(in);
	outputs[OUT_LP].value = filter.lp * 5;
	outputs[OUT_HP].value = filter.hp * 5;
	outputs[OUT_BP].value = filter.bp * 5;
}

struct PERCODisplay : TransparentWidget {
	PERCO *module;
	std::shared_ptr<Font> font;
	PERCODisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void draw(NVGcontext *vg) override {

	}
};

PERCOWidget::PERCOWidget() {
	PERCO *module = new PERCO();
	setModule(module);
	box.size = Vec(15*8, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/PERCO.svg")));
		addChild(panel);
	}

	PERCODisplay *display = new PERCODisplay();
	display->module = module;
	display->box.pos = Vec(5, 40);
	display->box.size = Vec(110, 70);
	addChild(display);

	addParam(createParam<BidooHugeBlueKnob>(Vec(33, 61), module, PERCO::CUTOFF_PARAM, 0, 1, 1));
	addParam(createParam<BidooLargeBlueKnob>(Vec(12, 143), module, PERCO::Q_PARAM, 0.1, 1, 0.1));
	addParam(createParam<BidooLargeBlueKnob>(Vec(71, 143), module, PERCO::CMOD_PARAM, -1, 1, 0));

	addInput(createInput<PJ301MPort>(Vec(10, 276), module, PERCO::IN));
	addInput(createInput<PJ301MPort>(Vec(48, 276), module, PERCO::CUTOFF_INPUT));
	addInput(createInput<PJ301MPort>(Vec(85, 276), module, PERCO::Q_INPUT));

	addOutput(createOutput<PJ301MPort>(Vec(10, 320), module, PERCO::OUT_LP));
	addOutput(createOutput<PJ301MPort>(Vec(48, 320), module, PERCO::OUT_BP));
	addOutput(createOutput<PJ301MPort>(Vec(85, 320), module, PERCO::OUT_HP));

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));
}

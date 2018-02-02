#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/decimator.hpp"
#include "dep/filters/biquad.h"

using namespace std;

#define BANDS 16

struct ZINC : Module {
	enum ParamIds {
		BG_PARAM,
		ATTACK_PARAM = BG_PARAM + BANDS,
		DECAY_PARAM,
		Q_PARAM,
		GMOD_PARAM,
		GCARR_PARAM,
		G_PARAM,
		SHAPE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_MOD,
		IN_CARR,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		LEARN_LIGHT,
		NUM_LIGHTS
	};
	Biquad* iFilter[2*BANDS];
	Biquad* cFilter[2*BANDS];
	float mem[BANDS] = {0};
	float freq[BANDS] = {125,185,270,350,430,530,630,780,950,1150,1380,1680,2070,2780,3800,6400};
	float peaks[BANDS] = {0};

	ZINC() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
		for(int i=0; i<2*BANDS; i++) {
			iFilter[i] = new Biquad(bq_type_bandpass, freq[i%BANDS] / engineGetSampleRate(), 5, 6);
			cFilter[i] = new Biquad(bq_type_bandpass, freq[i%BANDS] / engineGetSampleRate(), 5, 6);
			}
	}

	void step() override;

};

void ZINC::step() {
	float inM = inputs[IN_MOD].value/5;
	float inC = inputs[IN_CARR].value/5;
	const float slewMin = 0.001;
	const float slewMax = 500.0;
	const float shapeScale = 1/10.0;
	float attack = params[ATTACK_PARAM].value;
	float decay = params[DECAY_PARAM].value;
	float slewAttack = slewMax * powf(slewMin / slewMax, attack);
	float slewDecay = slewMax * powf(slewMin / slewMax, decay);
	float out = 0.0;

	for(int i=0; i<BANDS; i++) {
		float coeff = mem[i];
		float peak = abs(iFilter[i+BANDS]->process(iFilter[i]->process(inM*params[GMOD_PARAM].value)));
		if (peak>coeff) {
			coeff += slewAttack * shapeScale * (peak - coeff) / engineGetSampleRate();
			if (coeff > peak)
				coeff = peak;
		}
		else if (peak < coeff) {
			coeff -= slewDecay * shapeScale * (coeff - peak) / engineGetSampleRate();
			if (coeff < peak)
				coeff = peak;
		}
		peaks[i]=peak;
		mem[i]=coeff;
		out += cFilter[i+BANDS]->process(cFilter[i]->process(inC*params[GCARR_PARAM].value)) * coeff * params[BG_PARAM+i].value;
	}
	outputs[OUT].value = out * 5 * params[G_PARAM].value;
}

struct ZINCDisplay : TransparentWidget {
	ZINC *module;
	std::shared_ptr<Font> font;

	ZINCDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void draw(NVGcontext *vg) override {
		nvgFontSize(vg, 12);
		nvgFontFaceId(vg, font->handle);
		nvgStrokeWidth(vg, 2);
		nvgTextLetterSpacing(vg, -2);
		nvgTextAlign(vg, NVG_ALIGN_CENTER);
		static const int portX0[4] = {20, 63, 106, 149};
		for (int i=0; i<BANDS; i++) {
			char fVal[10];
			snprintf(fVal, sizeof(fVal), "%1i", (int)module->freq[i]);
			nvgFillColor(vg,nvgRGBA(0, 0, 0, 255));
			nvgText(vg, portX0[i%(BANDS/4)]+1, 35+43*(int)(i/4), fVal, NULL);
		}
	}
};

ZINCWidget::ZINCWidget() {
	ZINC *module = new ZINC();
	setModule(module);
	box.size = Vec(15*13, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/ZINC.svg")));
		addChild(panel);
	}

	ZINCDisplay *display = new ZINCDisplay();
	display->module = module;
	display->box.pos = Vec(12, 12);
	display->box.size = Vec(110, 70);
	addChild(display);

	static const float portX0[4] = {20, 63, 106, 149};

	for (int i = 0; i < BANDS; i++) {
		controls[i]=createParam<BidooziNCColoredKnob>(Vec(portX0[i%(BANDS/4)]-1, 50+43*(int)(i/4)), module, ZINC::BG_PARAM + i, 0, 2, 1);
		BidooziNCColoredKnob *control = dynamic_cast<BidooziNCColoredKnob*>(controls[i]);
		control->coeff=module->peaks+i;
		addParam(controls[i]);
	}
	addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1]+5, 230), module, ZINC::ATTACK_PARAM, 0.0, 0.25, 0.0));
	addParam(createParam<BidooBlueTrimpot>(Vec(portX0[2]+5, 230), module, ZINC::DECAY_PARAM, 0.0, 0.25, 0.0));
	addParam(createParam<BidooBlueKnob>(Vec(portX0[0]+21, 268), module, ZINC::GMOD_PARAM, 1, 10, 1));
	addParam(createParam<BidooBlueKnob>(Vec(portX0[1]+21, 268), module, ZINC::GCARR_PARAM, 1, 10, 1));
	addParam(createParam<BidooBlueKnob>(Vec(portX0[2]+21, 268), module, ZINC::G_PARAM, 1, 10, 1));

	addInput(createInput<PJ301MPort>(Vec(portX0[0]+27.5, 320), module, ZINC::IN_MOD));
	addInput(createInput<PJ301MPort>(Vec(portX0[1]+22.5, 320), module, ZINC::IN_CARR));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[2]+16.5, 320), module, ZINC::OUT));

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));
}

void ZINCWidget::step() {
	for (int i = 0; i < BANDS; i++) {
			BidooziNCColoredKnob* knob = dynamic_cast<BidooziNCColoredKnob*>(controls[i]);
			knob->dirty = true;
	}
	ModuleWidget::step();
}

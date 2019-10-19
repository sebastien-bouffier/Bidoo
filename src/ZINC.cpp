#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/resampler.hpp"
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
	Biquad* iFilter[2 * BANDS];
	Biquad* cFilter[2 * BANDS];
	float mem[BANDS] = { 0.0f };
	float freq[BANDS] = { 125.0f, 185.0f, 270.0f, 350.0f, 430.0f, 530.0f, 630.0f, 780.0f,
						  950.0f, 1150.0f, 1380.0f, 1680.0f, 2070.0f, 2780.0f, 3800.0f, 6400.0f };
	float peaks[BANDS] = { 0.0f };
	const float slewMin = 0.001f;
	const float slewMax = 500.0f;
	const float shapeScale = 0.1f;

	ZINC() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		//16 bands
		for (int i = 0; i < BANDS; i++) {
			configParam(BG_PARAM + i, 0.f, 2.f, 1.f, "Band " + std::to_string(i + 1) + " Gain", "dB", -10, 40);//gets cast to int
		}
		configParam(ATTACK_PARAM, 0., 0.25, 0., "Attack Time", "s", params[ATTACK_PARAM].getValue(), 1000.f);
		configParam(DECAY_PARAM, 0., 0.25, 0., "Decay Time", "s", params[DECAY_PARAM].getValue(), 1000.f);
		configParam(GMOD_PARAM, 1.f, 10.f, 1.f, "Modifier", "%", 0.f, 10.f);
		configParam(GCARR_PARAM, 1.f, 10.f, 1.f, "Carrier", "%", 0.f, 10.f);
		configParam(G_PARAM, 1.f, 10.f, 1.f, "Output Gain", "%", 0.f, 10.f);
		//configParam(G_PARAM, 1.f, 10.f, 1.f, "Output Gain", "dB", -10, 40);

		//obj Biquad
		for (int i = 0; i < 2 * BANDS; i++) { //inside process causes memory leak
			iFilter[i] = new Biquad(bq_type_bandpass, freq[i%BANDS] / APP->engine->getSampleRate(), 5.0, 6.0);
			cFilter[i] = new Biquad(bq_type_bandpass, freq[i%BANDS] / APP->engine->getSampleRate(), 5.0, 6.0);
		}
	}

	void process(const ProcessArgs &args) override {
		float inM = inputs[IN_MOD].getVoltage() / 5.0f;
		float inC = inputs[IN_CARR].getVoltage() / 5.0f;
		float attack = params[ATTACK_PARAM].getValue();
		float decay = params[DECAY_PARAM].getValue();
		float slewAttack = slewMax * powf(slewMin / slewMax, attack);
		float slewDecay = slewMax * powf(slewMin / slewMax, decay);
		float out = 0.0f;

		for (int i = 0; i < BANDS; i++) {
			float coeff = mem[i];
			float peak = abs(iFilter[i + BANDS]->process(iFilter[i]->process(inM * params[GMOD_PARAM].getValue())));
			if (peak > coeff) {
				coeff += slewAttack * shapeScale * (peak - coeff) / args.sampleRate;
				if (coeff > peak)
					coeff = peak;
			} else if (peak < coeff) {
				coeff -= slewDecay * shapeScale * (coeff - peak) / args.sampleRate;
				if (coeff < peak)
					coeff = peak;
			}
			peaks[i] = peak;
			mem[i] = coeff;
			out += cFilter[i + BANDS]->process(cFilter[i]->process(inC*params[GCARR_PARAM].getValue())) * coeff * params[BG_PARAM + i].getValue();
		}
		outputs[OUT].setVoltage(out * 5.0f * params[G_PARAM].getValue());
	}
};

struct ZINCDisplay : TransparentWidget {
	ZINC *module;
	std::shared_ptr<Font> font;

	ZINCDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void draw(NVGcontext *vg) override {
		nvgFontSize(vg, 12);
		nvgFontFaceId(vg, font->handle);
		nvgStrokeWidth(vg, 2);
		nvgTextLetterSpacing(vg, -2);
		nvgTextAlign(vg, NVG_ALIGN_CENTER);
		static const int portX0[4] = { 20, 63, 106, 149 };
		if (module) {
			for (int i = 0; i < BANDS; i++) {
				nvgStrokeWidth(vg, 0.0f);
				nvgBeginPath(vg);
				int corrCoef = rescale(clamp(module->peaks[i],0.0f,1.0f),0.0f,1.0f,0.0f,255.0f);
				nvgFillColor(vg, nvgRGBA(clamp(42+corrCoef,0,255), clamp(87-corrCoef,0,255), clamp(117-corrCoef,0,255), 255));
				nvgRoundedRect(vg,portX0[i % (BANDS / 4)]-10, 26 + 43 * (int)(i / 4),25,10,3.0f);
				nvgFill(vg);
				nvgClosePath(vg);


				char fVal[10];
				snprintf(fVal, sizeof(fVal), "%1i", (int)module->freq[i]);
				nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
				nvgText(vg, portX0[i % (BANDS / 4)] + 3, 35 + 43 * (int)(i / 4), fVal, NULL);
			}
		}
	}
};

struct ZINCWidget : ModuleWidget {
	ZINCWidget(ZINC *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ZINC.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		static const float portX0[4] = { 20, 63, 106, 149 };

		{
			ZINCDisplay *display = new ZINCDisplay();
			display->module = module;
			display->box.pos = Vec(14, 14);
			display->box.size = Vec(110, 70);
			addChild(display);
		}

		for (int i = 0; i < BANDS; i++) {
			addParam(createParam<BidooBlueKnob>(Vec(portX0[i % (BANDS / 4)] + 2, 50 + 43 * (int)(i / 4) + 2), module, ZINC::BG_PARAM + i));
		}

		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1] + 4, 230), module, ZINC::ATTACK_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[2] + 4, 230), module, ZINC::DECAY_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[0] + 20, 268), module, ZINC::GMOD_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[1] + 20, 268), module, ZINC::GCARR_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[2] + 20, 268), module, ZINC::G_PARAM));

		addInput(createInput<PJ301MPort>(Vec(portX0[0] + 27.5, 320), module, ZINC::IN_MOD));
		addInput(createInput<PJ301MPort>(Vec(portX0[1] + 22.5, 320), module, ZINC::IN_CARR));
		addOutput(createOutput<PJ301MPort>(Vec(portX0[2] + 16.5, 320), module, ZINC::OUT));
	}
};

Model *modelZINC = createModel<ZINC, ZINCWidget>("ziNC");

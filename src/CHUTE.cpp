#include "Bidoo.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include <vector>
#include "cmath"

using namespace std;


struct CHUTE : Module {
	enum ParamIds {
		ALTITUDE_PARAM,
		GRAVITY_PARAM,
		COR_PARAM,
		RUN_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		TRIG_INPUT,
		ALTITUDE_INPUT,
		GRAVITY_INPUT,
		COR_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		GATE_OUTPUT,
		PITCH_OUTPUT,
		PITCHSTEP_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	bool running = false;
	float phase = 0.0f;
	float altitude = 0.0f;
	float altitudeInit = 0.0f;
	float minAlt = 0.0f;
	float speed = 0.0f;
	bool desc = false;

	SchmittTrigger playTrigger;
	SchmittTrigger gateTypeTrigger;

	CHUTE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) { }

	void step() override;

};


void CHUTE::step() {

	// Running
	if (playTrigger.process(params[RUN_PARAM].value + inputs[TRIG_INPUT].value)) {
		running = true;
		desc = true;
		altitude = params[ALTITUDE_PARAM].value + inputs[ALTITUDE_INPUT].value;
		altitudeInit = altitude;
		minAlt = altitude;
		speed = 0.0f;
	}

	// Altitude calculation
	if (running) {
		if (minAlt<0.0001f) {
			running = false;
			altitude = 0.0f;
			minAlt = 0.0f;
		}
		else
		{
			phase = 1.0f / engineGetSampleRate();
			if (desc) {
				speed += (params[GRAVITY_PARAM].value + inputs[GRAVITY_INPUT].value)*phase;
				altitude = altitude - (speed * phase);
				if (altitude <= 0.0f) {
					desc=false;
					speed = speed * (params[COR_PARAM].value + + inputs[COR_INPUT].value);
					altitude = 0.0f;
				}
			}
			else {
				speed = speed - (params[GRAVITY_PARAM].value + inputs[GRAVITY_INPUT].value)*phase;
				if (speed<=0.0f) {
					speed = 0.0f;
					desc=true;
					minAlt=min(minAlt,altitude);
				}
				else {
					altitude = altitude + (speed * phase);
				}
			}
		}
	}

	//Calculate output
	outputs[GATE_OUTPUT].value = running ? desc ? 10.0f : 0.0f : 0.0f;
	outputs[PITCH_OUTPUT].value = running ? 10.0f * altitude/ altitudeInit : 0.0f;
	outputs[PITCHSTEP_OUTPUT].value = running ? 10.0f * minAlt/ altitudeInit : 0.0f;
}

struct CHUTEDisplay : TransparentWidget {
	CHUTE *module;
	int frame = 0;
	shared_ptr<Font> font;

	CHUTEDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void draw(NVGcontext *vg) override {
		frame = 0;
		nvgFontSize(vg, 18.0f);
		nvgFontFaceId(vg, font->handle);
		nvgTextLetterSpacing(vg, -2.0f);
		nvgFillColor(vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));

		float altRatio = clampf(module->altitude / module->altitudeInit, 0.0f, 1.0f);
		int pos = roundl(box.size.y + altRatio * (9.0f - box.size.y));

		nvgText(vg, 6.0f, pos, "☻", NULL);
	}
};

CHUTEWidget::CHUTEWidget() {
	CHUTE *module = new CHUTE();
	setModule(module);
	box.size = Vec(15.0f*9.0f, 380.0f);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/ChUTE.svg")));
		addChild(panel);
	}

	addChild(createScrew<ScrewSilver>(Vec(15.0f, 0.0f)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30.0f, 0.0f)));
	addChild(createScrew<ScrewSilver>(Vec(15.0f, 365.0f)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30.0f, 365.0f)));

	{
		CHUTEDisplay *display = new CHUTEDisplay();
		display->module = module;
		display->box.pos = Vec(110.0f, 30.0f);
		display->box.size = Vec(40.0f, 180.0f);
		addChild(display);
	}

	static const float portX[2] = {20.0f, 60.0f};
	static const float portY[3] = {52.0f, 116.0f, 178.0f};
 	addInput(createInput<PJ301MPort>(Vec(portX[0], portY[0]), module, CHUTE::ALTITUDE_INPUT));
	addParam(createParam<BidooBlueKnob>(Vec(portX[1], portY[0]-2.0f), module, CHUTE::ALTITUDE_PARAM, 0.01f, 3.0f, 1.0f));
	addInput(createInput<PJ301MPort>(Vec(portX[0], portY[1]), module, CHUTE::GRAVITY_INPUT));
	addParam(createParam<BidooBlueKnob>(Vec(portX[1], portY[1]-2.0f), module, CHUTE::GRAVITY_PARAM, 1.622f, 11.15f, 9.798f)); // between the Moon and Neptune
	addInput(createInput<PJ301MPort>(Vec(portX[0], portY[2]), module, CHUTE::COR_INPUT));
	addParam(createParam<BidooBlueKnob>(Vec(portX[1], portY[2]-2.0f), module, CHUTE::COR_PARAM, 0.0f, 1.0f, 0.69f)); // 0 inelastic, 1 perfect elastic, 0.69 glass

	addParam(createParam<BlueCKD6>(Vec(51.0f, 269.0f), module, CHUTE::RUN_PARAM, 0.0f, 1.0f, 0.0f));
	addInput(createInput<PJ301MPort>(Vec(11.0f, 270.0f), module, CHUTE::TRIG_INPUT));

	addOutput(createOutput<PJ301MPort>(Vec(11.0f, 320.0f), module, CHUTE::GATE_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(54.0f, 320.0f), module, CHUTE::PITCH_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(96.0f, 320.0f), module, CHUTE::PITCHSTEP_OUTPUT));
}

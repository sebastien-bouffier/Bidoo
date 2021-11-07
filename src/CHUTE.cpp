#include "plugin.hpp"
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
	float altitudeInit = 0.01f;
	float minAlt = 0.0f;
	float speed = 0.0f;
	bool desc = false;

	dsp::SchmittTrigger playTrigger;
	dsp::SchmittTrigger gateTypeTrigger;

	CHUTE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(ALTITUDE_PARAM, 0.01f, 3.f, 1.f, "Altitude", " m", 0.01f, 3.f);
		configParam(GRAVITY_PARAM, 1.622f, 11.15f, 9.798f, "Gravity", " m/s²", 1.622f, 11.15f); // between the Moon and Neptune
		configParam(COR_PARAM, 0.0f, 1.0f, 0.69f, "Coefficient of restitution", "", 0.f, 1.f);  // 0 inelastic, 1 perfect elastic, 0.69 glass
		configParam(RUN_PARAM, 0.f, 1.f, 0.f, "Trig");
	}

	void process(const ProcessArgs &args) override;
};


void CHUTE::process(const ProcessArgs &args) {

	// Running
	if (playTrigger.process(params[RUN_PARAM].getValue() + inputs[TRIG_INPUT].getVoltage())) {
		running = true;
		desc = true;
		altitude = params[ALTITUDE_PARAM].getValue() + inputs[ALTITUDE_INPUT].getVoltage();
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
			phase = 1.0f / args.sampleRate;
			if (desc) {
				speed += (params[GRAVITY_PARAM].value + inputs[GRAVITY_INPUT].value)*phase;
				altitude = altitude - (speed * phase);
				if (altitude <= 0.0f) {
					desc=false;
					speed = speed * (params[COR_PARAM].value + inputs[COR_INPUT].value);
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
	outputs[GATE_OUTPUT].setVoltage(running ? desc ? 10.0f : 0.0f : 0.0f);
	outputs[PITCH_OUTPUT].setVoltage(running ? 10.0f * altitude/ altitudeInit : 0.0f);
	outputs[PITCHSTEP_OUTPUT].setVoltage(running ? 10.0f * minAlt/ altitudeInit : 0.0f);
}

struct CHUTEDisplay : TransparentWidget {
	CHUTE *module;
	int frame = 0;

	CHUTEDisplay() {

	}

	void draw(const DrawArgs &args) override {
		nvgGlobalTint(args.vg, color::WHITE);
		frame = 0;
		nvgSave(args.vg);
		nvgFontSize(args.vg, 18.0f);
		nvgTextLetterSpacing(args.vg, -2.0f);
		nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));
		float altRatio = 0.f;
		if (module != NULL) {
			 altRatio = clamp(module->altitude / module->altitudeInit, 0.0f, 1.0f);
		}
		float pos = box.size.y + altRatio * (9.0f - box.size.y);
		nvgText(args.vg, 6.0f, pos, "☻", NULL);
		nvgRestore(args.vg);
	}
};

struct CHUTEWidget : ModuleWidget {
	CHUTEWidget(CHUTE *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CHUTE.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

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
		addInput(createInput<PJ301MPort>(Vec(portX[0], portY[1]), module, CHUTE::GRAVITY_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX[0], portY[2]), module, CHUTE::COR_INPUT));
    addInput(createInput<PJ301MPort>(Vec(7.0f, 283.0f), module, CHUTE::TRIG_INPUT));

		addParam(createParam<BidooBlueKnob>(Vec(portX[1]-1, portY[0]-2.0f), module, CHUTE::ALTITUDE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX[1]-1, portY[1]-2.0f), module, CHUTE::GRAVITY_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX[1]-1, portY[2]-2.0f), module, CHUTE::COR_PARAM));
		addParam(createParam<BlueCKD6>(Vec(53.0f, 277.0f), module, CHUTE::RUN_PARAM));

		addOutput(createOutput<PJ301MPort>(Vec(7, 330), module, CHUTE::GATE_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(55, 330), module, CHUTE::PITCH_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(103.5f, 330), module, CHUTE::PITCHSTEP_OUTPUT));
	}
};

Model *modelCHUTE = createModel<CHUTE, CHUTEWidget>("ChUTE");

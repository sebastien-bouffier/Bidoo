#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include <vector>
#include "cmath"

using namespace std;

// A ball dropped from an altitude, bouncing with a coefficient of
// restitution until it rests. The gate rises on each impact and stays high
// until the top of the bounce (at least 1 ms). One ball per polyphony channel.
struct ChuteBall {
	dsp::SchmittTrigger trigger;
	dsp::PulseGenerator impactPulse;
	bool running = false;
	bool desc = false;
	float altitude = 0.f;
	float altitudeInit = 1.f;
	float minAlt = 0.f;
	float speed = 0.f;

	void drop(float alt) {
		running = true;
		desc = true;
		altitude = alt;
		altitudeInit = alt;
		minAlt = alt;
		speed = 0.f;
	}

	// Returns the gate.
	bool process(float dt, float gravity, float cor) {
		if (running) {
			if (minAlt < 0.0001f) {
				running = false;
				altitude = 0.f;
				minAlt = 0.f;
			}
			else if (desc) {
				speed += gravity * dt;
				altitude -= speed * dt;
				if (altitude <= 0.f) {
					desc = false;
					speed *= cor;
					altitude = 0.f;
					impactPulse.trigger(1e-3f);
				}
			}
			else {
				speed -= gravity * dt;
				if (speed <= 0.f) {
					speed = 0.f;
					desc = true;
					minAlt = std::min(minAlt, altitude);
				}
				else {
					altitude += speed * dt;
				}
			}
		}
		bool pulse = impactPulse.process(dt);
		return pulse || (running && !desc);
	}
};

struct CHUTE : BidooModule {
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

	static constexpr float GRAVITY_MIN = 1.622f;   // the Moon
	static constexpr float GRAVITY_MAX = 11.15f;   // Neptune

	ChuteBall balls[16];
	dsp::SchmittTrigger buttonTrigger;

	CHUTE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(ALTITUDE_PARAM, 0.01f, 3.f, 1.f, "Altitude", " m");
		configParam(GRAVITY_PARAM, GRAVITY_MIN, GRAVITY_MAX, 9.798f, "Gravity", " m/s²");
		configParam(COR_PARAM, 0.0f, 1.0f, 0.69f, "Coefficient of restitution (0 inelastic, 1 elastic)");
		configButton(RUN_PARAM, "Drop");
		configInput(TRIG_INPUT, "Drop trigger");
		configInput(ALTITUDE_INPUT, "Altitude CV (1 V = 1 m)");
		configInput(GRAVITY_INPUT, "Gravity CV (1 V = 1 m/s²)");
		configInput(COR_INPUT, "Restitution CV (1 V = 0.1)");
		configOutput(GATE_OUTPUT, "Gate, high from each impact to the top of the bounce");
		configOutput(PITCH_OUTPUT, "Height (0-10 V of the drop)");
		configOutput(PITCHSTEP_OUTPUT, "Bounce peak height (0-10 V of the drop)");
	}

	void process(const ProcessArgs &args) override {
		int channels = std::max(inputs[TRIG_INPUT].getChannels(), 1);
		bool button = buttonTrigger.process(params[RUN_PARAM].getValue());

		for (int c = 0; c < channels; c++) {
			ChuteBall &b = balls[c];
			bool trig = b.trigger.process(inputs[TRIG_INPUT].getVoltage(c), 0.1f, 1.f);
			if (trig || button)
				b.drop(clamp(params[ALTITUDE_PARAM].getValue() + inputs[ALTITUDE_INPUT].getPolyVoltage(c), 0.01f, 10.f));

			float gravity = clamp(params[GRAVITY_PARAM].getValue() + inputs[GRAVITY_INPUT].getPolyVoltage(c), GRAVITY_MIN, GRAVITY_MAX);
			float cor = clamp(params[COR_PARAM].getValue() + inputs[COR_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f);
			bool gate = b.process(args.sampleTime, gravity, cor);

			outputs[GATE_OUTPUT].setVoltage(gate ? 10.f : 0.f, c);
			outputs[PITCH_OUTPUT].setVoltage(b.running ? 10.f * b.altitude / b.altitudeInit : 0.f, c);
			outputs[PITCHSTEP_OUTPUT].setVoltage(b.running ? 10.f * b.minAlt / b.altitudeInit : 0.f, c);
		}
		outputs[GATE_OUTPUT].setChannels(channels);
		outputs[PITCH_OUTPUT].setChannels(channels);
		outputs[PITCHSTEP_OUTPUT].setChannels(channels);
	}
};

struct CHUTEDisplay : TransparentWidget {
	CHUTE *module;

	CHUTEDisplay() {

	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			nvgSave(args.vg);
			nvgScissor(args.vg,-1,-1,box.size.x+2,box.size.y+1);
			nvgFillColor(args.vg, BLUE_BIDOO);
			nvgStrokeColor(args.vg, BLUE_BIDOO);
			nvgStrokeWidth(args.vg, 1);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg,0,0,box.size.x+2,box.size.y,0.0f);
			nvgClosePath(args.vg);
			nvgStroke(args.vg);
			nvgFill(args.vg);

			nvgFontSize(args.vg, 18.0f);

			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			// The ball of channel 1.
			float altRatio = 0.f;
			if (module != NULL) {
				const ChuteBall &b = module->balls[0];
				altRatio = clamp(b.altitude / b.altitudeInit, 0.0f, 1.0f);
			}
			float pos = box.size.y * (1-altRatio);
			nvgText(args.vg, -0.3f, pos, "☻", NULL);
			nvgResetScissor(args.vg);
			nvgRestore(args.vg);
		}
		Widget::drawLayer(args, layer);
	}

};

struct CHUTEWidget : BidooWidget {
	CHUTEWidget(CHUTE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/CHUTE.svg"));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		{
			CHUTEDisplay *display = new CHUTEDisplay();
			display->module = module;
			display->box.pos = Vec(111.0f, 30.0f);
			display->box.size = Vec(15.0f, 180.0f);
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

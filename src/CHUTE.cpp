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

		theme::addScrews(this);

		// The original hand-drawn panel. Top: three framed groups, a CV jack joined by a line to its knob; both sit on
		// the line the panel draws (the old code had the knob 0.9 px below the jack). Bottom: the boxes' centres for the
		// jacks, the run button over the ALT box on the line from the TRIG jack.
		CHUTEDisplay *display = new CHUTEDisplay();
		display->module = module;
		display->box = theme::specificRect(111, 30, 15, 180);
		addChild(display);

		const theme::Pos cvCol = theme::specificCorner(20, 52, theme::kJackSize);         // the CV jacks' column
		const theme::Pos knobCol = theme::specificCorner(59, 50, theme::kBlueKnobSize);   // the knobs' column
		const theme::Pos groupLine[3] = {theme::specific(0, 64.344f), theme::specific(0, 128.469f), theme::specific(0, 190.531f)};
		const int cvIds[3] = {CHUTE::ALTITUDE_INPUT, CHUTE::GRAVITY_INPUT, CHUTE::COR_INPUT};
		const int knobIds[3] = {CHUTE::ALTITUDE_PARAM, CHUTE::GRAVITY_PARAM, CHUTE::COR_PARAM};
		for (int i = 0; i < 3; i++) {
			addInput(theme::input<PJ301MPort>(theme::colRow(cvCol, groupLine[i]), module, cvIds[i]));
			addParam(theme::param<BidooBlueKnob>(theme::colRow(knobCol, groupLine[i]), module, knobIds[i]));
		}

		const float kBoxPitch = 48.344f;                            // from one bottom box's centre to the next
		const theme::Pos fallCol = theme::specific(19.156f, 0);     // the centre of the TRIG and FALL boxes
		const theme::Pos fall = theme::colRow(fallCol, theme::specificCorner(7, 330, theme::kJackSize));
		const theme::Pos alt = fall.right(kBoxPitch);
		addInput(theme::input<PJ301MPort>(theme::colRow(fallCol, theme::specificCorner(7, 283, theme::kJackSize)), module, CHUTE::TRIG_INPUT));
		addParam(theme::param<BlueCKD6>(theme::colRow(alt, theme::specificCorner(53, 277, theme::kCKD6Size)), module, CHUTE::RUN_PARAM));
		addOutput(theme::output<PJ301MPort>(fall, module, CHUTE::GATE_OUTPUT));
		addOutput(theme::output<PJ301MPort>(alt, module, CHUTE::PITCH_OUTPUT));
		addOutput(theme::output<PJ301MPort>(alt.right(kBoxPitch), module, CHUTE::PITCHSTEP_OUTPUT));
	}
};

Model *modelCHUTE = createModel<CHUTE, CHUTEWidget>("ChUTE");

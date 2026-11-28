#include "plugin.hpp"
#include "BidooComponents.hpp"
#include <math.h>

using namespace std;

struct LOURDE : BidooModule {
	enum ParamIds {
		WEIGHT1,
		WEIGHT2,
		WEIGHT3,
		OUTFLOOR,
		NUM_PARAMS
	};
	enum InputIds {
		IN1,
		IN2,
		IN3,
    INWEIGHT1,
		INWEIGHT2,
		INWEIGHT3,
    INFLOOR,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};


	bool state[PORT_MAX_CHANNELS] = {};
	// knob + CV of the first channel, for the displays
	float shown[4] = {};

	LOURDE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < 3; i++) {
			configParam(WEIGHT1 + i, -5.0f, 5.0f, 0.0f, "Weight " + std::to_string(i + 1));
			configInput(IN1 + i, "Input " + std::to_string(i + 1));
			configInput(INWEIGHT1 + i, "Weight " + std::to_string(i + 1) + " CV");
		}
		configParam(OUTFLOOR, -10.0f, 10.0f, 0.0f, "Threshold");
		configInput(INFLOOR, "Threshold CV");
		configOutput(OUT, "Gate (high while the weighted sum reaches the threshold)");
	}

	void process(const ProcessArgs &args) override;
};

// 0.1 either side of the threshold, so a sum sitting on it doesn't chatter
static const float LOURDE_HYSTERESIS = 0.1f;

void LOURDE::process(const ProcessArgs &args) {
	int channels = 1;
	// as many channels as the widest input, weight and threshold CVs included
	for (int i = 0; i < 3; i++) {
		channels = std::max(channels, inputs[IN1 + i].getChannels());
		channels = std::max(channels, inputs[INWEIGHT1 + i].getChannels());
	}
	channels = std::max(channels, inputs[INFLOOR].getChannels());
	for (int c = 0; c < channels; c++) {
		float w[3], sum = 0.f;
		for (int i = 0; i < 3; i++) {
			w[i] = clamp(params[WEIGHT1 + i].getValue() + inputs[INWEIGHT1 + i].getPolyVoltage(c), -5.0f, 5.0f);
			sum += w[i] * inputs[IN1 + i].getPolyVoltage(c);
		}
		float floor = clamp(params[OUTFLOOR].getValue() + inputs[INFLOOR].getPolyVoltage(c), -10.0f, 10.0f);
		if (state[c] ? sum < floor - LOURDE_HYSTERESIS : sum >= floor + LOURDE_HYSTERESIS)
			state[c] = !state[c];
		outputs[OUT].setVoltage(state[c] ? 10.0f : 0.0f, c);
		if (c == 0) {
			shown[0] = w[0];
			shown[1] = w[1];
			shown[2] = w[2];
			shown[3] = floor;
		}
	}
	outputs[OUT].setChannels(channels);
}

// A value, printed upwards in yellow on the panel's screen: the widget's box is the screen, the text at textAt in it
struct LourdeValueDisplay : TransparentWidget {
	float *value = nullptr;
	Vec textAt;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && value) {
			char display[128];
			snprintf(display, sizeof(display), "%2.2f", *value);
			nvgFontSize(args.vg, 14.0f);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgTranslate(args.vg, textAt.x, textAt.y);
			nvgRotate(args.vg, -1.0f * M_PI_2);
			nvgText(args.vg, 0.0f, 0.0f, display, NULL);
		}
		Widget::drawLayer(args, layer);
	}
};

struct LOURDEWidget : BidooWidget {
	LOURDEWidget(LOURDE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/LOURDE.svg"));

		// No screws, as before. The panel of tools/panels/lourde.py: the old arrangement, three groups of a value screen,
		// a weight knob joined by a line to its CV jack, and an input box below; the floor group above the output. The
		// knobs and the full jacks on the panel's centre (they were 0.24 and 0.15 px left), the rest at its old places.
		const float w = box.size.x;
		const float kGroupPitch = 80.f;   // from one group to the next, GROUP_PITCH in tools/panels/lourde.py
		const theme::Pos middle = theme::specific(w * 0.5f, 0);
		const theme::Pos cvCol = theme::specificCorner(56, 37, theme::kTinyJackSize);
		const theme::Pos groupLine[3] = {theme::specific(0, 45.312f), theme::specific(0, 125.188f), theme::specific(0, 205.125f)};
		const theme::Pos in1 = theme::colRow(middle, theme::specificCorner(25.5f, 77, theme::kJackSize));
		const int inIds[3] = {LOURDE::IN1, LOURDE::IN2, LOURDE::IN3};
		const int cvIds[3] = {LOURDE::INWEIGHT1, LOURDE::INWEIGHT2, LOURDE::INWEIGHT3};
		const int knobIds[3] = {LOURDE::WEIGHT1, LOURDE::WEIGHT2, LOURDE::WEIGHT3};
		for (int i = 0; i < 3; i++)
			addInput(theme::input<PJ301MPort>(in1.down(kGroupPitch * i), module, inIds[i]));
		for (int i = 0; i < 3; i++)
			addInput(theme::input<TinyPJ301MPort>(theme::colRow(cvCol, groupLine[i]), module, cvIds[i]));
		for (int i = 0; i < 3; i++)
			addParam(theme::param<BidooBlueKnob>(theme::colRow(middle, groupLine[i]), module, knobIds[i]));

		// The value screens drawn by the panel (SCREEN_X... in tools/panels/lourde.py); the text where it was
		const theme::Area screen = theme::specificRect(2.44f, 27.81f, 17.12f, 35.f);
		const theme::Area floorScreen = theme::specificRect(2.44f, 276.06f, 17.12f, 41.f);
		const theme::Pos valueAt = theme::specific(15, 45), floorValueAt = theme::specific(15, 296);
		auto display = [&](theme::Area area, theme::Pos text, float *value) {
			LourdeValueDisplay *d = new LourdeValueDisplay();
			d->box = area;
			d->textAt = Vec(text).minus(d->box.pos);
			d->value = value;
			addChild(d);
		};
		for (int i = 0; i < 3; i++)
			display(theme::specificRect(screen.x, screen.y + kGroupPitch * i, screen.w, screen.h), valueAt.down(kGroupPitch * i), module ? &module->shown[i] : NULL);
		display(floorScreen, floorValueAt, module ? &module->shown[3] : NULL);

		addParam(theme::param<BidooBlueKnob>(theme::colRow(middle, theme::specificCorner(22.5f, 282, theme::kBlueKnobSize)), module, LOURDE::OUTFLOOR));
		addInput(theme::input<TinyPJ301MPort>(theme::specificCorner(56, 289, theme::kTinyJackSize), module, LOURDE::INFLOOR));
		addOutput(theme::output<PJ301MPort>(theme::sameCol(middle, theme::row(0)), module, LOURDE::OUT));   // on the bottom row (0.15 px lower)
	}
};

Model *modelLOURDE = createModel<LOURDE, LOURDEWidget>("LoURdE");

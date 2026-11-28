#include "plugin.hpp"
#include "BidooComponents.hpp"
#include <math.h>

using namespace std;

struct DILEMO : BidooModule {
	enum ParamIds {
		THRESHOLD_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN1_AND,
		IN2_AND,
		IN_NOT,
    IN1_OR,
		IN2_OR,
		IN1_XOR,
		IN2_XOR,
		NUM_INPUTS
	};
	enum OutputIds {
		AND_OUT,
		NAND_OUT,
		OR_OUT,
		NOR_OUT,
		XOR_OUT,
		XNOR_OUT,
		NOT_OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	// each input's state per channel; goes high above threshold + 0.1 V and low at or below
	// the threshold, so 0/10 V gates work at the default 0 V threshold
	bool high[NUM_INPUTS][PORT_MAX_CHANNELS] = {};

	DILEMO() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(THRESHOLD_PARAM, 0.0f, 10.0f, 0.0f, "Threshold", " V");
		configInput(IN1_AND, "AND input 1");
		configInput(IN2_AND, "AND input 2");
		configInput(IN_NOT, "NOT input");
		configInput(IN1_OR, "OR input 1");
		configInput(IN2_OR, "OR input 2");
		configInput(IN1_XOR, "XOR input 1");
		configInput(IN2_XOR, "XOR input 2");
		configOutput(AND_OUT, "AND");
		configOutput(NAND_OUT, "NAND");
		configOutput(OR_OUT, "OR");
		configOutput(NOR_OUT, "NOR");
		configOutput(XOR_OUT, "XOR");
		configOutput(XNOR_OUT, "XNOR");
		configOutput(NOT_OUT, "NOT");
	}

	bool read(int input, int c, float threshold) {
		float v = inputs[input].getPolyVoltage(c);
		bool &h = high[input][c];
		if (h ? v <= threshold : v > threshold + 0.1f)
			h = !h;
		return h;
	}

	// a gate and its inverse, polyphonic over the channels of its inputs
	void gate(int in1, int in2, int out, int outInv, int op, float threshold) {
		int channels = std::max(1, std::max(inputs[in1].getChannels(), in2 >= 0 ? inputs[in2].getChannels() : 0));
		for (int c = 0; c < channels; c++) {
			// both inputs read every sample: a short-circuit would skip the
			// second one's hysteresis update and leave it in a stale state
			bool a = read(in1, c, threshold);
			bool b = in2 >= 0 ? read(in2, c, threshold) : false;
			bool r;
			if (op == 0)
				r = a && b;
			else if (op == 1)
				r = a || b;
			else if (op == 2)
				r = a != b;
			else
				r = a;
			if (out >= 0)
				outputs[out].setVoltage(r ? 10.0f : 0.0f, c);
			outputs[outInv].setVoltage(r ? 0.0f : 10.0f, c);
		}
		if (out >= 0)
			outputs[out].setChannels(channels);
		outputs[outInv].setChannels(channels);
	}

	void process(const ProcessArgs &args) override {
		float threshold = params[THRESHOLD_PARAM].getValue();
		gate(IN1_AND, IN2_AND, AND_OUT, NAND_OUT, 0, threshold);
		gate(IN1_OR, IN2_OR, OR_OUT, NOR_OUT, 1, threshold);
		gate(IN1_XOR, IN2_XOR, XOR_OUT, XNOR_OUT, 2, threshold);
		gate(IN_NOT, -1, -1, NOT_OUT, 3, threshold);
	}
};

struct DILEMOWidget : BidooWidget {
	DILEMOWidget(DILEMO *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/DILEMO.svg"));

		// No screws, as before. The panel of tools/panels/dilemo.py: the old arrangement, the threshold knob, then the AND,
		// OR and XOR pairs and NOT, each row an input beside its output. One box per pair of jacks (and one for NOT), 26 px
		// wide and 6 px from the sides as poupre's: the inputs on the left boxes' centre, the outputs on the right ones'
		// (0.15 px right of before). NOT on the bottom row, XOR's lower jacks on row 1, the pairs a row apart, the two jacks
		// of a pair 37 px apart (they were 43: 4 to 10 px higher, the first 2 px lower).
		const float w = box.size.x;
		const float kBoxSide = 6.f, kBoxWidth = 26.f;   // BOX_SIDE and BOX_W in tools/panels/dilemo.py
		const float kPairPitch = 37.f;                  // PAIR_PITCH in tools/panels/dilemo.py
		const float kKnobY = 49.f;                      // KNOB_Y in tools/panels/dilemo.py: 4.24 px lower, clear of the title
		const theme::Pos inCol = theme::specific(kBoxSide + 0.5f * kBoxWidth, 0);
		const theme::Pos outCol = theme::specific(w - inCol.x, 0);
		addParam(theme::param<BidooBlueKnob>(theme::specific(w * 0.5f, kKnobY), module, DILEMO::THRESHOLD_PARAM));   // on the centre (0.24 px right)

		// The rows, top to bottom: each pair's upper and lower jack, then NOT on the bottom row
		float rowY[7];
		for (int j = 0; j < 3; j++) {
			rowY[2 * j + 1] = theme::row(1) - (2 - j) * (theme::kRowPitch + kPairPitch);
			rowY[2 * j] = rowY[2 * j + 1] - kPairPitch;
		}
		rowY[6] = theme::row(0);
		const int inIds[7] = {DILEMO::IN1_AND, DILEMO::IN2_AND, DILEMO::IN1_OR, DILEMO::IN2_OR, DILEMO::IN1_XOR, DILEMO::IN2_XOR, DILEMO::IN_NOT};
		const int outIds[7] = {DILEMO::AND_OUT, DILEMO::NAND_OUT, DILEMO::OR_OUT, DILEMO::NOR_OUT, DILEMO::XOR_OUT, DILEMO::XNOR_OUT, DILEMO::NOT_OUT};
		for (int i = 0; i < 7; i++)
			addInput(theme::input<PJ301MPort>(theme::sameCol(inCol, rowY[i]), module, inIds[i]));
		for (int i = 0; i < 7; i++)
			addOutput(theme::output<PJ301MPort>(theme::sameCol(outCol, rowY[i]), module, outIds[i]));
	}
};

Model *modelDILEMO = createModel<DILEMO, DILEMOWidget>("DilEMO");

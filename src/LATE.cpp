#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;

// Swing: every second clock is delayed by a share of the time since the
// clock before it. Everything is counted in samples, so the result is the
// same in real time, offline or in a DAW. Gates keep their length, and each
// polyphony channel swings on its own.
struct LateChannel {
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	bool inHigh = false;
	long sinceRise = -1;      // samples since the last clock, -1 = none yet
	bool nextOdd = true;      // the next clock goes straight through

	bool direct = false;      // an odd gate, following the input
	long directStart = -1;    // samples until a postponed odd gate starts

	long delayLeft = -1;      // samples until the delayed even gate starts
	bool measuring = false;   // the even gate being delayed is still high at the input
	long measured = 0;
	long delayedLength = -1;  // its length once known
	bool delayed = false;     // the delayed gate is playing
	long delayedElapsed = 0;

	bool lastOut = false;
	bool gap = false;         // one low sample, so two gates in a row give two edges

	void startDelayed() {
		delayLeft = -1;
		delayed = true;
		delayedElapsed = 0;
		gap = gap || lastOut;
	}

	void startDirect() {
		direct = true;
		gap = gap || lastOut;
	}

	bool process(float clock, float reset, float swing) {
		if (resetTrigger.process(reset, 0.1f, 1.f)) {
			// Re-phase only: the next clock is an odd one, nothing is sent.
			nextOdd = true;
			delayLeft = -1;
			measuring = false;
		}

		// Counted before this sample's clock, so a delay of d samples lands d samples after it.
		if (delayLeft > 0 && --delayLeft == 0)
			startDelayed();

		if (sinceRise >= 0)
			sinceRise++;
		bool rise = clockTrigger.process(clock, 0.1f, 1.f);
		bool fall = inHigh && !clockTrigger.isHigh();
		inHigh = clockTrigger.isHigh();

		if (rise) {
			long interval = sinceRise;
			sinceRise = 0;
			bool flushed = false;
			if (delayLeft >= 0) {
				// The delayed clock is late for the next one: it leaves now.
				startDelayed();
				flushed = true;
			}
			if (nextOdd) {
				nextOdd = false;
				if (flushed)
					directStart = 2;
				else
					startDirect();
			}
			else {
				nextOdd = true;
				long delay = interval > 0 ? (long)std::round(swing * interval) : 0;
				if (delay < 1) {
					if (flushed)
						directStart = 2;
					else
						startDirect();
				}
				else {
					delayLeft = delay;
					measuring = true;
					measured = 0;
					delayedLength = -1;
				}
			}
		}

		if (measuring) {
			if (inHigh || rise)
				measured++;
			if (fall) {
				measuring = false;
				delayedLength = std::max(measured, 1L);
			}
		}
		if (fall)
			direct = false;

		if (directStart > 0 && --directStart == 0) {
			directStart = -1;
			startDirect();
			if (!inHigh)
				direct = false;
		}

		if (delayed) {
			delayedElapsed++;
			// Until the input gate has ended its length is unknown: keep it high.
			if (delayedLength >= 0 && delayedElapsed > delayedLength)
				delayed = false;
		}

		bool out = (direct || delayed) && !gap;
		gap = false;
		lastOut = out;
		return out;
	}
};

struct LATE : BidooModule {
	enum ParamIds {
		SWING_PARAM,
		CVCOEFF_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		SWING_INPUT,
		CLOCK_INPUT,
		RESET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		CLOCK_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	LateChannel channels[16];

	LATE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// 0..9 delays the even clocks by 0..90% of the interval: 50% (straight) to 95% swing.
		configParam(SWING_PARAM, 0, 9, 0, "Swing", "%", 0.f, 5.f, 50.f);
		configParam(CVCOEFF_PARAM, -1.0f, 1.0f, 0.0f, "Swing CV amount", "%", 0.f, 100.f);
		configInput(SWING_INPUT, "Swing CV");
		configInput(CLOCK_INPUT, "Clock / gates");
		configInput(RESET_INPUT, "Reset (next clock is not delayed)");
		configOutput(CLOCK_OUTPUT, "Swung clock / gates");
	}

	void process(const ProcessArgs &args) override {
		int n = std::max(inputs[CLOCK_INPUT].getChannels(), 1);
		float knob = params[SWING_PARAM].getValue();
		float amount = params[CVCOEFF_PARAM].getValue();
		for (int c = 0; c < n; c++) {
			float swing = clamp(knob + amount * inputs[SWING_INPUT].getPolyVoltage(c), 0.f, 9.f) / 10.f;
			bool out = channels[c].process(inputs[CLOCK_INPUT].getVoltage(c), inputs[RESET_INPUT].getPolyVoltage(c), swing);
			outputs[CLOCK_OUTPUT].setVoltage(out ? 10.f : 0.f, c);
		}
		outputs[CLOCK_OUTPUT].setChannels(n);
	}
};

struct LATEWidget : BidooWidget {
	LATEWidget(LATE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/LATE.svg"));

		// No screws, as before. The original hand-drawn panel: one column down the middle, where the panel draws the line
		// from the knob through its CV amount to its CV jack and centres every label. Each row keeps its old top; the
		// column is the panel's centre.
		const float kBoxPitch = 47.f;   // from one box to the next: RST, IN, OUT
		const theme::Pos swing = theme::sameRow(theme::specificCorner(8, 70, theme::kBlueKnobSize), box.size.x * 0.5f);
		const theme::Pos cvAmount = theme::colRow(swing, theme::specificCorner(13, 105, theme::kTrimpotSize));
		const theme::Pos swingCv = theme::colRow(swing, theme::specificCorner(10, 130, theme::kJackSize));
		const theme::Pos reset = theme::colRow(swing, theme::specificCorner(10, 236, theme::kJackSize));
		const theme::Pos clock = reset.down(kBoxPitch);
		addParam(theme::param<BidooBlueKnob>(swing, module, LATE::SWING_PARAM));
		addParam(theme::param<BidooBlueTrimpot>(cvAmount, module, LATE::CVCOEFF_PARAM));
		addInput(theme::input<PJ301MPort>(swingCv, module, LATE::SWING_INPUT));
		addInput(theme::input<PJ301MPort>(reset, module, LATE::RESET_INPUT));
		addInput(theme::input<PJ301MPort>(clock, module, LATE::CLOCK_INPUT));
		addOutput(theme::output<PJ301MPort>(clock.down(kBoxPitch), module, LATE::CLOCK_OUTPUT));
	}
};

Model *modelLATE = createModel<LATE, LATEWidget>("lATe");

#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "MetroSeq.hpp"

using namespace std;

// DTROY with a gate probability, a random pitch and an accent per stage; the
// engine is shared with DTROY (MetroSeq.hpp).
struct BORDL : MetroModule<BORDL> {
	enum ParamIds {
		CLOCK_PARAM,
		RUN_PARAM,
		RESET_PARAM,
		STEPS_PARAM,
		SLIDE_TIME_PARAM,
		GATE_TIME_PARAM,
		ROOT_NOTE_PARAM,
		SCALE_PARAM,
		PLAY_MODE_PARAM,
		COUNT_MODE_PARAM,
		PATTERN_PARAM,
		SENSITIVITY_PARAM,
		TRIG_COUNT_PARAM = SENSITIVITY_PARAM + 8,
		TRIG_TYPE_PARAM = TRIG_COUNT_PARAM + 8,
		TRIG_PITCH_PARAM = TRIG_TYPE_PARAM + 8,
		TRIG_SLIDE_PARAM = TRIG_PITCH_PARAM + 8,
		TRIG_SKIP_PARAM = TRIG_SLIDE_PARAM + 8,
		TRIG_GATEPROB_PARAM = TRIG_SKIP_PARAM + 8,
		TRIG_PITCHRND_PARAM = TRIG_GATEPROB_PARAM + 8,
		TRIG_ACCENT_PARAM = TRIG_PITCHRND_PARAM + 8,
		TRIG_RNDACCENT_PARAM = TRIG_ACCENT_PARAM + 8,
		LEFT_PARAM = TRIG_RNDACCENT_PARAM + 8,
		RIGHT_PARAM,
		UP_PARAM,
		DOWN_PARAM,
		COPY_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT,
		EXT_CLOCK_INPUT,
		RESET_INPUT,
		STEPS_INPUT,
		SLIDE_TIME_INPUT,
		GATE_TIME_INPUT,
		ROOT_NOTE_INPUT,
		SCALE_INPUT,
		EXTGATE1_INPUT,
		EXTGATE2_INPUT,
		PATTERN_INPUT,
		TRANSPOSE_INPUT,
		SENSITIVITY_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		GATE_OUTPUT,
		PITCH_OUTPUT,
		ACC_OUTPUT,
		STEP_OUTPUT,
		NUM_OUTPUTS = STEP_OUTPUT + 8
	};
	enum LightIds {
		RUNNING_LIGHT,
		RESET_LIGHT,
		STEPS_LIGHTS, // 8: each step's flash, not drawn: MetroSeq fades it into the slide and skip lights
		SLIDES_LIGHTS = STEPS_LIGHTS + 8,
		SKIPS_LIGHTS = SLIDES_LIGHTS + 8,
		COPY_LIGHT = SKIPS_LIGHTS + 8,
		PLAYMODE_LIGHT,
		COUNTMODE_LIGHT = PLAYMODE_LIGHT + 3,
		NUM_LIGHTS = COUNTMODE_LIGHT + 3
	};

	static constexpr bool EXTENDED = true;

	// as dTrOY: Rack runs it under the engine lock and keeps it in its undo
	void onRandomize(const RandomizeEvent &e) override {
		// the knobs first, as Rack does, then the slides and skips
		MetroModule<BORDL>::onRandomize(e);
		randomizeSlidesSkips();
	}

	BORDL() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configMetro(*this);
		configOutput(ACC_OUTPUT, "Accent");
		for (int i = 0; i < 8; i++) {
			configParam(TRIG_GATEPROB_PARAM + i, 0.f, 1.f, 1.f, rack::string::f("Stage %d probability", i + 1), "%", 0.f, 100.f);
			configParam(TRIG_PITCHRND_PARAM + i, 0.f, 1.f, 0.f, rack::string::f("Stage %d random pitch (100%% = +-5 V)", i + 1), "%", 0.f, 100.f);
			configParam(TRIG_ACCENT_PARAM + i, 0.f, 10.f, 0.f, rack::string::f("Stage %d accent", i + 1), " V");
			configParam(TRIG_RNDACCENT_PARAM + i, 0.f, 1.f, 0.f, rack::string::f("Stage %d random accent (100%% = +-5 V)", i + 1), "%", 0.f, 100.f);
		}
	}
};

struct BORDLGateDisplay : TransparentWidget {
	BORDL *module;

	int index;

	BORDLGateDisplay() {

	}

	void drawGate(const DrawArgs &args, Vec pos) {
		if (module) {
			int gateType = (int)module->params[BORDL::TRIG_TYPE_PARAM+index].getValue();
			nvgStrokeWidth(args.vg, 1.0f);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgFontSize(args.vg, 16.0f);
			nvgTextLetterSpacing(args.vg, -2.0f);
			if (gateType == 0) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,22.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
			}
			else if (gateType == 1) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,22.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,6.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgFill(args.vg);
			}
			else if (gateType == 2) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,6.0f,6.0f,0.0f);
				nvgRoundedRect(args.vg,pos.x+8.0f,pos.y,6.0f,6.0f,0.0f);
				nvgRoundedRect(args.vg,pos.x+16.0f,pos.y,6.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgFill(args.vg);
			}
			else if (gateType == 3) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,22.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgFill(args.vg);
			}
			else if (gateType == 4) {
			  nvgText(args.vg, pos.x+11.0f, pos.y+8.0f, "G1", NULL);
			}
			else if (gateType == 5) {
			  nvgText(args.vg, pos.x+11.0f, pos.y+8.0f, "G2", NULL);
			}
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			drawGate(args, Vec(0.0f, 0.0f));
		}
		Widget::drawLayer(args, layer);
	}

};

struct BORDLPulseDisplay : TransparentWidget {
	BORDL *module;

	int index;

	BORDLPulseDisplay() {

	}

	void drawPulse(const DrawArgs &args, Vec pos) {
		if (module) {
			nvgStrokeWidth(args.vg, 1.0f);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgFontSize(args.vg, 16.0f);
			nvgTextLetterSpacing(args.vg, -2.0f);
			char tCount[128];
			snprintf(tCount, sizeof(tCount), "%1i", (int)module->params[BORDL::TRIG_COUNT_PARAM+index].getValue());
			nvgText(args.vg, pos.x, pos.y, tCount, NULL);
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			drawPulse(args, Vec(0.0f, 0.0f));
		}
		Widget::drawLayer(args, layer);
	}

};

struct BORDLPitchDisplay : TransparentWidget {
	BORDL *module;

	int index;

	BORDLPitchDisplay() {

	}

	void drawPitch(const DrawArgs &args, Vec pos) {
		if (module) {
			nvgStrokeWidth(args.vg, 3.0f);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgFontSize(args.vg, 16.0f);
			const MetroPattern &p = module->engine.patterns[module->engine.selectedPattern];
			float v = module->engine.quantize(*module, p, module->params[BORDL::TRIG_PITCH_PARAM + index].getValue());
			nvgText(args.vg, pos.x, pos.y-9.0f, module->engine.quant.noteName(v).c_str(), NULL);
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			drawPitch(args, Vec(0.0f, 0.0f));
		}
		Widget::drawLayer(args, layer);
	}

};

struct BORDLWidget : MetroWidget<BORDL> {
	void appendContextMenu(ui::Menu *menu) override;

	BORDLWidget(BORDL *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/BORDL.svg"));

		theme::addScrews(this);

		// Migrated from the hand-drawn panel (tools/panels/bordl.py), keeping its arrangement. Left: the section shared
		// with DTROY (metroLeftControls in MetroSeq.hpp) over five jacks in their boxes. Right: the eight stages, a
		// dense grid at its old corners: the note screen over the pitch knob and its random trimpot, the accent knob
		// and its random trimpot, the pulse screen over the pulse knob and its probability trimpot, the gate screen
		// over the gate trimpot, slide and skip buttons, the tiny stage output on its red square.
		metroLeftControls(this, module);

		// the bottom row: five boxes as before, the jacks within 0.1 px of their old places (BOX_X in bordl.py)
		const float kBoxFirst = 21.9f, kBoxPitch = 33.2f;
		const int boxIds[5] = {BORDL::EXTGATE1_INPUT, BORDL::EXTGATE2_INPUT, BORDL::GATE_OUTPUT, BORDL::PITCH_OUTPUT, BORDL::ACC_OUTPUT};
		for (int i = 0; i < 5; i++) {
			const theme::Pos p = theme::specific(kBoxFirst + kBoxPitch * i, theme::row(0));
			if (i < 2)
				addInput(theme::input<PJ301MPort>(p, module, boxIds[i]));
			else
				addOutput(theme::output<PJ301MPort>(p, module, boxIds[i]));
		}

		// the pitch sensitivity under S, at its old corners (the jack 0.1 px right, under the trimpot)
		const theme::Pos sensitivity = theme::specificCorner(176, 30, theme::kTrimpotSize);
		addParam(theme::param<BidooBlueTrimpot>(sensitivity, module, BORDL::SENSITIVITY_PARAM));
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(sensitivity, theme::specificCorner(0, 52, theme::kTinyJackSize)), module, BORDL::SENSITIVITY_INPUT));

		// the stages, 41 px apart (STAGE_X in bordl.py): the knobs' column; the gate trimpots were 0.34 px right of it,
		// the slide and skip buttons 0.04, the tiny outputs 0.74. The trimpots beside the knobs and the displays keep
		// their old corners
		const float kStagePitch = 41.f;
		const theme::Pos firstStage = theme::specificCorner(201, 56, theme::kBlueKnobSize);   // the first pitch knob
		const float kAccentBelow = 54.f, kPulseBelow = 132.f;     // the accent and pulse knobs under the pitch knob
		const float kTrimRight = 26.f, kTrimBelow = 25.f;         // a random or probability trimpot's corner from its knob's
		const theme::Pos gateRow = theme::specificCorner(0, 267, theme::kTrimpotSize);
		const theme::Pos slideRow = theme::specific(0, 300.f);   // SLIDE_Y in bordl.py: 3.8 px higher than the old 10.6 px buttons
		const float kSkipBelow = 23.f;                           // the 20 px buttons need the room (they were 21 px apart, 1.8 px higher for skip)
		const theme::Pos outRow = theme::specificCorner(0, 344, theme::kTinyJackSize);
		const int knobIds[3] = {BORDL::TRIG_PITCH_PARAM, BORDL::TRIG_ACCENT_PARAM, BORDL::TRIG_COUNT_PARAM};
		const int trimIds[3] = {BORDL::TRIG_PITCHRND_PARAM, BORDL::TRIG_RNDACCENT_PARAM, BORDL::TRIG_GATEPROB_PARAM};
		const float knobBelow[3] = {0.f, kAccentBelow, kPulseBelow};
		for (int i = 0; i < 8; i++) {
			const theme::Pos stage = firstStage.right(kStagePitch * i);
			const float left = stage.left;   // the old corners' x
			for (int r = 0; r < 3; r++) {
				const theme::Pos knob = stage.down(knobBelow[r]);
				if (r == 2)
					addParam(theme::param<BidooBlueSnapKnob>(knob, module, knobIds[r] + i));
				else
					addParam(theme::param<BidooBlueKnob>(knob, module, knobIds[r] + i));
				addParam(theme::param<BidooBlueTrimpot>(theme::specificCorner(knob.left + kTrimRight, knob.top + kTrimBelow, theme::kTrimpotSize), module, trimIds[r] + i));
			}
			addParam(theme::param<BidooBlueSnapTrimpot>(theme::colRow(stage, gateRow), module, BORDL::TRIG_TYPE_PARAM + i));
			{
				BORDLPitchDisplay *displayPitch = new BORDLPitchDisplay();
				displayPitch->module = module;
				displayPitch->box = theme::specificRect(left + 15.f, 52.f, 20.f, 10.f);
				displayPitch->index = i;
				addChild(displayPitch);
				BORDLPulseDisplay *displayPulse = new BORDLPulseDisplay();
				displayPulse->module = module;
				displayPulse->box = theme::specificRect(left + 14.f, 179.f, 20.f, 10.f);
				displayPulse->index = i;
				addChild(displayPulse);
				BORDLGateDisplay *displayGate = new BORDLGateDisplay();
				displayGate->module = module;
				displayGate->box = theme::specificRect(left + 4.f, 250.f, 20.f, 10.f);
				displayGate->index = i;
				addChild(displayGate);
			}
			// slide and skip: round buttons, momentary as before, lit by their lights (the state, flipped while the
			// stage plays)
			const int lights[2] = {BORDL::SLIDES_LIGHTS + i, BORDL::SKIPS_LIGHTS + i};
			const int ids[2] = {BORDL::TRIG_SLIDE_PARAM + i, BORDL::TRIG_SKIP_PARAM + i};
			const theme::Pos rows[2] = {slideRow, slideRow.down(kSkipBelow)};
			for (int j = 0; j < 2; j++) {
				BidooRoundButton *b = theme::param<BidooRoundButton>(theme::colRow(stage, rows[j]), module, ids[j]);
				b->momentary = true;
				const int lightId = lights[j];
				b->litBy = [=]() { return module && module->lights[lightId].getBrightness() > 0.5f; };
				addParam(b);
			}
			addOutput(theme::output<TinyPJ301MPort>(theme::colRow(stage, outRow), module, BORDL::STEP_OUTPUT + i));
		}
	}
};

void BORDLWidget::appendContextMenu(ui::Menu *menu) {
	BidooWidget::appendContextMenu(menu);
	BORDL *m = dynamic_cast<BORDL*>(this->module);
	if (!m)
		return;
	appendMetroMenu(menu, m);
}

Model *modelBORDL = createModel<BORDL, BORDLWidget>("bordL");

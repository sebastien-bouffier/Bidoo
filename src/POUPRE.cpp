#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <iomanip>
#include "osdialog.h"
#include "SliceSampler.hpp"

using namespace std;

struct POUPRE : BidooModule {
	enum ParamIds {
		CHANNEL_PARAM,
		START_PARAM,
		LEN_PARAM,
		LOOP_PARAM,
		TUNE_PARAM,
		GATE_PARAM,
		PRESET_PARAM,
		NUM_PARAMS = PRESET_PARAM+4
	};
	enum InputIds {
		TRIG_INPUT,
		GATE_INPUT,
		START_INPUT,
		LEN_INPUT,
		LOOP_INPUT,
		VOCT_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		POLY_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		SAMPLE_LIGHT,
		PRESET_LIGHT = SAMPLE_LIGHT+3,
		NUM_LIGHTS = PRESET_LIGHT+4
	};

	static constexpr bool HAS_FILTER = false;
	SliceBank bank{1};
	SliceSettings settings[16];
	SliceVoice voices[16];
	int currentChannel = 0;
	dsp::SchmittTrigger presetTriggers[4];
	SliceRun run;
	dsp::ClockDivider slow;  // the sample handoff, lights and preset buttons

	POUPRE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		slow.setDivision(32);
		sliceConfig(*this);
		for (int i = 0; i < 4; i++)
			configButton(PRESET_PARAM + i, rack::string::f("Preset: %d slices", 8 << i));
	}

	void process(const ProcessArgs &args) override;

	// Bypassed: still takes the loaded samples, so the loader never waits.
	void processBypass(const ProcessArgs &args) override {
		bank.poll(args.sampleRate);
		Module::processBypass(args);
	}

	void onRandomize() override {
		for (int i = 0; i < 16; i++)
			settings[i].randomize();
		sliceLoadPanel(*this, settings[currentChannel]);
	}

	void onReset() override {
		for (int i = 0; i < 16; i++)
			settings[i].reset();
		currentChannel = 0;
		sliceLoadPanel(*this, settings[0]);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "lastPath", json_string(bank.path(0).c_str()));
		json_object_set_new(rootJ, "currentChannel", json_integer(currentChannel));
		for (int i = 0; i < 16; i++) {
			json_t *channelJ = settings[i].toJson();
			json_object_set_new(rootJ, ("channel" + to_string(i)).c_str(), channelJ);
		}
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (json_is_string(lastPathJ) && json_string_value(lastPathJ)[0])
			bank.load(0, json_string_value(lastPathJ));
		else
			bank.clear(0);   // saved with no sample: not the one loaded before
		for (int i = 0; i < 16; i++) {
			json_t *channelJ = json_object_get(rootJ, ("channel" + to_string(i)).c_str());
			settings[i].fromJson(channelJ);
		}
		json_t *currentChannelJ = json_object_get(rootJ, "currentChannel");
		currentChannel = currentChannelJ ? clamp((int)json_integer_value(currentChannelJ), 0, 15) : 0;
		params[CHANNEL_PARAM].setValue(currentChannel);
		sliceLoadPanel(*this, settings[currentChannel]);
	}
};

void POUPRE::process(const ProcessArgs &args) {
	if (slow.process()) {
		bank.poll(args.sampleRate);
		bool loaded = bank.current[0] && bank.current[0]->size() > 0;
		lights[SAMPLE_LIGHT].setBrightness(loaded ? 0.0f : 1.0f);
		lights[SAMPLE_LIGHT+1].setBrightness(loaded ? 1.0f : 0.0f);
		lights[SAMPLE_LIGHT+2].setBrightness(0.0f);

		float dt = args.sampleTime * slow.getDivision();
		for (int i = 0; i < 4; i++) {
			slicePresetLight(lights[PRESET_LIGHT + i], dt);
			if (presetTriggers[i].process(params[PRESET_PARAM + i].getValue())) {
				slicePreset(settings, i == 0 ? 8 : 16, 8 << i);
				lights[PRESET_LIGHT + i].setBrightness(1.0f);
				sliceLoadPanel(*this, settings[currentChannel]);
			}
		}
	}

	const SliceSample *smp = bank.current[0];
	sliceProcess(*this, run, args, [smp](int) { return smp; });
}

struct POUPREWidget : BidooWidget {
	POUPREWidget(POUPRE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/POUPRE.svg"));

		// No screws, as before. The panel of tools/panels/poupre.py: the old arrangement (the sample light and the channel
		// knob under the title, the four preset buttons on the right, five rows of a boxed jack joined by a line to its
		// switch or knob, TRIG and OUT). The light and the channel knob on the panel's centre like the title (they were
		// 0.55 and 0.26 px left). One box per jack, 6 px from the sides: the jacks on the left boxes' centre (0.15 px
		// right of before), the controls and OUT on the right box's (0.24 px for the knobs, 0.16 for OUT). The jacks on
		// the bottom row and the old 47 px rows above it (0.15 px lower); the controls keep their old rows, 4 px above
		// their jacks, where the panel's line meets them.
		const float w = box.size.x;
		const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};     // SmallLight
		const theme::Size kMiniButtonSize = {mm2px(1.8f), mm2px(1.8f)};   // MiniLEDButton
		const theme::Size kCKSSSize = {14.f, 20.64111f};                  // CKSS
		const float kBoxSide = 6.f, kBoxWidth = 26.f;   // BOX_SIDE and BOX_W in tools/panels/poupre.py
		const float kRowPitch = 47.f;                   // ROW_PITCH in tools/panels/poupre.py
		const theme::Pos middle = theme::specific(w * 0.5f, 0);
		const theme::Pos jackCol = theme::specific(kBoxSide + 0.5f * kBoxWidth, 0);
		const theme::Pos controlCol = theme::specific(w - jackCol.x, 0);
		addChild(theme::light<SmallLight<RedGreenBlueLight>>(theme::colRow(middle, theme::specificCorner(34, 20, kSmallLightSize)), module, POUPRE::SAMPLE_LIGHT));
		addParam(theme::param<BidooBlueSnapKnob>(theme::colRow(middle, theme::specificCorner(23, 35, theme::kBlueKnobSize)), module, POUPRE::CHANNEL_PARAM));

		const theme::Pos gate = theme::colRow(controlCol, theme::specificCorner(49, 92.5f, kCKSSSize));
		const theme::Pos start = theme::colRow(controlCol, theme::specificCorner(41, 182, theme::kBlueKnobSize));
		addParam(theme::param<CKSS>(gate, module, POUPRE::GATE_PARAM));
		addParam(theme::param<CKSS>(gate.down(kRowPitch), module, POUPRE::LOOP_PARAM));
		addParam(theme::param<BidooBlueKnob>(start, module, POUPRE::START_PARAM));
		addParam(theme::param<BidooBlueKnob>(start.down(kRowPitch), module, POUPRE::LEN_PARAM));
		addParam(theme::param<BidooBlueKnob>(start.down(2.f * kRowPitch), module, POUPRE::TUNE_PARAM));

		const theme::Pos trig = theme::sameCol(jackCol, theme::row(0));
		const int inIds[5] = {POUPRE::GATE_INPUT, POUPRE::LOOP_INPUT, POUPRE::START_INPUT, POUPRE::LEN_INPUT, POUPRE::VOCT_INPUT};
		for (int i = 0; i < 5; i++)
			addInput(theme::input<PJ301MPort>(trig.down(-kRowPitch * (5 - i)), module, inIds[i]));
		addInput(theme::input<PJ301MPort>(trig, module, POUPRE::TRIG_INPUT));
		addOutput(theme::output<PJ301MPort>(theme::colRow(controlCol, trig), module, POUPRE::POLY_OUTPUT));

		// Preset buttons at their old corners, as maGma's: MiniLEDButton with its light, since four BidooRoundButtons
		// (20 px) don't fit beside the channel knob on 5 HP. Their numbers are in tools/panels/poupre.py (PRESET_TOPS)
		const float kPresetPairs[2] = {20.f, 62.f};   // tops of the two pairs of preset buttons, 10 px apart in a pair
		for (int i = 0; i < 4; i++)
			addParam(theme::param<MiniLEDButton>(theme::specificCorner(66, kPresetPairs[i / 2] + 10.f * (i % 2), kMiniButtonSize), module, POUPRE::PRESET_PARAM + i));
		for (int i = 0; i < 4; i++)
			addChild(theme::light<SmallLight<BlueLight>>(theme::specificCorner(66, kPresetPairs[i / 2] + 10.f * (i % 2), kSmallLightSize), module, POUPRE::PRESET_LIGHT + i));
	}

	struct POUPREItem : MenuItem {
  	POUPRE *module;
  	void onAction(const event::Action &e) override {
  		// the last folder a sample came from, kept between openings and sessions (plugin.hpp)
  		std::string dir = sampleDialogDir(module->bank.path(0));
  		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
  		if (path) {
				rememberSampleFolder(path);
				module->bank.load(0, path);
				free(path);
			}
  	}
  };

	void onPathDrop(const PathDropEvent& e) override {
		Widget::onPathDrop(e);
		POUPRE *module = dynamic_cast<POUPRE*>(this->module);
		if (module && !e.paths.empty()) {
			rememberSampleFolder(e.paths[0]);
			module->bank.load(0, e.paths[0]);
		}
	}

  void appendContextMenu(ui::Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		POUPRE *module = dynamic_cast<POUPRE*>(this->module);
		if (!module)
			return;
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<POUPREItem>(&MenuItem::text, "Load sample", &POUPREItem::module, module));
	}
};

Model *modelPOUPRE = createModel<POUPRE, POUPREWidget>("POUPRE");

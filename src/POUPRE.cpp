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
		SPEED_PARAM,
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
		SPEED_INPUT,
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
		if (lastPathJ && json_string_value(lastPathJ)[0])
			bank.load(0, json_string_value(lastPathJ));
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

		// No screws, as before. The original hand-drawn panel: the sample light and channel knob under the title with the
		// four preset buttons on the right; five rows of a boxed jack joined by a line to its switch or knob; TRIG and
		// OUT. Old corners kept, except the light, now on the panel's centre like the title (it was 0.55 px left), and
		// OUT on its box's centre (it was 0.5 px left). The jacks sit 3.8 px below the lines to their controls, low in
		// their boxes under the labels: moving them up would hit the labels, so they stay.
		const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};     // SmallLight
		const theme::Size kMiniButtonSize = {mm2px(1.8f), mm2px(1.8f)};   // MiniLEDButton
		const theme::Size kCKSSSize = {14.f, 20.64111f};                  // CKSS
		const float kRowPitch = 47.f;
		addChild(theme::light<SmallLight<RedGreenBlueLight>>(theme::sameRow(theme::specificCorner(34, 20, kSmallLightSize), box.size.x * 0.5f), module, POUPRE::SAMPLE_LIGHT));
		addParam(theme::param<BidooBlueSnapKnob>(theme::specificCorner(23, 35, theme::kBlueKnobSize), module, POUPRE::CHANNEL_PARAM));

		const theme::Pos gate = theme::specificCorner(49, 92.5f, kCKSSSize);
		const theme::Pos start = theme::specificCorner(41, 182, theme::kBlueKnobSize);
		addParam(theme::param<CKSS>(gate, module, POUPRE::GATE_PARAM));
		addParam(theme::param<CKSS>(gate.down(kRowPitch), module, POUPRE::LOOP_PARAM));
		addParam(theme::param<BidooBlueKnob>(start, module, POUPRE::START_PARAM));
		addParam(theme::param<BidooBlueKnob>(start.down(kRowPitch), module, POUPRE::LEN_PARAM));
		addParam(theme::param<BidooBlueKnob>(start.down(2.f * kRowPitch), module, POUPRE::SPEED_PARAM));

		const theme::Pos gateIn = theme::specificCorner(7, 95, theme::kJackSize);
		const int inIds[5] = {POUPRE::GATE_INPUT, POUPRE::LOOP_INPUT, POUPRE::START_INPUT, POUPRE::LEN_INPUT, POUPRE::SPEED_INPUT};
		for (int i = 0; i < 5; i++)
			addInput(theme::input<PJ301MPort>(gateIn.down(kRowPitch * i), module, inIds[i]));

		const float kPresetPairs[2] = {20.f, 62.f};   // tops of the two pairs of preset buttons, 10 px apart in a pair
		for (int i = 0; i < 4; i++)
			addParam(theme::param<MiniLEDButton>(theme::specificCorner(66, kPresetPairs[i / 2] + 10.f * (i % 2), kMiniButtonSize), module, POUPRE::PRESET_PARAM + i));
		for (int i = 0; i < 4; i++)
			addChild(theme::light<SmallLight<BlueLight>>(theme::specificCorner(66, kPresetPairs[i / 2] + 10.f * (i % 2), kSmallLightSize), module, POUPRE::PRESET_LIGHT + i));

		const theme::Pos trig = gateIn.down(5.f * kRowPitch);
		addInput(theme::input<PJ301MPort>(trig, module, POUPRE::TRIG_INPUT));
		addOutput(theme::output<PJ301MPort>(theme::colRow(theme::specific(55.844f, 0), trig), module, POUPRE::POLY_OUTPUT));   // the OUT box's centre
	}

	struct POUPREItem : MenuItem {
  	POUPRE *module;
  	void onAction(const event::Action &e) override {
  		std::string last = module->bank.path(0);
			std::string dir = last.empty() ? asset::user("") : rack::system::getDirectory(last);
  		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
  		if (path) {
				module->bank.load(0, path);
				free(path);
			}
  	}
  };

	void onPathDrop(const PathDropEvent& e) override {
		Widget::onPathDrop(e);
		POUPRE *module = dynamic_cast<POUPRE*>(this->module);
		if (module && !e.paths.empty())
			module->bank.load(0, e.paths[0]);
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

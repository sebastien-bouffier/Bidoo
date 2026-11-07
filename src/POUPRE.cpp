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

		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(34.0f, 20.0f), module, POUPRE::SAMPLE_LIGHT));

		addParam(createParam<BidooBlueSnapKnob>(Vec(23.0f,35.0f), module, POUPRE::CHANNEL_PARAM));

		addParam(createParam<CKSS>(Vec(49.0f,92.5f), module, POUPRE::GATE_PARAM));
		addParam(createParam<CKSS>(Vec(49.0f,139.5f), module, POUPRE::LOOP_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(41.0f,182.0f), module, POUPRE::START_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(41.0f,229.0f), module, POUPRE::LEN_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(41.0f,276.0f), module, POUPRE::SPEED_PARAM));

		addInput(createInput<PJ301MPort>(Vec(7.0f, 95.0f), module, POUPRE::GATE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(7.0f, 142.0f), module, POUPRE::LOOP_INPUT));
		addInput(createInput<PJ301MPort>(Vec(7.0f, 189.0f), module, POUPRE::START_INPUT));
		addInput(createInput<PJ301MPort>(Vec(7.0f, 236.0f), module, POUPRE::LEN_INPUT));
		addInput(createInput<PJ301MPort>(Vec(7.0f, 283.0f), module, POUPRE::SPEED_INPUT));

		addParam(createParam<MiniLEDButton>(Vec(66.0f, 20.0f), module, POUPRE::PRESET_PARAM));
		addParam(createParam<MiniLEDButton>(Vec(66.0f, 30.0f), module, POUPRE::PRESET_PARAM+1));
		addParam(createParam<MiniLEDButton>(Vec(66.0f, 62.0f), module, POUPRE::PRESET_PARAM+2));
		addParam(createParam<MiniLEDButton>(Vec(66.0f, 72.0f), module, POUPRE::PRESET_PARAM+3));
		addChild(createLight<SmallLight<BlueLight>>(Vec(66.0f, 20.0f), module, POUPRE::PRESET_LIGHT));
		addChild(createLight<SmallLight<BlueLight>>(Vec(66.0f, 30.0f), module, POUPRE::PRESET_LIGHT+1));
		addChild(createLight<SmallLight<BlueLight>>(Vec(66.0f, 62.0f), module, POUPRE::PRESET_LIGHT+2));
		addChild(createLight<SmallLight<BlueLight>>(Vec(66.0f, 72.0f), module, POUPRE::PRESET_LIGHT+3));

		addInput(createInput<PJ301MPort>(Vec(7.0f, 330.0f), module, POUPRE::TRIG_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(43.5f, 330.0f), module, POUPRE::POLY_OUTPUT));
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

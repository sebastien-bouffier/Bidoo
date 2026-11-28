#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <iomanip>
#include "osdialog.h"
#include "SliceSampler.hpp"

using namespace std;

struct MAGMA : BidooModule {
	enum ParamIds {
		START_PARAM,
		LEN_PARAM,
		LOOP_PARAM,
		TUNE_PARAM,
		GATE_PARAM,
		Q_PARAM,
		FREQ_PARAM,
		FILTERTYPE_PARAM,
		CHANNEL_PARAM,
		KILL_PARAM,
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
		Q_INPUT,
		FREQ_INPUT,
		FILTERTYPE_INPUT,
		KILL_INPUT,
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

	static constexpr bool HAS_FILTER = true;
	SliceBank bank{1};
	SliceSettings settings[16];
	SliceVoice voices[16];
	int currentChannel = 0;
	dsp::SchmittTrigger presetTriggers[4];
	SliceRun run;
	dsp::ClockDivider slow;  // the sample handoff, lights and preset buttons

	MAGMA() {
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

void MAGMA::process(const ProcessArgs &args) {
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

struct MAGMAWidget : BidooWidget {
	MAGMAWidget(MAGMA *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/MAGMA.svg"));

		theme::addScrews(this);

		// The panel of tools/panels/magma.py: oAi's with four preset buttons. The controls they share are placed in
		// src/SliceSampler.hpp
		sliceSamplerControls(this, module);

		// Preset buttons right of the channel knob, 10 px apart, each light centred on its number (8, 16, 32, 64); the
		// button keeps its old corner relative to its light. Specific: MiniLEDButton with its light, as before, since
		// four BidooRoundButtons (20 px) don't fit 10 px apart beside the knob. The numbers are in tools/panels/magma.py
		const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};     // SmallLight
		const theme::Size kMiniButtonSize = {mm2px(1.8f), mm2px(1.8f)};   // MiniLEDButton
		const float kPresetBelow = 10.f;   // PRESET_BELOW in tools/panels/magma.py
		const theme::Pos firstLight = theme::colRow(theme::specificCorner(109, 20, kSmallLightSize), theme::specific(0, 23.562f));   // on the 8
		const float lightTop = firstLight.y - kSmallLightSize.h * 0.5f;
		for (int i = 0; i < 4; i++)
			addParam(theme::param<MiniLEDButton>(theme::specificCorner(109, lightTop + kPresetBelow * i, kMiniButtonSize), module, MAGMA::PRESET_PARAM + i));
		for (int i = 0; i < 4; i++)
			addChild(theme::light<SmallLight<BlueLight>>(theme::specificCorner(109, lightTop + kPresetBelow * i, kSmallLightSize), module, MAGMA::PRESET_LIGHT + i));
	}

	struct MAGMAItem : MenuItem {
  	MAGMA *module;
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
		MAGMA *module = dynamic_cast<MAGMA*>(this->module);
		if (module && !e.paths.empty()) {
			rememberSampleFolder(e.paths[0]);
			module->bank.load(0, e.paths[0]);
		}
	}

  void appendContextMenu(ui::Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		MAGMA *module = dynamic_cast<MAGMA*>(this->module);
		if (!module)
			return;
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MAGMAItem>(&MenuItem::text, "Load sample", &MAGMAItem::module, module));
	}
};

Model *modelMAGMA = createModel<MAGMA, MAGMAWidget>("MAGMA");

#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <iomanip>
#include "osdialog.h"
#include "SliceSampler.hpp"

using namespace std;

struct OAI : BidooModule {
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
		NUM_PARAMS
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
		NUM_LIGHTS = SAMPLE_LIGHT+3
	};

	static constexpr bool HAS_FILTER = true;
	SliceBank bank{16};
	SliceSettings settings[16];
	SliceVoice voices[16];
	int currentChannel = 0;
	SliceRun run;
	dsp::ClockDivider slow;  // the sample handoff and the light

	OAI() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		slow.setDivision(32);
		sliceConfig(*this);
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
		json_object_set_new(rootJ, "currentChannel", json_integer(currentChannel));
		for (int i = 0; i < 16; i++) {
			json_t *channelJ = settings[i].toJson();
			json_object_set_new(channelJ, "lastPath", json_string(bank.path(i).c_str()));
			json_object_set_new(rootJ, ("channel" + to_string(i)).c_str(), channelJ);
		}
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		for (int i = 0; i < 16; i++) {
			json_t *channelJ = json_object_get(rootJ, ("channel" + to_string(i)).c_str());
			settings[i].fromJson(channelJ);
			json_t *lastPathJ = channelJ ? json_object_get(channelJ, "lastPath") : nullptr;
			if (json_is_string(lastPathJ) && json_string_value(lastPathJ)[0])
				bank.load(i, json_string_value(lastPathJ));
			else
				bank.clear(i);   // saved with no sample: not the one loaded before
		}
		json_t *currentChannelJ = json_object_get(rootJ, "currentChannel");
		currentChannel = currentChannelJ ? clamp((int)json_integer_value(currentChannelJ), 0, 15) : 0;
		params[CHANNEL_PARAM].setValue(currentChannel);
		sliceLoadPanel(*this, settings[currentChannel]);
	}
};

void OAI::process(const ProcessArgs &args) {
	if (slow.process()) {
		bank.poll(args.sampleRate);
		bool loaded = bank.current[currentChannel] && bank.current[currentChannel]->size() > 0;
		lights[SAMPLE_LIGHT].setBrightness(loaded ? 0.0f : 1.0f);
		lights[SAMPLE_LIGHT+1].setBrightness(loaded ? 1.0f : 0.0f);
		lights[SAMPLE_LIGHT+2].setBrightness(0.0f);
	}

	sliceProcess(*this, run, args, [this](int i) { return (const SliceSample *)bank.current[i]; });
}

struct OAIWidget : BidooWidget {
	OAIWidget(OAI *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/OAI.svg"));

		theme::addScrews(this);

		// The panel of tools/panels/oai.py: the old arrangement, the controls placed with maGma's (src/SliceSampler.hpp)
		sliceSamplerControls(this, module);
	}

	struct OAIItem : MenuItem {
  	OAI *module;
  	void onAction(const event::Action &e) override {
  		// the last folder a sample came from, kept between openings and sessions (plugin.hpp)
  		std::string dir = sampleDialogDir(module->bank.path(module->currentChannel));
  		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
  		if (path) {
				rememberSampleFolder(path);
				module->bank.load(module->currentChannel, path);
				free(path);
			}
  	}
  };

	void onPathDrop(const PathDropEvent& e) override {
		Widget::onPathDrop(e);
		OAI *module = dynamic_cast<OAI*>(this->module);
		if (module && !e.paths.empty()) {
			rememberSampleFolder(e.paths[0]);
			module->bank.load(module->currentChannel, e.paths[0]);
		}
	}

  void appendContextMenu(ui::Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		OAI *module = dynamic_cast<OAI*>(this->module);
		if (!module)
			return;
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<OAIItem>(&MenuItem::text, "Load sample", &OAIItem::module, module));
	}
};

Model *modelOAI = createModel<OAI, OAIWidget>("OAI");

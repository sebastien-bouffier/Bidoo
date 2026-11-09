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
		SPEED_PARAM,
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
		SPEED_INPUT,
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
			if (channelJ) {
				json_t *lastPathJ = json_object_get(channelJ, "lastPath");
				if (lastPathJ && json_string_value(lastPathJ)[0])
					bank.load(i, json_string_value(lastPathJ));
			}
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

		// The original hand-drawn panel: the sample light and channel knob under the title, two rows of three knobs,
		// the loop and gate switches with the kill knob, then three rows of boxed jacks. Knobs keep their old corners
		// (within 0.25 px of their labels); the light goes on the panel's centre, the loop switch between LOOP and OFF,
		// and every jack on its box's centre (the old code had them 0.27 to 0.65 px left).
		const float centre = box.size.x * 0.5f;
		const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};   // SmallLight
		const theme::Size kCKSSSize = {14.f, 20.64111f};                // CKSS
		addChild(theme::light<SmallLight<RedGreenBlueLight>>(theme::sameRow(theme::specificCorner(56.5f, 20, kSmallLightSize), centre), module, OAI::SAMPLE_LIGHT));
		addParam(theme::param<BidooBlueSnapKnob>(theme::specificCorner(45, 35, theme::kBlueKnobSize), module, OAI::CHANNEL_PARAM));

		const float kKnobPitch = 38.f;    // across
		const float kKnobRowBelow = 50.f;
		const theme::Pos start = theme::specificCorner(7, 85, theme::kBlueKnobSize);
		addParam(theme::param<BidooBlueKnob>(start, module, OAI::START_PARAM));
		addParam(theme::param<BidooBlueKnob>(start.right(kKnobPitch), module, OAI::LEN_PARAM));
		addParam(theme::param<BidooBlueKnob>(start.right(2.f * kKnobPitch), module, OAI::SPEED_PARAM));
		addParam(theme::param<BidooBlueSnapKnob>(start.down(kKnobRowBelow), module, OAI::FILTERTYPE_PARAM));
		addParam(theme::param<BidooBlueKnob>(start.offset(kKnobPitch, kKnobRowBelow), module, OAI::FREQ_PARAM));
		addParam(theme::param<BidooBlueKnob>(start.offset(2.f * kKnobPitch, kKnobRowBelow), module, OAI::Q_PARAM));
		addParam(theme::param<CKSS>(theme::colRow(theme::specific(22.f, 0), theme::specificCorner(14.5f, 190, kCKSSSize)), module, OAI::LOOP_PARAM));   // between LOOP and OFF
		addParam(theme::param<CKSS>(theme::specificCorner(53, 190, kCKSSSize), module, OAI::GATE_PARAM));
		addParam(theme::param<BidooBlueSnapKnob>(start.offset(2.f * kKnobPitch, 2.f * kKnobRowBelow), module, OAI::KILL_PARAM));

		const float kRowBelow = 47.f;
		const theme::Pos cvRow = theme::specificCorner(4, 236, theme::kJackSize);
		const theme::Pos boxCol[4] = {theme::specific(16.156f, 0), theme::specific(45.406f, 0), theme::specific(74.594f, 0), theme::specific(103.844f, 0)};   // the boxes' centres
		const int topIds[4] = {OAI::START_INPUT, OAI::LEN_INPUT, OAI::SPEED_INPUT, OAI::LOOP_INPUT};
		const int midIds[4] = {OAI::FILTERTYPE_INPUT, OAI::FREQ_INPUT, OAI::Q_INPUT, OAI::GATE_INPUT};
		for (int i = 0; i < 4; i++)
			addInput(theme::input<PJ301MPort>(theme::colRow(boxCol[i], cvRow), module, topIds[i]));
		for (int i = 0; i < 4; i++)
			addInput(theme::input<PJ301MPort>(theme::colRow(boxCol[i], cvRow).down(kRowBelow), module, midIds[i]));

		const theme::Pos bottomRow = cvRow.down(2.f * kRowBelow);
		addInput(theme::input<PJ301MPort>(theme::colRow(theme::specific(19.156f, 0), bottomRow), module, OAI::TRIG_INPUT));
		addInput(theme::input<PJ301MPort>(theme::colRow(theme::specific(centre, 0), bottomRow), module, OAI::KILL_INPUT));
		addOutput(theme::output<PJ301MPort>(theme::colRow(theme::specific(100.844f, 0), bottomRow), module, OAI::POLY_OUTPUT));
	}

	struct OAIItem : MenuItem {
  	OAI *module;
  	void onAction(const event::Action &e) override {
  		std::string last = module->bank.path(module->currentChannel);
			std::string dir = last.empty() ? asset::user("") : rack::system::getDirectory(last);
  		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
  		if (path) {
				module->bank.load(module->currentChannel, path);
				free(path);
			}
  	}
  };

	void onPathDrop(const PathDropEvent& e) override {
		Widget::onPathDrop(e);
		OAI *module = dynamic_cast<OAI*>(this->module);
		if (module && !e.paths.empty())
			module->bank.load(module->currentChannel, e.paths[0]);
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

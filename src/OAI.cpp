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
	OAI() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		sliceConfig(*this);
	}

	void process(const ProcessArgs &args) override;

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
	bank.poll(args.sampleRate);
	bool loaded = bank.current[currentChannel] && bank.current[currentChannel]->size() > 0;
	lights[SAMPLE_LIGHT].setBrightness(loaded ? 0.0f : 1.0f);
	lights[SAMPLE_LIGHT+1].setBrightness(loaded ? 1.0f : 0.0f);
	lights[SAMPLE_LIGHT+2].setBrightness(0.0f);


	// The panel shows the selected channel.
	int sel = clamp((int)params[CHANNEL_PARAM].getValue(), 0, 15);
	if (sel != currentChannel) {
		currentChannel = sel;
		sliceLoadPanel(*this, settings[currentChannel]);
	}
	sliceSavePanel(*this, settings[currentChannel]);

	int c = std::max(inputs[TRIG_INPUT].getChannels(), 1);
	for (int i = 0; i < c; i++) {
		bool rise = voices[i].trigger.process(inputs[TRIG_INPUT].getVoltage(i), 0.1f, 1.f);
		bool high = voices[i].trigger.isHigh();
		SliceControls ctl = sliceControls(*this, settings[i], i);
			bool killed = false;
			for (int j = 0; j < c; j++) {
				if (j != i && voices[j].active && sliceKill(*this, settings[j], j) == i) {
					killed = true;
					break;
				}
			}
		outputs[POLY_OUTPUT].setVoltage(slicePlay(voices[i], bank.current[i], ctl, rise, high, killed, HAS_FILTER, args.sampleTime), i);
	}
	for (int i = c; i < 16; i++)
		voices[i].active = false;
	outputs[POLY_OUTPUT].setChannels(c);
}

struct OAIWidget : BidooWidget {
	OAIWidget(OAI *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/OAI.svg"));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(56.5f, 20.0f), module, OAI::SAMPLE_LIGHT));

		addParam(createParam<BidooBlueSnapKnob>(Vec(45.0f,35.0f), module, OAI::CHANNEL_PARAM));

		addParam(createParam<BidooBlueKnob>(Vec(7.0f,85.0f), module, OAI::START_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(45.0f,85.0f), module, OAI::LEN_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(83.0f,85.0f), module, OAI::SPEED_PARAM));

		addParam(createParam<BidooBlueSnapKnob>(Vec(7.0f,135.0f), module, OAI::FILTERTYPE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(45.0f,135.0f), module, OAI::FREQ_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(83.0f,135.0f), module, OAI::Q_PARAM));

		addParam(createParam<CKSS>(Vec(14.5f,190.f), module, OAI::LOOP_PARAM));
		addParam(createParam<CKSS>(Vec(53.0f,190.f), module, OAI::GATE_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(83.0f,185.0f), module, OAI::KILL_PARAM));

		addInput(createInput<PJ301MPort>(Vec(4.0f, 236.0f), module, OAI::START_INPUT));
		addInput(createInput<PJ301MPort>(Vec(33.0f, 236.0f), module, OAI::LEN_INPUT));
		addInput(createInput<PJ301MPort>(Vec(62.5f, 236.0f), module, OAI::SPEED_INPUT));
		addInput(createInput<PJ301MPort>(Vec(91.5f, 236.0f), module, OAI::LOOP_INPUT));

		addInput(createInput<PJ301MPort>(Vec(4.0f, 283.0f), module, OAI::FILTERTYPE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(33.0f, 283.0f), module, OAI::FREQ_INPUT));
		addInput(createInput<PJ301MPort>(Vec(62.5f, 283.0f), module, OAI::Q_INPUT));
		addInput(createInput<PJ301MPort>(Vec(91.5f, 283.0f), module, OAI::GATE_INPUT));

		addInput(createInput<PJ301MPort>(Vec(7.0f, 330.0f), module, OAI::TRIG_INPUT));
		addInput(createInput<PJ301MPort>(Vec(47.5f, 330.0f), module, OAI::KILL_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(88.5f, 330.0f), module, OAI::POLY_OUTPUT));
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

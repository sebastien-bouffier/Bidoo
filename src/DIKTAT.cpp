#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <iterator>
#include "dep/quantizer.hpp"

using namespace std;

struct DIKTAT : BidooModule {
	enum ParamIds {
		CHANNEL_PARAM,
		GLOBAL_PARAM,
		ROOT_NOTE_PARAM,
		SCALE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		NOTE_INPUT,
		ROOT_NOTE_INPUT,
		SCALE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NOTE_TONIC_OUTPUT,
		NOTE_THIRD_OUTPUT,
		NOTE_FIFTH_OUTPUT,
		NOTE_SEVENTH_OUTPUT,
		NOTE_NINTH_OUTPUT,
		NOTE_ELEVENTH_OUTPUT,
		NOTE_THIRTEENTH_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	int currentChannel = 0;
	bool globalMode = true;
	int rootNote[16] = {0};
	int scale[16] = {0};
	float inputNote[16] = {0.0f};
	int lRootNote[16] = {0};
	int lScale[16] = {0};

	quantizer::Quantizer quant;
	quantizer::Chord chord;

	DIKTAT() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CHANNEL_PARAM, 0.0f, 15.0f, 0.0f, "Channel","",0,1,1);
		configParam(ROOT_NOTE_PARAM, -1.0f, quantizer::numNotes-2, 0.0f, "Root note","",0,1,1);
		configParam(SCALE_PARAM, 0.0f, quantizer::numScales-1, 1.0f, "Scale","",0,1,1);


		configInput(NOTE_INPUT, "1V/oct pitch");
		configInput(ROOT_NOTE_INPUT, "Root note");
		configInput(SCALE_INPUT, "Scale");

		configOutput(NOTE_TONIC_OUTPUT, "Tonic");
		configOutput(NOTE_THIRD_OUTPUT, "Third");
		configOutput(NOTE_FIFTH_OUTPUT, "Fifth");
		configOutput(NOTE_SEVENTH_OUTPUT, "Seventh");
		configOutput(NOTE_NINTH_OUTPUT, "Ninth");
		configOutput(NOTE_ELEVENTH_OUTPUT, "Eleventh");
		configOutput(NOTE_THIRTEENTH_OUTPUT, "Thirteenth");

		configSwitch(GLOBAL_PARAM, 0, 1, 0, "Mode", {"Individual","Global"});
  }

  void process(const ProcessArgs &args) override;


	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "currentChannel", json_integer(currentChannel));
		json_object_set_new(rootJ, "globalMode", json_boolean(globalMode));
		for (size_t i = 0; i<16 ; i++) {
			json_t *channelJ = json_object();
			json_object_set_new(channelJ, "rootNote", json_integer(rootNote[i]));
			json_object_set_new(channelJ, "scale", json_integer(scale[i]));
			json_object_set_new(rootJ, ("channel"+ to_string(i)).c_str(), channelJ);
		}
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		for (size_t i = 0; i<16 ; i++) {
			json_t *channelJ = json_object_get(rootJ, ("channel" + to_string(i)).c_str());
			if (channelJ){
				json_t *rootNoteJ = json_object_get(channelJ, "rootNote");
				if (rootNoteJ)
					rootNote[i] = json_integer_value(rootNoteJ);

				json_t *scaleJ = json_object_get(channelJ, "scale");
				if (scaleJ)
					scale[i] = json_integer_value(scaleJ);
			}
		}
		json_t *currentChannelJ = json_object_get(rootJ, "currentChannel");
		if (currentChannelJ) {
			currentChannel = json_integer_value(currentChannelJ);
		}
		json_t *globalModeJ = json_object_get(rootJ, "globalMode");
		if (globalModeJ) {
			globalMode = json_boolean_value(globalModeJ);
		}
	}
};

void DIKTAT::process(const ProcessArgs &args) {
	if (currentChannel != params[CHANNEL_PARAM].getValue()) {
		currentChannel = params[CHANNEL_PARAM].getValue();
		params[ROOT_NOTE_PARAM].setValue(rootNote[currentChannel]);
		params[SCALE_PARAM].setValue(scale[currentChannel]);
	}
	else {
		rootNote[currentChannel] = params[ROOT_NOTE_PARAM].getValue();
		scale[currentChannel] = params[SCALE_PARAM].getValue();
	}

	globalMode = params[GLOBAL_PARAM].getValue();
	int c = std::max(inputs[NOTE_INPUT].getChannels(), 1);

	outputs[NOTE_TONIC_OUTPUT].setChannels(c);
	outputs[NOTE_THIRD_OUTPUT].setChannels(c);
	outputs[NOTE_FIFTH_OUTPUT].setChannels(c);
	outputs[NOTE_SEVENTH_OUTPUT].setChannels(c);
	outputs[NOTE_NINTH_OUTPUT].setChannels(c);
	outputs[NOTE_ELEVENTH_OUTPUT].setChannels(c);
	outputs[NOTE_THIRTEENTH_OUTPUT].setChannels(c);

	for (int i=0;i<c;i++) {


		if (inputs[ROOT_NOTE_INPUT].isConnected()) {
			lRootNote[i] = rescale(clamp(rootNote[globalMode ? 0 : i] + inputs[ROOT_NOTE_INPUT].getVoltage(globalMode ? 0 : i), 0.0f,10.0f),0.0f,10.0f,0.0f,quantizer::numNotes-1);
		}
		else {
			lRootNote[i] = rootNote[globalMode ? 0 : i];
		}

		if (inputs[SCALE_INPUT].isConnected()) {
			lScale[i] = rescale(clamp(scale[globalMode ? 0 : i] + inputs[SCALE_INPUT].getVoltage(globalMode ? 0 : i), 0.0f,10.0f),0.0f,10.0f,0.0f,quantizer::numScales-1);
		}
		else {
			lScale[i] = scale[globalMode ? 0 : i];
		}

		inputNote[i] = inputs[NOTE_INPUT].getVoltage(i);

		chord = quant.closestChordInScale(inputNote[i], lRootNote[i], lScale[i]);

		outputs[NOTE_TONIC_OUTPUT].setVoltage(chord.tonic,i);
		outputs[NOTE_THIRD_OUTPUT].setVoltage(chord.third,i);
		outputs[NOTE_FIFTH_OUTPUT].setVoltage(chord.fifth,i);
		outputs[NOTE_SEVENTH_OUTPUT].setVoltage(chord.seventh,i);
		outputs[NOTE_NINTH_OUTPUT].setVoltage(chord.ninth,i);
		outputs[NOTE_ELEVENTH_OUTPUT].setVoltage(chord.eleventh,i);
		outputs[NOTE_THIRTEENTH_OUTPUT].setVoltage(chord.thirteenth,i);
	}
}

struct DIKTATWidget : BidooWidget {
  DIKTATWidget(DIKTAT *module);
};

struct ChannelDisplay : OpaqueWidget {
	DIKTAT *module;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module != NULL) {
				nvgGlobalTint(args.vg, color::WHITE);
				nvgFillColor(args.vg, YELLOW_BIDOO);
				nvgFontSize(args.vg, 12.0f);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
				nvgText(args.vg, 0, 0, std::to_string(module->currentChannel+1).c_str(), NULL);
			}
		}
		Widget::drawLayer(args, layer);
	}

};

struct RootNoteDisplay : OpaqueWidget {
	DIKTAT *module;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module != NULL) {
				nvgGlobalTint(args.vg, color::WHITE);
				nvgFillColor(args.vg, YELLOW_BIDOO);
				nvgFontSize(args.vg, 12.0f);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
				nvgText(args.vg, 0, 0, quantizer::rootNotes[module->lRootNote[module->currentChannel]+1].label.c_str(), NULL);
			}
		}
		Widget::drawLayer(args, layer);
	}
};

struct ScaleDisplay : OpaqueWidget {
	DIKTAT *module;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module != NULL) {
				nvgGlobalTint(args.vg, color::WHITE);
				nvgFillColor(args.vg, YELLOW_BIDOO);
				nvgFontSize(args.vg, 12.0f);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
				nvgText(args.vg, 0, 0, quantizer::scales[module->lScale[module->currentChannel]].label.c_str(), NULL);
			}
		}
		Widget::drawLayer(args, layer);
	}

};

struct DiktatPJ301MPort : PJ301MPort {

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (getPort() && (module != NULL)) {
				nvgGlobalTint(args.vg, color::WHITE);
				nvgFillColor(args.vg, SCHEME_WHITE);
				nvgFontSize(args.vg, 10.0f);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
				DIKTAT *mod = dynamic_cast<DIKTAT*>(module);
				nvgText(args.vg, 12, -4, mod->quant.noteName(getPort()->getVoltage(mod->currentChannel)).c_str(), NULL);
			}
		}
		Widget::drawLayer(args, layer);
	}

};


DIKTATWidget::DIKTATWidget(DIKTAT *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/DIKTAT.svg"));

	addChild(createWidget<ScrewSilver>(Vec(15, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createWidget<ScrewSilver>(Vec(15, 365)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

	RootNoteDisplay *rootNoteDisplay = new RootNoteDisplay();
	rootNoteDisplay->box.pos = Vec(96, 44);
	rootNoteDisplay->box.size = Vec(20, 20);
	rootNoteDisplay->module =  module ? module : NULL;
	addChild(rootNoteDisplay);

	addParam(createParam<BidooBlueSnapKnob>(Vec(82,55), module, DIKTAT::ROOT_NOTE_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(120, 62), module, DIKTAT::ROOT_NOTE_INPUT));

	ScaleDisplay *scaleDisplay = new ScaleDisplay();
	scaleDisplay->box.pos = Vec(96, 117);
	scaleDisplay->box.size = Vec(20, 20);
	scaleDisplay->module =  module ? module : NULL;
	addChild(scaleDisplay);

	addParam(createParam<BidooBlueSnapKnob>(Vec(82,128), module, DIKTAT::SCALE_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(120, 135), module, DIKTAT::SCALE_INPUT));

	ChannelDisplay *channelDisplay = new ChannelDisplay();
	channelDisplay->box.pos = Vec(22, 44);
	channelDisplay->box.size = Vec(20, 20);
	channelDisplay->module =  module ? module : NULL;
	addChild(channelDisplay);

	addParam(createParam<BidooBlueSnapKnob>(Vec(7.5f,55.0f), module, DIKTAT::CHANNEL_PARAM));


	addParam(createParam<CKSS>(Vec(15.5f, 115.0f), module, DIKTAT::GLOBAL_PARAM));

	addInput(createInput<DiktatPJ301MPort>(Vec(7, 240), module, DIKTAT::NOTE_INPUT));

	addOutput(createOutput<DiktatPJ301MPort>(Vec(7.0f, 285), module, DIKTAT::NOTE_TONIC_OUTPUT));
	addOutput(createOutput<DiktatPJ301MPort>(Vec(45.0f, 285), module, DIKTAT::NOTE_THIRD_OUTPUT));
	addOutput(createOutput<DiktatPJ301MPort>(Vec(82.0f, 285), module, DIKTAT::NOTE_FIFTH_OUTPUT));
	addOutput(createOutput<DiktatPJ301MPort>(Vec(119.0f, 285), module, DIKTAT::NOTE_SEVENTH_OUTPUT));
	addOutput(createOutput<DiktatPJ301MPort>(Vec(7.0f, 330), module, DIKTAT::NOTE_NINTH_OUTPUT));
	addOutput(createOutput<DiktatPJ301MPort>(Vec(45.0f, 330), module, DIKTAT::NOTE_ELEVENTH_OUTPUT));
	addOutput(createOutput<DiktatPJ301MPort>(Vec(82.0f, 330), module, DIKTAT::NOTE_THIRTEENTH_OUTPUT));
}

Model *modelDIKTAT = createModel<DIKTAT, DIKTATWidget>("DIKTAT");

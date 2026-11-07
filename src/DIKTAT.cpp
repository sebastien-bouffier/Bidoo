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
	int editedSlot = 0;    // the slot the ROOT and SCALE knobs edit: 0 in global mode
	int rootNote[16] = {0};
	int scale[16] = {0};
	float inputNote[16] = {0.0f};
	int lRootNote[16] = {0};
	int lScale[16] = {0};

	quantizer::Quantizer quant;
	quantizer::Chord chord;

	// Per channel: the chord is searched again only when the root or the scale
	// changes, or when the input moves 1/48 V (a quarter semitone) away from the
	// voltage it was last searched for. That skips the search on a steady note
	// and is a hysteresis: noise on a note sitting between two scale notes no
	// longer flips the output between them.
	static constexpr float HYSTERESIS = 1.f / 48.f;
	struct ChordCache {
		float in = NAN;
		int root = -100, scale = -1;
		quantizer::Chord chord;
	};
	ChordCache chordCache[16];

	DIKTAT() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CHANNEL_PARAM, 0.0f, 15.0f, 0.0f, "Channel","",0,1,1);
		std::vector<std::string> roots, scales;
		for (int i = 0; i < quantizer::numNotes; i++)
			roots.push_back(quantizer::rootNotes[i].label);
		for (int i = 0; i < quantizer::numScales; i++)
			scales.push_back(quantizer::scales[i].label);
		configSwitch(ROOT_NOTE_PARAM, -1.0f, quantizer::numNotes-2, 0.0f, "Root note (No: semitones, no chord)", roots);
		configSwitch(SCALE_PARAM, 0.0f, quantizer::numScales-1, 1.0f, "Scale (No: not quantized)", scales);


		configInput(NOTE_INPUT, "1V/oct pitch");
		configInput(ROOT_NOTE_INPUT, "Root note (0-10 V = +0-11 semitones)");
		configInput(SCALE_INPUT, "Scale (0-10 V = through the list)");

		configOutput(NOTE_TONIC_OUTPUT, "Tonic");
		configOutput(NOTE_THIRD_OUTPUT, "Third");
		configOutput(NOTE_FIFTH_OUTPUT, "Fifth");
		configOutput(NOTE_SEVENTH_OUTPUT, "Seventh");
		configOutput(NOTE_NINTH_OUTPUT, "Ninth");
		configOutput(NOTE_ELEVENTH_OUTPUT, "Eleventh");
		configOutput(NOTE_THIRTEENTH_OUTPUT, "Thirteenth");

		configSwitch(GLOBAL_PARAM, 0, 1, 0, "Mode", {"Individual","Global"});
		resetSlots();
  }

	// Every slot starts as the knobs' defaults.
	void resetSlots() {
		for (int i = 0; i < 16; i++) {
			rootNote[i] = 0;
			scale[i] = 1;
		}
		editedSlot = 0;
	}

	void onReset() override {
		resetSlots();
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
					rootNote[i] = clamp((int)json_integer_value(rootNoteJ), -1, 11);

				json_t *scaleJ = json_object_get(channelJ, "scale");
				if (scaleJ)
					scale[i] = clamp((int)json_integer_value(scaleJ), 0, quantizer::numScales - 1);
			}
		}
		json_t *currentChannelJ = json_object_get(rootJ, "currentChannel");
		if (currentChannelJ) {
			currentChannel = clamp((int)json_integer_value(currentChannelJ), 0, 15);
		}
		json_t *globalModeJ = json_object_get(rootJ, "globalMode");
		if (globalModeJ) {
			globalMode = json_boolean_value(globalModeJ);
		}
		// The saved knobs show this slot.
		editedSlot = globalMode ? 0 : currentChannel;
	}
};

void DIKTAT::process(const ProcessArgs &args) {
	// In global mode every channel uses slot 0, so the knobs edit slot 0
	// whatever CHANNEL shows; in individual mode they edit CHANNEL's slot.
	currentChannel = clamp((int)params[CHANNEL_PARAM].getValue(), 0, 15);
	globalMode = params[GLOBAL_PARAM].getValue() > 0.5f;
	int slot = globalMode ? 0 : currentChannel;
	if (slot != editedSlot) {
		editedSlot = slot;
		params[ROOT_NOTE_PARAM].setValue(rootNote[slot]);
		params[SCALE_PARAM].setValue(scale[slot]);
	}
	else {
		rootNote[slot] = params[ROOT_NOTE_PARAM].getValue();
		scale[slot] = params[SCALE_PARAM].getValue();
	}

	int c = std::max(inputs[NOTE_INPUT].getChannels(), 1);

	outputs[NOTE_TONIC_OUTPUT].setChannels(c);
	outputs[NOTE_THIRD_OUTPUT].setChannels(c);
	outputs[NOTE_FIFTH_OUTPUT].setChannels(c);
	outputs[NOTE_SEVENTH_OUTPUT].setChannels(c);
	outputs[NOTE_NINTH_OUTPUT].setChannels(c);
	outputs[NOTE_ELEVENTH_OUTPUT].setChannels(c);
	outputs[NOTE_THIRTEENTH_OUTPUT].setChannels(c);

	// Root and scale for all 16 channels, so the displays are right for any
	// CHANNEL. A mono CV applies to every channel; in global mode channel 1's
	// CV drives them all.
	for (int i=0;i<16;i++) {
		int k = globalMode ? 0 : i;
		if (inputs[ROOT_NOTE_INPUT].isConnected()) {
			// 0-10 V adds 0-11 semitones to the root, round the octave; root No stays No.
			int r = rootNote[k];
			int semis = (int)(clamp(inputs[ROOT_NOTE_INPUT].getPolyVoltage(k), 0.0f, 10.0f) * 1.1f);
			lRootNote[i] = r < 0 ? r : (r + semis) % 12;
		}
		else {
			lRootNote[i] = rootNote[k];
		}

		if (inputs[SCALE_INPUT].isConnected()) {
			// 0-10 V moves through the whole scale list from the knob's scale.
			lScale[i] = clamp((int)std::round(scale[k] + clamp(inputs[SCALE_INPUT].getPolyVoltage(k), 0.0f, 10.0f) * (quantizer::numScales - 1) / 10.0f), 0, quantizer::numScales - 1);
		}
		else {
			lScale[i] = scale[k];
		}
	}

	for (int i=0;i<c;i++) {
		inputNote[i] = inputs[NOTE_INPUT].getVoltage(i);

		if (lScale[i] == 0) {
			// not quantized: the chord is the input itself, every sample
			chord = quant.closestChordInScale(inputNote[i], lRootNote[i], lScale[i]);
		}
		else {
			ChordCache &cc = chordCache[i];
			if (lRootNote[i] != cc.root || lScale[i] != cc.scale || !(std::fabs(inputNote[i] - cc.in) < HYSTERESIS)) {
				cc.in = inputNote[i];
				cc.root = lRootNote[i];
				cc.scale = lScale[i];
				cc.chord = quant.closestChordInScale(inputNote[i], lRootNote[i], lScale[i]);
			}
			chord = cc.chord;
		}

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

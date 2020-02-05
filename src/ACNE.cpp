#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <iomanip>
#include <sstream>
#include "window.hpp"

using namespace std;

struct ACNE : Module {
	enum ParamIds {
	  COPY_PARAM,
		MAIN_OUT_GAIN_PARAM,
		RAMP_PARAM,
		MUTE_PARAM,
		SOLO_PARAM,
		SEND_MUTE_PARAM,
		TRACKLINK_PARAMS,
		OUT_MUTE_PARAMS = TRACKLINK_PARAMS + 8,
		IN_MUTE_PARAMS = OUT_MUTE_PARAMS + ACNE_NB_OUTS,
		IN_SOLO_PARAMS = IN_MUTE_PARAMS + ACNE_NB_TRACKS,
		SNAPSHOT_PARAMS = IN_SOLO_PARAMS + ACNE_NB_TRACKS,
		FADERS_PARAMS = SNAPSHOT_PARAMS + ACNE_NB_SNAPSHOTS,
		AUTOSAVE_PARAM = FADERS_PARAMS + (ACNE_NB_TRACKS * ACNE_NB_OUTS),
		SAVE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		SNAPSHOT_INPUT,
		TRACKS_INPUTS,
		NUM_INPUTS = TRACKS_INPUTS + ACNE_NB_TRACKS
	};
	enum OutputIds {
		TRACKS_OUTPUTS,
		NUM_OUTPUTS = TRACKS_OUTPUTS + ACNE_NB_OUTS
	};
	enum LightIds {
		COPY_LIGHT,
		TRACKLINK_LIGHTS,
		OUT_MUTE_LIGHTS = TRACKLINK_LIGHTS + 8,
		IN_MUTE_LIGHTS = OUT_MUTE_LIGHTS + ACNE_NB_OUTS,
		IN_SOLO_LIGHTS = IN_MUTE_LIGHTS + ACNE_NB_TRACKS,
		SNAPSHOT_LIGHTS = IN_SOLO_LIGHTS + ACNE_NB_TRACKS,
		AUTOSAVE_LIGHT = SNAPSHOT_LIGHTS + ACNE_NB_SNAPSHOTS,
		SAVE_LIGHT,
		NUM_LIGHTS
	};

	int currentSnapshot = 0;
	int previousSnapshot = 0;
	int copySnapshot = 0;
	bool copyState = false;
	bool autosave = true;
	float snapshots[ACNE_NB_SNAPSHOTS][ACNE_NB_OUTS][ACNE_NB_TRACKS] = {{{0.0f}}};
	bool  outMutes[ACNE_NB_OUTS] = {0};
	bool  inMutes[ACNE_NB_TRACKS] = {0};
	bool  inSolo[ACNE_NB_TRACKS] = {0};
	dsp::SchmittTrigger outMutesTriggers[ACNE_NB_OUTS];
	dsp::SchmittTrigger inMutesTriggers[ACNE_NB_TRACKS];
	dsp::SchmittTrigger inSoloTriggers[ACNE_NB_TRACKS];
	dsp::SchmittTrigger snapshotTriggers[ACNE_NB_SNAPSHOTS];
	dsp::SchmittTrigger muteTrigger;
	dsp::SchmittTrigger soloTrigger;
	dsp::SchmittTrigger copyPasteTrigger;
	dsp::SchmittTrigger autoSaveTrigger;
	dsp::SchmittTrigger saveTrigger;
	int rampSteps = 0;
	int rampSize = 1;
	int eFader = -1;
	float rampedValue = 0.0;
	dsp::SchmittTrigger linksTriggers[8];
	bool links[8] = {0,0,0,0,0,0,0,0};
	bool shifted = false;

	ACNE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(MAIN_OUT_GAIN_PARAM, 0.f, 10.f, 7.f, "Out gain", "%", 0.f, 100.f);
		configParam(COPY_PARAM, 0.f, 1.f, 0.f, "Copy/Paste");
		configParam(RAMP_PARAM, 0.f, 0.01f, 0.001f, "Snapshots ramp", "%", 0.f, 100.f);
		configParam(SEND_MUTE_PARAM, 0.f, 1.f, 0.f, "Sends mute");
		configParam(MUTE_PARAM, 0.f, 1.f, 0.f, "Mute");
		configParam(SOLO_PARAM, 0.f, 1.f, 0.f, "Solo");
		configParam(AUTOSAVE_PARAM, 0.f, 1.f, 1.f, "Auto save");
		configParam(SAVE_PARAM, 0.f, 1.f, 0.f, "Save");

		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			configParam(OUT_MUTE_PARAMS + i, 0.f, 1.f, 0.f, "Mute output " + std::to_string(i));
		}

		for (int i = 0; i < ACNE_NB_TRACKS; i++) {
			configParam(SNAPSHOT_PARAMS + i, 0.f, 1.f, 0.f, "Snapshot " + std::to_string(i));
			configParam(IN_MUTE_PARAMS + i, 0.f, 1.f, 0.f, "Mute track " + std::to_string(i));
			configParam(IN_SOLO_PARAMS + i, 0.f, 1.f, 0.f, "Solo track " + std::to_string(i));
		}

		for (int i = 0; i < 8; i++) {
			configParam(TRACKLINK_PARAMS + i, 0.f, 1.f, 0.f, "Track link " + std::to_string(i));
		}

		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			for (int j = 0; j < ACNE_NB_TRACKS; j++) {
				configParam(FADERS_PARAMS + j + i * (ACNE_NB_TRACKS), 0.f, 10.f, 0.f, "Volume " + std::to_string(i) + std::to_string(j));
			}
		}
	}

	void process(const ProcessArgs &args) override;

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "autosave", json_boolean(autosave));
		json_t *snapShotsJ = json_array();
		for (int i = 0; i < ACNE_NB_SNAPSHOTS; i++) {
			json_t *snapshotJ = json_array();
			for (int j = 0; j < ACNE_NB_OUTS; j++) {
				json_t *outJ = json_array();
				for (int k = 0 ; k < ACNE_NB_TRACKS; k ++) {
					json_t *controlJ = json_real(snapshots[i][j][k]);
					json_array_append_new(outJ, controlJ);
				}
				json_array_append_new(snapshotJ, outJ);
			}
			json_array_append_new(snapShotsJ, snapshotJ);
		}
		json_object_set_new(rootJ, "snapshots", snapShotsJ);

		for (int i = 0; i < 8; i++) {
			json_object_set_new(rootJ, ("link" + to_string(i)).c_str(), json_boolean(links[i]));
		}

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *autosaveJ = json_object_get(rootJ, "autosave");
		if (autosaveJ) autosave = json_is_true(autosaveJ);
		json_t *snapShotsJ = json_object_get(rootJ, "snapshots");
		if (snapShotsJ) {
			for (int i = 0; i < ACNE_NB_SNAPSHOTS; i++) {
				json_t *snapshotJ = json_array_get(snapShotsJ, i);
				if (snapshotJ) {
					for (int j = 0; j < ACNE_NB_OUTS; j++) {
						json_t *outJ = json_array_get(snapshotJ, j);
						if (outJ) {
							for (int k = 0; k < ACNE_NB_TRACKS; k++) {
								json_t *inJ = json_array_get(outJ, k);
								if (inJ) {
									snapshots[i][j][k] = json_number_value(inJ);
								}
							}
						}
					}
				}
			}
		}

		for (int i = 0; i < 8; i++) {
			json_t *linkJ = json_object_get(rootJ, ("link" + to_string(i)).c_str());
			if (linkJ)
				links[i] = json_boolean_value(linkJ);
		}

		updateFaders();
	}

	float getRampedValue(int i, int j) {
		if (rampSize>0) {
			return crossfade(snapshots[currentSnapshot][i][j],snapshots[previousSnapshot][i][j],(float)rampSteps/(float)rampSize);
		}
		else {
			return snapshots[currentSnapshot][i][j];
		}
	}

	void updateFaders() {
		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			for (int j = 0; j < ACNE_NB_TRACKS; j++) {
				if (eFader != i*ACNE_NB_TRACKS+j) params[FADERS_PARAMS+i*ACNE_NB_TRACKS+j].setValue(snapshots[currentSnapshot][i][j]);
			}
		}
	}
};

void ACNE::process(const ProcessArgs &args) {
	rampSize = static_cast<int>(args.sampleRate*params[RAMP_PARAM].value);

	if (autoSaveTrigger.process(params[AUTOSAVE_PARAM].getValue())) {
		autosave = !autosave;
	}

	if (copyPasteTrigger.process(params[COPY_PARAM].getValue())) {
		if (!copyState) {
			copySnapshot = currentSnapshot;
			copyState = true;
		} else if ((copyState) && (copySnapshot != currentSnapshot)) {
			for (int i = 0; i < ACNE_NB_OUTS; i++) {
				for (int j = 0; j < ACNE_NB_TRACKS; j++) {
					snapshots[currentSnapshot][i][j] = snapshots[copySnapshot][i][j];
				}
			}
			copyState = false;
			updateFaders();
		}
	}

	if (inputs[SNAPSHOT_INPUT].isConnected()) {
		int newSnapshot = clamp((int)(inputs[SNAPSHOT_INPUT].getVoltage() * 16 * 0.1f),0,ACNE_NB_SNAPSHOTS-1);
		if (currentSnapshot != newSnapshot) {
			previousSnapshot = currentSnapshot;
			currentSnapshot = newSnapshot;
			rampSteps = rampSize;
			updateFaders();
		}
	}
	else {
		for (int i = 0; i < ACNE_NB_SNAPSHOTS; i++) {
			if (snapshotTriggers[i].process(params[SNAPSHOT_PARAMS + i].getValue())) {
				previousSnapshot = currentSnapshot;
				currentSnapshot = i;
				rampSteps = rampSize;
				updateFaders();
			}
		}
	}

	if ((eFader>-1) && shifted) {
		int index = eFader%ACNE_NB_TRACKS;
		int outOffset = (eFader/ACNE_NB_TRACKS)%2==0?1:-1;
		int inOffset = links[index/2]?(index%2==0?1:-1):0;
		int relative=eFader+ACNE_NB_TRACKS*outOffset+inOffset;
		params[FADERS_PARAMS+relative].setValue(params[FADERS_PARAMS+eFader].getValue());
	}

	bool save = saveTrigger.process(params[SAVE_PARAM].getValue());

	lights[SAVE_LIGHT].setBrightness(lights[SAVE_LIGHT].getBrightness()-0.0001f*lights[SAVE_LIGHT].getBrightness());
	if (save == true) {

		lights[SAVE_LIGHT].setBrightness(1.0f);
	}

	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		if (autosave || save) {
			for (int j = 0; j < ACNE_NB_TRACKS; j++) {
				snapshots[currentSnapshot][i][j] = params[FADERS_PARAMS+i*ACNE_NB_TRACKS+j].getValue();
			}
		}

		if (outMutesTriggers[i].process(params[OUT_MUTE_PARAMS + i].getValue())) {
			outMutes[i] = !outMutes[i];
		}

		if (linksTriggers[i].process(params[TRACKLINK_PARAMS + i].getValue()))
			links[i] = !links[i];

		lights[TRACKLINK_LIGHTS + i].setBrightness(links[i] == true ? 1 : 0);
	}

	for (int i = 0; i < ACNE_NB_TRACKS; i++) {
		int linkIndex = i/2;
		int linkSwitch = i%2;
		if (inMutesTriggers[i].process(params[IN_MUTE_PARAMS + i].getValue())) {
			inMutes[i] = !inMutes[i];
			if (links[linkIndex]) {
				if (linkSwitch == 0)
					inMutes[i+1] = inMutes[i];
				else
					inMutes[i-1] = inMutes[i];
			}
		}
		if (inSoloTriggers[i].process(params[IN_SOLO_PARAMS + i].getValue())) {
			inSolo[i] = !inSolo[i];
			if (links[linkIndex]) {
				if (linkSwitch == 0)
					inSolo[i+1] = inSolo[i];
				else
					inSolo[i-1] = inSolo[i];
			}
		}
	}

	if (muteTrigger.process(params[MUTE_PARAM].getValue())) {
		for (int i = 0; i < ACNE_NB_TRACKS; i++) {
			inMutes[i] = false;
		}
	}

	if (muteTrigger.process(params[SEND_MUTE_PARAM].getValue())) {
		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			outMutes[i] = false;
		}
	}

	if (soloTrigger.process(params[SOLO_PARAM].getValue())) {
		for (int i = 0; i < ACNE_NB_TRACKS; i++) {
			inSolo[i] = false;
		}
	}

	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		outputs[TRACKS_OUTPUTS + i].setVoltage(0.0f);
		if (!outMutes[i]) {
			int sum = 0;
			for (int s = 0; s < ACNE_NB_TRACKS; ++s) {
			  sum |= (inSolo[s] == true ? 1 : 0);
			}
			if (sum > 0) {
				for (int j = 0; j < ACNE_NB_TRACKS; j ++) {
					if ((inputs[TRACKS_INPUTS + j].isConnected()) && (inSolo[j])) {
						outputs[TRACKS_OUTPUTS + i].setVoltage(outputs[TRACKS_OUTPUTS + i].getVoltage() + getRampedValue(i,j) * 0.1f * inputs[TRACKS_INPUTS + j].getVoltage() * 30517578125e-15f);
					}
				}
			}
			else {
				for (int j = 0; j < ACNE_NB_TRACKS; j ++) {
					if ((inputs[TRACKS_INPUTS + j].isConnected()) && (!inMutes[j])) {
						if (rampSize>0) {
							rampedValue = crossfade(snapshots[currentSnapshot][i][j],snapshots[previousSnapshot][i][j],(float)rampSteps/(float)rampSize);
						}
						else {
							rampedValue = snapshots[currentSnapshot][i][j];
						}
						outputs[TRACKS_OUTPUTS + i].setVoltage(outputs[TRACKS_OUTPUTS + i].getVoltage() + getRampedValue(i,j) * 0.1f * inputs[TRACKS_INPUTS + j].getVoltage() * 30517578125e-15f);
					}
				}
			}
			outputs[TRACKS_OUTPUTS + i].setVoltage(outputs[TRACKS_OUTPUTS + i].getVoltage() * 32768.0f);
		}
	}

	if (rampSteps > 0)
		rampSteps--;

	outputs[TRACKS_OUTPUTS].setVoltage(outputs[TRACKS_OUTPUTS].getVoltage() * params[MAIN_OUT_GAIN_PARAM].value * 0.1f);
	outputs[TRACKS_OUTPUTS + 1].setVoltage(outputs[TRACKS_OUTPUTS + 1].getVoltage() * params[MAIN_OUT_GAIN_PARAM].value * 0.1f);

	lights[COPY_LIGHT].setBrightness(copyState == true ? 1 : 0);
	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		lights[OUT_MUTE_LIGHTS + i].setBrightness(outMutes[i] == true ? 1 : 0);
	}
	for (int i = 0; i < ACNE_NB_TRACKS; i++) {
		lights[IN_MUTE_LIGHTS + i].setBrightness(inMutes[i] == true ? 1 : 0);
		lights[IN_SOLO_LIGHTS + i].setBrightness(inSolo[i] == true ? 1 : 0);
	}
	for (int i = 0; i < ACNE_NB_SNAPSHOTS; i++) {
		lights[SNAPSHOT_LIGHTS + i].setBrightness(i == currentSnapshot ? 1 : 0);
	}

	lights[AUTOSAVE_LIGHT].setBrightness(autosave == true ? 1 : 0);
}

struct ACNEWidget : ModuleWidget {
	ACNEWidget(ACNE *module);
};

struct AcneBidooColoredTrimpot : BidooColoredTrimpot {
	int index=0;

	void onDragStart(const event::DragStart& e) override {
		ACNE* acne = dynamic_cast<ACNE*>(paramQuantity->module);
		acne->eFader=index;
		BidooColoredTrimpot::onDragStart(e);
	}

	void onDragEnd(const event::DragEnd& e) override {
		ACNE* acne = dynamic_cast<ACNE*>(paramQuantity->module);
		acne->eFader=-1;
		acne->shifted=false;
		BidooColoredTrimpot::onDragEnd(e);
	}

	void onButton(const event::Button &e) override {
		ACNE* acne = dynamic_cast<ACNE*>(paramQuantity->module);
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) {
					acne->shifted=true;
		}
		BidooColoredTrimpot::onButton(e);
	}
};

ACNEWidget::ACNEWidget(ACNE *module) {
	setModule(module);
	setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ACNE.svg")));

	addChild(createWidget<ScrewSilver>(Vec(15, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createWidget<ScrewSilver>(Vec(15, 365)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

	addParam(createParam<BidooBlueKnob>(Vec(474.0f, 39.0f), module, ACNE::MAIN_OUT_GAIN_PARAM));

	addParam(createParam<BlueCKD6>(Vec(7.0f, 39.0f), module, ACNE::COPY_PARAM));
	addChild(createLight<SmallLight<GreenLight>>(Vec(18.0f, 28.0f), module, ACNE::COPY_LIGHT));

	addParam(createParam<BidooBlueTrimpot>(Vec(432.0f, 28.0f), module, ACNE::RAMP_PARAM));

	addInput(createInput<TinyPJ301MPort>(Vec(58.0f, 30.0f), module, ACNE::SNAPSHOT_INPUT));

	addParam(createParam<MuteBtn>(Vec(2.0f, 293.0f), module, ACNE::SEND_MUTE_PARAM));
	addParam(createParam<MuteBtn>(Vec(21.0f, 293.0f), module, ACNE::MUTE_PARAM));
	addParam(createParam<SoloBtn>(Vec(11.0f, 314.0f), module, ACNE::SOLO_PARAM));

	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		addOutput(createOutput<TinyPJ301MPort>(Vec(482.0f, 79.0f+i*27.0f), module, ACNE::TRACKS_OUTPUTS + i));

		addParam(createParam<LEDButton>(Vec(10.0f, 77.0f+i*27.0f), module, ACNE::OUT_MUTE_PARAMS + i));
		addChild(createLight<SmallLight<RedLight>>(Vec(16.0f, 83.0f+i*27.0f), module, ACNE::OUT_MUTE_LIGHTS + i));
	}

	for (int i = 0; i < ACNE_NB_TRACKS; i++) {
		addParam(createParam<LEDButton>(Vec(43.0f+i*27.0f, 49.0f), module, ACNE::SNAPSHOT_PARAMS + i));
		addChild(createLight<SmallLight<BlueLight>>(Vec(49.0f+i*27.0f, 55.0f), module, ACNE::SNAPSHOT_LIGHTS + i));

		addInput(createInput<TinyPJ301MPort>(Vec(45.0f+i*27.0f, 340.0f), module, ACNE::TRACKS_INPUTS + i));

		addParam(createParam<LEDButton>(Vec(43.0f+i*27.0f, 292.0f), module, ACNE::IN_MUTE_PARAMS + i));
		addChild(createLight<SmallLight<RedLight>>(Vec(49.0f+i*27.0f, 298.0f), module, ACNE::IN_MUTE_LIGHTS + i));

		addParam(createParam<LEDButton>(Vec(43.0f+i*27.0f, 314.0f), module, ACNE::IN_SOLO_PARAMS + i));
		addChild(createLight<SmallLight<GreenLight>>(Vec(49.0f+i*27.0f, 320.0f), module, ACNE::IN_SOLO_LIGHTS + i));
	}

	for (int i = 0; i < 8; i++) {
		addParam(createParam<MiniLEDButton>(Vec(62.0f+i*54.0f, 309.0f), module, ACNE::TRACKLINK_PARAMS + i));
		addChild(createLight<SmallLight<BlueLight>>(Vec(62.0f+i*54.0f, 309.0f), module, ACNE::TRACKLINK_LIGHTS + i));
	}

	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		for (int j = 0; j < ACNE_NB_TRACKS; j++) {
			ParamWidget* trim = createParam<AcneBidooColoredTrimpot>(Vec(43.0f+j*27.0f, 77.0f+i*27.0f), module, ACNE::FADERS_PARAMS + j + i * (ACNE_NB_TRACKS));
			AcneBidooColoredTrimpot* fader = dynamic_cast<AcneBidooColoredTrimpot*>(trim);
			fader->index= j + i * (ACNE_NB_TRACKS);
			addParam(trim);
		}
	}

	addParam(createParam<LEDButton>(Vec(32.0f, 3.0f), module, ACNE::AUTOSAVE_PARAM));
	addChild(createLight<SmallLight<BlueLight>>(Vec(38.0f, 9.0f), module, ACNE::AUTOSAVE_LIGHT));

	addParam(createParam<LEDButton>(Vec(460.0f, 3.0f), module, ACNE::SAVE_PARAM));
	addChild(createLight<SmallLight<BlueLight>>(Vec(466.0f, 9.0f), module, ACNE::SAVE_LIGHT));
}

Model *modelACNE = createModel<ACNE, ACNEWidget>("ACnE");

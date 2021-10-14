#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <iomanip>
#include <sstream>

using namespace std;
using simd::float_4;

const int ACNE_NB_TRACKS = 16;
const int ACNE_NB_OUTS = 8;
const int ACNE_NB_SNAPSHOTS = 16;

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
	float_4 snapshots[ACNE_NB_SNAPSHOTS][ACNE_NB_OUTS][ACNE_NB_TRACKS/4] = {{{0.0f}}};
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
	int eFader = -1;
	dsp::SchmittTrigger linksTriggers[8];
	bool links[8] = {0,0,0,0,0,0,0,0};
	bool shifted = false;
	bool solo = false;
	float_4 ramp = 0.0f;
	float_4 rampMin = 0.0f;
	float_4 rampMax = 0.01f;
	float_4 in = 0.0f;
	float_4 out = 0.0f;

	ACNE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(MAIN_OUT_GAIN_PARAM, 0.f, 1.f, 0.7f, "Out gain", "%", 0.f, 100.f);
		configSwitch(COPY_PARAM, 0.f, 1.f, 0.f, "Copy/Paste");
		configParam(RAMP_PARAM, 0.f, 0.01f, 0.001f, "Snapshots ramp", "%", 0.f, 100.f);
		configSwitch(SEND_MUTE_PARAM, 0.f, 1.f, 0.f, "Sends mute");
		configSwitch(MUTE_PARAM, 0.f, 1.f, 0.f, "Mute track");
		configSwitch(SOLO_PARAM, 0.f, 1.f, 0.f, "Solo track");
		configSwitch(AUTOSAVE_PARAM, 0.f, 1.f, 1.f, "Auto save");
		configSwitch(SAVE_PARAM, 0.f, 1.f, 0.f, "Save");

		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			configParam(OUT_MUTE_PARAMS + i, 0.f, 1.f, 0.f, "Mute output " + std::to_string(i+1));
			configOutput(TRACKS_OUTPUTS+i, "Output " + std::to_string(i+1));
		}

		for (int i = 0; i < ACNE_NB_TRACKS; i++) {
			configSwitch(SNAPSHOT_PARAMS + i, 0.f, 1.f, 0.f, "Snapshot " + std::to_string(i+1));
			configSwitch(IN_MUTE_PARAMS + i, 0.f, 1.f, 0.f, "Mute track " + std::to_string(i+1));
			configSwitch(IN_SOLO_PARAMS + i, 0.f, 1.f, 0.f, "Solo track " + std::to_string(i+1));
			configLight(SNAPSHOT_LIGHTS + i, "Snapshot " + std::to_string(i+1));
			configInput(TRACKS_INPUTS + i, "Input " + std::to_string(i+1));
		}

		for (int i = 0; i < 8; i++) {
			configParam(TRACKLINK_PARAMS + i, 0.f, 1.f, 0.f, "Track link " + std::to_string(i+1) + "/" + std::to_string((i+1)/2));
		}

		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			for (int j = 0; j < ACNE_NB_TRACKS; j++) {
				configParam(FADERS_PARAMS + j + i * (ACNE_NB_TRACKS), 0.f, 1.f, 0.f, "Volume " + std::to_string(j+1) + "/" + std::to_string(i+1));
			}
		}

		configInput(SNAPSHOT_INPUT, "Snapshot input");
		lights[SNAPSHOT_LIGHTS + currentSnapshot].setBrightness(1);

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
				for (int k = 0 ; k < ACNE_NB_TRACKS/4; k++) {
					json_t* controls = json_pack("[f, f, f, f]", snapshots[i][j][k][0], snapshots[i][j][k][1], snapshots[i][j][k][2], snapshots[i][j][k][3]);
					json_array_append_new(outJ, controls);
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
							for (int k = 0; k < ACNE_NB_TRACKS/4; k++) {
								json_t *inJ = json_array_get(outJ, k);
								if (inJ) {
									double s0,s1,s2,s3;
									json_unpack(inJ, "[f, f, f, f]", &s0, &s1, &s2, &s3);
									snapshots[i][j][k][0] = s0;
									snapshots[i][j][k][1] = s1;
									snapshots[i][j][k][2] = s2;
									snapshots[i][j][k][3] = s3;
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


	void updateFaders() {
		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			for (int j = 0; j < ACNE_NB_TRACKS/4; j++) {
				if (eFader != i*ACNE_NB_TRACKS+j*4) params[FADERS_PARAMS+i*ACNE_NB_TRACKS+j*4].setValue(snapshots[currentSnapshot][i][j][0]);
				if (eFader != i*ACNE_NB_TRACKS+j*4+1) params[FADERS_PARAMS+i*ACNE_NB_TRACKS+j*4+1].setValue(snapshots[currentSnapshot][i][j][1]);
				if (eFader != i*ACNE_NB_TRACKS+j*4+2) params[FADERS_PARAMS+i*ACNE_NB_TRACKS+j*4+2].setValue(snapshots[currentSnapshot][i][j][2]);
				if (eFader != i*ACNE_NB_TRACKS+j*4+3) params[FADERS_PARAMS+i*ACNE_NB_TRACKS+j*4+3].setValue(snapshots[currentSnapshot][i][j][3]);
			}
		}
	}

	void onReset() override {
		for (int k = 0; k < ACNE_NB_SNAPSHOTS; k++) {
			for (int i = 0; i < ACNE_NB_OUTS; i++) {
				for (int j = 0; j < ACNE_NB_TRACKS/4; j++) {
					snapshots[k][i][j]=0.0f;
				}
			}
		}
		updateFaders();
	}

	void switchSnapshot(int snapshot) {
		previousSnapshot = currentSnapshot;
		currentSnapshot = snapshot;
		lights[SNAPSHOT_LIGHTS + previousSnapshot].setBrightness(0);
		lights[SNAPSHOT_LIGHTS + currentSnapshot].setBrightness(1);
		ramp = params[RAMP_PARAM].getValue();
		rampMax = params[RAMP_PARAM].getValue();
		updateFaders();
	}
};

void ACNE::process(const ProcessArgs &args) {

	bool save = saveTrigger.process(params[SAVE_PARAM].getValue());
	lights[SAVE_LIGHT].setBrightness(lights[SAVE_LIGHT].getBrightness()-0.0001f*lights[SAVE_LIGHT].getBrightness());

	if (save == true) {
		lights[SAVE_LIGHT].setBrightness(1.0f);
	}

	if (autoSaveTrigger.process(params[AUTOSAVE_PARAM].getValue())) {
		autosave = !autosave;
	}
	lights[AUTOSAVE_LIGHT].setBrightness(autosave == true ? 1 : 0);

	if (copyPasteTrigger.process(params[COPY_PARAM].getValue())) {
		if (!copyState) {
			copySnapshot = currentSnapshot;
			copyState = true;
		} else if ((copyState) && (copySnapshot != currentSnapshot)) {
			for (int i = 0; i < ACNE_NB_OUTS; i++) {
				for (int j = 0; j < ACNE_NB_TRACKS/4; j++) {
					snapshots[currentSnapshot][i][j] = snapshots[copySnapshot][i][j];
				}
			}
			copyState = false;
			updateFaders();
		}
	}
	lights[COPY_LIGHT].setBrightness(copyState == true ? 1 : 0);

	if (inputs[SNAPSHOT_INPUT].isConnected()) {
		int newSnapshot = clamp((int)(inputs[SNAPSHOT_INPUT].getVoltage() * 16 * 0.1f),0,ACNE_NB_SNAPSHOTS-1);
		if (currentSnapshot != newSnapshot) {
			switchSnapshot(newSnapshot);
		}
	}
	else {
		for (int i = 0; i < ACNE_NB_SNAPSHOTS; i++) {
			if (snapshotTriggers[i].process(params[SNAPSHOT_PARAMS + i].getValue())) {
				switchSnapshot(i);
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

	solo = false;

	for (int i = 0; i < ACNE_NB_TRACKS; i++) {
		int linkIndex = i/2;
		int linkSwitch = i%2;

		if (linksTriggers[linkIndex].process(params[TRACKLINK_PARAMS + linkIndex].getValue()))	links[linkIndex] = !links[linkIndex];
		lights[TRACKLINK_LIGHTS + linkIndex].setBrightness(links[linkIndex] == true ? 1 : 0);

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
		lights[IN_MUTE_LIGHTS + i].setBrightness(inMutes[i] == true ? 1 : 0);
		lights[IN_SOLO_LIGHTS + i].setBrightness(inSolo[i] == true ? 1 : 0);
		solo = solo || inSolo[i];
	}

	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		outputs[TRACKS_OUTPUTS + i].setVoltage(0.0f);
		if (outMutesTriggers[i].process(params[OUT_MUTE_PARAMS + i].getValue())) {
			outMutes[i] = !outMutes[i];
		}
		lights[OUT_MUTE_LIGHTS + i].setBrightness(outMutes[i] == true ? 1 : 0);

		if (!outMutes[i]) {
			for (int j = 0; j < ACNE_NB_TRACKS/4; j++) {
				if (autosave || save) {
					snapshots[currentSnapshot][i][j][0] = params[FADERS_PARAMS+i*ACNE_NB_TRACKS+j*4].getValue();
					snapshots[currentSnapshot][i][j][1] = params[FADERS_PARAMS+i*ACNE_NB_TRACKS+j*4+1].getValue();
					snapshots[currentSnapshot][i][j][2] = params[FADERS_PARAMS+i*ACNE_NB_TRACKS+j*4+2].getValue();
					snapshots[currentSnapshot][i][j][3] = params[FADERS_PARAMS+i*ACNE_NB_TRACKS+j*4+3].getValue();
				}

				float_4 mutesNSolo;
				mutesNSolo[0] = inMutes[j*4] ? 0 : ((solo&&!inSolo[j*4]) ? 0 : 1);
				mutesNSolo[1] = inMutes[j*4+1] ? 0 : ((solo&&!inSolo[j*4+1]) ? 0 : 1);
				mutesNSolo[2] = inMutes[j*4+2] ? 0 : ((solo&&!inSolo[j*4+2]) ? 0 : 1);
				mutesNSolo[3] = inMutes[j*4+3] ? 0 : ((solo&&!inSolo[j*4+3]) ? 0 : 1);

				in = {inputs[TRACKS_INPUTS + j*4].getVoltage(), inputs[TRACKS_INPUTS + j*4+1].getVoltage(), inputs[TRACKS_INPUTS + j*4+2].getVoltage(), inputs[TRACKS_INPUTS + j*4+3].getVoltage()};
				if (ramp[0]>0) {
					out = simd::rescale(ramp,rampMin,rampMax,snapshots[currentSnapshot][i][j],snapshots[previousSnapshot][i][j])*mutesNSolo*in;
				}
				else {
					out = snapshots[currentSnapshot][i][j]*mutesNSolo*in;
				}

				outputs[TRACKS_OUTPUTS + i].setVoltage(outputs[TRACKS_OUTPUTS + i].getVoltage() + out[0] + out[1]	+ out[2] + out[3]);

			}
			outputs[TRACKS_OUTPUTS + i].setVoltage(outputs[TRACKS_OUTPUTS + i].getVoltage());
		}
	}

	outputs[TRACKS_OUTPUTS].setVoltage(outputs[TRACKS_OUTPUTS].getVoltage() * params[MAIN_OUT_GAIN_PARAM].value);
	outputs[TRACKS_OUTPUTS + 1].setVoltage(outputs[TRACKS_OUTPUTS + 1].getVoltage() * params[MAIN_OUT_GAIN_PARAM].value);

	ramp = simd::fmax(ramp-args.sampleTime,0.f);
}

struct ACNEWidget : ModuleWidget {
	ACNEWidget(ACNE *module);
};

struct AcneBidooColoredTrimpot : BidooColoredTrimpot {
	int index=0;

	void onDragStart(const event::DragStart& e) override {
		ACNE* acne = dynamic_cast<ACNE*>(getParamQuantity()->module);
		acne->eFader=index;
		BidooColoredTrimpot::onDragStart(e);
	}

	void onDragEnd(const event::DragEnd& e) override {
		ACNE* acne = dynamic_cast<ACNE*>(getParamQuantity()->module);
		acne->eFader=-1;
		acne->shifted=false;
		BidooColoredTrimpot::onDragEnd(e);
	}

	void onButton(const event::Button &e) override {
		ACNE* acne = dynamic_cast<ACNE*>(getParamQuantity()->module);
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

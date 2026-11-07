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

struct ACNE : BidooModule {
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

	// Fader gains of every scene, 4 inputs per vector.
	float_4 snapshots[ACNE_NB_SNAPSHOTS][ACNE_NB_OUTS][ACNE_NB_TRACKS/4] = {{{0.0f}}};
	// Mutes and solos of every scene, one bit per input or output.
	uint32_t sceneInMutes[ACNE_NB_SNAPSHOTS] = {0};
	uint32_t sceneInSolos[ACNE_NB_SNAPSHOTS] = {0};
	uint32_t sceneOutMutes[ACNE_NB_SNAPSHOTS] = {0};

	int currentSnapshot = 0;
	int copySnapshot = 0;
	bool copyState = false;
	bool autosave = true;
	// Live state of the current scene.
	bool outMutes[ACNE_NB_OUTS] = {0};
	bool inMutes[ACNE_NB_TRACKS] = {0};
	bool inSolo[ACNE_NB_TRACKS] = {0};
	bool links[8] = {0,0,0,0,0,0,0,0};
	bool solo = false;

	dsp::SchmittTrigger outMutesTriggers[ACNE_NB_OUTS];
	dsp::SchmittTrigger inMutesTriggers[ACNE_NB_TRACKS];
	dsp::SchmittTrigger inSoloTriggers[ACNE_NB_TRACKS];
	dsp::SchmittTrigger snapshotTriggers[ACNE_NB_SNAPSHOTS];
	dsp::SchmittTrigger clearMutesTrigger;
	dsp::SchmittTrigger clearSendMutesTrigger;
	dsp::SchmittTrigger clearSolosTrigger;
	dsp::SchmittTrigger copyPasteTrigger;
	dsp::SchmittTrigger autoSaveTrigger;
	dsp::SchmittTrigger saveTrigger;
	dsp::SchmittTrigger linksTriggers[8];

	// Set by the fader being dragged (UI thread).
	int eFader = -1;
	bool shifted = false;

	// Gains actually applied: they glide to the scene over the morph time,
	// then follow every change through a 5 ms smoother.
	float_4 gains[ACNE_NB_OUTS][ACNE_NB_TRACKS/4] = {{0.0f}};
	float_4 morphFrom[ACNE_NB_OUTS][ACNE_NB_TRACKS/4] = {{0.0f}};
	float morphPhase = 1.0f;
	float mainGain = 0.7f;
	float smoothCoef = 0.f;
	float smoothRate = 0.f;
	float saveLight = 0.f;

	// Control rate: buttons, lights, the linked fader mirror, the autosave and
	// the fader reads run every CONTROL_DIV samples; the morph and the 5 ms
	// gain smoother stay per sample, towards the gains read at control rate.
	static constexpr int CONTROL_DIV = 16;
	int controlCounter = 0;
	float_4 targets[ACNE_NB_OUTS][ACNE_NB_TRACKS/4] = {{0.0f}};
	// Scene CV: a scene is left only 50 mV past the edges of its 0.625 V band.
	static constexpr float SCENE_CV_HYSTERESIS = 0.05f;

	ACNE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(MAIN_OUT_GAIN_PARAM, 0.f, 1.f, 0.7f, "Out 1-2 gain", "%", 0.f, 100.f);
		configButton(COPY_PARAM, "Copy / paste scene");
		configParam(RAMP_PARAM, 0.f, 10.f, 0.f, "Scene morph time", " s");
		configButton(SEND_MUTE_PARAM, "Unmute all outputs");
		configButton(MUTE_PARAM, "Unmute all inputs");
		configButton(SOLO_PARAM, "Clear all solos");
		configButton(AUTOSAVE_PARAM, "Auto save scene");
		configButton(SAVE_PARAM, "Save scene");

		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			configButton(OUT_MUTE_PARAMS + i, "Mute output " + std::to_string(i+1));
			configOutput(TRACKS_OUTPUTS+i, "Output " + std::to_string(i+1));
		}

		for (int i = 0; i < ACNE_NB_TRACKS; i++) {
			configButton(SNAPSHOT_PARAMS + i, "Scene " + std::to_string(i+1));
			configButton(IN_MUTE_PARAMS + i, "Mute input " + std::to_string(i+1));
			configButton(IN_SOLO_PARAMS + i, "Solo input " + std::to_string(i+1));
			configLight(SNAPSHOT_LIGHTS + i, "Scene " + std::to_string(i+1));
			configInput(TRACKS_INPUTS + i, "Input " + std::to_string(i+1));
		}

		for (int i = 0; i < 8; i++) {
			configButton(TRACKLINK_PARAMS + i, "Link inputs " + std::to_string(2*i+1) + "-" + std::to_string(2*i+2));
		}

		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			for (int j = 0; j < ACNE_NB_TRACKS; j++) {
				configParam(FADERS_PARAMS + j + i * (ACNE_NB_TRACKS), 0.f, 1.f, 0.f, "Input " + std::to_string(j+1) + " to output " + std::to_string(i+1), "%", 0.f, 100.f);
			}
		}

		configInput(SNAPSHOT_INPUT, "Scene (0.625 V per scene)");
	}

	void process(const ProcessArgs &args) override;

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "autosave", json_boolean(autosave));
		json_object_set_new(rootJ, "currentSnapshot", json_integer(currentSnapshot));
		json_t *snapShotsJ = json_array();
		json_t *inMutesJ = json_array();
		json_t *inSolosJ = json_array();
		json_t *outMutesJ = json_array();
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
			json_array_append_new(inMutesJ, json_integer(sceneInMutes[i]));
			json_array_append_new(inSolosJ, json_integer(sceneInSolos[i]));
			json_array_append_new(outMutesJ, json_integer(sceneOutMutes[i]));
		}
		json_object_set_new(rootJ, "snapshots", snapShotsJ);
		json_object_set_new(rootJ, "sceneInMutes", inMutesJ);
		json_object_set_new(rootJ, "sceneInSolos", inSolosJ);
		json_object_set_new(rootJ, "sceneOutMutes", outMutesJ);

		for (int i = 0; i < 8; i++) {
			json_object_set_new(rootJ, ("link" + to_string(i)).c_str(), json_boolean(links[i]));
		}

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *autosaveJ = json_object_get(rootJ, "autosave");
		if (autosaveJ) autosave = json_is_true(autosaveJ);
		json_t *currentJ = json_object_get(rootJ, "currentSnapshot");
		currentSnapshot = currentJ ? clamp((int)json_integer_value(currentJ), 0, ACNE_NB_SNAPSHOTS-1) : 0;
		json_t *snapShotsJ = json_object_get(rootJ, "snapshots");
		json_t *inMutesJ = json_object_get(rootJ, "sceneInMutes");
		json_t *inSolosJ = json_object_get(rootJ, "sceneInSolos");
		json_t *outMutesJ = json_object_get(rootJ, "sceneOutMutes");
		for (int i = 0; i < ACNE_NB_SNAPSHOTS; i++) {
			json_t *snapshotJ = json_array_get(snapShotsJ, i);
			for (int j = 0; j < ACNE_NB_OUTS; j++) {
				json_t *outJ = json_array_get(snapshotJ, j);
				for (int k = 0; k < ACNE_NB_TRACKS/4; k++) {
					json_t *inJ = json_array_get(outJ, k);
					for (int l = 0; l < 4; l++)
						snapshots[i][j][k][l] = clamp((float)json_number_value(json_array_get(inJ, l)), 0.f, 1.f);
				}
			}
			sceneInMutes[i] = json_integer_value(json_array_get(inMutesJ, i));
			sceneInSolos[i] = json_integer_value(json_array_get(inSolosJ, i));
			sceneOutMutes[i] = json_integer_value(json_array_get(outMutesJ, i));
		}

		for (int i = 0; i < 8; i++) {
			json_t *linkJ = json_object_get(rootJ, ("link" + to_string(i)).c_str());
			links[i] = linkJ && json_boolean_value(linkJ);
		}

		loadScene();
		// Start at the stored levels instead of fading in.
		morphPhase = 1.f;
		for (int i = 0; i < ACNE_NB_OUTS; i++)
			for (int j = 0; j < ACNE_NB_TRACKS/4; j++)
				gains[i][j] = targetGain(i, j);
	}

	// Faders and mutes of the current scene become the live state.
	void loadScene() {
		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			outMutes[i] = (sceneOutMutes[currentSnapshot] >> i) & 1;
			for (int j = 0; j < ACNE_NB_TRACKS/4; j++) {
				for (int l = 0; l < 4; l++) {
					int f = i*ACNE_NB_TRACKS + j*4 + l;
					if (eFader != f)
						params[FADERS_PARAMS + f].setValue(snapshots[currentSnapshot][i][j][l]);
				}
			}
		}
		for (int i = 0; i < ACNE_NB_TRACKS; i++) {
			inMutes[i] = (sceneInMutes[currentSnapshot] >> i) & 1;
			inSolo[i] = (sceneInSolos[currentSnapshot] >> i) & 1;
		}
	}

	// Live faders and mutes are written into the current scene.
	void storeScene() {
		uint32_t im = 0, is = 0, om = 0;
		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			if (outMutes[i]) om |= 1u << i;
			for (int j = 0; j < ACNE_NB_TRACKS/4; j++)
				for (int l = 0; l < 4; l++)
					snapshots[currentSnapshot][i][j][l] = params[FADERS_PARAMS + i*ACNE_NB_TRACKS + j*4 + l].getValue();
		}
		for (int i = 0; i < ACNE_NB_TRACKS; i++) {
			if (inMutes[i]) im |= 1u << i;
			if (inSolo[i]) is |= 1u << i;
		}
		sceneInMutes[currentSnapshot] = im;
		sceneInSolos[currentSnapshot] = is;
		sceneOutMutes[currentSnapshot] = om;
	}

	// Gain the live faders and mutes ask for, for 4 inputs of one output.
	float_4 targetGain(int out, int group) {
		if (outMutes[out])
			return 0.f;
		float_4 g;
		for (int l = 0; l < 4; l++) {
			int track = group*4 + l;
			bool open = !inMutes[track] && (!solo || inSolo[track]);
			g[l] = open ? params[FADERS_PARAMS + out*ACNE_NB_TRACKS + track].getValue() : 0.f;
		}
		return g;
	}

	void onReset() override {
		for (int k = 0; k < ACNE_NB_SNAPSHOTS; k++) {
			for (int i = 0; i < ACNE_NB_OUTS; i++)
				for (int j = 0; j < ACNE_NB_TRACKS/4; j++)
					snapshots[k][i][j] = 0.0f;
			sceneInMutes[k] = sceneInSolos[k] = sceneOutMutes[k] = 0;
		}
		for (int i = 0; i < 8; i++)
			links[i] = false;
		currentSnapshot = 0;
		copyState = false;
		autosave = true;
		loadScene();
	}

	void switchSnapshot(int snapshot) {
		if (snapshot == currentSnapshot)
			return;
		if (autosave)
			storeScene();
		currentSnapshot = snapshot;
		loadScene();
		// Morph from what is playing now, even in the middle of another morph.
		for (int i = 0; i < ACNE_NB_OUTS; i++)
			for (int j = 0; j < ACNE_NB_TRACKS/4; j++)
				morphFrom[i][j] = gains[i][j];
		morphPhase = params[RAMP_PARAM].getValue() > 0.f ? 0.f : 1.f;
	}
};

void ACNE::process(const ProcessArgs &args) {
	if (args.sampleRate != smoothRate) {
		smoothCoef = 1.f - std::exp(-args.sampleTime / 0.005f);
		smoothRate = args.sampleRate;
	}

	// Scene CV, per sample so sequenced scene changes stay on time.
	bool sceneSwitched = false;
	if (inputs[SNAPSHOT_INPUT].isConnected()) {
		float v = inputs[SNAPSHOT_INPUT].getVoltage();
		int newSnapshot = clamp((int)(v * 16 * 0.1f),0,ACNE_NB_SNAPSHOTS-1);
		if (newSnapshot != currentSnapshot) {
			float lo = currentSnapshot * 0.625f - SCENE_CV_HYSTERESIS;
			float hi = (currentSnapshot + 1) * 0.625f + SCENE_CV_HYSTERESIS;
			if (v < lo || v > hi) {
				switchSnapshot(newSnapshot);
				sceneSwitched = true;
			}
		}
	}

	bool control = --controlCounter < 0;
	if (control) {
		controlCounter = CONTROL_DIV - 1;
		const float controlTime = args.sampleTime * CONTROL_DIV;

		bool save = saveTrigger.process(params[SAVE_PARAM].getValue());
		if (save)
			saveLight = 1.f;
		saveLight = std::max(0.f, saveLight - controlTime * 2.f);
		lights[SAVE_LIGHT].setBrightness(saveLight);

		if (autoSaveTrigger.process(params[AUTOSAVE_PARAM].getValue())) {
			autosave = !autosave;
		}
		lights[AUTOSAVE_LIGHT].setBrightness(autosave ? 1 : 0);

		if (copyPasteTrigger.process(params[COPY_PARAM].getValue())) {
			if (!copyState) {
				if (autosave)
					storeScene();
				copySnapshot = currentSnapshot;
				copyState = true;
			} else if (copySnapshot != currentSnapshot) {
				for (int i = 0; i < ACNE_NB_OUTS; i++)
					for (int j = 0; j < ACNE_NB_TRACKS/4; j++)
						snapshots[currentSnapshot][i][j] = snapshots[copySnapshot][i][j];
				sceneInMutes[currentSnapshot] = sceneInMutes[copySnapshot];
				sceneInSolos[currentSnapshot] = sceneInSolos[copySnapshot];
				sceneOutMutes[currentSnapshot] = sceneOutMutes[copySnapshot];
				copyState = false;
				loadScene();
			} else {
				// pressed again on the copied scene: cancel
				copyState = false;
			}
		}
		lights[COPY_LIGHT].setBrightness(copyState ? 1 : 0);

		if (!inputs[SNAPSHOT_INPUT].isConnected()) {
			for (int i = 0; i < ACNE_NB_SNAPSHOTS; i++) {
				if (snapshotTriggers[i].process(params[SNAPSHOT_PARAMS + i].getValue())) {
					switchSnapshot(i);
				}
			}
		}
		for (int i = 0; i < ACNE_NB_SNAPSHOTS; i++)
			lights[SNAPSHOT_LIGHTS + i].setBrightness(i == currentSnapshot ? 1 : 0);

		if ((eFader>-1) && shifted) {
			int index = eFader%ACNE_NB_TRACKS;
			int outOffset = (eFader/ACNE_NB_TRACKS)%2==0?1:-1;
			int inOffset = links[index/2]?(index%2==0?1:-1):0;
			int relative=eFader+ACNE_NB_TRACKS*outOffset+inOffset;
			params[FADERS_PARAMS+relative].setValue(params[FADERS_PARAMS+eFader].getValue());
		}

		if (clearMutesTrigger.process(params[MUTE_PARAM].getValue())) {
			for (int i = 0; i < ACNE_NB_TRACKS; i++)
				inMutes[i] = false;
		}
		if (clearSendMutesTrigger.process(params[SEND_MUTE_PARAM].getValue())) {
			for (int i = 0; i < ACNE_NB_OUTS; i++)
				outMutes[i] = false;
		}
		if (clearSolosTrigger.process(params[SOLO_PARAM].getValue())) {
			for (int i = 0; i < ACNE_NB_TRACKS; i++)
				inSolo[i] = false;
		}

		solo = false;
		for (int i = 0; i < ACNE_NB_TRACKS; i++) {
			int linkIndex = i/2;
			int linkSwitch = i%2;

			if (linksTriggers[linkIndex].process(params[TRACKLINK_PARAMS + linkIndex].getValue()))	links[linkIndex] = !links[linkIndex];
			lights[TRACKLINK_LIGHTS + linkIndex].setBrightness(links[linkIndex] ? 1 : 0);

			if (inMutesTriggers[i].process(params[IN_MUTE_PARAMS + i].getValue())) {
				inMutes[i] = !inMutes[i];
				if (links[linkIndex])
					inMutes[linkSwitch == 0 ? i+1 : i-1] = inMutes[i];
			}

			if (inSoloTriggers[i].process(params[IN_SOLO_PARAMS + i].getValue())) {
				inSolo[i] = !inSolo[i];
				if (links[linkIndex])
					inSolo[linkSwitch == 0 ? i+1 : i-1] = inSolo[i];
			}
			lights[IN_MUTE_LIGHTS + i].setBrightness(inMutes[i] ? 1 : 0);
			lights[IN_SOLO_LIGHTS + i].setBrightness(inSolo[i] ? 1 : 0);
			solo = solo || inSolo[i];
		}

		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			if (outMutesTriggers[i].process(params[OUT_MUTE_PARAMS + i].getValue())) {
				outMutes[i] = !outMutes[i];
			}
			lights[OUT_MUTE_LIGHTS + i].setBrightness(outMutes[i] ? 1 : 0);
		}

		// The live faders and mutes go into the scene at control rate (and
		// before any scene switch or copy, see switchSnapshot).
		if (autosave || save)
			storeScene();
	}
	if (control || sceneSwitched) {
		for (int i = 0; i < ACNE_NB_OUTS; i++)
			for (int j = 0; j < ACNE_NB_TRACKS/4; j++)
				targets[i][j] = targetGain(i, j);
	}

	// Morph progress, then the 5 ms smoother towards it.
	float morphTime = params[RAMP_PARAM].getValue();
	if (morphPhase < 1.f)
		morphPhase = morphTime > 0.f ? std::min(1.f, morphPhase + args.sampleTime / morphTime) : 1.f;

	bool anyOut = false;
	for (int i = 0; i < ACNE_NB_OUTS; i++)
		anyOut = anyOut || outputs[TRACKS_OUTPUTS + i].isConnected();

	float_4 in[ACNE_NB_TRACKS/4] = {};
	if (anyOut) {
		for (int j = 0; j < ACNE_NB_TRACKS/4; j++)
			for (int l = 0; l < 4; l++)
				in[j][l] = inputs[TRACKS_INPUTS + j*4 + l].getVoltageSum();
	}

	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		// The gains keep gliding without a cable, so a cable plugged later starts from the right level.
		float_4 sum = 0.f;
		for (int j = 0; j < ACNE_NB_TRACKS/4; j++) {
			float_4 target = targets[i][j];
			if (morphPhase < 1.f)
				target = morphFrom[i][j] + (target - morphFrom[i][j]) * morphPhase;
			gains[i][j] += (target - gains[i][j]) * smoothCoef;
			sum += gains[i][j] * in[j];
		}
		outputs[TRACKS_OUTPUTS + i].setVoltage(anyOut ? sum[0] + sum[1] + sum[2] + sum[3] : 0.f);
	}

	mainGain += (params[MAIN_OUT_GAIN_PARAM].getValue() - mainGain) * smoothCoef;
	outputs[TRACKS_OUTPUTS].setVoltage(outputs[TRACKS_OUTPUTS].getVoltage() * mainGain);
	outputs[TRACKS_OUTPUTS + 1].setVoltage(outputs[TRACKS_OUTPUTS + 1].getVoltage() * mainGain);
}

struct ACNEWidget : BidooWidget {
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
	prepareThemes(asset::plugin(pluginInstance, "res/ACNE.svg"));

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

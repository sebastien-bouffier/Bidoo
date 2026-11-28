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
	// a polyphonic input spreads its channels over the next inputs left empty (off in patches made before it)
	bool polySpread = true;
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

	// The fader that Shift moves with fader f: the same input on the other output of its pair (1-2, 3-4...), and
	// with the input's stereo link on, the other input of that pair too (the diagonal)
	int pairOf(int f) const {
		int index = f%ACNE_NB_TRACKS;
		int outOffset = (f/ACNE_NB_TRACKS)%2==0?1:-1;
		int inOffset = links[index/2]?(index%2==0?1:-1):0;
		return f+ACNE_NB_TRACKS*outOffset+inOffset;
	}

	// Gains actually applied: they glide to the scene over the morph time,
	// then follow every change through a 5 ms smoother.
	float_4 gains[ACNE_NB_OUTS][ACNE_NB_TRACKS/4] = {{0.0f}};
	float_4 morphFrom[ACNE_NB_OUTS][ACNE_NB_TRACKS/4] = {{0.0f}};
	float morphPhase = 1.0f;
	float mainGain = 0.7f;
	float smoothCoef = 0.f;
	// level meters: peak of each block of 32 samples, falling 20 dB/s (read by the panel's meters)
	static constexpr int METER_BLOCK = 32;
	float inLevel[ACNE_NB_TRACKS] = {}, outLevel[ACNE_NB_OUTS] = {};
	float inPeak[ACNE_NB_TRACKS] = {}, outPeak[ACNE_NB_OUTS] = {};
	float meterFall = 1.f;
	int meterPos = 0;
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
		json_object_set_new(rootJ, "polySpread", json_boolean(polySpread));
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
		// the live state, which may differ from the scene when AUTOSAVE is off (the faders are Rack params)
		uint32_t im = 0, is = 0, om = 0;
		for (int i = 0; i < ACNE_NB_OUTS; i++)
			if (outMutes[i]) om |= 1u << i;
		for (int i = 0; i < ACNE_NB_TRACKS; i++) {
			if (inMutes[i]) im |= 1u << i;
			if (inSolo[i]) is |= 1u << i;
		}
		json_object_set_new(rootJ, "liveInMutes", json_integer(im));
		json_object_set_new(rootJ, "liveInSolos", json_integer(is));
		json_object_set_new(rootJ, "liveOutMutes", json_integer(om));

		for (int i = 0; i < 8; i++) {
			json_object_set_new(rootJ, ("link" + to_string(i)).c_str(), json_boolean(links[i]));
		}

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *autosaveJ = json_object_get(rootJ, "autosave");
		if (autosaveJ) autosave = json_is_true(autosaveJ);
		json_t *polySpreadJ = json_object_get(rootJ, "polySpread");
		polySpread = polySpreadJ && json_is_true(polySpreadJ);  // older patches summed every channel into its input
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

		json_t *liveInJ = json_object_get(rootJ, "liveInMutes");
		json_t *liveSoloJ = json_object_get(rootJ, "liveInSolos");
		json_t *liveOutJ = json_object_get(rootJ, "liveOutMutes");
		if (liveInJ && liveSoloJ && liveOutJ) {
			// keep the faders Rack restored and the saved live mutes
			for (int i = 0; i < ACNE_NB_OUTS; i++)
				outMutes[i] = (json_integer_value(liveOutJ) >> i) & 1;
			for (int i = 0; i < ACNE_NB_TRACKS; i++) {
				inMutes[i] = (json_integer_value(liveInJ) >> i) & 1;
				inSolo[i] = (json_integer_value(liveSoloJ) >> i) & 1;
			}
		}
		else
			loadScene();   // older patches
		// targetGain reads solo, which process() only works out at control rate
		solo = false;
		for (int i = 0; i < ACNE_NB_TRACKS; i++)
			solo = solo || inSolo[i];
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

	void onReset(const ResetEvent &e) override {
		// the params back to their defaults first, then the scenes over them
		BidooModule::onReset(e);
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
		meterFall = std::pow(10.f, -20.f * METER_BLOCK * args.sampleTime / 20.f);
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

		if ((eFader>-1) && shifted)
			params[FADERS_PARAMS+pairOf(eFader)].setValue(params[FADERS_PARAMS+eFader].getValue());

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
	{
		float trk[ACNE_NB_TRACKS] = {};
		for (int t = 0; t < ACNE_NB_TRACKS; t++) {
			Input &cable = inputs[TRACKS_INPUTS + t];
			if (!polySpread) {
				trk[t] = cable.getVoltageSum();
				continue;
			}
			int n = cable.getChannels();
			if (n == 0)
				continue;
			// channel c goes to input t + c while those inputs have no cable; the rest stays summed in t
			int stop = t + 1;
			while (stop < ACNE_NB_TRACKS && stop < t + n && !inputs[TRACKS_INPUTS + stop].isConnected())
				stop++;
			for (int c = 0; c < n; c++)
				trk[t + c < stop ? t + c : t] += cable.getVoltage(c);
		}
		for (int j = 0; j < ACNE_NB_TRACKS/4; j++)
			in[j] = float_4::load(trk + j*4);
		for (int t = 0; t < ACNE_NB_TRACKS; t++)
			inPeak[t] = std::max(inPeak[t], std::fabs(trk[t]));
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

	for (int i = 0; i < ACNE_NB_OUTS; i++)
		outPeak[i] = std::max(outPeak[i], std::fabs(outputs[TRACKS_OUTPUTS + i].getVoltage()));
	if (++meterPos >= METER_BLOCK) {
		meterPos = 0;
		for (int t = 0; t < ACNE_NB_TRACKS; t++) {
			inLevel[t] = std::max(inPeak[t], inLevel[t] * meterFall);
			inPeak[t] = 0.f;
		}
		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			outLevel[i] = std::max(outPeak[i], outLevel[i] * meterFall);
			outPeak[i] = 0.f;
		}
	}
}

// A thin level bar between two jacks (vertical, or horizontal when wider than tall): green below -6 dB (5 V = 0 dB), amber up to
// 10 V, red at 10 V and over; a peak line held 1 s. On the light layer.
struct AcneMeter : TransparentWidget {
	const float *level = nullptr;
	float peak = 0.f, held = 0.f;

	static float pos(float v) {  // -48 dB .. +6 dB (10 V) -> 0..1
		float db = 20.f * std::log10(std::max(v, 1e-6f) / 5.f);
		return clamp((db + 48.f) / 54.f, 0.f, 1.f);
	}

	void drawLayer(const DrawArgs &args, int layer) override {
		if (layer == 1) {
			float w = box.size.x, h = box.size.y;
			bool horizontal = w > h;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, w, h);
			nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 110));
			nvgFill(args.vg);
			float v = level ? *level : 0.f;
			float dt = APP->window->getLastFrameDuration();
			if (v >= peak) { peak = v; held = 0.f; }
			else if ((held += dt) > 1.f) peak = std::max(v, peak * std::pow(10.f, -20.f * dt / 20.f));
			float p = pos(v);
			const float amberAt = pos(2.5f), redAt = pos(10.f) - 0.04f;
			auto seg = [&](float from, float to, NVGcolor c) {
				if (to <= from) return;
				nvgBeginPath(args.vg);
				if (horizontal)
					nvgRect(args.vg, w * from, 0, w * (to - from), h);
				else
					nvgRect(args.vg, 0, h * (1.f - to), w, h * (to - from));
				nvgFillColor(args.vg, c);
				nvgFill(args.vg);
			};
			seg(0.f, std::min(p, amberAt), nvgRGB(0x1d, 0x9e, 0x75));
			seg(amberAt, std::min(p, redAt), nvgRGB(0xef, 0x9f, 0x27));
			if (v >= 10.f) seg(redAt, 1.f, nvgRGB(0xe2, 0x4b, 0x4a));
			if (peak > 1e-3f) {
				nvgBeginPath(args.vg);
				if (horizontal)
					nvgRect(args.vg, std::min(w * pos(peak), w - 1.f), 0, 1.f, h);
				else
					nvgRect(args.vg, 0, std::min(h * (1.f - pos(peak)), h - 1.f), w, 1.f);
				nvgFillColor(args.vg, peak >= 10.f ? nvgRGB(0xe2, 0x4b, 0x4a) : nvgRGB(0xe0, 0xe0, 0xe0));
				nvgFill(args.vg);
			}
		}
		Widget::drawLayer(args, layer);
	}
};

static AcneMeter *acneMeter(const float *level, float x, float y, float w, float h) {
	AcneMeter *m = new AcneMeter;
	m->level = level;
	m->box.pos = Vec(x, y);
	m->box.size = Vec(w, h);
	return m;
}

struct ACNEWidget : BidooWidget {
	ACNEWidget(ACNE *module);

	void appendContextMenu(ui::Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		ACNE *module = dynamic_cast<ACNE*>(this->module);
		if (!module)
			return;
		menu->addChild(new MenuSeparator());
		menu->addChild(createBoolPtrMenuItem("Spread a polyphonic input over the next empty inputs", "", &module->polySpread));
	}
};

struct AcneBidooColoredTrimpot : BidooColoredTrimpot {
	int index=0;

	// no module in the library browser
	ACNE *acneModule() {
		ParamQuantity *pq = getParamQuantity();
		return pq ? dynamic_cast<ACNE*>(pq->module) : nullptr;
	}

	void onDragStart(const event::DragStart& e) override {
		ACNE* acne = acneModule();
		if (acne)
			acne->eFader=index;
		BidooColoredTrimpot::onDragStart(e);
	}

	void onDragEnd(const event::DragEnd& e) override {
		ACNE* acne = acneModule();
		if (acne) {
			acne->eFader=-1;
			acne->shifted=false;
		}
		BidooColoredTrimpot::onDragEnd(e);
	}

	void onButton(const event::Button &e) override {
		ACNE* acne = acneModule();
		if (acne && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) {
					acne->shifted=true;
		}
		BidooColoredTrimpot::onButton(e);
	}

	// Shift + scroll moves the pair too, as Shift + drag does
	void onHoverScroll(const event::HoverScroll& e) override {
		BidooColoredTrimpot::onHoverScroll(e);
		ParamQuantity *pq = getParamQuantity();
		ACNE* acne = pq ? dynamic_cast<ACNE*>(pq->module) : nullptr;
		if (acne && (APP->window->getMods() & RACK_MOD_MASK) == GLFW_MOD_SHIFT)
			acne->params[ACNE::FADERS_PARAMS+acne->pairOf(index)].setValue(pq->getValue());
	}
};

ACNEWidget::ACNEWidget(ACNE *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/ACNE.svg"));

	theme::addScrews(this);

	// The panel of tools/panels/acne.py: the old arrangement. The matrix of coloured trimpots keeps its old corners and
	// gives every column and row of the module (specific: the old panel's 27 px pitch, not the grid). The buttons are
	// BidooRoundButtons at the old LED buttons' centres, snapped onto the matrix's columns and rows (0.3 px): lit orange
	// by their state (snapshot, links, autosave, save, copy), except the mutes and solos, which keep their red and green
	// lights (the colour says mute or solo, as the M and S clear buttons do). The jacks stay tiny, in strips: full jacks
	// 27 px apart would leave 3 px between them.
	const theme::Size kAcneTrimpotSize = {mm2px(6.3f), mm2px(6.3f)};   // TrimpotBidoo.svg
	const theme::Size kMuteBtnSize = {16.f, 16.f};                     // MuteBtn, SoloBtn
	const float kPitch = 27.f;                                         // PITCH in acne.py
	const theme::Pos firstTrim = theme::specificCorner(43, 77, kAcneTrimpotSize);
	auto colOf = [&](float j) { return firstTrim.right(kPitch * j); };
	auto rowOf = [&](float i) { return firstTrim.down(kPitch * i); };

	// the matrix
	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		for (int j = 0; j < ACNE_NB_TRACKS; j++) {
			AcneBidooColoredTrimpot *fader = theme::param<AcneBidooColoredTrimpot>(firstTrim.offset(kPitch * j, kPitch * i), module, ACNE::FADERS_PARAMS + j + i * ACNE_NB_TRACKS);
			fader->index = j + i * ACNE_NB_TRACKS;
			addParam(fader);
		}
	}

	auto button = [&](theme::Pos p, int paramId, int lightId) {
		BidooRoundButton *b = theme::param<BidooRoundButton>(p, module, paramId);
		b->momentary = true;
		b->litBy = [=]() { return module && module->lights[lightId].getBrightness() > 0.5f; };
		addParam(b);
		return b;
	};
	// a mute or a solo: the button's centre stays white, its coloured light on it shows the state
	auto colouredButton = [&](theme::Pos p, int paramId) {
		BidooRoundButton *b = theme::param<BidooRoundButton>(p, module, paramId);
		b->momentary = true;
		b->litBy = []() { return false; };
		addParam(b);
	};

	// the snapshots: a row of buttons on the matrix's columns, the scene CV jack at the left end of their bracket, RAMP
	// on its right end (old corners, the CV jack 0.1 px lower onto the bracket through RAMP)
	const float kSnapY = 58.f;   // SNAP_Y in acne.py
	const theme::Pos ramp = theme::specificCorner(432, 28, theme::kTrimpotSize);
	addParam(theme::param<BidooBlueTrimpot>(ramp, module, ACNE::RAMP_PARAM));
	addInput(theme::input<TinyPJ301MPort>(theme::sameRow(ramp, theme::specificCorner(58, 30, theme::kTinyJackSize).x), module, ACNE::SNAPSHOT_INPUT));
	for (int i = 0; i < ACNE_NB_SNAPSHOTS; i++)
		button(theme::sameCol(colOf(i), kSnapY), ACNE::SNAPSHOT_PARAMS + i, ACNE::SNAPSHOT_LIGHTS + i);

	// the top corners: AUTOSAVE and SAVE (2 px further from the screws than the old LED buttons), COPY/PASTE at the old
	// CKD6's centre, VOLUME over the outputs' column (0.74 px right)
	const theme::Pos kAutosave = theme::specific(43, 12), kSave = theme::specific(467, 12);   // AUTOSAVE, SAVE in acne.py
	button(kAutosave, ACNE::AUTOSAVE_PARAM, ACNE::AUTOSAVE_LIGHT);
	button(kSave, ACNE::SAVE_PARAM, ACNE::SAVE_LIGHT);
	button(theme::specificCorner(7, 39, theme::kCKD6Size), ACNE::COPY_PARAM, ACNE::COPY_LIGHT);
	const theme::Pos outCol = theme::specificCorner(482, 0, theme::kTinyJackSize);   // OUT_X in acne.py
	addParam(theme::param<BidooBlueKnob>(theme::colRow(outCol, theme::specificCorner(0, 39, theme::kBlueKnobSize)), module, ACNE::MAIN_OUT_GAIN_PARAM));

	// the outputs, on the matrix's rows (0.2 px higher): tiny jacks in the red strips, a meter left of each; their mutes
	// left of the matrix (0.3 px lower)
	const float kMeterLeft = 13.5f, kMeterW = 3.f, kMeterH = 15.f;   // a meter's left edge from its jack's centre
	const theme::Size kLEDButtonSize = {17.9996f, 18.f};                                 // the old LEDButtons: their centres
	const theme::Pos outMuteCol = theme::specificCorner(10, 0, kLEDButtonSize);
	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		const theme::Pos out = theme::colRow(outCol, rowOf(i));
		addOutput(theme::output<TinyPJ301MPort>(out, module, ACNE::TRACKS_OUTPUTS + i));
		addChild(acneMeter(module ? &module->outLevel[i] : nullptr, out.x - kMeterLeft, out.y - kMeterH * 0.5f, kMeterW, kMeterH));
		const theme::Pos mute = theme::colRow(outMuteCol, rowOf(i));
		colouredButton(mute, ACNE::OUT_MUTE_PARAMS + i);
		addChild(theme::light<SmallLight<RedLight>>(mute, module, ACNE::OUT_MUTE_LIGHTS + i));
	}

	// the inputs, on the matrix's columns (0.2 px left): tiny jacks in the black strip, a meter between each and the
	// next; their mutes and solos above them (0.3 px right), the track links between two columns and between the two
	// rows (1.15 px right, 0.35 px lower)
	const float kMuteY = 301.f, kSoloY = 323.f, kInY = 347.5f;   // MUTE_Y, SOLO_Y, IN_Y in acne.py
	for (int i = 0; i < ACNE_NB_TRACKS; i++) {
		const theme::Pos in = theme::sameCol(colOf(i), kInY);
		addInput(theme::input<TinyPJ301MPort>(in, module, ACNE::TRACKS_INPUTS + i));
		addChild(acneMeter(module ? &module->inLevel[i] : nullptr, in.x + kPitch * 0.5f - kMeterW * 0.5f, in.y - kMeterH * 0.5f, kMeterW, kMeterH));
		const theme::Pos mute = theme::sameCol(colOf(i), kMuteY), solo = theme::sameCol(colOf(i), kSoloY);
		colouredButton(mute, ACNE::IN_MUTE_PARAMS + i);
		addChild(theme::light<SmallLight<RedLight>>(mute, module, ACNE::IN_MUTE_LIGHTS + i));
		colouredButton(solo, ACNE::IN_SOLO_PARAMS + i);
		addChild(theme::light<SmallLight<GreenLight>>(solo, module, ACNE::IN_SOLO_LIGHTS + i));
	}
	const float kLinkButton = 10.f;   // small: it sits in the gap between four buttons, 2.4 px from each
	for (int i = 0; i < 8; i++) {
		const theme::Pos link = theme::sameCol(colOf(2 * i + 0.5f), 0.5f * (kMuteY + kSoloY));
		BidooRoundButton *b = button(link, ACNE::TRACKLINK_PARAMS + i, ACNE::TRACKLINK_LIGHTS + i);
		b->box.size = Vec(kLinkButton, kLinkButton);   // placed again at the same centre
		theme::place(b, link);
	}

	// the clear buttons, as before: M (outputs), M (inputs), S (solos), S 1 px lower onto the solos' row
	addParam(theme::param<MuteBtn>(theme::specificCorner(2, 293, kMuteBtnSize), module, ACNE::SEND_MUTE_PARAM));
	addParam(theme::param<MuteBtn>(theme::specificCorner(21, 293, kMuteBtnSize), module, ACNE::MUTE_PARAM));
	addParam(theme::param<SoloBtn>(theme::sameCol(theme::specificCorner(11, 314, kMuteBtnSize), kSoloY), module, ACNE::SOLO_PARAM));
}

Model *modelACNE = createModel<ACNE, ACNEWidget>("ACnE");

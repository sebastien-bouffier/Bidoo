#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <iomanip>
#include <sstream>

using namespace std;

struct ACNE : Module {
	enum ParamIds {
	  COPY_PARAM,
		MAIN_OUT_GAIN_PARAM,
		OUT_MUTE_PARAMS,
		IN_MUTE_PARAMS = OUT_MUTE_PARAMS + ACNE_NB_OUTS,
		IN_SOLO_PARAMS = IN_MUTE_PARAMS + ACNE_NB_TRACKS,
		SNAPSHOT_PARAMS = IN_SOLO_PARAMS + ACNE_NB_TRACKS,
		FADERS_PARAMS = SNAPSHOT_PARAMS + ACNE_NB_SNAPSHOTS,
		NUM_PARAMS = FADERS_PARAMS + (ACNE_NB_TRACKS * ACNE_NB_OUTS)
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
		OUT_MUTE_LIGHTS,
		IN_MUTE_LIGHTS = OUT_MUTE_LIGHTS + ACNE_NB_OUTS,
		IN_SOLO_LIGHTS = IN_MUTE_LIGHTS + ACNE_NB_TRACKS,
		SNAPSHOT_LIGHTS = IN_SOLO_LIGHTS + ACNE_NB_TRACKS,
		NUM_LIGHTS = SNAPSHOT_LIGHTS + ACNE_NB_SNAPSHOTS
	};

	int currentSnapshot = 0;
	int copySnapshot = 0;
	bool copyState = false;
	float snapshots[ACNE_NB_SNAPSHOTS][ACNE_NB_OUTS][ACNE_NB_TRACKS] = {{{0}}};
	bool  outMutes[ACNE_NB_OUTS] = {0};
	bool  inMutes[ACNE_NB_TRACKS] = {0};
	bool  inSolo[ACNE_NB_TRACKS] = {0};
	SchmittTrigger outMutesTriggers[ACNE_NB_OUTS];
	SchmittTrigger inMutesTriggers[ACNE_NB_TRACKS];
	SchmittTrigger inSoloTriggers[ACNE_NB_TRACKS];
	SchmittTrigger snapshotTriggers[ACNE_NB_SNAPSHOTS];
	//bool updateWidget = false;


	ACNE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {	}

	void step() override;

	json_t *toJson() override {
		json_t *rootJ = json_object();
		// scenes
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

		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		// scenes
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
									snapshots[i][j][k] = json_real_value(inJ);
								}
							}
						}
					}
				}
			}
		}
	}
};

void ACNE::step() {

	if (inputs[SNAPSHOT_INPUT].active) {
		int newSnapshot = clampi((int)(inputs[SNAPSHOT_INPUT].value * 16 / 10),0,ACNE_NB_SNAPSHOTS-1);
		if (currentSnapshot != newSnapshot) {
			//updateWidget = true;
			currentSnapshot = newSnapshot;
		}
	}
	else {
		for (int i = 0; i < ACNE_NB_SNAPSHOTS; i++) {
			if (snapshotTriggers[i].process(params[SNAPSHOT_PARAMS + i].value)) {
				currentSnapshot = i;
			}
		}
	}

	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		if (outMutesTriggers[i].process(params[OUT_MUTE_PARAMS + i].value)) {
			outMutes[i] = !outMutes[i];
		}
	}

	for (int i = 0; i < ACNE_NB_TRACKS; i++) {
		if (inMutesTriggers[i].process(params[IN_MUTE_PARAMS + i].value)) {
			inMutes[i] = !inMutes[i];
		}
		if (inSoloTriggers[i].process(params[IN_SOLO_PARAMS + i].value)) {
			inSolo[i] = !inSolo[i];
		}
	}

	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		outputs[TRACKS_OUTPUTS + i].value = 0;
		if (!outMutes[i]) {
			int sum = 0;
			for (int s = 0; s < ACNE_NB_TRACKS; ++s) {
			  sum |= (inSolo[s] == true ? 1 : 0);
			}
			if (sum > 0) {
				for (int j = 0; j < ACNE_NB_TRACKS; j ++) {
					if ((inputs[TRACKS_INPUTS + j].active) && (inSolo[j])) {
						outputs[TRACKS_OUTPUTS + i].value = outputs[TRACKS_OUTPUTS + i].value + (snapshots[currentSnapshot][i][j] / 10) * inputs[TRACKS_INPUTS + j].value / 32768.0f;
					}
				}
			}
			else {
				for (int j = 0; j < ACNE_NB_TRACKS; j ++) {
					if ((inputs[TRACKS_INPUTS + j].active) && (!inMutes[j])) {
						outputs[TRACKS_OUTPUTS + i].value = outputs[TRACKS_OUTPUTS + i].value + (snapshots[currentSnapshot][i][j] / 10) * inputs[TRACKS_INPUTS + j].value / 32768.0f;
					}
				}
			}
			outputs[TRACKS_OUTPUTS + i].value = outputs[TRACKS_OUTPUTS + i].value * 32768.0f;
		}
	}

	outputs[TRACKS_OUTPUTS].value = outputs[TRACKS_OUTPUTS].value * params[MAIN_OUT_GAIN_PARAM].value / 10;
	outputs[TRACKS_OUTPUTS + 1].value = outputs[TRACKS_OUTPUTS + 1].value * params[MAIN_OUT_GAIN_PARAM].value / 10;

	lights[COPY_LIGHT].value = (copyState == true) ? 1 : 0;
	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		lights[OUT_MUTE_LIGHTS + i].value = (outMutes[i] == true) ? 1 : 0;
	}
	for (int i = 0; i < ACNE_NB_TRACKS; i++) {
		lights[IN_MUTE_LIGHTS + i].value = (inMutes[i] == true) ? 1 : 0;
		lights[IN_SOLO_LIGHTS + i].value = (inSolo[i] == true) ? 1 : 0;
	}
	for (int i = 0; i < ACNE_NB_SNAPSHOTS; i++) {
		lights[SNAPSHOT_LIGHTS + i].value = (i == currentSnapshot) ? 1 : 0;
	}
}

struct ACNETrimPot : BidooColoredTrimpot {
	void onChange(EventChange &e) override {
			ACNEWidget *parent = dynamic_cast<ACNEWidget*>(this->parent);
			ACNE *module = dynamic_cast<ACNE*>(this->module);
			if (parent && module) {
				module->snapshots[module->currentSnapshot][(int)((this->paramId - ACNE::FADERS_PARAMS)/ACNE_NB_TRACKS)][(this->paramId - ACNE::FADERS_PARAMS)%ACNE_NB_TRACKS] = value;
			}
			BidooColoredTrimpot::onChange(e);
	}

	virtual void onMouseDown(EventMouseDown &e) override {
		BidooColoredTrimpot::onMouseDown(e);
		ACNEWidget *parent = dynamic_cast<ACNEWidget*>(this->parent);
		ACNE *module = dynamic_cast<ACNE*>(this->module);
		if (parent && module) {
			if (e.button == 2) {
				this->setValue(10);
				module->snapshots[module->currentSnapshot][(int)((this->paramId - ACNE::FADERS_PARAMS)/ACNE_NB_TRACKS)][(this->paramId - ACNE::FADERS_PARAMS)%ACNE_NB_TRACKS] = 10;
			}
		}
	}
};

struct ACNEChoseSceneLedButton : LEDButton {
	void onMouseDown(EventMouseDown &e) override {
		ACNEWidget *parent = dynamic_cast<ACNEWidget*>(this->parent);
		ACNE *module = dynamic_cast<ACNE*>(this->module);
		if (parent && module) {
			module->currentSnapshot = this->paramId - ACNE::SNAPSHOT_PARAMS;
			parent->UpdateSnapshot(module->currentSnapshot);
		}
		LEDButton::onMouseDown(e);
	}
};

struct ACNECOPYPASTECKD6 : BlueCKD6 {
	void onMouseDown(EventMouseDown &e) override {
		ACNEWidget *parent = dynamic_cast<ACNEWidget*>(this->parent);
		ACNE *module = dynamic_cast<ACNE*>(this->module);
		if (parent && module) {
			if (!module->copyState) {
				module->copySnapshot = module->currentSnapshot;
				module->copyState = true;
			} else if ((module->copyState) && (module->copySnapshot != module->currentSnapshot)) {
				for (int i = 0; i < ACNE_NB_OUTS; i++) {
					for (int j = 0; j < ACNE_NB_TRACKS; j++) {
						module->snapshots[module->currentSnapshot][i][j] = module->snapshots[module->copySnapshot][i][j];
					}
				}
				parent->UpdateSnapshot(module->currentSnapshot);
				module->copyState = false;
			}
		}
		BlueCKD6::onMouseDown(e);
	}
};

ACNEWidget::ACNEWidget() {
	ACNE *module = new ACNE();
	setModule(module);
	box.size = Vec(15*34, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/ACNE.svg")));
		addChild(panel);
	}

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));


	addParam(createParam<BidooBlueKnob>(Vec(472, 39), module, ACNE::MAIN_OUT_GAIN_PARAM, 0, 10, 7));

	addParam(createParam<ACNECOPYPASTECKD6>(Vec(7, 39), module, ACNE::COPY_PARAM, 0, 1, 0.0));
	addChild(createLight<SmallLight<GreenLight>>(Vec(18, 28), module, ACNE::COPY_LIGHT));

	addInput(createInput<TinyPJ301MPort>(Vec(58, 30), module, ACNE::SNAPSHOT_INPUT));

	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		addOutput(createOutput<TinyPJ301MPort>(Vec(482, 79+i*27), module, ACNE::TRACKS_OUTPUTS + i));

		addParam(createParam<LEDButton>(Vec(10, 77+i*27), module, ACNE::OUT_MUTE_PARAMS + i, 0.0, 1.0,  0));
		addChild(createLight<SmallLight<RedLight>>(Vec(16, 82+i*27), module, ACNE::OUT_MUTE_LIGHTS + i));
	}

	for (int i = 0; i < ACNE_NB_TRACKS; i++) {
		addParam(createParam<ACNEChoseSceneLedButton>(Vec(43+i*27, 49), module, ACNE::SNAPSHOT_PARAMS + i, 0.0, 1.0,  0));
		addChild(createLight<SmallLight<BlueLight>>(Vec(49+i*27, 55), module, ACNE::SNAPSHOT_LIGHTS + i));

		addInput(createInput<TinyPJ301MPort>(Vec(45+i*27, 338), module, ACNE::TRACKS_INPUTS + i));

		addParam(createParam<LEDButton>(Vec(43+i*27, 292), module, ACNE::IN_MUTE_PARAMS + i, 0.0, 1.0,  0));
		addChild(createLight<SmallLight<RedLight>>(Vec(49+i*27, 297), module, ACNE::IN_MUTE_LIGHTS + i));

		addParam(createParam<LEDButton>(Vec(43+i*27, 314), module, ACNE::IN_SOLO_PARAMS + i, 0.0, 1.0,  0));
		addChild(createLight<SmallLight<GreenLight>>(Vec(49+i*27, 320), module, ACNE::IN_SOLO_LIGHTS + i));
	}

	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		for (int j = 0; j < ACNE_NB_TRACKS; j++) {
				faders[i][j] = createParam<ACNETrimPot>(Vec(43+j*27, 77+i*27), module, ACNE::FADERS_PARAMS + j + i * (ACNE_NB_TRACKS), 0.0, 10, 0);
				addParam(faders[i][j]);
		}
	}
	module->currentSnapshot = 0;
	UpdateSnapshot(module->currentSnapshot);
}

void ACNEWidget::UpdateSnapshot(int snapshot) {
	ACNE *module = dynamic_cast<ACNE*>(this->module);
	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		for (int j = 0; j < ACNE_NB_TRACKS; j++) {
				faders[i][j]->setValue(module->snapshots[snapshot][i][j]);
		}
	}
}

void ACNEWidget::step() {
	// ACNE *module = dynamic_cast<ACNE*>(this->module);
	// if (module->updateWidget) {
	// 	UpdateSnapshot(module->currentSnapshot);
	// 	module->updateWidget = false;
	// }
	ModuleWidget::step();
}

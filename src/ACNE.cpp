#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;

struct ACNE : Module {
	enum ParamIds {
	  COPY_PARAM,
		SNAPSHOT_PARAMS,
		FADERS_PARAMS = SNAPSHOT_PARAMS + ACNE_NB_SNAPSHOTS,
		NUM_PARAMS = FADERS_PARAMS + (ACNE_NB_SNAPSHOTS * ACNE_NB_TRACKS * ACNE_NB_OUTS)
	};
	enum InputIds {
		TRACKS_INPUTS,
		NUM_INPUTS = TRACKS_INPUTS + ACNE_NB_TRACKS
	};
	enum OutputIds {
		TRACKS_OUTPUTS,
		NUM_OUTPUTS = TRACKS_OUTPUTS + ACNE_NB_OUTS
	};
	enum LightIds {
		COPY_LIGHT,
		SNAPSHOT_LIGHTS,
		NUM_LIGHTS = SNAPSHOT_LIGHTS + ACNE_NB_SNAPSHOTS
	};

	int currentSnapshot = 0;
	int copySnapshot = 0;
	bool copyState = false;

	ACNE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {	}

	void step() override;
};


void ACNE::step() {

	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		outputs[TRACKS_OUTPUTS + i].value = 0;
		int nbActiveTracks = 0 ;
		for (int j = 0; j < ACNE_NB_TRACKS; j ++) {
			if (inputs[TRACKS_INPUTS + j].active) {
				outputs[TRACKS_OUTPUTS + i].value = outputs[TRACKS_OUTPUTS + i].value + params[currentSnapshot * (ACNE_NB_TRACKS * ACNE_NB_OUTS) + FADERS_PARAMS + j + i * ACNE_NB_TRACKS].value * inputs[TRACKS_INPUTS + j].value / 10;
				nbActiveTracks++;
			}
		}
		if (nbActiveTracks>0) {
			outputs[TRACKS_OUTPUTS + i].value = outputs[TRACKS_OUTPUTS + i].value / nbActiveTracks;
		}
	}

	for (int i = 0 ; i < ACNE_NB_SNAPSHOTS ; i++ ) {
		lights[SNAPSHOT_LIGHTS+i].value = (i == currentSnapshot);
	}
	lights[COPY_LIGHT].value = (copyState == true) ? 1 : 0;
}

struct ACNETrimPot : Trimpot {
	ACNETrimPot() {
		box.size = Vec(17, 17);
		minAngle = -0.75*M_PI;
		maxAngle = 0.75*M_PI;
		setSVG(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/ACNETrimpot.svg")));
	}
	void onChange(EventChange &e) override {
		ACNEWidget *parent = dynamic_cast<ACNEWidget*>(this->parent);
		ACNE *module = dynamic_cast<ACNE*>(this->module);
		if (parent && module) {
			if ((this->value > 0) && (this->value < 5))  {
				setSVG(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/ACNETrimpot05.svg")));
			} else if ((this->value >= 5) && (this->value <10))  {
				setSVG(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/ACNETrimpot510.svg")));
			} else if (this->value == 10)  {
				setSVG(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/ACNETrimpotON.svg")));
			} else {
				setSVG(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/ACNETrimpot.svg")));
			}
		}
		Trimpot::onChange(e);
	}

	virtual void onMouseDown(EventMouseDown &e) override {
		Trimpot::onMouseDown(e);
		ACNEWidget *parent = dynamic_cast<ACNEWidget*>(this->parent);
		ACNE *module = dynamic_cast<ACNE*>(this->module);
		if (parent && module) {
			if (e.button == 2) {
				this->setValue(10);
				module->params[this->paramId].value = 10;
			}
		}
	}
};

struct ACNECKD6 : CKD6 {
	void onMouseDown(EventMouseDown &e) override {
		ACNEWidget *parent = dynamic_cast<ACNEWidget*>(this->parent);
		ACNE *module = dynamic_cast<ACNE*>(this->module);
		if (parent && module) {
			parent->UpdateSnapshot(this->paramId - ACNE::SNAPSHOT_PARAMS);
			for (int i = 0 ; i < ACNE_NB_SNAPSHOTS; i++) {
				module->params[ACNE::SNAPSHOT_PARAMS + i].value = ACNE::SNAPSHOT_PARAMS + i == this->paramId ? 1 : 0;
				module->currentSnapshot = this->paramId - ACNE::SNAPSHOT_PARAMS;
			}
		}
		CKD6::onMouseDown(e);
	}
};

struct ACNECOPYPASTECKD6 : CKD6 {
	void onMouseDown(EventMouseDown &e) override {
		ACNEWidget *parent = dynamic_cast<ACNEWidget*>(this->parent);
		ACNE *module = dynamic_cast<ACNE*>(this->module);
		if (parent && module) {
			if (!module->copyState) {
				module->copySnapshot = this->paramId;
				module->copyState = true;
			} else if ((module->copyState) && (module->copySnapshot != module->currentSnapshot)) {
				for (int i = 0; i < ACNE_NB_OUTS; i++) {
					for (int j = 0; j < ACNE_NB_TRACKS; j++) {
							parent->faders[module->currentSnapshot][i][j]->setValue(parent->faders[module->copySnapshot][i][j]->value);
					}
				}
				module->copyState = false;
			}
		}
		CKD6::onMouseDown(e);
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

	static const float portX0[35] = {20,34,48,62,76,90,104,118,132,146,160,174,188,202,216,230,244,258,272,286,300,314,328,342,356,370,384,398,412,426,440,454,468,482,496};
	static const float portY0[10] = {80,111,142,173,204,235,266,297,328,359};

	addParam(createParam<ACNECOPYPASTECKD6>(Vec(portX0[1]-5, 42), module, ACNE::COPY_PARAM, 0, 1, 0.0));
	addChild(createLight<SmallLight<GreenLight>>(Vec(portX0[1]+6, 31), module, ACNE::COPY_LIGHT));

	for (int i = 0 ; i < ACNE_NB_SNAPSHOTS ; i++) {
		addParam(createParam<ACNECKD6>(Vec(portX0[3*i+21]-5, 42), module, ACNE::SNAPSHOT_PARAMS + i, 0.0, 1.0, 0.0));
		addChild(createLight<SmallLight<GreenLight>>(Vec(portX0[3*i+21]+6, 31), module, ACNE::SNAPSHOT_LIGHTS + i));
  }

	for (int i = 0; i < ACNE_NB_OUTS; i++) {
		addOutput(createOutput<PJ301MPort>(Vec(portX0[32] + 4, portY0[i]-3), module, ACNE::TRACKS_OUTPUTS + i));
	}

	for (int i = 0; i < ACNE_NB_TRACKS; i++) {
		addInput(createInput<PJ301MPort>(Vec(portX0[i*2], 325), module, ACNE::TRACKS_INPUTS + i));
	}

	for (int s = 0 ; s < ACNE_NB_SNAPSHOTS; s++ ) {
		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			for (int j = 0; j < ACNE_NB_TRACKS; j++) {
					faders[s][i][j] = createParam<ACNETrimPot>(Vec(portX0[j*2]+3, portY0[i]), module, ACNE::FADERS_PARAMS + j + i * (ACNE_NB_TRACKS) + s * (ACNE_NB_TRACKS * ACNE_NB_OUTS), 0.0, 10, 0);
					addParam(faders[s][i][j]);
			}
		}
	}
	module->currentSnapshot = 0;
	UpdateSnapshot(module->currentSnapshot);
}

void ACNEWidget::UpdateSnapshot(int snapshot) {
	for (int s = 0 ; s < ACNE_NB_SNAPSHOTS; s++ ) {
		for (int i = 0; i < ACNE_NB_OUTS; i++) {
			for (int j = 0; j < ACNE_NB_TRACKS; j++) {
					faders[s][i][j]->visible = (s == snapshot);
			}
		}
	}
}

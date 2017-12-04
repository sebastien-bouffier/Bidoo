#include "rack.hpp"

using namespace rack;

const int ACNE_NB_TRACKS = 16;
const int ACNE_NB_OUTS = 8;
const int ACNE_NB_SNAPSHOTS = 16;

extern Plugin *plugin;

////////////////////
// module widgets
////////////////////

struct DTROYWidget : ModuleWidget {
	ParamWidget *scaleParam;
	DTROYWidget();
	Menu *createContextMenu() override;
};

struct OUAIVEWidget : ModuleWidget {
	OUAIVEWidget();
	Menu *createContextMenu() override;
};

struct CHUTEWidget : ModuleWidget {
	CHUTEWidget();
};

struct VOIDWidget : ModuleWidget {
	VOIDWidget();
};

struct DUKEWidget : ModuleWidget {
	ParamWidget *sliders[4];
	DUKEWidget();
};

struct ACNEWidget : ModuleWidget {
	ParamWidget *faders[ACNE_NB_OUTS][ACNE_NB_TRACKS];
	void UpdateSnapshot(int snapshot);
	void step() override;
	ACNEWidget();
};

struct MOIREWidget : ModuleWidget {
	ParamWidget *controls[16];
	ParamWidget *morphButton;
	void step() override;
	MOIREWidget();
	Menu *createContextMenu() override;
};

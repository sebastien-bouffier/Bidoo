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
	ParamWidget *stepsParam, *scaleParam, *rootNoteParam, *sensitivityParam, *gateTimeParam, *slideTimeParam, *playModeParam, *countModeParam, *patternParam, *pitchParams[8], *pulseParams[8], *typeParams[8], *slideParams[8], *skipParams[8];
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

struct LATEWidget : ModuleWidget {
	LATEWidget();
};

struct FORKWidget : ModuleWidget {
	ParamWidget *F1;
	ParamWidget *F2;
	ParamWidget *F3;
	ParamWidget *F4;
	ParamWidget *A1;
	ParamWidget *A2;
	ParamWidget *A3;
	ParamWidget *A4;
	FORKWidget();
};

struct TIAREWidget : ModuleWidget {
	TIAREWidget();
};

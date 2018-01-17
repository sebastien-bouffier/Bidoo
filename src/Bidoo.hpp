#include "rack.hpp"

using namespace rack;

const int ACNE_NB_TRACKS = 16;
const int ACNE_NB_OUTS = 8;
const int ACNE_NB_SNAPSHOTS = 16;

const NVGcolor BLUE_BIDOO = nvgRGBA(42, 87, 117, 255);
const NVGcolor LIGHTBLUE_BIDOO = nvgRGBA(45, 114, 143, 255);
const NVGcolor RED_BIDOO = nvgRGBA(205, 31, 0, 255);
const NVGcolor YELLOW_BIDOO = nvgRGBA(255, 233, 0, 255);
const NVGcolor SAND_BIDOO = nvgRGBA(230, 220, 191, 255);
const NVGcolor ORANGE_BIDOO = nvgRGBA(228, 87, 46, 255);
const NVGcolor PINK_BIDOO = nvgRGBA(164, 3, 111, 255);
const NVGcolor GREEN_BIDOO = nvgRGBA(2, 195, 154, 255);

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

struct CLACOSWidget : ModuleWidget {
	CLACOSWidget();
};

struct BARWidget : ModuleWidget {
	BARWidget();
};

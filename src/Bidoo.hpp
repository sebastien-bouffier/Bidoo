#include "rack.hpp"

using namespace rack;


extern Plugin *plugin;

////////////////////
// module widgets
////////////////////

struct DTROYWidget : ModuleWidget {
	ParamWidget *scaleParam;
	DTROYWidget();
	//void step() override;
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
	DUKEWidget();
};

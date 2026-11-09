#include "plugin.hpp"
#include "BidooComponents.hpp"

using namespace std;

struct VOID_ : BidooModule {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	VOID_() {	}
};

struct VOIDWidget : BidooWidget {
	VOIDWidget(VOID_ *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/VOID.svg"));

		theme::addScrews(this);   // a blank panel: the screws are its only widgets
	}
};

Model *modelVOID = createModel<VOID_, VOIDWidget>("VOID");

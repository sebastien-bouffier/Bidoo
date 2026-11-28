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

		// The panel of tools/panels/void.py: the old drawing, the title and the logo. The screws are its only widgets
		theme::addScrews(this);
	}
};

Model *modelVOID = createModel<VOID_, VOIDWidget>("VOID");

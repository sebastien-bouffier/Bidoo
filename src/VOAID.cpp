#include "plugin.hpp"
#include "BidooComponents.hpp"

using namespace std;

struct VOAID : BidooModule {
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

	VOAID() {	}
};

struct VOAIDWidget : BidooWidget {
	VOAIDWidget(VOAID *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/VOID.svg"));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));
	}
};

Model *modelVOAID = createModel<VOAID, VOAIDWidget>("VOAID");

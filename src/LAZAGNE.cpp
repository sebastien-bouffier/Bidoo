#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/samplerate.hpp"

using namespace std;

struct LAZAGNE : Module {
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


	LAZAGNE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {

	}

	void step() override;
};


void LAZAGNE::step() {

}

struct LAZAGNEWidget : ModuleWidget {
	LAZAGNEWidget(LAZAGNE *module) : ModuleWidget(module) {
		setPanel(SVG::load(assetPlugin(plugin, "res/LAZAGNE.svg")));

  	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
  	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

  }
};


Model *modelLAZAGNE= Model::create<LAZAGNE, LAZAGNEWidget>("Bidoo", "laZagne", "laZagne gate", LOGIC_TAG);

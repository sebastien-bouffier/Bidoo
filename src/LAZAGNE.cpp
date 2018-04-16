#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/samplerate.hpp"
#include "dsp/ringbuffer.hpp"
#include "dsp/frame.hpp"

using namespace std;

struct LAZAGNE : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};
	DoubleRingBuffer<float,128> dataRingBuffer;

	LAZAGNE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {

	}

	void step() override;
};


void LAZAGNE::step() {
	dataRingBuffer.push(inputs[INPUT].value);
	if (dataRingBuffer.full()) {
		dataRingBuffer.clear();
	}
}

struct LAZAGNEDisplay : TransparentWidget {
	LAZAGNE *module;
	std::shared_ptr<Font> font;
	LAZAGNEDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void draw(NVGcontext *vg) override {


	}
};

struct LAZAGNEWidget : ModuleWidget {
	LAZAGNEWidget(LAZAGNE *module) : ModuleWidget(module) {
		setPanel(SVG::load(assetPlugin(plugin, "res/LAZAGNE.svg")));

		LAZAGNEDisplay *display = new LAZAGNEDisplay();
		display->module = module;
		display->box.pos = Vec(12.0f, 40.0f);
		display->box.size = Vec(110.0f, 70.0f);
		addChild(display);

		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
  	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(Port::create<PJ301MPort>(Vec(25, 322), Port::INPUT, module, LAZAGNE::INPUT));

  }
};


Model *modelLAZAGNE= Model::create<LAZAGNE, LAZAGNEWidget>("Bidoo", "laZagne", "laZagne gate", LOGIC_TAG);

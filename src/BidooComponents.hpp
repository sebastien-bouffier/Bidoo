#pragma once
#include "components.hpp"
#include <vector>
#include <jansson.h>
#include "widgets.hpp"
#include <iostream>

using namespace std;
//#include "../../../ext/nanosvg/src/nanosvg.h"

namespace rack {

struct BidooColoredKnob : RoundKnob {
	BidooColoredKnob() {
		setSVG(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/BlackKnobBidoo.svg")));
		box.size = Vec(28, 28);
	}

	void draw(NVGcontext *vg) override {
		for (NSVGshape *shape = this->sw->svg->handle->shapes; shape != NULL; shape = shape->next) {
			std::string str(shape->id);
			if (str == "bidooKnob") {
				shape->fill.color = (((unsigned int)value*25) | ((unsigned int)0 << 8) | ((unsigned int)value*10 << 16));
				shape->fill.color |= (unsigned int)(255) << 24;
			}
		}
		RoundKnob::draw(vg);
	};
};

//path38955

struct BidooSlidePotLong : SVGSlider {
	BidooSlidePotLong() {
		snap = true;
		maxHandlePos = Vec(0, 0);
		minHandlePos = Vec(0, 84);
		background->svg = SVG::load(assetPlugin(plugin,"res/ComponentLibrary/bidooSlidePotLong.svg"));
		background->wrap();
		background->box.pos = Vec(0, 0);
		box.size = background->box.size;
		handle->svg = SVG::load(assetPlugin(plugin,"res/ComponentLibrary/bidooSlidePotHandle.svg"));
		handle->wrap();
	}
};

struct BidooSlidePotShort : SVGSlider {
	BidooSlidePotShort() {
		snap = true;
		maxHandlePos = Vec(0, 0);
		minHandlePos = Vec(0, 60);
		background->svg = SVG::load(assetPlugin(plugin,"res/ComponentLibrary/bidooSlidePotShort.svg"));
		background->wrap();
		background->box.pos = Vec(0, 0);
		box.size = background->box.size;
		handle->svg = SVG::load(assetPlugin(plugin,"res/ComponentLibrary/bidooSlidePotHandle.svg"));
		handle->wrap();
	}
};

struct BidooLongSlider : SVGSlider {
	BidooLongSlider() {
		maxHandlePos = Vec(0, 0);
		minHandlePos = Vec(0, 84);
		background->svg = SVG::load(assetPlugin(plugin,"res/ComponentLibrary/bidooLongSlider.svg"));
		background->wrap();
		background->box.pos = Vec(0, 0);
		box.size = background->box.size;
		handle->svg = SVG::load(assetPlugin(plugin,"res/ComponentLibrary/bidooLongSliderHandle.svg"));
		handle->wrap();
	}
};


struct CKSS8 : SVGSwitch, ToggleSwitch {
	CKSS8() {
		addFrame(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/CKSS8_0.svg")));
		addFrame(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/CKSS8_1.svg")));
		addFrame(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/CKSS8_2.svg")));
		addFrame(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/CKSS8_3.svg")));
		addFrame(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/CKSS8_4.svg")));
		addFrame(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/CKSS8_5.svg")));
		addFrame(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/CKSS8_6.svg")));
		addFrame(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/CKSS8_7.svg")));
		sw->wrap();
		box.size = sw->box.size;
	}
};

struct CKSS4 : SVGSwitch, ToggleSwitch {
	CKSS4() {
		addFrame(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/CKSS4_0.svg")));
		addFrame(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/CKSS4_1.svg")));
		addFrame(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/CKSS4_2.svg")));
		addFrame(SVG::load(assetPlugin(plugin,"res/ComponentLibrary/CKSS4_3.svg")));
		sw->wrap();
		box.size = sw->box.size;
	}
};


} // namespace rack

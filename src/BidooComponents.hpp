#pragma once
#include "components.hpp"

namespace rack {


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

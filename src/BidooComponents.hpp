#pragma once
#include "componentlibrary.hpp"
#include "random.hpp"
#include <vector>
#include <jansson.h>
#include "widget/Widget.hpp"
#include <iostream>

using namespace std;

namespace rack {

struct BidooBlueKnob : RoundKnob {
	BidooBlueKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueKnobBidoo.svg")));
	}
};

struct BidooGreenKnob : RoundKnob {
	BidooGreenKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/GreenKnobBidoo.svg")));
	}
};

struct BidooRedKnob : RoundKnob {
	BidooRedKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/RedKnobBidoo.svg")));
	}
};

struct BidooHugeBlueKnob : RoundKnob {
	BidooHugeBlueKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/HugeBlueKnobBidoo.svg")));
	}
};

struct BidooLargeBlueKnob : RoundKnob {
	BidooLargeBlueKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/LargeBlueKnobBidoo.svg")));
	}
};

struct BidooSmallBlueKnob : RoundKnob {
	BidooSmallBlueKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SmallBlueKnobBidoo.svg")));
	}
};

struct BidooBlueSnapKnob : RoundBlackSnapKnob  {
	BidooBlueSnapKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueKnobBidoo.svg")));
	}
};

struct BidooBlueSnapTrimpot : Trimpot  {
	BidooBlueSnapTrimpot() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueTrimpotBidoo.svg")));
		snap = true;
		smooth = false;
	}
};

struct BidooBlueTrimpot : Trimpot {
	BidooBlueTrimpot() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueTrimpotBidoo.svg")));
	}
};

struct BlueCKD6 : SvgSwitch {
	BlueCKD6() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueCKD6_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueCKD6_1.svg")));
	}
};

struct BlueBtn : SvgSwitch {
	std::string caption;
	shared_ptr<Font> font;

	BlueBtn() {
		momentary = true;
		font = APP->window->loadFont(asset::plugin(pluginInstance,"res/DejaVuSansMono.ttf"));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueBtn_1.svg")));
	}

	void draw(const DrawArgs &args) override {
		SvgSwitch::draw(args);
		nvgFontSize(args.vg, 12.0f);
		nvgFontFaceId(args.vg, font->handle);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
		nvgText(args.vg, 8.0f, 12.0f, (caption).c_str(), NULL);
	}
};

struct RedBtn : app::SvgSwitch {
	std::string caption;
	shared_ptr<Font> font;

	RedBtn() {
		momentary = true;
		font = APP->window->loadFont(asset::plugin(pluginInstance,"res/DejaVuSansMono.ttf"));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/RedBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/RedBtn_1.svg")));
	}

	void draw(const DrawArgs &args) override {
		SvgSwitch::draw(args);
		nvgFontSize(args.vg, 12.0f);
		nvgFontFaceId(args.vg, font->handle);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
		nvgText(args.vg, 8.0f, 12.0f, (caption).c_str(), NULL);
		//nvgStroke(args.vg);
	}
};

struct MuteBtn : app::SvgSwitch {
	MuteBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/MuteBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/MuteBtn_1.svg")));
	}
};

struct SoloBtn : app::SvgSwitch {
	SoloBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SoloBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SoloBtn_1.svg")));
	}
};

struct LeftBtn : app::SvgSwitch {
	LeftBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/LeftBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/LeftBtn_1.svg")));
	}
};

struct RightBtn : app::SvgSwitch {
	RightBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/RightBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/RightBtn_1.svg")));
	}
};

struct UpBtn : app::SvgSwitch {
	UpBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/UpBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/UpBtn_1.svg")));
	}
};

struct DownBtn : app::SvgSwitch {
	DownBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/DownBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/DownBtn_1.svg")));
	}
};

struct BidooColoredKnob : RoundKnob {
	BidooColoredKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlackKnobBidoo.svg")));
	}

	void draw(const DrawArgs &args) override {
		if (paramQuantity) {
			for (NSVGshape *shape = this->sw->svg->handle->shapes; shape != NULL; shape = shape->next) {
				std::string str(shape->id);
				if (str == "bidooKnob") {
					shape->fill.color = (((unsigned int)42+(unsigned int)paramQuantity->getValue()*21) | (((unsigned int)87-(unsigned int)paramQuantity->getValue()*8) << 8) | (((unsigned int)117-(unsigned int)paramQuantity->getValue()) << 16));
					shape->fill.color |= (unsigned int)(255) << 24;
				}
			}
		}
		RoundKnob::draw(args);
	}
};

struct BidooMorphKnob : RoundKnob {
	BidooMorphKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SpiralKnobBidoo.svg")));
	}
};

struct BidooColoredTrimpot : RoundKnob {
	BidooColoredTrimpot() {
		minAngle = -0.75f*M_PI;
		maxAngle = 0.75f*M_PI;
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/TrimpotBidoo.svg")));
	}

	void draw(const DrawArgs &args) override {
		for (NSVGshape *shape = this->sw->svg->handle->shapes; shape != NULL; shape = shape->next) {
			std::string str(shape->id);
			if (str == "bidooTrimPot") {
				if ((paramQuantity == NULL) || (paramQuantity->getValue() == 0.0f)) {
					shape->fill.color = (((unsigned int)128) | ((unsigned int)128 << 8) | ((unsigned int)128 << 16));
					shape->fill.color |= (unsigned int)(120) << 24;
				} else {
					shape->fill.color = (((unsigned int)255) | (((unsigned int)(205 - (paramQuantity->getValue() * 15.0f))) << 8) | ((unsigned int)10 << 16));
					shape->fill.color |= ((unsigned int)255) << 24;
				}
			}
		}
		RoundKnob::draw(args);
	}
};

struct BidooSlidePotLong : SvgSlider {
	BidooSlidePotLong() {
		snap = true;
		maxHandlePos = Vec(0.0f, 0.0f);
		minHandlePos = Vec(0.0f, 84.0f);
		background->svg = APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/bidooSlidePotLong.svg"));
		background->wrap();
		background->box.pos = Vec(0.0f, 0.0f);
		box.size = background->box.size;
		handle->svg = APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/bidooSlidePotHandle.svg"));
		handle->wrap();
	}

	void randomize() override {
		random::init();
  	paramQuantity->setValue(roundf(rescale(random::uniform(), 0.0f, 1.0f, paramQuantity->getMinValue(), paramQuantity->getMaxValue())));
  }
};

struct BidooSlidePotShort : SvgSlider {
	BidooSlidePotShort() {
		snap = true;
		maxHandlePos = Vec(0.0f, 0.0f);
		minHandlePos = Vec(0.0f, 60.0f);
		background->svg = APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/bidooSlidePotShort.svg"));
		background->wrap();
		background->box.pos = Vec(0.0f, 0.0f);
		box.size = background->box.size;
		handle->svg = APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/bidooSlidePotHandle.svg"));
		handle->wrap();
	}

	void randomize() override {
		random::init();
  	paramQuantity->setValue(roundf(rescale(random::uniform(), 0.0f, 1.0f, paramQuantity->getMinValue(), paramQuantity->getMaxValue())));
  }
};

struct BidooLongSlider : SvgSlider {
	BidooLongSlider() {
		maxHandlePos = Vec(0.0f, 0.0f);
		minHandlePos = Vec(0.0f, 84.0f);
		background->svg = APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/bidooLongSlider.svg"));
		background->wrap();
		background->box.pos = Vec(0.0f, 0.0f);
		box.size = background->box.size;
		handle->svg = APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/bidooLongSliderHandle.svg"));
		handle->wrap();
	}
};

struct CKSS8 : SvgSwitch {
	CKSS8() {
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS8_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS8_1.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS8_2.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS8_3.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS8_4.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS8_5.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS8_6.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS8_7.svg")));
		sw->wrap();
		box.size = sw->box.size;
	}
};

struct CKSS4 : SvgSwitch {
	CKSS4() {
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS4_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS4_1.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS4_2.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS4_3.svg")));
		sw->wrap();
		box.size = sw->box.size;
	}
};

struct TinyPJ301MPort : SvgPort {
	TinyPJ301MPort() {
		// background->svg = APP->window->loadSvg(asset::system("res/ComponentLibrary/TinyPJ301M.svg"));
		// background->wrap();
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/TinyPJ301M.svg")));
		sw->wrap();
		box.size = sw->box.size;
	}
};

struct MiniLEDButton : SvgSwitch {
	MiniLEDButton() {
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/miniLEDButton.svg")));
		sw->wrap();
		box.size = sw->box.size;
	}
};

} // namespace rack

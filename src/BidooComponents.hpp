#pragma once
#include "componentlibrary.hpp"
#include "random.hpp"
#include <vector>
#include <jansson.h>
#include "widget/Widget.hpp"
#include <iostream>

using namespace std;

namespace rack {


struct BidooRoundBlackSnapKnob : RoundBlackSnapKnob {
	BidooRoundBlackSnapKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlackKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlackKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooRoundBlackKnob : RoundBlackKnob {
	BidooRoundBlackKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlackKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlackKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooBlueKnob : RoundKnob {
	BidooBlueKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooGreenKnob : RoundKnob {
	BidooGreenKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/GreenKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/GreenKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooRedKnob : RoundKnob {
	BidooRedKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/RedKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/RedKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooHugeRedKnob : RoundKnob {
	BidooHugeRedKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/HugeRedKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/HugeRedKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooHugeBlueKnob : RoundKnob {
	BidooHugeBlueKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/HugeBlueKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/HugeBlueKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooLargeBlueKnob : RoundKnob {
	BidooLargeBlueKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/LargeBlueKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/LargeBlueKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooSmallBlueKnob : RoundKnob {
	BidooSmallBlueKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/SmallBlueKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/SmallBlueKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooSmallSnapBlueKnob : RoundBlackSnapKnob {
	BidooSmallSnapBlueKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/SmallBlueKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/SmallBlueKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooBlueSnapKnob : RoundBlackSnapKnob  {
	BidooBlueSnapKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooBlueSnapTrimpot : Trimpot  {
	BidooBlueSnapTrimpot() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueTrimpotBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueTrimpotBidoo-bg.svg")));
		shadow->opacity = 0.0f;
		snap = true;
		smooth = false;
	}
};

struct BidooBlueTrimpot : Trimpot {
	BidooBlueTrimpot() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueTrimpotBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueTrimpotBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BlueCKD6 : app::SvgSwitch {
	BlueCKD6() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueCKD6_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueCKD6_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BlueBtn : app::SvgSwitch {
	std::string caption;

	BlueBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueBtn_1.svg")));
		shadow->opacity = 0.0f;
	}

	void draw(const DrawArgs &args) override {
		SvgSwitch::draw(args);
		nvgFontSize(args.vg, 12.0f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
		nvgText(args.vg, 8.0f, 12.0f, (caption).c_str(), NULL);
	}
};

struct RedBtn : app::SvgSwitch {
	std::string caption;

	RedBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/RedBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/RedBtn_1.svg")));
		shadow->opacity = 0.0f;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			nvgFontSize(args.vg, 12.0f);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgText(args.vg, 8.0f, 12.0f, (caption).c_str(), NULL);
		}
		Widget::drawLayer(args, layer);
	}

};

struct SaveBtn : app::SvgSwitch {
	SaveBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SaveBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SaveBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct Rnd2Btn : app::SvgSwitch {
	Rnd2Btn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/Rnd2Btn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/Rnd2Btn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct MuteBtn : app::SvgSwitch {
	MuteBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/MuteBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/MuteBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct SoloBtn : app::SvgSwitch {
	SoloBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SoloBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SoloBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct RndBtn : app::SvgSwitch {
	RndBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/RndBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/RndBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct LeftBtn : app::SvgSwitch {
	LeftBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/LeftBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/LeftBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct RightBtn : app::SvgSwitch {
	RightBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/RightBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/RightBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct UpBtn : app::SvgSwitch {
	UpBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/UpBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/UpBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct DownBtn : app::SvgSwitch {
	DownBtn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/DownBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/DownBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooColoredKnob : RoundKnob {
	BidooColoredKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/ColoredKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/ColoredKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (getParamQuantity()) {
				for (NSVGshape *shape = this->sw->svg->handle->shapes; shape != NULL; shape = shape->next) {
					std::string str(shape->id);
					if (str == "bidooKnob") {
						shape->fill.color = (((unsigned int)42+(unsigned int)getParamQuantity()->getValue()*21) | (((unsigned int)87-(unsigned int)getParamQuantity()->getValue()*8) << 8) | (((unsigned int)117-(unsigned int)getParamQuantity()->getValue()) << 16));
						shape->fill.color |= (unsigned int)(255) << 24;
					}
				}
			}
		}
		Widget::drawLayer(args, layer);
	}

};

struct BidooMorphKnob : RoundKnob {
	BidooMorphKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/SpiralKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/SpiralKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooColoredTrimpot : RoundKnob {
	NSVGshape *tShape = NULL;
	NSVGshape *tShapeInterior = NULL;

	BidooColoredTrimpot() {
		minAngle = -0.75f*M_PI;
		maxAngle = 0.75f*M_PI;
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/TrimpotBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/TrimpotBidoo-bg.svg")));
		shadow->opacity = 0.0f;

		if (bg->svg && bg->svg->handle && bg->svg->handle->shapes) {
			for (NSVGshape *shape = bg->svg->handle->shapes; shape != NULL; shape = shape->next) {
				std::string str(shape->id);
				if (str == "bidooKnob") {
					tShape = shape;
				}
				if (str == "bidooInterior") {
					tShapeInterior = shape;
				}
			}
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (getParamQuantity() && tShape) {
				tShape->fill.color = int(42+getParamQuantity()->getValue() * 210) | int(87-getParamQuantity()->getValue() * 80) << 8 | int(117-getParamQuantity()->getValue()*10) << 16;
				tShape->fill.color |= 255 << 24;
			}
			if (getParamQuantity() && tShapeInterior) {
				tShapeInterior->fill.color = int(42+getParamQuantity()->getValue() * 210) | int(87-getParamQuantity()->getValue() * 80) << 8 | int(117-getParamQuantity()->getValue()*10) << 16;
				tShapeInterior->fill.color |= 255 << 24;
			}
		}
		Widget::drawLayer(args, layer);
	}

};

struct BidooSlidePotLong : app::SvgSlider {
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
};

struct BidooSlidePotShort : app::SvgSlider {
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
};

struct CKSS8 : app::SvgSwitch {
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
		shadow->opacity = 0.0f;
	}
};

struct CKSS4 : app::SvgSwitch {
	CKSS4() {
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS4_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS4_1.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS4_2.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/CKSS4_3.svg")));
		sw->wrap();
		box.size = sw->box.size;
		shadow->opacity = 0.0f;
	}
};

struct TinyPJ301MPort : app::SvgPort {
	TinyPJ301MPort() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/TinyPJ301M.svg")));
		sw->wrap();
		box.size = sw->box.size;
	}
};

struct MiniLEDButton : app::SvgSwitch {
	MiniLEDButton() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/miniLEDButton.svg")));
	}
};

struct BidooLEDButton : LEDButton {
	BidooLEDButton() {

	}
};

template <typename TBase>
struct SmallLEDBezelLight : TBase {
	SmallLEDBezelLight() {
		this->borderColor = color::BLACK_TRANSPARENT;
		this->bgColor = color::BLACK_TRANSPARENT;
		this->box.size = math::Vec(8, 8);
	}
};

struct SmallLEDBezel : app::SvgSwitch {
	SmallLEDBezel() {
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/SmallLEDBezel.svg")));
	}
};

template <typename TLightBase = WhiteLight>
struct SmallLEDLightBezel : SmallLEDBezel {
	app::ModuleLightWidget* light;

	SmallLEDLightBezel() {
		light = new SmallLEDBezelLight<TLightBase>;
		// Move center of light to center of box
		light->box.pos = box.size.div(2).minus(light->box.size.div(2));
		addChild(light);
	}
};

} // namespace rack

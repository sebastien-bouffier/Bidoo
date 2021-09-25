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
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlackKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooRoundBlackKnob : RoundBlackKnob {
	BidooRoundBlackKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlackKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooBlueKnob : RoundKnob {
	BidooBlueKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooGreenKnob : RoundKnob {
	BidooGreenKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/GreenKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooRedKnob : RoundKnob {
	BidooRedKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/RedKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooHugeRedKnob : RoundKnob {
	BidooHugeRedKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/HugeRedKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooHugeBlueKnob : RoundKnob {
	BidooHugeBlueKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/HugeBlueKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooLargeBlueKnob : RoundKnob {
	BidooLargeBlueKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/LargeBlueKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooSmallBlueKnob : RoundKnob {
	BidooSmallBlueKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SmallBlueKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooSmallSnapBlueKnob : RoundBlackSnapKnob {
	BidooSmallSnapBlueKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SmallBlueKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooBlueSnapKnob : RoundBlackSnapKnob  {
	BidooBlueSnapKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooBlueSnapTrimpot : Trimpot  {
	BidooBlueSnapTrimpot() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueTrimpotBidoo.svg")));
		snap = true;
		smooth = false;
		shadow->opacity = 0.0f;
	}
};

struct BidooBlueTrimpot : Trimpot {
	BidooBlueTrimpot() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueTrimpotBidoo.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BlueCKD6 : app::SvgSwitch {
	BlueCKD6() {
		momentary = true;
		momentaryPressed = true;
		momentaryReleased = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueCKD6_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueCKD6_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BlueBtn : app::SvgSwitch {
	std::string caption;
	shared_ptr<Font> font;

	BlueBtn() {
		momentary = true;
		font = APP->window->loadFont(asset::plugin(pluginInstance,"res/DejaVuSansMono.ttf"));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueBtn_1.svg")));
		shadow->opacity = 0.0f;
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
		shadow->opacity = 0.0f;
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
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/ColoredKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}

	void step() override {
		if (paramQuantity) {
			for (NSVGshape *shape = this->sw->svg->handle->shapes; shape != NULL; shape = shape->next) {
				std::string str(shape->id);
				if (str == "bidooKnob") {
					shape->fill.color = (((unsigned int)42+(unsigned int)paramQuantity->getValue()*21) | (((unsigned int)87-(unsigned int)paramQuantity->getValue()*8) << 8) | (((unsigned int)117-(unsigned int)paramQuantity->getValue()) << 16));
					shape->fill.color |= (unsigned int)(255) << 24;
				}
			}
		}
		RoundKnob::step();
	}
};

struct BidooLargeColoredKnob : RoundKnob {
	bool *blink=NULL;
	int frame=0;
	unsigned int tFade=255;

	BidooLargeColoredKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/ColoredLargeKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}

	void step() override {
		if (paramQuantity) {
			for (NSVGshape *shape = this->sw->svg->handle->shapes; shape != NULL; shape = shape->next) {
				std::string str(shape->id);
				if (str == "bidooKnob") {
					shape->fill.color = (((unsigned int)42+(unsigned int)(paramQuantity->getValue()*210)) | (((unsigned int)87-(unsigned int)(paramQuantity->getValue()*80)) << 8) | (((unsigned int)117-(unsigned int)(paramQuantity->getValue()*10)) << 16));
					if (!*blink) {
						tFade = 255;
					}
					else {
						if (++frame <= 30) {
							tFade -= frame*3;
						}
						else if (++frame<60) {
							tFade = 255;
						}
						else {
							tFade = 255;
							frame = 0;
						}
					}
					shape->fill.color |= (unsigned int)(tFade) << 24;
				}
			}
		}
		RoundKnob::step();
	}
};



struct BidooMorphKnob : RoundKnob {
	BidooMorphKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SpiralKnobBidoo.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooColoredTrimpot : RoundKnob {
	NSVGshape *tShape = NULL;

	BidooColoredTrimpot() {
		minAngle = -0.75f*M_PI;
		maxAngle = 0.75f*M_PI;
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/TrimpotBidoo.svg")));
		if (this->sw && this->sw->svg && this->sw->svg->handle && this->sw->svg->handle->shapes) {
			for (NSVGshape *shape = this->sw->svg->handle->shapes; shape != NULL; shape = shape->next) {
				std::string str(shape->id);
				if (str == "bidooTrimPot") {
					tShape = shape;
				}
			}
		}
		shadow->opacity = 0.0f;
	}

	void step() override {
		if (paramQuantity && tShape) {
			tShape->fill.color = int(42+paramQuantity->getValue() * 210) | int(87-paramQuantity->getValue() * 80) << 8 | int(117-paramQuantity->getValue()*10) << 16;
			tShape->fill.color |= 255 << 24;
		}
		RoundKnob::step();
	}
};

struct BidooziNCColoredKnob : RoundKnob {
	float *coeff = NULL;
	float corrCoef = 0;
	NSVGshape *tShape = NULL;

	BidooziNCColoredKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueKnobBidoo.svg")));
		box.size = Vec(28, 28);
		if (this->sw && this->sw->svg && this->sw->svg->handle && this->sw->svg->handle->shapes) {
			for (NSVGshape *shape = this->sw->svg->handle->shapes; shape != NULL; shape = shape->next) {
				std::string str(shape->id);
				if (str == "bidooBlueKnob") {
					tShape = shape;
				}
			}
		}
		shadow->opacity = 0.0f;
	}

	void step() override {
		if (paramQuantity) {
			corrCoef = rescale(clamp(*coeff,0.f,2.f),0.f,2.f,0.f,255.f);
		}

		if (tShape) {
			tShape->fill.color = (((unsigned int)clamp(42.f+corrCoef,0.f,255.f)) | ((unsigned int)clamp(87.f-corrCoef,0.f,255.f) << 8) | ((unsigned int)clamp(117.f-corrCoef,0.f,255.f) << 16));
			tShape->fill.color |= (unsigned int)(255) << 24;
		}
		RoundKnob::step();
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

	void randomize() override {
		random::init();
  	paramQuantity->setValue(roundf(rescale(random::uniform(), 0.0f, 1.0f, paramQuantity->getMinValue(), paramQuantity->getMaxValue())));
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

	void randomize() override {
		random::init();
  	paramQuantity->setValue(roundf(rescale(random::uniform(), 0.0f, 1.0f, paramQuantity->getMinValue(), paramQuantity->getMaxValue())));
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
		// background->svg = APP->window->loadSvg(asset::system("res/ComponentLibrary/TinyPJ301M.svg"));
		// background->wrap();
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/TinyPJ301M.svg")));
		sw->wrap();
		box.size = sw->box.size;
		shadow->opacity = 0.0f;
	}
};

struct MiniLEDButton : app::SvgSwitch {
	MiniLEDButton() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/miniLEDButton.svg")));
		shadow->opacity = 0.0f;
	}
};

struct BidooLEDButton : LEDButton {
	BidooLEDButton() {
		shadow->opacity = 0.0f;
	}
};

} // namespace rack

#include "plugin.hpp"
#include "dsp/resampler.hpp"
#include "dsp/filter.hpp"
#include "dep/pffft/pffft.h"
#include "dep/filters/fftanalysis.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <limits>
#include "dsp/ringbuffer.hpp"
#include <mutex>

using namespace std;

const int N = 4096;

struct GARCON : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		// OUT_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	FfftAnalysis *processor;
	vector<vector<float>> fft;
	dsp::DoubleRingBuffer<float,N> in_Buffer;
	std::mutex mylock;

	GARCON() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		processor = new FfftAnalysis(N, 4, APP->engine->getSampleRate());
	}

	~GARCON() {
		delete processor;
	}

	void process(const ProcessArgs &args) override;
};

void GARCON::process(const ProcessArgs &args) {
	in_Buffer.push(inputs[INPUT].getVoltage()/10.0f);
	if (in_Buffer.full()) {
		processor->process(in_Buffer.startData(), &fft, &mylock);
		in_Buffer.clear();
	}
}

struct GARCONDisplay : OpaqueWidget {
	GARCON *module;
	shared_ptr<Font> font;
	const float width = 130.0f;
	const float height = 256.0f;
	float threshold = 5.0f;

	GARCONDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	NVGcolor getColor(float f) {
		if (f <= threshold)
			return nvgRGBA(0, 0, (int)(f * 255 / threshold), 255);
		else if (f <= (2 * threshold) )
			return nvgRGBA(0, (int)((f - threshold) * 255 / threshold), 255, 255);
		else
			return nvgRGBA((int)((f - 2 * threshold) * 255 / threshold), 255, 255, 255);
	}

	void draw(const DrawArgs &args) override {
		if (module) {
			module->mylock.lock();
			vector<vector<float>> tmp(module->fft);
			module->mylock.unlock();

			if (tmp.size()>0) {
				for (size_t i = 0; i < width; i++) {
					if (i < tmp.size()) {
						if (tmp[i].size()>0) {
							float iHeith =  1.0f / height;
							for (size_t j = 0; j < height; j++) {
								nvgBeginPath(args.vg);
								float index = (height - j) * iHeith * (height - j) * iHeith * tmp[i].size();
								nvgStrokeColor(args.vg, getColor(interpolateLinear(&tmp[i][0], index)));
								nvgMoveTo(args.vg, i, j);
								nvgLineTo(args.vg, i, j + 1);
								nvgLineCap(args.vg, NVG_MITER);
								nvgClosePath(args.vg);
								nvgStrokeWidth(args.vg, 1);
								nvgStroke(args.vg);
							}
						}
					}
				}
			}
		}
	}
};


struct GARCONWidget : ModuleWidget {
	GARCONWidget(GARCON *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/GARCON.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

  	{
  		GARCONDisplay *display = new GARCONDisplay();
  		display->module = module;
  		display->box.pos = Vec(10, 28);
  		display->box.size = Vec(130, 256);
  		addChild(display);
  	}

  	addInput(createInput<PJ301MPort>(Vec(11, 330), module, GARCON::INPUT));
  }
};

Model *modelGARCON = createModel<GARCON, GARCONWidget>("Garcon");

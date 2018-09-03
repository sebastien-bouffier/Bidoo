#include "Bidoo.hpp"
#include "dsp/resampler.hpp"
#include "dsp/filter.hpp"
#include "dep/pffft/pffft.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <limits>

using namespace std;

const int N = 512;

struct GARCON : Module {
	enum ParamIds {
		// FREQ_PARAM,
		// FREQSPREAD_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		L_INPUT,
		// R_INPUT,
		// PITCH_INPUT,
		// FREQSPREAD_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		// OUT_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	float buff0[N],buff1[N],buff2[N],buff3[N];
	vector<vector<float>> fft;
	size_t inc0 = 0;
	size_t inc1 = 0;
	size_t inc2 = 0;
	size_t inc3 = 0;
	bool start1 = false;
	bool start2 = false;
	bool start3 = false;
	std::mutex mylock;

	GARCON() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
	}

	void step() override;

	void calculateFFT(float *buffer, vector<vector<float>> *result) {
		float *in,*out;
		out = (float*)pffft_aligned_malloc(2 * N * sizeof(float));
		in = (float*)pffft_aligned_malloc(2 * N * sizeof(float));
		memset(in, 0, 2 * N * sizeof(*in));
		memcpy(in, buffer, N);
		PFFFT_Setup *s = pffft_new_setup(N, PFFFT_COMPLEX);
		pffft_transform_ordered(s, in, out, 0, PFFFT_FORWARD);
		vector<float> tmp;
		for (size_t i = 0; i < N/2; i++) {
			tmp.push_back(sqrt(pow(out[2*i], 2) + pow(out[(2*i)+1], 2)));
		}
		pffft_destroy_setup(s);
		pffft_aligned_free(out);
		pffft_aligned_free(in);
		mylock.lock();
		if (result->size() == 0) {
			result->push_back(tmp);
		}
		else if (result->size()>= N) {
			std::rotate(result->rbegin(), result->rbegin() + 1, result->rend());
			vector<vector<float>>& resultRef = *result;
			resultRef[0] = tmp;
		}
		else {
			result->push_back(tmp);
			std::rotate(result->rbegin(), result->rbegin() + 1, result->rend());
		}
		mylock.unlock();
	}
};

void GARCON::step() {
	if (inc0 < N) {
		buff0[inc0] = inputs[L_INPUT].active ? inputs[L_INPUT].value * 100 * 0.5f * (1 - cos(2*3.14159265359f*inc0/(N-1))) : 0.0f;
		inc0++;
	}
	else
	{
		calculateFFT(buff0, &fft);
		inc0 = 0;
	}

	if (!start1 && (inc0 >= (N/4 - 1))) {
		start1 = true;
	}

	if (start1) {
		buff1[inc1] = inputs[L_INPUT].active ? inputs[L_INPUT].value * 100 * 0.5f * (1 - cos(2*3.14159265359f*inc1/(N-1))) : 0.0f;
		inc1++;
	}

	if (inc1 == N) {
		calculateFFT(buff1, &fft);
		inc1 = 0;
	}

	if (!start2 && (inc0 >= (N/2 - 1))) {
		start2 = true;
	}

	if (start2) {
		buff2[inc2] = inputs[L_INPUT].active ? inputs[L_INPUT].value * 100 * 0.5f * (1 - cos(2*3.14159265359f*inc2/(N-1))) : 0.0f;
		inc2++;
	}

	if (inc2 == N) {
		calculateFFT(buff2, &fft);
		inc2 = 0;
	}

	if (!start3 && (inc0 >= (3*N/4 - 1))) {
		start3 = true;
	}

	if (start3) {
		buff3[inc3] = inputs[L_INPUT].active ? inputs[L_INPUT].value * 100 * 0.5f * (1 - cos(2*3.14159265359f*inc3/(N-1))) : 0.0f;
		inc3++;
	}

	if (inc3 == N) {
		calculateFFT(buff3, &fft);
		inc3 = 0;
	}
}

struct GARCONDisplay : OpaqueWidget {
	GARCON *module;
	shared_ptr<Font> font;
	const float width = 130.0f;
	const float height = 256.0f;
	float threshold = 300.0f;

	GARCONDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	NVGcolor getColor(float f) {
		if (f <= threshold)
			return nvgRGBA(0, 0, (int)(f * 255 / threshold), 255);
		else if (f <= (2 * threshold) )
			return nvgRGBA(0, (int)((f - threshold) * 255 / threshold), 255, 255);
		else
			return nvgRGBA((int)((f - 2 * threshold) * 255 / threshold), 255, 255, 255);
	}

	void draw(NVGcontext *vg) override {
			module->mylock.lock();
			vector<vector<float>> tmp(module->fft);
			module->mylock.unlock();

			if (tmp.size()>0) {
				for (size_t i = 0; i < tmp.size(); i++) {
					float x, y;
					x = (float)i * width / tmp.size();
					if (tmp[i].size()>0) {
						for (size_t j = 0; j < tmp[i].size(); j++) {
							y = height - float(j) * height / tmp[i].size();
							nvgBeginPath(vg);
							nvgStrokeColor(vg, getColor(tmp[i][j]));
							nvgMoveTo(vg, x, y);
							nvgLineTo(vg, x, y + 1);
							nvgLineCap(vg, NVG_MITER);
							nvgClosePath(vg);
							nvgStrokeWidth(vg, 1);
							nvgStroke(vg);
						}
					}
				}
			}
		}

};

struct GARCONWidget : ModuleWidget {
	GARCONWidget(GARCON *module);
};

GARCONWidget::GARCONWidget(GARCON *module) : ModuleWidget(module) {
	setPanel(SVG::load(assetPlugin(plugin, "res/GARCON.svg")));

	addChild(Widget::create<ScrewSilver>(Vec(15, 0)));
	addChild(Widget::create<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(Widget::create<ScrewSilver>(Vec(15, 365)));
	addChild(Widget::create<ScrewSilver>(Vec(box.size.x-30, 365)));

	{
		GARCONDisplay *display = new GARCONDisplay();
		display->module = module;
		display->box.pos = Vec(10, 28);
		display->box.size = Vec(130, 256);
		addChild(display);
	}

	// addParam(ParamWidget::create<RoundHugeBlackKnob>(Vec(47, 61), module, GARCON::FREQ_PARAM, -54.0f, 54.0f, 0.0f));
	// addParam(ParamWidget::create<RoundLargeBlackKnob>(Vec(23, 143), module, GARCON::FREQSPREAD_PARAM, -0.01f, 2.0f, 1.0f));

	addInput(Port::create<PJ301MPort>(Vec(11, 330), Port::INPUT, module, GARCON::L_INPUT));
	//addInput(Port::create<PJ301MPort>(Vec(45, 276), Port::INPUT, module, GARCON::R_INPUT));

	//addOutput(Port::create<PJ301MPort>(Vec(114, 320), Port::OUTPUT, module, GARCON::OUT_OUTPUT));
}


Model *modelGARCON = Model::create<GARCON, GARCONWidget>("Bidoo", "Garçon", "Garçon additive oscillator", OSCILLATOR_TAG);

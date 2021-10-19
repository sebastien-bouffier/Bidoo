#include "plugin.hpp"
#include "dsp/resampler.hpp"
#include "dsp/filter.hpp"
#include "dep/filters/fftanalysis.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <limits>
#include "dsp/ringbuffer.hpp"

using namespace std;

const int N = 1024;
const int N2 = N/2;
const int H = 256;

struct FLAME : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	FfftAnalysis *processor;
	vector<vector<float>> fft;
	vector<float> windowSum;
	dsp::DoubleRingBuffer<float,N> in_Buffer;
	float xBox, yBox, wBox, hBox;
	size_t xSampleWindow, nxSampleWindow, ySampleWindow, wSampleWindow, hSampleWindow;
	float runningSum = 0.f;
	bool initRunninSum = false;

	FLAME() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		processor = new FfftAnalysis(N, H, 2, APP->engine->getSampleRate());
		fft = vector<vector<float>>(H,vector<float>(N2,0));
		windowSum = vector<float>(H,0);
	}

	~FLAME() {
		delete processor;
	}

	void process(const ProcessArgs &args) override;
};

void FLAME::process(const ProcessArgs &args) {
	xSampleWindow = (1.0f-pow(1.0f-((wBox<0 ? xBox+wBox : xBox) / 130),0.1f))*N2;
	ySampleWindow = ((hBox<0 ? H-yBox : H-yBox-hBox) / H) * fft.size();
	wSampleWindow = (abs(wBox) / 130)*N2;
	nxSampleWindow = (1.0f-pow(1.0f-((wBox<0 ? xBox : xBox+wBox) / 130),0.1f))*N2;
	hSampleWindow = abs(hBox) * fft.size() / H;

	in_Buffer.push(inputs[INPUT].getVoltage()/10.0f);

	if ((wSampleWindow>0) && (hSampleWindow>0) && initRunninSum) {
		runningSum = 0.0f;
		for (size_t i = ySampleWindow; i < ySampleWindow+hSampleWindow; i++)  runningSum += windowSum[i];
		runningSum = runningSum/(wSampleWindow*hSampleWindow);
	}

	if (in_Buffer.full()) {
		processor->process(in_Buffer.startData(), &fft, &windowSum, xSampleWindow, nxSampleWindow);
		in_Buffer.clear();
		runningSum -= windowSum[ySampleWindow+hSampleWindow+1]/(wSampleWindow*hSampleWindow);
		runningSum += windowSum[ySampleWindow]/(wSampleWindow*hSampleWindow);
	}

	outputs[OUTPUT].setVoltage(runningSum);
}

struct FLAMEDisplay : OpaqueWidget {
	FLAME *module;
	const float width = 130.0f;
	float threshold = 1.0f;

	FLAMEDisplay() {

	}

	void onButton(const event::Button &e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			e.setTarget(this);
			module->xBox=e.pos.x;
			module->yBox=e.pos.y;
			module->hBox=0;
			module->wBox=0;
		}
	}

	void onDragStart(const event::DragStart &e) override {
		APP->window->cursorLock();
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(const event::DragMove &e) override {
		module->hBox+=e.mouseDelta.y;
		if (module->hBox+module->yBox>=H) module->hBox=H-module->yBox;
		if (module->hBox+module->yBox<=0) module->hBox=-1*module->yBox;
		module->wBox+=e.mouseDelta.x;
		if (module->wBox+module->xBox>=width) module->wBox=width-module->xBox;
		if (module->wBox+module->xBox<=0) module->wBox=-1*module->xBox;
		OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const event::DragEnd &e) override {
		APP->window->cursorUnlock();
		module->initRunninSum = true;
		OpaqueWidget::onDragEnd(e);
	}

	void draw(const DrawArgs &args) override {
		nvgGlobalTint(args.vg, color::WHITE);
		if (module) {
			float iWidth =  1.0f/width;
			nvgSave(args.vg);
			nvgScissor(args.vg,0,0,width,H);

			for (size_t j=module->fft.size()-1; j>0; j--) {
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0, H-j);
				for (size_t i = 0; i < width; i++) {
					float magn = interpolateLinear(&module->fft[j][0], (1.0f-pow(1.0f-i*iWidth,0.1f))*N2)*5e-4f;
					nvgLineTo(args.vg, i, H-(magn*H) - j);
				}
				nvgLineTo(args.vg, width, H - j);
				nvgClosePath(args.vg);
				nvgStrokeWidth(args.vg, 1);
				nvgStrokeColor(args.vg, nvgRGBA(min(255+j,size_t(255)), min(233+j,size_t(255)), min(0+j,size_t(255)), max(255.f-(j*1.2f),0.0f)));
				nvgFillColor(args.vg, nvgRGBA(min(228+j,size_t(255)), min(87+j,size_t(255)), min(46+j,size_t(255)), max(255.f-(j*1.2f),0.0f)));
				nvgStroke(args.vg);
				nvgFill(args.vg);
			}

			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, width, 0);
			for (size_t j=module->fft.size()-1; j>0; j--) {
				nvgLineTo(args.vg, width - module->windowSum[j]*5e-3f, H - j);
			}
			nvgLineTo(args.vg, width, H);
			nvgClosePath(args.vg);
			nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 120));
			nvgFill(args.vg);

			nvgBeginPath(args.vg);
			nvgRect(args.vg, module->xBox, module->yBox, module->wBox, module->hBox);
			nvgClosePath(args.vg);
			nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 80));
			nvgFill(args.vg);

			nvgResetScissor(args.vg);
			nvgRestore(args.vg);
		}
	}
};


struct FLAMEWidget : ModuleWidget {
	FLAMEWidget(FLAME *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/FLAME.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

  	{
  		FLAMEDisplay *display = new FLAMEDisplay();
  		display->module = module;
  		display->box.pos = Vec(10, 28);
  		display->box.size = Vec(130, H);
  		addChild(display);
  	}

  	addInput(createInput<PJ301MPort>(Vec(7, 330), module, FLAME::INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(119.0f, 330), module, FLAME::OUTPUT));
  }
};

Model *modelFLAME = createModel<FLAME, FLAMEWidget>("fLAME");

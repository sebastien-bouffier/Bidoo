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

struct FLAME : Module {
	enum ParamIds {
		MIN_PARAM,
		MED_PARAM,
		MAX_PARAM,
		YELLOW_PARAM,
		BLUE_PARAM,
		GREEN_PARAM,
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
		MIN_LIGHT,
		MED_LIGHT,
		MAX_LIGHT,
		YELLOW_LIGHT,
		BLUE_LIGHT,
		GREEN_LIGHT,
		NUM_LIGHTS
	};

	int N = 1024;
	int N2 = N/2;
	int H = 256;
	FfftAnalysis *processor;
	vector<vector<float>> fft;
	vector<float> windowSum;
	vector<float> inBuffer;
	float xBox, yBox, wBox, hBox;
	size_t xSampleWindow, nxSampleWindow, ySampleWindow, wSampleWindow, hSampleWindow;
	float runningSum = 0.f;
	bool initRunninSum = false;
	dsp::SchmittTrigger minTrigger, medTrigger, maxTrigger;
	dsp::SchmittTrigger yellowTrigger, blueTrigger, greenTrigger;
	int colorScheme = 0;


	FLAME() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		processor = new FfftAnalysis(N, H, 2, APP->engine->getSampleRate());
		fft = vector<vector<float>>(H,vector<float>(N2,0));
		windowSum = vector<float>(H,0);
	}

	~FLAME() {
		delete processor;
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "xBox", json_real(xBox));
		json_object_set_new(rootJ, "yBox", json_real(yBox));
		json_object_set_new(rootJ, "wBox", json_real(wBox));
		json_object_set_new(rootJ, "hBox", json_real(hBox));
		json_object_set_new(rootJ, "frameSize", json_real(N));
		json_object_set_new(rootJ, "colorScheme", json_real(colorScheme));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *xBoxJ = json_object_get(rootJ, "xBox");
		if (xBoxJ) xBox = json_real_value(xBoxJ);
		json_t *yBoxJ = json_object_get(rootJ, "yBox");
		if (yBoxJ) yBox = json_real_value(yBoxJ);
		json_t *wBoxJ = json_object_get(rootJ, "wBox");
		if (wBoxJ) wBox = json_real_value(wBoxJ);
		json_t *hBoxJ = json_object_get(rootJ, "hBox");
		if (hBoxJ) hBox = json_real_value(hBoxJ);
		json_t *colorSchemeJ = json_object_get(rootJ, "colorScheme");
		if (colorSchemeJ) colorScheme = json_real_value(colorSchemeJ);
		json_t *NJ = json_object_get(rootJ, "frameSize");
		if (NJ) N = json_real_value(NJ);
		N2=N/2;
		processor = new FfftAnalysis(N, H, 2, APP->engine->getSampleRate());
		fft = vector<vector<float>>(H,vector<float>(N2,0));
		windowSum = vector<float>(H,0);
	}

	void process(const ProcessArgs &args) override;
};

void FLAME::process(const ProcessArgs &args) {
	if (minTrigger.process(params[MIN_PARAM].getValue())) {
		N = 512;
		N2 = N/2;
		H = 256;
		processor = new FfftAnalysis(N, H, 2, APP->engine->getSampleRate());
		fft = vector<vector<float>>(H,vector<float>(N2,0));
		windowSum = vector<float>(H,0);
		inBuffer.clear();
	}

	if (minTrigger.process(params[MED_PARAM].getValue())) {
		N = 1024;
		N2 = N/2;
		H = 256;
		processor = new FfftAnalysis(N, H, 2, APP->engine->getSampleRate());
		fft = vector<vector<float>>(H,vector<float>(N2,0));
		windowSum = vector<float>(H,0);
		inBuffer.clear();
	}

	if (minTrigger.process(params[MAX_PARAM].getValue())) {
		N = 2048;
		N2 = N/2;
		H = 256;
		processor = new FfftAnalysis(N, H, 2, APP->engine->getSampleRate());
		fft = vector<vector<float>>(H,vector<float>(N2,0));
		windowSum = vector<float>(H,0);
		inBuffer.clear();
	}

	lights[MIN_LIGHT].setBrightness(N == 512 ? 1.0f : 0.0f);
	lights[MED_LIGHT].setBrightness(N == 1024 ? 1.0f : 0.0f);
	lights[MAX_LIGHT].setBrightness(N == 2048 ? 1.0f : 0.0f);

	if (yellowTrigger.process(params[YELLOW_PARAM].getValue())) {
		colorScheme = 0;
	}

	if (blueTrigger.process(params[BLUE_PARAM].getValue())) {
		colorScheme = 1;
	}

	if (greenTrigger.process(params[GREEN_PARAM].getValue())) {
		colorScheme = 2;
	}

	lights[YELLOW_LIGHT].setBrightness(colorScheme == 0 ? 1.0f : 0.0f);
	lights[BLUE_LIGHT].setBrightness(colorScheme == 1 ? 1.0f : 0.0f);
	lights[GREEN_LIGHT].setBrightness(colorScheme == 2 ? 1.0f : 0.0f);

	xSampleWindow = (1.0f-pow(1.0f-((wBox<0 ? xBox+wBox : xBox) / 130),0.1f))*N2;
	ySampleWindow = ((hBox<0 ? H-yBox : H-yBox-hBox) / H) * fft.size();
	wSampleWindow = (abs(wBox) / 130)*N2;
	nxSampleWindow = (1.0f-pow(1.0f-((wBox<0 ? xBox : xBox+wBox) / 130),0.1f))*N2;
	hSampleWindow = abs(hBox) * fft.size() / H;

	inBuffer.push_back(inputs[INPUT].getVoltage()/10.0f);

	if ((wSampleWindow>0) && (hSampleWindow>0) && initRunninSum) {
		runningSum = 0.0f;
		for (size_t i = ySampleWindow; i < ySampleWindow+hSampleWindow; i++)  runningSum += windowSum[i];
		runningSum = runningSum/(wSampleWindow*hSampleWindow);
		initRunninSum = false;
	}

	if (inBuffer.size()==(long long unsigned int)N) {
		processor->process(&inBuffer[0], &fft, &windowSum, xSampleWindow, nxSampleWindow);
		inBuffer.clear();
		runningSum -= windowSum[ySampleWindow+hSampleWindow+1]/(wSampleWindow*hSampleWindow);
		runningSum += windowSum[ySampleWindow]/(wSampleWindow*hSampleWindow);
	}

	outputs[OUTPUT].setVoltage(clamp(runningSum,0.0f,10.0f));
}

struct FLAMEDisplay : OpaqueWidget {
	FLAME *module;
	const float width = 130.0f;

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
		if (module->hBox+module->yBox>=module->H) module->hBox=module->H-module->yBox;
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

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module) {
				float iWidth =  1.0f/width;
				nvgSave(args.vg);
				nvgScissor(args.vg,0.2f,00.2f,width-0.4f,box.size.y-0.4f);
				nvgShapeAntiAlias(args.vg,false);
				nvgStrokeWidth(args.vg, 1);

				if (module->inputs[FLAME::INPUT].isConnected()) {
					for (size_t j=module->fft.size()-1; j>0; j--) {
						nvgBeginPath(args.vg);
						float y = box.size.y*(1.0f - (float)j/(float)module->H);
						nvgMoveTo(args.vg, 0, y);
						for (size_t i = 0; i < width; i++) {
							float magn = interpolateLinear(&module->fft[j][0], (1.0f-pow(1.0f-i*iWidth,0.1f))*module->N2)*5e-4f;
							nvgLineTo(args.vg, i, y-(magn*box.size.y));
						}
						nvgLineTo(args.vg, width, y);
						nvgLineTo(args.vg, 0, y);
						nvgClosePath(args.vg);

						if (module->colorScheme == 0) {
							nvgStrokeColor(args.vg, nvgRGBA(min(255+j,size_t(255)), min(233+j,size_t(255)), min(0+j,size_t(255)), max(255.f-(j*1.2f),0.0f)));
							nvgFillColor(args.vg, nvgRGBA(min(228+j,size_t(255)), min(87+j,size_t(255)), min(46+j,size_t(255)), max(255.f-(j*1.2f),0.0f)));
						}
						if (module->colorScheme == 1) {
							nvgStrokeColor(args.vg, nvgRGBA(min(0+j,size_t(255)), min(233+j,size_t(255)), min(255+j,size_t(255)), max(255.f-(j*1.2f),0.0f)));
							nvgFillColor(args.vg, nvgRGBA(min(46+j,size_t(255)), min(87+j,size_t(255)), min(228+j,size_t(255)), max(255.f-(j*1.2f),0.0f)));
						}
						if (module->colorScheme == 2) {
							nvgStrokeColor(args.vg, nvgRGBA(min(150+j,size_t(255)), min(255+j,size_t(255)), min(150+j,size_t(255)), max(255.f-(j*1.2f),0.0f)));
							nvgFillColor(args.vg, nvgRGBA(min(46+j,size_t(255)), min(228+j,size_t(255)), min(46+j,size_t(255)), max(255.f-(j*1.2f),0.0f)));
						}
						nvgStroke(args.vg);
						nvgFill(args.vg);
					}

					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, width, 0);
					for (size_t j=module->fft.size()-1; j>0; j--) {
						float y = box.size.y*(1.0f - (float)j/(float)module->H);
						nvgLineTo(args.vg, width - module->windowSum[j]*5e-3f, y);
					}
					nvgLineTo(args.vg, width, box.size.y);
					nvgLineTo(args.vg, width, 0);
					nvgClosePath(args.vg);
					nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 120));
					nvgFill(args.vg);
				}

				nvgBeginPath(args.vg);
				nvgRect(args.vg, module->xBox, module->yBox, module->wBox, module->hBox);
				nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 80));
				nvgFill(args.vg);

				nvgResetScissor(args.vg);
				nvgRestore(args.vg);
			}
		}
		Widget::drawLayer(args, layer);
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
  		display->box.size = Vec(130, 256);
  		addChild(display);
  	}

		const float xOffset = 21.0f;

		addParam(createParam<LEDButton>(Vec(45.0f, 300.0f), module, FLAME::MIN_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(51.0f, 306.0f), module, FLAME::MIN_LIGHT));

		addParam(createParam<LEDButton>(Vec(45.0f+xOffset, 300.0f), module, FLAME::MED_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(51.0f+xOffset, 306.0f), module, FLAME::MED_LIGHT));

		addParam(createParam<LEDButton>(Vec(45.0f+2*xOffset, 300.0f), module, FLAME::MAX_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(51.0f+2*xOffset, 306.0f), module, FLAME::MAX_LIGHT));

		addParam(createParam<LEDButton>(Vec(45.0f, 330.0f), module, FLAME::YELLOW_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(51.0f, 336.0f), module, FLAME::YELLOW_LIGHT));

		addParam(createParam<LEDButton>(Vec(45.0f+xOffset, 330.0f), module, FLAME::BLUE_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(51.0f+xOffset, 336.0f), module, FLAME::BLUE_LIGHT));

		addParam(createParam<LEDButton>(Vec(45.0f+2*xOffset, 330.0f), module, FLAME::GREEN_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(51.0f+2*xOffset, 336.0f), module, FLAME::GREEN_LIGHT));

  	addInput(createInput<PJ301MPort>(Vec(7, 330), module, FLAME::INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(119.0f, 330), module, FLAME::OUTPUT));
  }
};

Model *modelFLAME = createModel<FLAME, FLAMEWidget>("fLAME");

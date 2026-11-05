#include "plugin.hpp"
#include "dsp/resampler.hpp"
#include "dsp/filter.hpp"
#include "dep/pffft/pffft.h"
#include <atomic>
#include <cstring>
#include <iostream>
#include <vector>
#include <algorithm>
#include <limits>
#include "dsp/ringbuffer.hpp"

using namespace std;

// A spectrogram. Drawing a box on it picks a band and a time span; the
// output is the amplitude of what is inside: a 5 V sine in the box gives 5 V.
struct FLAME : BidooModule {
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

	static constexpr int H = 256;       // rows of history, one per hop
	static constexpr int MAXN = 2048;
	static constexpr float WIDTH = 130.0f;  // display width in px; its height is H px

	int N = 1024;
	int N2 = 512;

	// everything allocated once: nothing is allocated or freed while running,
	// and the display always reads valid memory
	PFFFT_Setup *setups[3] = {};
	float *work = nullptr, *workOut = nullptr;
	float hann[MAXN];
	float input[MAXN] = {};
	int inPos = 0, hop = 0;
	float rows[H][MAXN / 2 + 2] = {};   // magnitudes, a ring: newest at head
	float rowAmp[H] = {};               // amplitude (V) of the band in each row
	int head = 0, filled = 0;

	// the box, in display px (set by the display)
	float xBox = 0.f, yBox = 0.f, wBox = 0.f, hBox = 0.f;
	std::atomic<bool> boxChanged {true};
	int binLo = 0, binHi = -1;
	float out = 0.f;

	dsp::SchmittTrigger sizeTriggers[3];
	dsp::SchmittTrigger yellowTrigger, blueTrigger, greenTrigger;
	int colorScheme = 0;

	FLAME() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(MIN_PARAM, "FFT size 512");
		configButton(MED_PARAM, "FFT size 1024");
		configButton(MAX_PARAM, "FFT size 2048");
		configButton(YELLOW_PARAM, "Yellow display");
		configButton(BLUE_PARAM, "Blue display");
		configButton(GREEN_PARAM, "Green display");
		configInput(INPUT, "Audio");
		configOutput(OUTPUT, "Amplitude inside the box (a 5 V sine gives 5 V)");
		for (int i = 0; i < 3; i++)
			setups[i] = pffft_new_setup(512 << i, PFFFT_REAL);
		work = (float *)pffft_aligned_malloc(MAXN * sizeof(float));
		workOut = (float *)pffft_aligned_malloc(MAXN * sizeof(float));
		setSize(1024);
	}

	~FLAME() {
		for (PFFFT_Setup *s : setups)
			pffft_destroy_setup(s);
		pffft_aligned_free(work);
		pffft_aligned_free(workOut);
	}

	void setSize(int n) {
		N = n;
		N2 = n / 2;
		for (int k = 0; k < N; k++)
			hann[k] = 0.5f - 0.5f * std::cos(2.0f * M_PI * k / N);
		std::memset(input, 0, sizeof(input));
		std::memset(rows, 0, sizeof(rows));
		std::memset(rowAmp, 0, sizeof(rowAmp));
		inPos = hop = head = filled = 0;
		boxChanged = true;
	}

	// row of the given age (0: newest), for the display
	const float *row(int age) const {
		return rows[(head - age + H) % H];
	}
	float amp(int age) const {
		return rowAmp[(head - age + H) % H];
	}

	// the display's warped frequency axis: px -> bin
	float pxToBin(float px) const {
		return (1.0f - std::pow(1.0f - clamp(px, 0.f, WIDTH) / WIDTH, 0.1f)) * N2;
	}

	// For a Hann window the energy of a sine of amplitude A, summed over its
	// bins, is (3/8) N^2 A^2 / 2 in squared magnitudes 2|X|: A = sqrt(8 E / 3) / N.
	// The input is read at 1/10 (for the display), hence the 10.
	float bandAmp(const float *r) const {
		float e = 0.f;
		for (int k = binLo; k <= binHi; k++)
			e += r[k] * r[k];
		return 10.0f * std::sqrt(8.0f * e / 3.0f) / N;
	}

	void analyse() {
		// the last N samples, oldest first
		for (int k = 0; k < N; k++)
			work[k] = input[(inPos + k) % N] * hann[k];
		pffft_transform_ordered(setups[N == 512 ? 0 : (N == 1024 ? 1 : 2)], work, workOut, nullptr, PFFFT_FORWARD);
		head = (head + 1) % H;
		float *r = rows[head];
		r[0] = 2.0f * std::fabs(workOut[0]);
		for (int k = 1; k < N2; k++)
			r[k] = 2.0f * std::sqrt(workOut[2 * k] * workOut[2 * k] + workOut[2 * k + 1] * workOut[2 * k + 1]);
		r[N2] = 2.0f * std::fabs(workOut[1]);  // Nyquist, packed in [1]
		rowAmp[head] = bandAmp(r);
		filled = std::min(filled + 1, H);
	}

	// the box moved: a new band for every stored row
	void updateBand() {
		float x0 = std::min(xBox, xBox + wBox), x1 = std::max(xBox, xBox + wBox);
		binLo = clamp((int)std::floor(pxToBin(x0)), 0, N2);
		binHi = clamp((int)std::ceil(pxToBin(x1)), 0, N2);
		if (std::fabs(wBox) < 1.f)
			binHi = binLo - 1;
		for (int i = 0; i < H; i++)
			rowAmp[i] = bandAmp(rows[i]);
	}

	// the box's time span: rows are H px tall, the newest at the bottom
	void updateOutput() {
		if (std::fabs(wBox) < 1.f || std::fabs(hBox) < 1.f || binHi < binLo) {
			out = 0.f;
			return;
		}
		float y0 = std::min(yBox, yBox + hBox), y1 = std::max(yBox, yBox + hBox);
		int ageLo = clamp((int)std::floor(H - y1), 0, H - 1);
		int ageHi = clamp((int)std::ceil(H - y0), 0, H - 1);
		ageHi = std::min(ageHi, filled - 1);
		if (ageHi < ageLo) {
			out = 0.f;
			return;
		}
		float e = 0.f;
		for (int a = ageLo; a <= ageHi; a++)
			e += amp(a) * amp(a);
		out = std::sqrt(e / (ageHi - ageLo + 1));
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "xBox", json_real(xBox));
		json_object_set_new(rootJ, "yBox", json_real(yBox));
		json_object_set_new(rootJ, "wBox", json_real(wBox));
		json_object_set_new(rootJ, "hBox", json_real(hBox));
		json_object_set_new(rootJ, "frameSize", json_integer(N));
		json_object_set_new(rootJ, "colorScheme", json_integer(colorScheme));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		if (json_t *j = json_object_get(rootJ, "xBox")) xBox = json_number_value(j);
		if (json_t *j = json_object_get(rootJ, "yBox")) yBox = json_number_value(j);
		if (json_t *j = json_object_get(rootJ, "wBox")) wBox = json_number_value(j);
		if (json_t *j = json_object_get(rootJ, "hBox")) hBox = json_number_value(j);
		if (json_t *j = json_object_get(rootJ, "colorScheme")) colorScheme = json_number_value(j);
		int n = 1024;
		if (json_t *j = json_object_get(rootJ, "frameSize")) n = json_number_value(j);
		setSize(n == 512 || n == 2048 ? n : 1024);
	}

	void process(const ProcessArgs &args) override {
		for (int i = 0; i < 3; i++) {
			if (sizeTriggers[i].process(params[MIN_PARAM + i].getValue()) && N != (512 << i))
				setSize(512 << i);
		}
		lights[MIN_LIGHT].setBrightness(N == 512 ? 1.0f : 0.0f);
		lights[MED_LIGHT].setBrightness(N == 1024 ? 1.0f : 0.0f);
		lights[MAX_LIGHT].setBrightness(N == 2048 ? 1.0f : 0.0f);

		if (yellowTrigger.process(params[YELLOW_PARAM].getValue()))
			colorScheme = 0;
		if (blueTrigger.process(params[BLUE_PARAM].getValue()))
			colorScheme = 1;
		if (greenTrigger.process(params[GREEN_PARAM].getValue()))
			colorScheme = 2;
		lights[YELLOW_LIGHT].setBrightness(colorScheme == 0 ? 1.0f : 0.0f);
		lights[BLUE_LIGHT].setBrightness(colorScheme == 1 ? 1.0f : 0.0f);
		lights[GREEN_LIGHT].setBrightness(colorScheme == 2 ? 1.0f : 0.0f);

		input[inPos] = inputs[INPUT].getVoltageSum() * 0.1f;
		inPos = (inPos + 1) % N;
		bool changed = boxChanged.exchange(false);
		if (changed)
			updateBand();
		// a new row every half frame
		if (++hop >= N2) {
			hop = 0;
			analyse();
			updateOutput();
		}
		else if (changed)
			updateOutput();
		outputs[OUTPUT].setVoltage(out);
	}
};

struct FLAMEDisplay : OpaqueWidget {
	FLAME *module;
	const float width = 130.0f;

	FLAMEDisplay() {

	}

	void onButton(const event::Button &e) override {
		if (module && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			e.setTarget(this);
			module->xBox=e.pos.x;
			module->yBox=e.pos.y;
			module->hBox=0;
			module->wBox=0;
			module->boxChanged = true;
		}
	}

	void onDragStart(const event::DragStart &e) override {
		APP->window->cursorLock();
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(const event::DragMove &e) override {
		if (!module)
			return;
		module->hBox+=e.mouseDelta.y;
		if (module->hBox+module->yBox>=module->H) module->hBox=module->H-module->yBox;
		if (module->hBox+module->yBox<=0) module->hBox=-1*module->yBox;
		module->wBox+=e.mouseDelta.x;
		if (module->wBox+module->xBox>=width) module->wBox=width-module->xBox;
		if (module->wBox+module->xBox<=0) module->wBox=-1*module->xBox;
		module->boxChanged = true;
		OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const event::DragEnd &e) override {
		APP->window->cursorUnlock();
		if (module)
			module->boxChanged = true;
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
					for (size_t j=FLAME::H-1; j>0; j--) {
						nvgBeginPath(args.vg);
						float y = box.size.y*(1.0f - (float)j/(float)module->H);
						nvgMoveTo(args.vg, 0, y);
						for (size_t i = 0; i < width; i++) {
							float magn = interpolateLinear(module->row(j), (1.0f-pow(1.0f-i*iWidth,0.1f))*module->N2)*5e-4f;
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
					// the band's amplitude per row: 3 px per volt
					for (size_t j=FLAME::H-1; j>0; j--) {
						float y = box.size.y*(1.0f - (float)j/(float)module->H);
						nvgLineTo(args.vg, width - std::min(module->amp(j)*3.0f, width), y);
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


struct FLAMEWidget : BidooWidget {
	FLAMEWidget(FLAME *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/FLAME.svg"));

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

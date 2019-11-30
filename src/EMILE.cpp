#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "osdialog.h"
#include <vector>
#include "cmath"
#include "dep/lodepng/lodepng.h"
#include "dep/filters/fftsynth.h"
#include "dsp/ringbuffer.hpp"
#include <algorithm>

#define FFT_SIZE 8192
#define STEPS 8
using namespace std;

inline double fastPow(double a, double b) {
  union {
    double d;
    int x[2];
  } u = { a };
  u.x[1] = (int)(b * (u.x[1] - 1072632447) + 1072632447);
  u.x[0] = 0;
  return u.d;
}

struct EMILE : Module {
	enum ParamIds {
		SPEED_PARAM,
		CURVE_PARAM,
    GAIN_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		GATE_INPUT,
		CURVE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

  FftSynth *synth;
	std::string lastPath;
	bool loading = false;
	std::vector<unsigned char> image;
  unsigned width = 0;
	unsigned height = 0;
	unsigned samplePos = 0;
	int delay = 0;
  bool play = false;
  dsp::SchmittTrigger playTrigger;
  float *magn;
  float *phas;
  dsp::DoubleRingBuffer<float,FFT_SIZE/STEPS> outBuffer;
  long fftSize2 = FFT_SIZE / 2;

	EMILE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(GAIN_PARAM, 0.1f, 10.0f, 1.0f);
		configParam(CURVE_PARAM, 0.5f, 8.0f, 1.0f);
		configParam(SPEED_PARAM, 200.0f, 1.0f, 50.0f);

    synth = new FftSynth(FFT_SIZE, STEPS, APP->engine->getSampleRate());
    magn = (float*)calloc(fftSize2,sizeof(float));
    phas = (float*)calloc(fftSize2,sizeof(float));
	}

  ~EMILE() {
		delete synth;
    free(magn);
		free(phas);
	}

	void process(const ProcessArgs &args) override;

	void loadSample(std::string path);

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (lastPathJ) {
			lastPath = json_string_value(lastPathJ);
			loadSample(lastPath);
		}
	}

};

void EMILE::loadSample(std::string path) {
	loading = true;
  image.clear();
	unsigned error = lodepng::decode(image, width, height, path, LCT_RGB);
	if(error != 0)
  {
    std::cout << "error " << error << ": " << lodepng_error_text(error) << std::endl;
		lastPath = "";
	}
  else {
    lastPath = path;
    samplePos = 0;
  }
	loading = false;
}

void EMILE::process(const ProcessArgs &args) {

  if (playTrigger.process(inputs[GATE_INPUT].value)) {
    play = true;
    samplePos = 0;
  }
  memset(phas, 0, fftSize2*sizeof(float));
  memset(magn, 0, fftSize2*sizeof(float));

	if (play && !loading && (lastPath != "")) {
    if (outBuffer.size() == 0) {
      float iHeight = 1.0f/height;
      for (unsigned i = 0; i < height; i++) {
        float volume = (0.33f * image[samplePos * 3 + (height - i - 1) * width * 3] + 0.5f * image[samplePos * 3 + (height - i - 1) * width * 3 + 1] +  0.16f * image[samplePos * 3 + (height - i - 1) * width * 3 + 2]) / 255.0f;
  			if (volume > 0.0f) {
          float fact = fastPow(10.0f,i*params[CURVE_PARAM].value/height)*0.1f;
          size_t index = clamp((int)(i * fact * fftSize2 * iHeight * 0.5f),0,fftSize2/2);
          magn[index] = clamp(volume,0.0f,1.0f);
  			}
  		}

      synth->process(magn,phas,outBuffer.endData());
      outBuffer.endIncr(FFT_SIZE/STEPS);
    }

		outputs[OUT].setVoltage(params[GAIN_PARAM].value * clamp(*outBuffer.startData() * 10.0f,-10.0f,10.0f));
    outBuffer.startIncr(1);

		delay++;

		if (samplePos >= width) {
      samplePos = 0;
      play = false;
    }
		else {
			if (delay>params[SPEED_PARAM].value)
			{
				samplePos++;
				delay = 0;
			}
		}

	}
}

struct EMILEDisplay : OpaqueWidget {
	EMILE *module;
	shared_ptr<Font> font;
	const float width = 125.0f;
	const float height = 130.0f;
	std::string path = "";
	bool first = true;
	int img = 0;

	EMILEDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void draw(const DrawArgs &args) override {
		if (module && !module->loading) {
			if (path != module->lastPath) {
				img = nvgCreateImage(args.vg, module->lastPath.c_str(), 0);
				path = module->lastPath;
			}

			nvgBeginPath(args.vg);
			if (module->width>0 && module->height>0)
				nvgScale(args.vg, width/module->width, height/module->height);
		 	NVGpaint imgPaint = nvgImagePattern(args.vg, 0, 0, module->width,module->height, 0, img, 1.0f);
		 	nvgRect(args.vg, 0, 0, module->width, module->height);
		 	nvgFillPaint(args.vg, imgPaint);
		 	nvgFill(args.vg);
			nvgClosePath(args.vg);
		}
	}
};

struct EMILEPositionDisplay : OpaqueWidget {
	EMILE *module;
	shared_ptr<Font> font;
	const float width = 125.0f;
	const float height = 130.0f;
	std::string path = "";
	bool first = true;
	int img = 0;

	EMILEPositionDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void draw(const DrawArgs &args) override {
		if (module && !module->loading) {
			nvgStrokeColor(args.vg, LIGHTBLUE_BIDOO);
			{
				nvgBeginPath(args.vg);
				nvgStrokeWidth(args.vg, 2);
				if (module->image.size()>0) {
					nvgMoveTo(args.vg, (float)module->samplePos * width / module->width, 0);
					nvgLineTo(args.vg, (float)module->samplePos * width / module->width, height);
				}
				else {
					nvgMoveTo(args.vg, 0, 0);
					nvgLineTo(args.vg, 0, height);
				}
				nvgClosePath(args.vg);
			}
			nvgStroke(args.vg);
		}
	}
};

struct EMILEWidget : ModuleWidget {
	EMILEWidget(EMILE *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/EMILE.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		{
			EMILEDisplay *display = new EMILEDisplay();
			display->module = module;
			display->box.pos = Vec(5, 30);
			display->box.size = Vec(125, 130);
			addChild(display);
		}

		{
			EMILEPositionDisplay *display = new EMILEPositionDisplay();
			display->module = module;
			display->box.pos = Vec(5, 30);
			display->box.size = Vec(125, 130);
			addChild(display);
		}

		static const float portX0[4] = {34, 67, 101};

    addParam(createParam<BidooBlueKnob>(Vec(portX0[1]-15, 180), module, EMILE::GAIN_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[1]-15, 235), module, EMILE::CURVE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[1]-15, 290), module, EMILE::SPEED_PARAM));
		addInput(createInput<PJ301MPort>(Vec(portX0[0]-25, 321), module, EMILE::GATE_INPUT));

		addOutput(createOutput<PJ301MPort>(Vec(portX0[2], 321), module, EMILE::OUT));
	}

  struct EMILEItem : MenuItem {
  	EMILE *module;
  	void onAction(const event::Action &e) override {

  		std::string dir = module->lastPath.empty() ?  asset::user("") : rack::string::directory(module->lastPath);
  		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
  		if (path) {
  			module->loadSample(path);
  			free(path);
  		}
  	}
  };

  void appendContextMenu(ui::Menu *menu) override {
		EMILE *module = dynamic_cast<EMILE*>(this->module);
		assert(module);

		menu->addChild(construct<MenuLabel>());
		menu->addChild(construct<EMILEItem>(&MenuItem::text, "Load image (png)", &EMILEItem::module, module));
	}
};

Model *modelEMILE = createModel<EMILE, EMILEWidget>("EMILE");

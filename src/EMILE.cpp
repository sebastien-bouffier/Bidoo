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

const int FS = 4096;
const int N = 4;
const int FS2 = FS / 2;
const float IFS = 1.0f / FS;
const int STS = FS/N;
const float IFS2 = 1.0f/FS2;


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
		CURVE_PARAM,
    GAIN_PARAM,
    R_PARAM,
    G_PARAM,
    B_PARAM,
    A_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		POS_INPUT,
    CURVE_INPUT,
    R_INPUT,
    G_INPUT,
    B_INPUT,
    A_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
    R_LIGTH,
    G_LIGTH,
    B_LIGTH,
    A_LIGTH,
		NUM_LIGHTS
	};

	std::string lastPath;
	bool loading = false;
	std::vector<unsigned char> image;
  unsigned width = 0;
	unsigned height = 0;
	unsigned samplePos = 0;
  float *magn;
  float *out;
  float *acc;
  int rIdx = 0;
  PFFFT_Setup *pffftSetup;
  float *fftIn;
  float *fftOut;
  bool r, g, b, a;
  dsp::SchmittTrigger rTrigger, gTrigger, bTrigger, aTrigger;
  float curve=0.0f;

	EMILE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(GAIN_PARAM, 0.1f, 10.0f, 1.0f);
		configParam(CURVE_PARAM, 0.01f, 0.1f, 0.05f);

    magn = (float*) pffft_aligned_malloc(FS2*sizeof(float));
    out = (float*) pffft_aligned_malloc(STS*sizeof(float));
    acc = (float*) pffft_aligned_malloc(2*FS*sizeof(float));
    memset(acc, 0, 2*FS*sizeof(float));
    memset(out, 0, STS*sizeof(float));
    pffftSetup = pffft_new_setup(FS, PFFFT_REAL);
    fftIn = (float*)pffft_aligned_malloc(FS*sizeof(float));
    fftOut =  (float*)pffft_aligned_malloc(FS*sizeof(float));
	}

  ~EMILE() {
    pffft_aligned_free(magn);
    pffft_aligned_free(acc);
    pffft_destroy_setup(pffftSetup);
    pffft_aligned_free(fftIn);
    pffft_aligned_free(fftOut);
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
	unsigned error = lodepng::decode(image, width, height, path, LCT_RGBA, 16);
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
  if (rTrigger.process(params[R_PARAM].getValue()+inputs[R_INPUT].getVoltage())) {
    r=!r;
  }
  lights[R_LIGTH].setBrightness(r?1:0);
  if (gTrigger.process(params[G_PARAM].getValue()+inputs[G_INPUT].getVoltage())) {
    g=!g;
  }
  lights[G_LIGTH].setBrightness(g?1:0);
  if (bTrigger.process(params[B_PARAM].getValue()+inputs[B_INPUT].getVoltage())) {
    b=!b;
  }
  lights[B_LIGTH].setBrightness(b?1:0);
  if (aTrigger.process(params[A_PARAM].getValue()+inputs[A_INPUT].getVoltage())) {
    a=!a;
  }
  lights[A_LIGTH].setBrightness(a?1:0);

  curve = params[CURVE_PARAM].getValue() + rescale(inputs[CURVE_INPUT].getVoltage(),0.0f,10.0f,0.01f, 0.1f);



	if (!loading && (lastPath != "")) {
    samplePos = rescale(clamp(inputs[POS_INPUT].getVoltage(),0.0f,10.0f),0.0f,10.0f,0.0f,height-1);

    if (rIdx == STS) {

    	memset(fftIn, 0, FS*sizeof(float));
    	memset(fftOut, 0, FS*sizeof(float));
      memset(magn, 0, FS2*sizeof(float));

      float iWidth = 1.0f/width;
      for(unsigned x = 0; x < width; x++) {
        unsigned short red = 256 * image[samplePos * 8 * width + x * 8 + 0] + image[samplePos * 8 * width + x * 8 + 1];
        unsigned short green = 256 * image[samplePos * 8 * width + x * 8 + 2] + image[samplePos * 8 * width + x * 8 + 3];
        unsigned short blue = 256 * image[samplePos * 8 * width + x * 8 + 4] + image[samplePos * 8 * width + x * 8 + 5];
        unsigned short alpha = 256 * image[samplePos * 8 * width + x * 8 + 6] + image[samplePos * 8 * width + x * 8 + 7];
        float mix = 1e-7f*((r?red:0)+(g?green:0)+(b?blue:0)+(a?alpha:0))/max(1,r+g+b+a);
        float index = (1.0f-pow(1.0f-x*iWidth,curve))*(FS2);
        magn[(size_t)index] += mix*(1-index+(size_t)index);
        if (x<width-1) {
          magn[(size_t)index+1] += mix*(index-(size_t)index);
        }
      }

    	for (size_t i = 0; i < FS2; i++) {
    		fftIn[2*i] = magn[i];
    	}

    	pffft_transform_ordered(pffftSetup, fftIn, fftOut, NULL, PFFFT_BACKWARD);


    	for (size_t i = 0; i < FS; i++) {
        float window = -0.5f * cos(2.0f * M_PI * (double)i * IFS) + 0.5f;
        acc[i] += 2.0f*fftOut[i]*window;
    	}

      for (size_t i = 0; i < STS; i++) {
    		out[i] = acc[i];
    	}

      memmove(acc, acc+STS, FS*sizeof(float));
      rIdx = 0;
    }

		outputs[OUT].setVoltage(tanh(params[GAIN_PARAM].value * (out[rIdx]))*5.0f);
    rIdx++;
	}
}

struct EMILEDisplay : OpaqueWidget {
	EMILE *module;
	const float width = 125.0f;
	const float height = 130.0f;
	std::string path = "";
	int img = 0;

	EMILEDisplay() {

	}

	void draw(const DrawArgs &args) override {
		if (module && !module->loading) {
			if (path != module->lastPath) {
				img = nvgCreateImage(args.vg, module->lastPath.c_str(), 0);
				path = module->lastPath;
			}
      nvgSave(args.vg);
			nvgBeginPath(args.vg);
			if (module->width>0 && module->height>0)
				nvgScale(args.vg, width/module->width, height/module->height);
		 	NVGpaint imgPaint = nvgImagePattern(args.vg, 0, 0, module->width,module->height, 0, img, 1.0f);
		 	nvgRect(args.vg, 0, 0, module->width, module->height);
		 	nvgFillPaint(args.vg, imgPaint);
		 	nvgFill(args.vg);
			nvgClosePath(args.vg);

      nvgStrokeColor(args.vg, LIGHTBLUE_BIDOO);
			nvgBeginPath(args.vg);
			nvgStrokeWidth(args.vg, 5);
				if (module->image.size()>0) {
					nvgMoveTo(args.vg, 0, (float)module->samplePos);
					nvgLineTo(args.vg, (float)module->width, (float)module->samplePos);
				}
				else {
					nvgMoveTo(args.vg, 0, 0);
					nvgLineTo(args.vg, 0, height);
				}
			nvgClosePath(args.vg);
			nvgStroke(args.vg);
      nvgRestore(args.vg);
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

    const int xlightsAnchor = 8;
    const int ylightsAnchor = 170;
    const float xlightsOffset = 33.5;
    const int olights = 6;
    const int inputYOffset = 25;

    addParam(createParam<LEDButton>(Vec(xlightsAnchor, ylightsAnchor), module, EMILE::R_PARAM));
		addChild(createLight<SmallLight<RedLight>>(Vec(xlightsAnchor+olights, ylightsAnchor+olights), module, EMILE::R_LIGTH));
    addParam(createParam<LEDButton>(Vec(xlightsAnchor+xlightsOffset, ylightsAnchor), module, EMILE::G_PARAM));
		addChild(createLight<SmallLight<GreenLight>>(Vec(xlightsAnchor+xlightsOffset+olights, ylightsAnchor+olights), module, EMILE::G_LIGTH));
    addParam(createParam<LEDButton>(Vec(xlightsAnchor+2*xlightsOffset, ylightsAnchor), module, EMILE::B_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(xlightsAnchor+2*xlightsOffset+olights, ylightsAnchor+olights), module, EMILE::B_LIGTH));
    addParam(createParam<LEDButton>(Vec(xlightsAnchor+3*xlightsOffset, ylightsAnchor), module, EMILE::A_PARAM));
		addChild(createLight<SmallLight<WhiteLight>>(Vec(xlightsAnchor+3*xlightsOffset+olights, ylightsAnchor+olights), module, EMILE::A_LIGTH));

    addInput(createInput<TinyPJ301MPort>(Vec(xlightsAnchor+1, ylightsAnchor+inputYOffset), module, EMILE::R_INPUT));
    addInput(createInput<TinyPJ301MPort>(Vec(xlightsAnchor+xlightsOffset+1, ylightsAnchor+inputYOffset), module, EMILE::G_INPUT));
    addInput(createInput<TinyPJ301MPort>(Vec(xlightsAnchor+2*xlightsOffset+1, ylightsAnchor+inputYOffset), module, EMILE::B_INPUT));
    addInput(createInput<TinyPJ301MPort>(Vec(xlightsAnchor+3*xlightsOffset+1, ylightsAnchor+inputYOffset), module, EMILE::A_INPUT));

		static const float portX0[4] = {7, 56, 104};

    addParam(createParam<BidooBlueKnob>(Vec(portX0[1]-4, 235), module, EMILE::GAIN_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[1]-4, 290), module, EMILE::CURVE_PARAM));
    addInput(createInput<PJ301MPort>(Vec(portX0[1], 330), module, EMILE::CURVE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[0], 330), module, EMILE::POS_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(portX0[2], 330), module, EMILE::OUT));
	}

  struct EMILEItem : MenuItem {
  	EMILE *module;
  	void onAction(const event::Action &e) override {

  		std::string dir = module->lastPath.empty() ?  asset::user("") : rack::system::getDirectory(module->lastPath);
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

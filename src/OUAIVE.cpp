#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "osdialog.h"
#define DR_WAV_IMPLEMENTATION
#include "dep/dr_wav/dr_wav.h"
#include <vector>
#include "cmath"
#include <iomanip>
#include <sstream>
#include "window.hpp"
#include <mutex>

using namespace std;

struct OUAIVE : Module {
	enum ParamIds {
		NB_SLICES_PARAM,
		TRIG_MODE_PARAM,
		READ_MODE_PARAM,
		SPEED_PARAM,
		CVSLICES_PARAM,
		CVSPEED_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		GATE_INPUT,
		POS_INPUT,
		NB_SLICES_INPUT,
		READ_MODE_INPUT,
		SPEED_INPUT,
		POS_RESET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTL_OUTPUT,
		OUTR_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	bool play = false;
	unsigned int channels;
  unsigned int sampleRate;
  drwav_uint64 totalSampleCount;

	float samplePos = 0.0f;
	vector<vector<float>> playBuffer;
	std::string lastPath;
	std::string waveFileName;
	std::string waveExtension;
	bool loading = false;
	int trigMode = 0; // 0 trig 1 gate, 2 sliced
	int sliceIndex = -1;
	int sliceLength = 0;
	int nbSlices = 1;
	int readMode = 0; // 0 formward, 1 backward, 2 repeat
	float speed;
	dsp::SchmittTrigger playTrigger;
	dsp::SchmittTrigger trigModeTrigger;
	dsp::SchmittTrigger readModeTrigger;
	dsp::SchmittTrigger posResetTrigger;
	std::mutex mylock;


	OUAIVE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(TRIG_MODE_PARAM, 0.0, 2.0, 0.0);
		configParam(OUAIVE::READ_MODE_PARAM, 0.0, 2.0, 0.0);
		configParam(NB_SLICES_PARAM, 1.0, 128.01, 1.0);
		configParam(CVSLICES_PARAM, -1.0f, 1.0f, 0.0f);
		configParam(SPEED_PARAM, -0.05, 10, 1.0);
		configParam(CVSPEED_PARAM, -1.0f, 1.0f, 0.0f);

		playBuffer.resize(2);
		playBuffer[0].resize(0);
		playBuffer[1].resize(0);
	}

	void process(const ProcessArgs &args) override;

	void loadSample(std::string path);

	// persistence

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		// lastPath
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
		json_object_set_new(rootJ, "trigMode", json_integer(trigMode));
		json_object_set_new(rootJ, "readMode", json_integer(readMode));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		// lastPath
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (lastPathJ) {
			lastPath = json_string_value(lastPathJ);
			loadSample(lastPath);
		}
		json_t *trigModeJ = json_object_get(rootJ, "trigMode");
		if (trigModeJ) {
			trigMode = json_integer_value(trigModeJ);
		}
		json_t *readModeJ = json_object_get(rootJ, "readMode");
		if (readModeJ) {
			readMode = json_integer_value(readModeJ);
		}
	}
};

void OUAIVE::loadSample(std::string path) {
	loading = true;
	unsigned int c;
  unsigned int sr;
  drwav_uint64 sc;
	float* pSampleData;
  pSampleData = drwav_open_and_read_file_f32(path.c_str(), &c, &sr, &sc);
  if (pSampleData != NULL)  {
		lastPath = path;
    waveFileName = string::filename(lastPath);
    waveExtension = string::filenameBase(lastPath);
		channels = c;
		sampleRate = sr;
		playBuffer[0].clear();
		playBuffer[1].clear();
		for (unsigned int i=0; i < sc; i = i + c) {
			playBuffer[0].push_back(pSampleData[i]);
			if (channels == 2)
				playBuffer[1].push_back((float)pSampleData[i+1]);
			else
				playBuffer[1].push_back((float)pSampleData[i]);
		}
		totalSampleCount = playBuffer[0].size();
		drwav_free(pSampleData);
	}
	loading = false;
}

void OUAIVE::process(const ProcessArgs &args) {
	if (trigModeTrigger.process(params[TRIG_MODE_PARAM].value)) {
		trigMode = (((int)trigMode + 1) % 3);
	}
	if (inputs[READ_MODE_INPUT].active) {
		readMode = round(rescale(inputs[READ_MODE_INPUT].value, 0.0f,10.0f,0.0f,2.0f));
	} else if (readModeTrigger.process(params[READ_MODE_PARAM].value + inputs[READ_MODE_INPUT].value)) {
		readMode = (((int)readMode + 1) % 3);
	}
	nbSlices = clamp(roundl(params[NB_SLICES_PARAM].value + params[CVSLICES_PARAM].value * inputs[NB_SLICES_INPUT].value), 1, 128);
	speed = clamp(params[SPEED_PARAM].value + params[CVSPEED_PARAM].value * inputs[SPEED_INPUT].value, 0.2f, 10.0f);

	if (!loading) {
		sliceLength = clamp(totalSampleCount / nbSlices, 1, totalSampleCount);

		if ((trigMode == 0) && (playTrigger.process(inputs[GATE_INPUT].value))) {
			play = true;
			if (inputs[POS_INPUT].active)
				samplePos = clamp((int)(inputs[POS_INPUT].value * totalSampleCount * 0.1f), 0 , totalSampleCount - 1);
			else {
				if (readMode != 1)
					samplePos = 0;
				else
					samplePos = totalSampleCount - 1;
			}
		}	else if (trigMode == 1) {
			play = (inputs[GATE_INPUT].value > 0);
			samplePos = clamp((int)(inputs[POS_INPUT].value * totalSampleCount * 0.1f), 0 , totalSampleCount - 1);
		} else if ((trigMode == 2) && (playTrigger.process(inputs[GATE_INPUT].value))) {
			play = true;
			if (inputs[POS_INPUT].active)
				sliceIndex = clamp((int)(inputs[POS_INPUT].value * nbSlices * 0.1f), 0, nbSlices);
			 else
				sliceIndex = (sliceIndex+1)%nbSlices;
			if (readMode != 1)
				samplePos = clamp(sliceIndex*sliceLength, 0, totalSampleCount);
			else
				samplePos = clamp((sliceIndex + 1) * sliceLength - 1, 0 , totalSampleCount);
		}

		if (posResetTrigger.process(inputs[POS_RESET_INPUT].value)) {
			sliceIndex = 0;
			samplePos = 0;
		}

		if ((!loading) && (play) && (samplePos>=0) && (samplePos < totalSampleCount)) {
			if (channels == 1) {
				outputs[OUTL_OUTPUT].value = 5.0f * playBuffer[0][floor(samplePos)];
				outputs[OUTR_OUTPUT].value = 5.0f * playBuffer[0][floor(samplePos)];
			}
			else if (channels == 2) {
				if (outputs[OUTL_OUTPUT].active && outputs[OUTR_OUTPUT].active) {
					outputs[OUTL_OUTPUT].value = 5.0f * playBuffer[0][floor(samplePos)];
					outputs[OUTR_OUTPUT].value = 5.0f * playBuffer[1][floor(samplePos)];
				}
				else {
					outputs[OUTL_OUTPUT].value = 5.0f * (playBuffer[0][floor(samplePos)] + playBuffer[1][floor(samplePos)]);
					outputs[OUTR_OUTPUT].value = 5.0f * (playBuffer[0][floor(samplePos)] + playBuffer[1][floor(samplePos)]);
				}
			}

			if (trigMode == 0) {
				if (readMode != 1)
					samplePos = samplePos + speed * channels;
				else
					samplePos = samplePos - speed * channels;
				//manage eof readMode
				if ((readMode == 0) && (samplePos >= totalSampleCount))
						play = false;
				else if ((readMode == 1) && (samplePos <=0))
						play = false;
				else if ((readMode == 2) && (samplePos >= totalSampleCount))
					samplePos = clamp((int)(inputs[POS_INPUT].value * totalSampleCount * 0.1f), 0 , totalSampleCount -1);
			}
			else if (trigMode == 2)
			{
				if (readMode != 1)
					samplePos = samplePos + speed * channels;
				else
					samplePos = samplePos - speed * channels;

				//manage eof readMode
				if ((readMode == 0) && ((samplePos >= (sliceIndex+1) * sliceLength) || (samplePos >= totalSampleCount)))
						play = false;
				if ((readMode == 1) && ((samplePos <= (sliceIndex) * sliceLength) || (samplePos <= 0)))
						play = false;
				if ((readMode == 2) && ((samplePos >= (sliceIndex+1) * sliceLength) || (samplePos >= totalSampleCount)))
					samplePos = clamp(sliceIndex*sliceLength, 0 , totalSampleCount);
			}
		}
		else if (samplePos == totalSampleCount)
			play = false;
	}
}

struct OUAIVEDisplay : OpaqueWidget {
	OUAIVE *module;
	shared_ptr<Font> font;
	const float width = 125.0f;
	const float height = 50.0f;
	float zoomWidth = 125.0f;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refX = 0.0f;

	OUAIVEDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

  void onDragStart(const event::DragStart &e) override {
		appGet()->window->cursorLock();
		OpaqueWidget::onDragStart(e);
	}

  void onDragMove(const event::DragMove &e) override {
		float zoom = 1.0f;
    if (e.mouseDelta.y > 0.0f) {
      zoom = 1.0f/(((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 2.0f : 1.1f);
    }
    else if (e.mouseDelta.y < 0.0f) {
      zoom = ((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 2.0f : 1.1f;
    }
    zoomWidth = clamp(zoomWidth*zoom,width,zoomWidth*((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT) ? 2.0f : 1.1f));
    zoomLeftAnchor = clamp(refX - (refX - zoomLeftAnchor)*zoom + e.mouseDelta.x, width - zoomWidth,0.0f);
		OpaqueWidget::onDragMove(e);
	}

  void onDragEnd(const event::DragEnd &e) override {
    appGet()->window->cursorUnlock();
    OpaqueWidget::onDragEnd(e);
  }

	void draw(const DrawArgs &args) override {
    if (module) {
      module->mylock.lock();
  		std::vector<float> vL(module->playBuffer[0]);
  		std::vector<float> vR(module->playBuffer[1]);
  		module->mylock.unlock();
  		size_t nbSample = vL.size();

  		nvgFontSize(args.vg, 14);
  		nvgFillColor(args.vg, YELLOW_BIDOO);

  		std::string trigMode = "";
  		std::string slices = "";
  		if (module->trigMode == 0) {
  			trigMode = "TRIG ";
  		}
  		else if (module->trigMode==1)	{
  			trigMode = "GATE ";
  		}
  		else {
  			trigMode = "SLICE ";
  			slices = "|" + to_string(module->nbSlices) + "|";
  		}

  		nvgTextBox(args.vg, 3, -15, 40, trigMode.c_str(), NULL);
  		nvgTextBox(args.vg, 59, -15, 40, slices.c_str(), NULL);

  		std::string readMode = "";
  		if (module->readMode == 0) {
  			readMode = "►";
  		}
  		else if (module->readMode == 2) {
  			readMode = "►►";
  		}
  		else {
  			readMode = "◄";
  		}

  		nvgTextBox(args.vg, 40, -15, 40, readMode.c_str(), NULL);

  		stringstream stream;
  		stream << fixed << setprecision(1) << module->speed;
  		std::string s = stream.str();
  		std::string speed = "x" + s;

  		nvgTextBox(args.vg, 90, -15, 40, speed.c_str(), NULL);

  		//Draw play line
  		if ((module->play) && (!module->loading)) {
  			nvgStrokeColor(args.vg, LIGHTBLUE_BIDOO);
  			{
  				nvgBeginPath(args.vg);
  				nvgStrokeWidth(args.vg, 2);
  				if (module->totalSampleCount>0) {
  					nvgMoveTo(args.vg, module->samplePos * zoomWidth / nbSample + zoomLeftAnchor, 0);
  					nvgLineTo(args.vg, module->samplePos * zoomWidth / nbSample + zoomLeftAnchor, 2 * height+10);
  				}
  				else {
  					nvgMoveTo(args.vg, 0, 0);
  					nvgLineTo(args.vg, 0, 2 * height+10);
  				}
  				nvgClosePath(args.vg);
  			}
  			nvgStroke(args.vg);
  		}

  		//Draw ref line
  		nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
  		nvgStrokeWidth(args.vg, 1);
  		{
  			nvgBeginPath(args.vg);
  			nvgMoveTo(args.vg, 0, height * 0.5f);
  			nvgLineTo(args.vg, width, height * 0.5f);
  			nvgClosePath(args.vg);
  		}
  		nvgStroke(args.vg);

  		nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
  		nvgStrokeWidth(args.vg, 1);
  		{
  			nvgBeginPath(args.vg);
  			nvgMoveTo(args.vg, 0, 3*height * 0.5f + 10);
  			nvgLineTo(args.vg, width, 3*height * 0.5f + 10);
  			nvgClosePath(args.vg);
  		}
  		nvgStroke(args.vg);

  		if ((!module->loading) && (vL.size()>0)) {
  			//Draw waveform
  			nvgStrokeColor(args.vg, PINK_BIDOO);
  			nvgSave(args.vg);
  			Rect b = Rect(Vec(zoomLeftAnchor, 0), Vec(zoomWidth, height));
  			nvgScissor(args.vg, 0, b.pos.y, width, height);
  			nvgBeginPath(args.vg);
  			for (size_t i = 0; i < vL.size(); i++) {
  				float x, y;
  				x = (float)i/vL.size();
  				y = vL[i] / 2.0f + 0.5f;
  				Vec p;
  				p.x = b.pos.x + b.size.x * x;
  				p.y = b.pos.y + b.size.y * (1.0f - y);
  				if (i == 0) {
  					nvgMoveTo(args.vg, p.x, p.y);
  				}
  				else {
  					nvgLineTo(args.vg, p.x, p.y);
  				}
  			}
  			nvgLineCap(args.vg, NVG_MITER);
  			nvgStrokeWidth(args.vg, 1);
  			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
  			nvgStroke(args.vg);

  			b = Rect(Vec(zoomLeftAnchor, height+10), Vec(zoomWidth, height));
  			nvgScissor(args.vg, 0, b.pos.y, width, height);
  			nvgBeginPath(args.vg);
  			for (size_t i = 0; i < vR.size(); i++) {
  				float x, y;
  				x = (float)i/vR.size();
  				y = vR[i] / 2.0f + 0.5f;
  				Vec p;
  				p.x = b.pos.x + b.size.x * x;
  				p.y = b.pos.y + b.size.y * (1.0f - y);
  				if (i == 0)
  					nvgMoveTo(args.vg, p.x, p.y);
  				else
  					nvgLineTo(args.vg, p.x, p.y);
  			}
  			nvgLineCap(args.vg, NVG_MITER);
  			nvgStrokeWidth(args.vg, 1);
  			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
  			nvgStroke(args.vg);
  			nvgResetScissor(args.vg);

  			//draw slices

  			if (module->trigMode == 2) {
  				nvgScissor(args.vg, 0, 0, width, 2*height+10);
  				for (int i = 1; i < module->nbSlices; i++) {
  					nvgStrokeColor(args.vg, YELLOW_BIDOO);
  					nvgStrokeWidth(args.vg, 1);
  					{
  						nvgBeginPath(args.vg);
  						nvgMoveTo(args.vg, (int)(i * module->sliceLength * zoomWidth / nbSample + zoomLeftAnchor) , 0);
  						nvgLineTo(args.vg, (int)(i * module->sliceLength * zoomWidth / nbSample + zoomLeftAnchor) , 2*height+10);
  						nvgClosePath(args.vg);
  					}
  					nvgStroke(args.vg);
  				}
  				nvgResetScissor(args.vg);
  			}

  			nvgRestore(args.vg);
  		}
    }
	}
};

struct OUAIVEWidget : ModuleWidget {
	OUAIVEWidget(OUAIVE *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/OUAIVE.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		{
			OUAIVEDisplay *display = new OUAIVEDisplay();
			display->module = module;
			display->box.pos = Vec(5, 70);
			display->box.size = Vec(125, 110);
			addChild(display);
		}

		static const float portX0[4] = {34, 67, 101};

		addInput(createInput<TinyPJ301MPort>(Vec(10, 18), module, OUAIVE::POS_RESET_INPUT));

		addParam(createParam<BlueCKD6>(Vec(portX0[0]-25, 215), module, OUAIVE::TRIG_MODE_PARAM));

		addParam(createParam<BlueCKD6>(Vec(portX0[1]-14, 215), module, OUAIVE::READ_MODE_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(portX0[2]+5, 222), module, OUAIVE::READ_MODE_INPUT));

		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1]-9, 250), module, OUAIVE::NB_SLICES_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1]+15, 250), module, OUAIVE::CVSLICES_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(portX0[2]+5, 252), module, OUAIVE::NB_SLICES_INPUT));

		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1]-9, 275), module, OUAIVE::SPEED_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1]+15, 275), module, OUAIVE::CVSPEED_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(portX0[2]+5, 277), module, OUAIVE::SPEED_INPUT));

		addInput(createInput<PJ301MPort>(Vec(portX0[0]-25, 321), module, OUAIVE::GATE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1]-19, 321), module, OUAIVE::POS_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(portX0[2]-13, 331), module, OUAIVE::OUTL_OUTPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(portX0[2]+11, 331), module, OUAIVE::OUTR_OUTPUT));
	}

  struct OUAIVEItem : MenuItem {
  	OUAIVE *module;
  	void onAction(const event::Action &e) override {

  		std::string dir = module->lastPath.empty() ? asset::user("") : string::directory(module->lastPath);
  		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
  		if (path) {
  			module->play = false;
  			module->loadSample(path);
  			module->samplePos = 0;
  			module->lastPath = path;
  			module->sliceIndex = -1;
  			free(path);
  		}
  	}
  };

  void appendContextMenu(ui::Menu *menu) override {
		OUAIVE *module = dynamic_cast<OUAIVE*>(this->module);
		assert(module);

		menu->addChild(construct<MenuLabel>());
		menu->addChild(construct<OUAIVEItem>(&MenuItem::text, "Load sample", &OUAIVEItem::module, module));
	}
};

Model *modelOUAIVE = createModel<OUAIVE, OUAIVEWidget>("OUAIve");

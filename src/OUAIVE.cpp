#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "osdialog.h"
#include <vector>
#include "cmath"
#include <iomanip>
#include <sstream>
#include <mutex>
#include "dep/waves.hpp"

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
		EOC_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	bool play = false;
	int channels;
  int sampleRate;
  int totalSampleCount=0;
	float samplePos = 0.0f;
	vector<dsp::Frame<2>> playBuffer;
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
	bool first = true;
	int eoc=0;
	bool pulse = false;
	dsp::PulseGenerator eocPulse;

	OUAIVE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(TRIG_MODE_PARAM, 0.0, 2.0, 0.0);
		configParam(OUAIVE::READ_MODE_PARAM, 0.0, 2.0, 0.0);
		configParam(NB_SLICES_PARAM, 1.0, 128.01, 1.0);
		configParam(CVSLICES_PARAM, -1.0f, 1.0f, 0.0f);
		configParam(SPEED_PARAM, -0.05, 10, 1.0);
		configParam(CVSPEED_PARAM, -1.0f, 1.0f, 0.0f);

		playBuffer.resize(0);
	}

	void process(const ProcessArgs &args) override;

	void loadSample();

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
			if (!lastPath.empty()) loadSample();
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

	void onSampleRateChange() override {
		if (!lastPath.empty()) loadSample();
	}
};

void OUAIVE::loadSample() {
	APP->engine->yieldWorkers();
	mylock.lock();
	playBuffer = waves::getStereoWav(lastPath, APP->engine->getSampleRate(), waveFileName, waveExtension, channels, sampleRate, totalSampleCount);
	mylock.unlock();
	loading = false;
}

void OUAIVE::process(const ProcessArgs &args) {
	if (loading) {
		loadSample();
	}
	if (trigModeTrigger.process(params[TRIG_MODE_PARAM].getValue())) {
		trigMode = (((int)trigMode + 1) % 3);
	}
	if (inputs[READ_MODE_INPUT].isConnected()) {
		readMode = round(rescale(inputs[READ_MODE_INPUT].getVoltage(), 0.0f,10.0f,0.0f,2.0f));
	} else if (readModeTrigger.process(params[READ_MODE_PARAM].getValue() + inputs[READ_MODE_INPUT].getVoltage())) {
		readMode = (((int)readMode + 1) % 3);
	}
	nbSlices = clamp(roundl(params[NB_SLICES_PARAM].getValue() + params[CVSLICES_PARAM].getValue() * inputs[NB_SLICES_INPUT].getVoltage()), 1, 128);
	speed = clamp(params[SPEED_PARAM].getValue() + params[CVSPEED_PARAM].getValue() * inputs[SPEED_INPUT].getVoltage(), 0.2f, 10.0f);

	sliceLength = clamp(totalSampleCount / nbSlices, 1, totalSampleCount);

	if ((trigMode == 0) && (playTrigger.process(inputs[GATE_INPUT].getVoltage()))) {
		play = true;
		if (inputs[POS_INPUT].isConnected())
			samplePos = clamp(inputs[POS_INPUT].getVoltage() * (totalSampleCount-1.0f) * 0.1f, 0.0f , totalSampleCount - 1.0f);
		else {
			if (readMode != 1)
				samplePos = 0.0f;
			else
				samplePos = totalSampleCount - 1.0f;
		}
	}	else if (trigMode == 1) {
		play = (inputs[GATE_INPUT].getVoltage() > 0);
		samplePos = clamp(inputs[POS_INPUT].getVoltage() * (totalSampleCount-1.0f) * 0.1f, 0.0f , totalSampleCount - 1.0f);
	} else if ((trigMode == 2) && (playTrigger.process(inputs[GATE_INPUT].getVoltage()))) {
		play = true;
		if (inputs[POS_INPUT].isConnected())
			sliceIndex = clamp((int)(inputs[POS_INPUT].getVoltage() * nbSlices * 0.1f), 0, nbSlices);
		 else
			sliceIndex = (sliceIndex+1)%nbSlices;
		if (readMode != 1)
			samplePos = clamp(sliceIndex*sliceLength, 0, totalSampleCount-1);
		else
			samplePos = clamp((sliceIndex + 1) * sliceLength - 1, 0 , totalSampleCount-1);
	}

	if (posResetTrigger.process(inputs[POS_RESET_INPUT].getVoltage())) {
		sliceIndex = 0;
		samplePos = 0.0f;
	}

	if (play && (samplePos>=0) && (samplePos < totalSampleCount)) {
		if (channels == 1) {
			int xi = samplePos;
			float xf = samplePos - xi;
			float crossfaded = crossfade(playBuffer[xi].samples[0], playBuffer[min(xi + 1,(int)totalSampleCount-1)].samples[0], xf);
			outputs[OUTL_OUTPUT].setVoltage(5.0f * crossfaded);
			outputs[OUTR_OUTPUT].setVoltage(5.0f * crossfaded);
		}
		else if (channels == 2) {
			if (outputs[OUTL_OUTPUT].isConnected() && outputs[OUTR_OUTPUT].isConnected()) {
				int xi = samplePos;
				float xf = samplePos - xi;
				float crossfaded = crossfade(playBuffer[xi].samples[0], playBuffer[min(xi + 1,(int)totalSampleCount-1)].samples[0], xf);
				outputs[OUTL_OUTPUT].setVoltage(5.0f * crossfaded);
				crossfaded = crossfade(playBuffer[xi].samples[1], playBuffer[min(xi + 1,(int)totalSampleCount-1)].samples[1], xf);
				outputs[OUTR_OUTPUT].setVoltage(5.0f * crossfaded);
			}
			else {
				int xi = samplePos;
				float xf = samplePos - xi;
				float crossfaded = crossfade(0.5f*(playBuffer[xi].samples[0]+playBuffer[xi].samples[1]), 0.5f*(playBuffer[min(xi + 1,(int)totalSampleCount-1)].samples[0]+playBuffer[min(xi + 1,(int)totalSampleCount-1)].samples[1]), xf);
				outputs[OUTL_OUTPUT].setVoltage(5.0f * crossfaded);
				outputs[OUTR_OUTPUT].setVoltage(5.0f * crossfaded);
			}
		}

		if (trigMode == 0) {
			if (readMode != 1)
				samplePos = samplePos + speed;
			else
				samplePos = samplePos - speed;
			//manage eof readMode
			if ((readMode == 0) && (samplePos >= totalSampleCount))
					play = false;
			else if ((readMode == 1) && (samplePos <=0))
					play = false;
			else if ((readMode == 2) && (samplePos >= totalSampleCount))
				samplePos = clamp(inputs[POS_INPUT].getVoltage() * (totalSampleCount-1.0f) * 0.1f , 0.0f ,totalSampleCount - 1.0f);
		}
		else if (trigMode == 2)
		{
			if (readMode != 1)
				samplePos = samplePos + speed;
			else
				samplePos = samplePos - speed;

			//manage eof readMode
			if ((readMode == 0) && ((samplePos >= (sliceIndex+1) * sliceLength) || (samplePos >= totalSampleCount)))
					play = false;
			if ((readMode == 1) && ((samplePos <= (sliceIndex) * sliceLength) || (samplePos <= 0)))
					play = false;
			if ((readMode == 2) && ((samplePos >= (sliceIndex+1) * sliceLength) || (samplePos >= totalSampleCount)))
				samplePos = clamp(sliceIndex*sliceLength, 0 , totalSampleCount);
		}
	}
	else if (samplePos == totalSampleCount) {
		play = false;
	}

	if ((eoc == 0) && (play)) {
		eoc = 1;
	}

	if ((eoc == 1) && (!play)) {
		eoc = 0;
		eocPulse.reset();
		eocPulse.trigger(1e-3f);
	}

	pulse = eocPulse.process(args.sampleTime);

	outputs[EOC_OUTPUT].setVoltage(pulse ? 10 : 0);

}

struct OUAIVEDisplay : OpaqueWidget {
	OUAIVE *module;
	const float width = 125.0f;
	const float height = 50.0f;
	float zoomWidth = 125.0f;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refX = 0.0f;

	OUAIVEDisplay() {

	}

  void onDragStart(const event::DragStart &e) override {
		APP->window->cursorLock();
		OpaqueWidget::onDragStart(e);
	}

  void onDragMove(const event::DragMove &e) override {
		float zoom = 1.0f;
    if (e.mouseDelta.y > 0.0f) {
      zoom = 1.0f/(((APP->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 2.0f : 1.1f);
    }
    else if (e.mouseDelta.y < 0.0f) {
      zoom = ((APP->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 2.0f : 1.1f;
    }
    zoomWidth = clamp(zoomWidth*zoom,width,zoomWidth*((APP->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT) ? 2.0f : 1.1f));
    zoomLeftAnchor = clamp(refX - (refX - zoomLeftAnchor)*zoom + e.mouseDelta.x, width - zoomWidth,0.0f);
		OpaqueWidget::onDragMove(e);
	}

  void onDragEnd(const event::DragEnd &e) override {
    APP->window->cursorUnlock();
    OpaqueWidget::onDragEnd(e);
  }

	void draw(const DrawArgs &args) override {
		nvgGlobalTint(args.vg, color::WHITE);
    if (module && (module->playBuffer.size()>0)) {
      module->mylock.lock();
  		std::vector<float> vL;
  		std::vector<float> vR;
			for (int i=0;i<module->totalSampleCount;i++) {
				vL.push_back(module->playBuffer[i].samples[0]);
				vR.push_back(module->playBuffer[i].samples[1]);
			}
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
  		if ((module->play) && (nbSample>0)) {
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

  		if ((!module->loading) && (nbSample>0)) {
  			//Draw waveform
  			nvgStrokeColor(args.vg, PINK_BIDOO);
  			nvgSave(args.vg);
  			Rect b = Rect(Vec(zoomLeftAnchor, 0), Vec(zoomWidth, height));
				size_t inc = std::max(vL.size()/zoomWidth/4,1.f);
  			nvgScissor(args.vg, 0, b.pos.y, width, height);
  			nvgBeginPath(args.vg);
  			for (size_t i = 0; i < vL.size(); i+=inc) {
  				float x, y;
  				x = (float)i/vL.size();
  				y = (-1.f)*vL[i] / 2.0f + 0.5f;
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
  			for (size_t i = 0; i < vR.size(); i+=inc) {
  				float x, y;
  				x = (float)i/vR.size();
  				y = (-1.f)*vR[i] / 2.0f + 0.5f;
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
		addOutput(createOutput<TinyPJ301MPort>(Vec(112, 18), module, OUAIVE::EOC_OUTPUT));

		addParam(createParam<BlueCKD6>(Vec(portX0[0]-25, 215), module, OUAIVE::TRIG_MODE_PARAM));

		addParam(createParam<BlueCKD6>(Vec(portX0[1]-14, 215), module, OUAIVE::READ_MODE_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(portX0[2]+5, 222), module, OUAIVE::READ_MODE_INPUT));

		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1]-9, 250), module, OUAIVE::NB_SLICES_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1]+15, 250), module, OUAIVE::CVSLICES_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(portX0[2]+5, 252), module, OUAIVE::NB_SLICES_INPUT));

		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1]-9, 275), module, OUAIVE::SPEED_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1]+15, 275), module, OUAIVE::CVSPEED_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(portX0[2]+5, 277), module, OUAIVE::SPEED_INPUT));

		addInput(createInput<PJ301MPort>(Vec(7, 330), module, OUAIVE::GATE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(40, 330), module, OUAIVE::POS_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(portX0[2]-11, 340), module, OUAIVE::OUTL_OUTPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(portX0[2]+11, 340), module, OUAIVE::OUTR_OUTPUT));
	}

  struct OUAIVEItem : MenuItem {
  	OUAIVE *module;
  	void onAction(const event::Action &e) override {

  		std::string dir = module->lastPath.empty() ? asset::user("") : rack::system::getDirectory(module->lastPath);
  		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
  		if (path) {
  			module->samplePos = 0;
  			module->lastPath = path;
  			module->sliceIndex = -1;
				module->loading=true;
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

	void onPathDrop(const PathDropEvent& e) override {
		Widget::onPathDrop(e);
		OUAIVE *module = dynamic_cast<OUAIVE*>(this->module);
		module->samplePos = 0;
		module->lastPath = e.paths[0];
		module->sliceIndex = -1;
		module->loading=true;
	}
};

Model *modelOUAIVE = createModel<OUAIVE, OUAIVEWidget>("OUAIve");

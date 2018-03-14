#include "Bidoo.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "osdialog.h"
#include "dep/audiofile/AudioFile.h"
#include <vector>
#include "cmath"
#include <iomanip> // setprecision
#include <sstream> // stringstream
#include <algorithm>

using namespace std;


struct CANARD : Module {
	enum ParamIds {
		RECORD_PARAM,
		SAMPLE_START_PARAM,
		LOOP_LENGTH_PARAM,
		READ_MODE_PARAM,
		SPEED_PARAM,
		FADE_PARAM,
		MODE_PARAM,
		SLICE_PARAM,
		CLEAR_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		INL_INPUT,
		INR_INPUT,
		TRIG_INPUT,
		GATE_INPUT,
		SAMPLE_START_INPUT,
		LOOP_LENGTH_INPUT,
		READ_MODE_INPUT,
		SPEED_INPUT,
		RECORD_INPUT,
		FADE_INPUT,
		SLICE_INPUT,
		CLEAR_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTL_OUTPUT,
		OUTR_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		REC_LIGHT,
		NUM_LIGHTS
	};

	bool play = false;
	bool record = false;
	AudioFile<float> playBuffer, recordBuffer;
	float samplePos = 0.0f, sampleStart = 0.0f, loopLength = 0.0f, fadeLenght = 0.0f, fadeCoeff = 1.0f;
	vector<float> displayBuffL;
	vector<float> displayBuffR;
	int readMode = 0; // 0 formward, 1 backward, 2 repeat
	float speed;
	std::vector<int> slices;
	int selected = -1;
	bool deleteFlag = false;

	SchmittTrigger trigTrigger;
	SchmittTrigger recordTrigger;
	SchmittTrigger clearTrigger;


	CANARD() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
		recordBuffer.setBitDepth(16);
		recordBuffer.setSampleRate(engineGetSampleRate());
		recordBuffer.setNumChannels(2);
		recordBuffer.samples[0].resize(0);
		recordBuffer.samples[1].resize(0);
		playBuffer.setBitDepth(16);
		playBuffer.setSampleRate(engineGetSampleRate());
		playBuffer.setNumChannels(2);
		playBuffer.samples[0].resize(0);
		playBuffer.samples[1].resize(0);
	}

	void step() override;

	void displaySample();

	// persistence

	json_t *toJson() override {
		json_t *rootJ = json_object();
		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
	}
};

void CANARD::displaySample() {
		vector<float>().swap(displayBuffL);
		vector<float>().swap(displayBuffR);
		for (int i=0; i < playBuffer.getNumSamplesPerChannel(); i = i + floor(playBuffer.getNumSamplesPerChannel()/170)) {
			displayBuffL.push_back(playBuffer.samples[0][i]);
			displayBuffR.push_back(playBuffer.samples[1][i]);
		}
}

void CANARD::step() {
	if (clearTrigger.process(inputs[CLEAR_INPUT].value + params[CLEAR_PARAM].value))
	{
		playBuffer.samples[0].clear();
		playBuffer.samples[1].clear();
		displaySample();
		slices.clear();
	}

	if ((selected>=0) && (deleteFlag)) {
		int nbSample=0;
		if ((size_t)selected<(slices.size()-1)) {
			nbSample = slices[selected + 1] - slices[selected] - 1;
			playBuffer.samples[0].erase(playBuffer.samples[0].begin() + slices[selected], playBuffer.samples[0].begin() + slices[selected + 1]-1);
			playBuffer.samples[1].erase(playBuffer.samples[1].begin() + slices[selected], playBuffer.samples[1].begin() + slices[selected + 1]-1);
		}
		else {
			nbSample = playBuffer.getNumSamplesPerChannel() - slices[selected];
			playBuffer.samples[0].erase(playBuffer.samples[0].begin() + slices[selected], playBuffer.samples[0].end());
			playBuffer.samples[1].erase(playBuffer.samples[1].begin() + slices[selected], playBuffer.samples[1].end());
		}
		slices.erase(slices.begin()+selected);
		for (size_t i = selected; i < slices.size(); i++)
		{
			slices[i] = slices[i]-nbSample;
		}
		displaySample();
		selected = -1;
		deleteFlag = false;
	}

	if (recordTrigger.process(inputs[RECORD_INPUT].value + params[RECORD_PARAM].value))
	{
		lights[REC_LIGHT].value = 10.0f;
		if(record) {
			if (floor(params[MODE_PARAM].value) == 0) {
				slices.clear();
				slices.push_back(0);
				playBuffer.setAudioBuffer(recordBuffer.samples);
			}
			else {
				slices.push_back(playBuffer.getNumSamplesPerChannel() > 0 ? (playBuffer.getNumSamplesPerChannel()-1) : 0);
				playBuffer.samples[0].insert(playBuffer.samples[0].end(), recordBuffer.samples[0].begin(), recordBuffer.samples[0].end());
				playBuffer.samples[1].insert(playBuffer.samples[1].end(), recordBuffer.samples[1].begin(), recordBuffer.samples[1].end());
			}
			recordBuffer.samples[0].resize(0);
			recordBuffer.samples[1].resize(0);
			displaySample();
			lights[REC_LIGHT].value = 0.0f;
		}
		record = !record;
	}

	if (record) {
		recordBuffer.samples[0].push_back(inputs[INL_INPUT].value/10);
		recordBuffer.samples[1].push_back(inputs[INR_INPUT].value/10);
	}

	int sliceStart = 0;;
	int sliceEnd = playBuffer.getNumSamplesPerChannel() > 0 ? playBuffer.getNumSamplesPerChannel() - 1 : 0;
	if ((params[MODE_PARAM].value == 1) && (slices.size()>0))
	{
		size_t index = round(clamp(params[SLICE_PARAM].value + inputs[SLICE_INPUT].value, 0.0f,10.0f)*(slices.size()-1)/10);
		sliceStart = slices[index] == 0 ? 0 : slices[index];
		sliceEnd = (index < (slices.size() - 1)) ? (slices[index+1] - 1) : (playBuffer.getNumSamplesPerChannel() - 1);
	}

	if (playBuffer.getNumSamplesPerChannel() > 0) {
		loopLength = rescale(clamp(inputs[LOOP_LENGTH_INPUT].value + params[LOOP_LENGTH_PARAM].value, 0.0f, 10.0f), 0.0f, 10.0f, 0.0f, (sliceEnd - sliceStart) > 0.0f ? (sliceEnd - sliceStart) : 0.0f);
		sampleStart = rescale(clamp(inputs[SAMPLE_START_INPUT].value + params[SAMPLE_START_PARAM].value, 0.0f, 10.0f), 0.0f, 10.0f, sliceStart, sliceEnd - loopLength);
		fadeLenght = rescale(clamp(inputs[FADE_INPUT].value + params[FADE_PARAM].value, 0.0f, 10.0f), 0.0f, 10.0f,0.0f, floor(loopLength/2));
	}
	else {
		loopLength = 0;
		sampleStart = 0;
		fadeLenght = 0;
	}

	if (trigTrigger.process(inputs[TRIG_INPUT].value))
	{
		if ((inputs[SPEED_INPUT].value + params[SPEED_PARAM].value)>0)
			samplePos = sampleStart;
		else
			samplePos = sampleStart + loopLength;
	}

	if (((inputs[GATE_INPUT].active) && (inputs[GATE_INPUT].value>0)) || (inputs[TRIG_INPUT].active)) {
		if (samplePos<playBuffer.getNumSamplesPerChannel()) {
			if (fadeLenght>1000) {
				if ((samplePos-sampleStart)<fadeLenght)
					fadeCoeff = rescale(samplePos-sampleStart,0.0f,fadeLenght,0.0f,1.0f);
				else if ((sampleStart+loopLength-samplePos)<fadeLenght)
					fadeCoeff = rescale(sampleStart+loopLength-samplePos,fadeLenght,0.0f,1.0f,0.0f);
				else
					fadeCoeff = 1.0f;
			}
			else
				fadeCoeff = 1.0f;

			outputs[OUTL_OUTPUT].value = playBuffer.samples[0][floor(samplePos)]*fadeCoeff;
			outputs[OUTR_OUTPUT].value = playBuffer.samples[1][floor(samplePos)]*fadeCoeff;
		}
		if ((samplePos == (sampleStart+loopLength)) && ((inputs[SPEED_INPUT].value + params[SPEED_PARAM].value)>0))
			samplePos = sampleStart;
		else if ((samplePos == sampleStart) && ((inputs[SPEED_INPUT].value + params[SPEED_PARAM].value)<0))
			samplePos = sampleStart + loopLength;
	  else
		  samplePos =samplePos + inputs[SPEED_INPUT].value + params[SPEED_PARAM].value;

		samplePos = clamp(samplePos,sampleStart,sampleStart+loopLength);
	}
	else {
		outputs[OUTL_OUTPUT].value = 0.0f;
		outputs[OUTR_OUTPUT].value = 0.0f;
	}
}

struct CANARDDisplay : TransparentWidget {
	CANARD *module;
	int frame = 0;
	shared_ptr<Font> font;
	string displayParams;
	const int width = 175;
	const int height = 50;

	CANARDDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void onMouseDown(EventMouseDown &e) override {
		if (module->slices.size()>0) {
			int refElem = clamp((e.pos.x - this->box.pos.x)/(float)width,0.0f,1.0f)*(module->playBuffer.getNumSamplesPerChannel()-1);
	    auto i = min_element(begin(module->slices), end(module->slices), [=] (int x, int y)
	    {
	        return abs(x - refElem) < abs(y - refElem);
	    });
			module->selected = distance(begin(module->slices), i);
		}
		TransparentWidget::onMouseDown(e);
	}

	void draw(NVGcontext *vg) override {
		nvgFontSize(vg, 12);
		nvgFontFaceId(vg, font->handle);
		nvgStrokeWidth(vg, 1);
		nvgTextLetterSpacing(vg, -2);
		nvgFillColor(vg, YELLOW_BIDOO);

		// Draw play line
		nvgStrokeColor(vg, LIGHTBLUE_BIDOO);
		{
			nvgBeginPath(vg);
			nvgStrokeWidth(vg, 2);
			if (module->playBuffer.getNumSamplesPerChannel()>0) {
				nvgMoveTo(vg, (int)(module->samplePos * width / module->playBuffer.getNumSamplesPerChannel()) , 0);
				nvgLineTo(vg, (int)(module->samplePos * width / module->playBuffer.getNumSamplesPerChannel()) , 2*height+10);
			}
			else {
				nvgMoveTo(vg, 0, 0);
				nvgLineTo(vg, 0, 2*height+10);
			}

			nvgClosePath(vg);
		}
		nvgStroke(vg);


		// Draw ref line
		nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
		{
			nvgBeginPath(vg);
			nvgMoveTo(vg, 0, height/2);
			nvgLineTo(vg, width, height/2);
			nvgMoveTo(vg, 0, 3*height/2+10);
			nvgLineTo(vg, width, 3*height/2+10);
			nvgClosePath(vg);
		}
		nvgStroke(vg);

		// Draw loop
		nvgFillColor(vg, nvgRGBA(255, 255, 255, 40));
		{
			nvgBeginPath(vg);
			nvgMoveTo(vg, (int)((module->sampleStart + module->fadeLenght) * width / module->playBuffer.getNumSamplesPerChannel()) , 0);
			nvgLineTo(vg, (int)(module->sampleStart * width / module->playBuffer.getNumSamplesPerChannel()) , 2*height+10);
			nvgLineTo(vg, (int)((module->sampleStart + module->loopLength) * width / module->playBuffer.getNumSamplesPerChannel()) , 2*height+10);
			nvgLineTo(vg, (int)((module->sampleStart + module->loopLength - module->fadeLenght) * width / module->playBuffer.getNumSamplesPerChannel()) , 0);
			nvgLineTo(vg, (int)((module->sampleStart + module->fadeLenght) * width / module->playBuffer.getNumSamplesPerChannel()) , 0);
			nvgClosePath(vg);
		}
		nvgFill(vg);

		//draw slices
		if (floor(module->params[CANARD::MODE_PARAM].value) == 1) {
			for (size_t i = 1; i < module->slices.size(); i++) {
				nvgStrokeColor(vg, YELLOW_BIDOO);
				{
					nvgBeginPath(vg);
					nvgStrokeWidth(vg, 1);
					nvgMoveTo(vg, module->slices[i] * width / module->playBuffer.getNumSamplesPerChannel() , 0);
					nvgLineTo(vg, module->slices[i] * width / module->playBuffer.getNumSamplesPerChannel() , 2*height+10);
					nvgClosePath(vg);
				}
				nvgStroke(vg);
			}
		}

		//draw selected
		if ((module->selected >= 0) && ((size_t)module->selected < module->slices.size())) {
				nvgStrokeColor(vg, RED_BIDOO);
				{
					nvgBeginPath(vg);
					nvgStrokeWidth(vg, 4);
					nvgMoveTo(vg, module->slices[module->selected] * width / module->playBuffer.getNumSamplesPerChannel() , 2*height+9);
					if ((size_t)module->selected < (module->slices.size()-1))
						nvgLineTo(vg, module->slices[module->selected+1] * width / module->playBuffer.getNumSamplesPerChannel() , 2*height+9);
					else
						nvgLineTo(vg, width, 2*height+9);
					nvgClosePath(vg);
				}
				nvgStroke(vg);
		}

		// Draw waveform
		if (module->displayBuffL.size()>0) {
			nvgStrokeColor(vg, PINK_BIDOO);
			nvgSave(vg);
			Rect b = Rect(Vec(0, 0), Vec(width, height));
			nvgScissor(vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
			nvgBeginPath(vg);
			for (unsigned int i = 0; i < module->displayBuffL.size(); i++) {
				float x, y;
				x = (float)i / (module->displayBuffL.size());
				y = module->displayBuffL[i] / 2.0f + 0.5f;
				Vec p;
				p.x = b.pos.x + b.size.x * x;
				p.y = b.pos.y + b.size.y * (1.0f - y);
				if (i == 0)
					nvgMoveTo(vg, p.x, p.y);
				else
					nvgLineTo(vg, p.x, p.y);
			}
			nvgLineCap(vg, NVG_ROUND);
			nvgMiterLimit(vg, 2.0);
			nvgStrokeWidth(vg, 1);
			nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
			nvgStroke(vg);

			b = Rect(Vec(0, height+10), Vec(width, height));
			nvgScissor(vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
			nvgBeginPath(vg);
			for (unsigned int i = 0; i < module->displayBuffR.size(); i++) {
				float x, y;
				x = (float)i / (module->displayBuffR.size());
				y = module->displayBuffR[i] / 2.0f + 0.5f;
				Vec p;
				p.x = b.pos.x + b.size.x * x;
				p.y = b.pos.y + b.size.y * (1.0f - y);
				if (i == 0)
					nvgMoveTo(vg, p.x, p.y);
				else
					nvgLineTo(vg, p.x, p.y);
			}
			nvgLineCap(vg, NVG_ROUND);
			nvgMiterLimit(vg, 2.0f);
			nvgStrokeWidth(vg, 1);
			nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
			nvgStroke(vg);
			nvgResetScissor(vg);
			nvgRestore(vg);
		}
	}
};

struct CANARDWidget : ModuleWidget {
	CANARDWidget(CANARD *module);
	Menu *createContextMenu() override;
};

CANARDWidget::CANARDWidget(CANARD *module) : ModuleWidget(module) {
		setPanel(SVG::load(assetPlugin(plugin, "res/CANARD.svg")));

		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		{
			CANARDDisplay *display = new CANARDDisplay();
			display->module = module;
			display->box.pos = Vec(10, 35);
			display->box.size = Vec(175, 150);
			addChild(display);
		}

		static const float portX0[4] = {32, 71, 110, 149};

		addChild(ModuleLightWidget::create<SmallLight<RedLight>>(Vec(portX0[0]-10, 158), module, CANARD::REC_LIGHT));
		addParam(ParamWidget::create<BlueCKD6>(Vec(portX0[0]-6, 170), module, CANARD::RECORD_PARAM, 0.0f, 1.0f, 0.0f));

		addParam(ParamWidget::create<BidooBlueKnob>(Vec(portX0[2]-7, 170), module, CANARD::SAMPLE_START_PARAM, 0.0f, 10.0f, 0.0f));
		addParam(ParamWidget::create<BidooBlueKnob>(Vec(portX0[3]-7, 170), module, CANARD::LOOP_LENGTH_PARAM, 0.0f, 10.0f, 10.0f));

		addInput(Port::create<PJ301MPort>(Vec(portX0[0]-4, 202), Port::INPUT, module, CANARD::RECORD_INPUT));
		addInput(Port::create<PJ301MPort>(Vec(portX0[1]-4, 202), Port::INPUT, module, CANARD::TRIG_INPUT));
		addInput(Port::create<PJ301MPort>(Vec(portX0[1]-4, 172), Port::INPUT, module, CANARD::GATE_INPUT));
		addInput(Port::create<PJ301MPort>(Vec(portX0[2]-4, 202), Port::INPUT, module, CANARD::SAMPLE_START_INPUT));
		addInput(Port::create<PJ301MPort>(Vec(portX0[3]-4, 202), Port::INPUT, module, CANARD::LOOP_LENGTH_INPUT));

		addParam(ParamWidget::create<BidooBlueKnob>(Vec(portX0[0]-7, 245), module, CANARD::SPEED_PARAM, -10.0f, 10.0f, 1.0f));
		addParam(ParamWidget::create<BidooBlueKnob>(Vec(portX0[1]-7, 245), module, CANARD::FADE_PARAM, 0.0f, 10.0f, 0.0f));
		addParam(ParamWidget::create<BidooBlueKnob>(Vec(portX0[2]-7, 245), module, CANARD::SLICE_PARAM, 0.0f, 10.0f, 0.0f));
		addParam(ParamWidget::create<BlueCKD6>(Vec(portX0[3]-7, 245), module, CANARD::CLEAR_PARAM, 0.0f, 1.0f, 0.0f));

		addInput(Port::create<PJ301MPort>(Vec(portX0[0]-4, 277), Port::INPUT, module, CANARD::SPEED_INPUT));
		addInput(Port::create<PJ301MPort>(Vec(portX0[1]-4, 277), Port::INPUT, module, CANARD::FADE_INPUT));
		addInput(Port::create<PJ301MPort>(Vec(portX0[2]-4, 277), Port::INPUT, module, CANARD::SLICE_INPUT));
		addInput(Port::create<PJ301MPort>(Vec(portX0[3]-4, 277), Port::INPUT, module, CANARD::CLEAR_INPUT));

		addParam(ParamWidget::create<CKSS>(Vec(90, 320), module, CANARD::MODE_PARAM, 0.0f, 1.0f, 0.0f));

		addInput(Port::create<TinyPJ301MPort>(Vec(19, 331), Port::INPUT, module, CANARD::INL_INPUT));
		addInput(Port::create<TinyPJ301MPort>(Vec(19+24, 331), Port::INPUT, module, CANARD::INR_INPUT));
		addOutput(Port::create<TinyPJ301MPort>(Vec(138, 331), Port::OUTPUT, module, CANARD::OUTL_OUTPUT));
		addOutput(Port::create<TinyPJ301MPort>(Vec(138+24, 331), Port::OUTPUT, module, CANARD::OUTR_OUTPUT));
}

struct CANARDDeleteSlice : MenuItem {
	CANARDWidget *canardWidget;
	CANARD *canardModule;
	void onAction(EventAction &e) override {
		canardModule->deleteFlag = true;
	}
};

Menu *CANARDWidget::createContextMenu() {
	CANARDWidget *canardWidget = dynamic_cast<CANARDWidget*>(this);
	assert(canardWidget);

	CANARD *canardModule = dynamic_cast<CANARD*>(module);
	assert(canardModule);

	Menu *menu = gScene->createMenu();

	if (canardModule->selected>=0) {
		CANARDDeleteSlice *deleteItem = new CANARDDeleteSlice();
		deleteItem->text = "Delete slice";
		deleteItem->canardWidget = this;
		deleteItem->canardModule = canardModule;
		menu->addChild(deleteItem);
	}

	return menu;
}

Model *modelCANARD = Model::create<CANARD, CANARDWidget>("Bidoo","cANARd", "cANARd sampler", SAMPLER_TAG);

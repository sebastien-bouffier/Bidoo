#include "Bidoo.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "../ext/osdialog/osdialog.h"
#include "AudioFile.h"
#include <vector>
#include "cmath"

using namespace std;


struct OUAIVE : Module {
	enum ParamIds {
		PLAY_PARAM,
		TRIG_MODE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		GATE_INPUT,
		POS_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	bool play = false;
	string lastPath;
	AudioFile<double> audioFile;
	int samplePos = 0;
	vector<double> displayBuff;
	string fileDesc;
	bool fileLoaded = false;
	int trigMode = 0; // 0 trig 1 gate

	SchmittTrigger playTrigger;
	SchmittTrigger trigModeTrigger;

	OUAIVE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) { }

	void step() override;

	void loadSample(std::string path);

	// persistence

	json_t *toJson() override {
		json_t *rootJ = json_object();
		// lastPath
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		// lastPath
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (lastPathJ) {
			lastPath = json_string_value(lastPathJ);
			loadSample(lastPath);
		}
	}
};

void OUAIVE::loadSample(std::string path) {
	if (audioFile.load (path.c_str())) {
		fileLoaded = true;
		vector<double>().swap(displayBuff);
		for (int i=0; i < audioFile.getNumSamplesPerChannel(); i = i + floor(audioFile.getNumSamplesPerChannel()/125)) {
			displayBuff.push_back(audioFile.samples[0][i]);
		}
		fileDesc = extractFilename(path)+ "\n";
		fileDesc += std::to_string(audioFile.getSampleRate()) + " Hz " + std::to_string(audioFile.getBitDepth()) + " bit\n";
		fileDesc += std::to_string(roundf(audioFile.getLengthInSeconds() * 100) / 100) + " s\n";
	}
	else {
		fileLoaded = false;
	}
}


void OUAIVE::step() {
	// Play
	if (fileLoaded) {
		if (trigModeTrigger.process(params[TRIG_MODE_PARAM].value)) {
			trigMode = (((int)trigMode + 1) % 2);
		}

		if ((trigMode == 0) && (playTrigger.process(params[PLAY_PARAM].value + inputs[GATE_INPUT].value))) {
			play = true;
			samplePos = clampi((int)(inputs[POS_INPUT].value*audioFile.getNumSamplesPerChannel()/10), 0 , audioFile.getNumSamplesPerChannel() -1);
		}	else if (trigMode == 1) {
			play = (inputs[GATE_INPUT].value > 0);
			samplePos = clampi((int)(inputs[POS_INPUT].value*audioFile.getNumSamplesPerChannel()/10), 0 , audioFile.getNumSamplesPerChannel() -1);
		}

		if ((play) && (samplePos < audioFile.getNumSamplesPerChannel())) {
			if (audioFile.getNumChannels() == 1)
				outputs[OUT_OUTPUT].value = 5 * audioFile.samples[0][samplePos];
			else if (audioFile.getNumChannels() ==2)
				outputs[OUT_OUTPUT].value = 5 * (audioFile.samples[0][samplePos] + audioFile.samples[1][samplePos]) / 2;
			samplePos++;
		}
		else if (samplePos == audioFile.getNumSamplesPerChannel())
		{
			play = false;
		}
	}
}

struct OUAIVEDisplay : TransparentWidget {
	OUAIVE *module;
	int frame = 0;
	shared_ptr<Font> font;

	OUAIVEDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void draw(NVGcontext *vg) override {
		nvgFontSize(vg, 12);
		nvgFontFaceId(vg, font->handle);
		nvgTextLetterSpacing(vg, -2);
		nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
		nvgTextBox(vg, 5, 5,120, module->fileDesc.c_str(), NULL);

		nvgFontSize(vg, 14);
		nvgFontFaceId(vg, font->handle);
		nvgTextLetterSpacing(vg, -2);
		nvgFillColor(vg, nvgRGBA(75, 199, 75, 0xff));
		nvgTextBox(vg, 5, 43,120, module->trigMode == 0 ? "TRIG MODE" : "GATE MODE", NULL);

		// Draw ref line
		nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
		{
			nvgBeginPath(vg);
			nvgMoveTo(vg, 0, 125);
			nvgLineTo(vg, 130, 125);
			nvgClosePath(vg);
		}
		nvgStroke(vg);

		if (module->fileLoaded) {
			// Draw play line
			nvgStrokeColor(vg, nvgRGBA(0x28, 0xb0, 0xf3, 0xc0));
			{
				nvgBeginPath(vg);
				nvgMoveTo(vg, (int)(module->samplePos * 125 / module->audioFile.getNumSamplesPerChannel()) , 85);
				nvgLineTo(vg, (int)(module->samplePos * 125 / module->audioFile.getNumSamplesPerChannel()) , 165);
				nvgClosePath(vg);
			}
			nvgStroke(vg);

			// Draw waveform
			nvgStrokeColor(vg, nvgRGBA(0xe1, 0x02, 0x78, 0xc0));
			nvgSave(vg);
			Rect b = Rect(Vec(0, 85), Vec(125, 80));
			nvgScissor(vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
			nvgBeginPath(vg);
			for (unsigned int i = 0; i < module->displayBuff.size(); i++) {
				float x, y;
				x = (float)i / (module->displayBuff.size() - 1);
				y = module->displayBuff[i] / 2.0 + 0.5;
				Vec p;
				p.x = b.pos.x + b.size.x * x;
				p.y = b.pos.y + b.size.y * (1.0 - y);
				if (i == 0)
					nvgMoveTo(vg, p.x, p.y);
				else
					nvgLineTo(vg, p.x, p.y);
			}
			nvgLineCap(vg, NVG_ROUND);
			nvgMiterLimit(vg, 2.0);
			nvgStrokeWidth(vg, 1.5);
			nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
			nvgStroke(vg);
			nvgResetScissor(vg);
			nvgRestore(vg);
		}
	}
};

OUAIVEWidget::OUAIVEWidget() {
	OUAIVE *module = new OUAIVE();
	setModule(module);
	box.size = Vec(15*9, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/OUAIve.svg")));
		addChild(panel);
	}

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));

	{
		OUAIVEDisplay *display = new OUAIVEDisplay();
		display->module = module;
		display->box.pos = Vec(5, 40);
		display->box.size = Vec(130, 250);
		addChild(display);
	}

	static const float portX0[4] = {34, 67, 101};

	addParam(createParam<CKD6>(Vec(portX0[1]-15, 225), module, OUAIVE::PLAY_PARAM, 0.0, 4.0, 0.0));
	addParam(createParam<CKD6>(Vec(portX0[1]-15, 260), module, OUAIVE::TRIG_MODE_PARAM, 0.0, 1.0, 0.0));

	addInput(createInput<PJ301MPort>(Vec(portX0[0]-22, 321), module, OUAIVE::GATE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1]-11, 321), module, OUAIVE::POS_INPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[2]-2, 321), module, OUAIVE::OUT_OUTPUT));
}

struct OUAIVEItem : MenuItem {
	OUAIVE *ouaive;
	void onAction(EventAction &e) override {

		std::string dir = ouaive->lastPath.empty() ? assetLocal("") : extractDirectory(ouaive->lastPath);
		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
		if (path) {
			ouaive->play = false;
			ouaive->fileLoaded = false;
			ouaive->loadSample(path);
			ouaive->samplePos = 0;
			ouaive->lastPath = path;
			free(path);
		}
	}
};

Menu *OUAIVEWidget::createContextMenu() {
	Menu *menu = ModuleWidget::createContextMenu();

	MenuLabel *spacerLabel = new MenuLabel();
	menu->addChild(spacerLabel);

	OUAIVE *ouaive = dynamic_cast<OUAIVE*>(module);
	assert(ouaive);

	OUAIVEItem *sampleItem = new OUAIVEItem();
	sampleItem->text = "Load sample";
	sampleItem->ouaive = ouaive;
	menu->addChild(sampleItem);

	return menu;
}

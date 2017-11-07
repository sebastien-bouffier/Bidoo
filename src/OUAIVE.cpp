#include "Bidoo.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "../ext/osdialog/osdialog.h"
#include "AudioFile.h"
#include <vector>

using namespace std;


struct OUAIVE : Module {
	enum ParamIds {
		PLAY_PARAM,
		NUM_PARAMS 
	};
	enum InputIds {
		TRIG_INPUT,
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
	string fileName;
	string lastPath;
	AudioFile<double> audioFile;
	int samplePos = 0;
	vector<double> displayBuff;
	         

	SchmittTrigger playTrigger;

	OUAIVE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
		if (!fileName.empty()) {
			loadSample(fileName);
		}
	}

	void step() override;
	
	void loadSample(std::string path);
	
	// persistence
	
	json_t *toJson() override {
		json_t *rootJ = json_object();

		// fileName
		json_object_set_new(rootJ, "fileName", json_string(fileName.c_str()));
		
		// lastPath
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
		
		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		// fileName
		json_t *fileNameJ = json_object_get(rootJ, "fileName");
		if (fileNameJ) {
			fileName = json_string_value(fileNameJ);
		}
		
		// lastPath
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (lastPathJ) {
			lastPath = json_string_value(lastPathJ);
			loadSample(lastPath);
		}
	}
};

void OUAIVE::loadSample(std::string path) {
	audioFile.load (path.c_str());
}


void OUAIVE::step() {
 	
	// Play
	if (playTrigger.process(params[PLAY_PARAM].value + inputs[TRIG_INPUT].value)) {
		play = !play;
		samplePos = 0;
	}
	
	if ((play) && (samplePos < audioFile.getNumSamplesPerChannel())) {
		outputs[OUT_OUTPUT].value = 10 * audioFile.samples[0][samplePos]; // to do stereo management
		
		vector<double>().swap(displayBuff);
		for (int i=samplePos; i < clampi(samplePos + 100, samplePos + 100, audioFile.getNumSamplesPerChannel()); i++) {
			displayBuff.push_back(audioFile.samples[0][i]);
		}
		
		samplePos++;
	}
	else if (samplePos == audioFile.getNumSamplesPerChannel())
	{ 
		play = false;
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
		if (++frame >= 4) {
			frame = 0;
		}
		nvgFontSize(vg, 12);
		nvgFontFaceId(vg, font->handle);
		nvgTextLetterSpacing(vg, -2);
		nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));	
		nvgText(vg, 5, 5,  module->fileName.c_str(), NULL);
		nvgText(vg, 5, 15, std::to_string(module->audioFile.getSampleRate()).c_str(), NULL);
		nvgText(vg, 5, 25, std::to_string(module->audioFile.getBitDepth()).c_str(), NULL);
		nvgText(vg, 5, 35, std::to_string(module->audioFile.getNumSamplesPerChannel()).c_str(), NULL);
		nvgText(vg, 5, 45, std::to_string(module->audioFile.getLengthInSeconds()).c_str(), NULL);
		nvgText(vg, 5, 55, std::to_string(module->audioFile.getNumChannels()).c_str(), NULL);	
		nvgText(vg, 5, 65, std::to_string(module->audioFile.isMono()).c_str(), NULL);	
		nvgText(vg, 5, 75, std::to_string(module->audioFile.isStereo()).c_str(), NULL);	
		nvgText(vg, 5, 85, std::to_string(module->outputs[0].value).c_str(), NULL);	
		
		// Draw line
		nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
		{
			nvgBeginPath(vg);
			nvgMoveTo(vg, 0, 125);
			nvgLineTo(vg, 130, 125);
			nvgClosePath(vg);
		}
		nvgStroke(vg);
		
		nvgStrokeColor(vg, nvgRGBA(0xe1, 0x02, 0x78, 0xc0));
		nvgSave(vg);
		Rect b = Rect(Vec(0, 85), Vec(130, 80));
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
		
	static const float portX0[4] = {20, 58, 96, 135};
	
	addParam(createParam<CKD6>(Vec(portX0[1]-5, 244), module, OUAIVE::PLAY_PARAM, 0.0, 4.0, 0.0));
		
	addInput(createInput<PJ301MPort>(Vec(portX0[1]-21, 321), module, OUAIVE::TRIG_INPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[2]-21, 321), module, OUAIVE::OUT_OUTPUT));
}

struct OUAIVEItem : MenuItem {
	OUAIVE *ouaive;
	void onAction(EventAction &e) override {
		
		std::string dir = ouaive->lastPath.empty() ? assetLocal("") : extractDirectory(ouaive->lastPath);
		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
		if (path) {
			ouaive->play = false;
			ouaive->loadSample(path);
			ouaive->samplePos = 0;
			ouaive->lastPath = path;
			ouaive->fileName = extractFilename(path);
			free(path);
		}
	}
};

Menu *OUAIVEWidget::createContextMenu() {
	Menu *menu = ModuleWidget::createContextMenu();

	MenuLabel *spacerLabel = new MenuLabel();
	menu->pushChild(spacerLabel);

	OUAIVE *ouaive = dynamic_cast<OUAIVE*>(module);
	assert(ouaive);

	OUAIVEItem *sampleItem = new OUAIVEItem();
	sampleItem->text = "Load sample";
	sampleItem->ouaive = ouaive;
	menu->pushChild(sampleItem);

	return menu;
}

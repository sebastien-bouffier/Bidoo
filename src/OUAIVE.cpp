#include "Bidoo.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "../ext/osdialog/osdialog.h"
#include "AudioFile.h"
#include <vector>
#include "cmath"
#include <iomanip> // setprecision
#include <sstream> // stringstream

using namespace std;


struct OUAIVE : Module {
	enum ParamIds {
		NB_SLICES_PARAM,
		TRIG_MODE_PARAM,
		READ_MODE_PARAM,
		SPEED_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		GATE_INPUT,
		POS_INPUT,
		NB_SLICES_INPUT,
		READ_MODE_INPUT,
		SPEED_INPUT,
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
	float floatingSamplePos = 0;
	vector<double> displayBuff;
	string fileDesc;
	bool fileLoaded = false;
	int trigMode = 0; // 0 trig 1 gate, 2 sliced
	int sliceIndex = -1;
	int sliceLength = 0;
	int nbSlices = 1;
	int readMode = 0; // 0 formward, 1 backward, 2 repeat
	float speed;
	string displayParams = "";
	string displayReadMode = "";
	string displaySlices = "";
	string displaySpeed;

	SchmittTrigger playTrigger;
	SchmittTrigger trigModeTrigger;
	SchmittTrigger readModeTrigger;


	OUAIVE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {	}

	void step() override;

	void loadSample(std::string path);

	// persistence

	json_t *toJson() override {
		json_t *rootJ = json_object();
		// lastPath
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
		json_object_set_new(rootJ, "trigMode", json_integer(trigMode));
		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
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
	}
};

void OUAIVE::loadSample(std::string path) {
	if (audioFile.load (path.c_str())) {
		fileLoaded = true;
		vector<double>().swap(displayBuff);
		for (int i=0; i < audioFile.getNumSamplesPerChannel(); i = i + floor(audioFile.getNumSamplesPerChannel()/125)) {
			displayBuff.push_back(audioFile.samples[0][i]);
		}
		fileDesc = (extractFilename(path)).substr(0,20) + ((extractFilename(path)).length() >=20  ? "...\n" :  "\n");
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
			trigMode = (((int)trigMode + 1) % 3);
		}

		if (readModeTrigger.process(params[READ_MODE_PARAM].value + inputs[READ_MODE_INPUT].value)) {
			readMode = (((int)readMode + 1) % 3);
		}

		nbSlices = clampi(roundl(params[NB_SLICES_PARAM].value + inputs[NB_SLICES_INPUT].value), 1, 128);
		sliceLength = clampi(audioFile.getNumSamplesPerChannel() / nbSlices, 1, audioFile.getNumSamplesPerChannel());

		speed = clampf(params[SPEED_PARAM].value + inputs[SPEED_INPUT].value, 0.5, 10);
		if (speed < 1) {
			speed = 0.5;
			stringstream stream;
			stream << fixed << setprecision(1) << speed;
			string s = stream.str();
			displaySpeed = "x" + s;
		}
		else {
			speed=round(speed);
			stringstream stream;
			stream << fixed << setprecision(0) << speed;
			string s = stream.str();
			displaySpeed = "x" + s;
		}


		if ((trigMode == 0) && (playTrigger.process(inputs[GATE_INPUT].value))) {
			play = true;
			displayParams = "TRIG";
			samplePos = clampi((int)(inputs[POS_INPUT].value*audioFile.getNumSamplesPerChannel()/10), 0 , audioFile.getNumSamplesPerChannel() -1);
			floatingSamplePos = samplePos;
		}	else if (trigMode == 1) {
			displayParams = "GATE";
			play = (inputs[GATE_INPUT].value > 0);
			samplePos = clampi((int)(inputs[POS_INPUT].value*audioFile.getNumSamplesPerChannel()/10), 0 , audioFile.getNumSamplesPerChannel() -1);
			floatingSamplePos = samplePos;
		} else if ((trigMode == 2) && (playTrigger.process(inputs[GATE_INPUT].value))) {
			play = true;
			displayParams = "SLICE ";
			if (inputs[POS_INPUT].active)
				sliceIndex = clampi(inputs[POS_INPUT].value * nbSlices / 10, 0, nbSlices);
			 else
				sliceIndex = (sliceIndex+1)%nbSlices;

			if (readMode == 0) {
				displayReadMode = "►";
				samplePos = clampi(sliceIndex*sliceLength, 0 , audioFile.getNumSamplesPerChannel());
				floatingSamplePos = samplePos;
			} else if (readMode == 2) {
				displayReadMode = "►►";
				samplePos = clampi(sliceIndex*sliceLength, 0 , audioFile.getNumSamplesPerChannel());
				floatingSamplePos = samplePos;
			}
			else {
				displayReadMode = "◄";
				samplePos = clampi((sliceIndex + 1) * sliceLength - 1, 0 , audioFile.getNumSamplesPerChannel());
				floatingSamplePos = samplePos;
			}

		}

		if ((play) && (samplePos>=0) && (samplePos < audioFile.getNumSamplesPerChannel())) {
			//calulate outputs
			if (audioFile.getNumChannels() == 1)
				outputs[OUT_OUTPUT].value = 5 * audioFile.samples[0][samplePos];
			else if (audioFile.getNumChannels() ==2)
				outputs[OUT_OUTPUT].value = 5 * (audioFile.samples[0][samplePos] + audioFile.samples[1][samplePos]) / 2;

			//shift samplePos
			if (trigMode == 0) {
				floatingSamplePos = floatingSamplePos + speed;
				if (floatingSamplePos == floor(floatingSamplePos))
					samplePos = floatingSamplePos;
			}
			else if (trigMode == 2)
			{
				if (readMode != 1) {
					floatingSamplePos = floatingSamplePos + speed;
					if (floatingSamplePos == floor(floatingSamplePos))
						samplePos = floatingSamplePos;
				}
				else {
					floatingSamplePos = floatingSamplePos - speed;
					if (floatingSamplePos == floor(floatingSamplePos))
						samplePos = floatingSamplePos;
				}

				//update diplay slices
				displaySlices = "|" + std::to_string(nbSlices) + "|";

				//manage eof readMode
				if ((readMode == 0) && ((samplePos >= (sliceIndex+1) * sliceLength) || (samplePos >= audioFile.getNumSamplesPerChannel())))
						play = false;

				if ((readMode == 1) && ((samplePos <= (sliceIndex) * sliceLength) || (samplePos <=0)))
						play = false;

				if ((readMode == 2) && ((samplePos >= (sliceIndex+1) * sliceLength) || (samplePos >= audioFile.getNumSamplesPerChannel()))) {
					samplePos = clampi(sliceIndex*sliceLength, 0 , audioFile.getNumSamplesPerChannel());
					floatingSamplePos = samplePos;
				}
			}
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
	string displayParams;


	OUAIVEDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void draw(NVGcontext *vg) override {
		nvgFontSize(vg, 12);
		nvgFontFaceId(vg, font->handle);
		nvgTextLetterSpacing(vg, -2);
		nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
		nvgTextBox(vg, 5, 3,120, module->fileDesc.c_str(), NULL);

		nvgFontSize(vg, 14);
		nvgFillColor(vg, nvgRGBA(75, 199, 75, 0xff));
		nvgTextBox(vg, 5, 55,40, module->displayParams.c_str(), NULL);
		if (module->trigMode != 1)
			nvgTextBox(vg, 97, 55,30, module->displaySpeed.c_str(), NULL);

		if (module->trigMode == 2) {
			nvgTextBox(vg, 45, 55,20, module->displayReadMode.c_str(), NULL);
			nvgTextBox(vg, 62, 55,40, module->displaySlices.c_str(), NULL);
		}
		// Draw ref line
		nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
		{
			nvgBeginPath(vg);
			nvgMoveTo(vg, 0, 110);
			nvgLineTo(vg, 130, 110);
			nvgClosePath(vg);
		}
		nvgStroke(vg);

		if (module->fileLoaded) {
			// Draw play line
			nvgStrokeColor(vg, nvgRGBA(0x28, 0xb0, 0xf3, 0xc0));
			{
				nvgBeginPath(vg);
				nvgMoveTo(vg, (int)(module->samplePos * 125 / module->audioFile.getNumSamplesPerChannel()) , 70);
				nvgLineTo(vg, (int)(module->samplePos * 125 / module->audioFile.getNumSamplesPerChannel()) , 150);
				nvgClosePath(vg);
			}
			nvgStroke(vg);

			//draw slices
			if (module->trigMode == 2) {
				for (int i = 1; i < module->nbSlices; i++) {
					nvgStrokeColor(vg, nvgRGBA(252, 236, 64, 250));
					{
						nvgBeginPath(vg);
						nvgMoveTo(vg, (int)(i * module->sliceLength * 125 / module->audioFile.getNumSamplesPerChannel()) , 70);
						nvgLineTo(vg, (int)(i * module->sliceLength * 125 / module->audioFile.getNumSamplesPerChannel()) , 150);
						nvgClosePath(vg);
					}
					nvgStroke(vg);
				}
			}

			// Draw waveform
			nvgStrokeColor(vg, nvgRGBA(0xe1, 0x02, 0x78, 0xc0));
			nvgSave(vg);
			Rect b = Rect(Vec(0, 70), Vec(125, 80));
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

	addParam(createParam<CKD6>(Vec(portX0[0]-25, 215), module, OUAIVE::TRIG_MODE_PARAM, 0.0, 2.0, 0.0));

	addParam(createParam<CKD6>(Vec(portX0[1]-14, 215), module, OUAIVE::READ_MODE_PARAM, 0.0, 3.0, 0.0));
	addInput(createInput<TinyPJ301MPort>(Vec(portX0[2]+5, 222), module, OUAIVE::READ_MODE_INPUT));

	addParam(createParam<Trimpot>(Vec(portX0[1]-9, 250), module, OUAIVE::NB_SLICES_PARAM, 1.0, 128.01, 1.0));
	addInput(createInput<TinyPJ301MPort>(Vec(portX0[2]+5, 252), module, OUAIVE::NB_SLICES_INPUT));

	addParam(createParam<Trimpot>(Vec(portX0[1]-9, 275), module, OUAIVE::SPEED_PARAM, 0.5, 10, 1.0));
	addInput(createInput<TinyPJ301MPort>(Vec(portX0[2]+5, 277), module, OUAIVE::SPEED_INPUT));




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
			ouaive->sliceIndex = -1;
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

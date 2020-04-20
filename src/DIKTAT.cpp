#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <iterator>

using namespace std;

struct DIKTAT : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NOTE_INPUT,
		ROOT_NOTE_INPUT,
		SCALE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NOTE1_OUTPUT,
		NOTE2_OUTPUT,
		NOTE3_OUTPUT,
		NOTE4_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	int rootNote = 0;
	int scale = 0;
	int index1=0;
	int index2=0;
	int index3=0;
	int index4=0;
	int offset2=0;
	int offset3=0;
	int offset4=0;
	float inputNote = 0.0f;
	float note1 = 0.0f;
	float note2 = 0.0f;
	float note3 = 0.0f;
	float note4 = 0.0f;
	int octaveInVolts = 0;
	int *curScaleArr = SCALE_IONIAN;

	int SCALE_IONIAN											[7] = {0,	2, 4,	5, 7, 9, 11};
	int SCALE_DORIAN											[7] = {0,	2, 3,	5, 7,	9, 10};
	int SCALE_PHRYGIAN										[7] = {0,	1, 3,	5, 7,	8, 10};
	int SCALE_LYDIAN											[7] = {0,	2, 4,	6, 7,	9, 11};
	int SCALE_MIXOLYDIAN									[7] = {0,	2, 4,	5, 7,	9, 10};
	int SCALE_AEOLIAN											[7] = {0,	2, 3,	5, 7,	8, 10};
	int SCALE_LOCRIAN											[7] = {0,	1, 3,	5, 6,	8, 10};
	int SCALE_IONIAN_MIN									[7] = {0,	2, 3,	5, 7,	9, 11};
	int SCALE_DORIAN_FLAT_9								[7] = {0,	1, 3,	5, 7,	9, 10};
	int SCALE_LYDIAN_AUG									[7] = {0,	2, 4,	6, 8,	9, 11};
	int SCALE_LYDIAN_DOM									[7] = {0,	2, 4,	6, 7,	9, 10};
	int SCALE_MIXOLYDIAN_FLAT_6						[7] = {0,	2, 4,	5, 7,	8, 10};
	int SCALE_AEOLIAN_DIM									[7] = {0,	2, 3,	5, 6,	8, 10};
	int SCALE_SUPER_LOCRIAN								[7] = {0,	1, 3,	4, 6,	8, 10};
	int SCALE_AEOLIAN_NATURAL_7						[7] = {0,	2, 3,	5, 7,	8, 11};
	int SCALE_LOCRIAN_NATURAL_6						[7] = {0,	1, 3,	5, 6,	9, 10};
	int SCALE_IONIAN_AUG									[7] = {0,	2, 4,	5, 8,	9, 11};
	int SCALE_DORIAN_SHARP_4							[7] = {0,	2, 3,	6, 7,	9, 10};
	int SCALE_PHRYGIAN_DOM								[7] = {0,	1, 4,	5, 7,	8, 10};
	int SCALE_LYDIAN_SHARP_9							[7] = {0,	3, 4,	6, 7,	9, 11};
	int SCALE_SUPER_LOCRIAN_DOUBLE_FLAT_7	[7] = {0,	1, 3,	4, 6,	8, 9};


	enum Notes {
		NOTE_C,
		NOTE_C_SHARP,
		NOTE_D,
		NOTE_D_SHARP,
		NOTE_E,
		NOTE_F,
		NOTE_F_SHARP,
		NOTE_G,
		NOTE_G_SHARP,
		NOTE_A,
		NOTE_A_SHARP,
		NOTE_B,
		NUM_NOTES
	};

	enum Scales {
		IONIAN,
		DORIAN,
		PHRYGIAN,
		LYDIAN,
		MIXOLYDIAN,
		AEOLIAN,
		LOCRIAN,
		IONIAN_MIN,
		DORIAN_FLAT_9,
		LYDIAN_AUG,
		LYDIAN_DOM,
		MIXOLYDIAN_FLAT_6,
		AEOLIAN_DIM,
		SUPER_LOCRIAN,
		AEOLIAN_NATURAL_7,
		LOCRIAN_NATURAL_6,
		IONIAN_AUG,
		DORIAN_SHARP_4,
		PHRYGIAN_DOM,
		LYDIAN_SHARP_9,
		SUPER_LOCRIAN_DOUBLE_FLAT_7,
		NUM_SCALES
	};

	DIKTAT() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
  }

  void process(const ProcessArgs &args) override;


	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "rootNote", json_integer(rootNote));
		json_object_set_new(rootJ, "scale", json_integer(scale));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *rootNoteJ = json_object_get(rootJ, "rootNote");
		if (rootNoteJ)
			rootNote = json_integer_value(rootNoteJ);

		json_t *scaleJ = json_object_get(rootJ, "scale");
		if (scaleJ)
			scale = json_integer_value(scaleJ);
	}
};

void DIKTAT::process(const ProcessArgs &args) {
	if (inputs[ROOT_NOTE_INPUT].isConnected()) {
		rootNote = rescale(clamp(inputs[ROOT_NOTE_INPUT].getVoltage(), 0.0f,10.0f),0.0f,10.0f,0.0f,11.0f);
	}

	if (inputs[SCALE_INPUT].isConnected()) {
		scale = rescale(clamp(inputs[SCALE_INPUT].getVoltage(), 0.0f,10.0f),0.0f,10.0f,0.0f,20.0f);
	}

	inputNote = inputs[NOTE_INPUT].getVoltage();

	switch(scale){
		case IONIAN:											curScaleArr = SCALE_IONIAN; break;
		case DORIAN:											curScaleArr = SCALE_DORIAN; break;
		case PHRYGIAN:										curScaleArr = SCALE_PHRYGIAN; break;
		case LYDIAN:											curScaleArr = SCALE_LYDIAN; break;
		case MIXOLYDIAN:									curScaleArr = SCALE_MIXOLYDIAN; break;
		case AEOLIAN:											curScaleArr = SCALE_AEOLIAN; break;
		case LOCRIAN:											curScaleArr = SCALE_LOCRIAN; break;
		case IONIAN_MIN:									curScaleArr = SCALE_IONIAN_MIN; break;
		case DORIAN_FLAT_9:								curScaleArr = SCALE_DORIAN_FLAT_9; break;
		case LYDIAN_AUG:									curScaleArr = SCALE_LYDIAN_AUG; break;
		case LYDIAN_DOM:									curScaleArr = SCALE_LYDIAN_DOM; break;
		case MIXOLYDIAN_FLAT_6:						curScaleArr = SCALE_MIXOLYDIAN_FLAT_6; break;
		case AEOLIAN_DIM:									curScaleArr = SCALE_AEOLIAN_DIM; break;
		case SUPER_LOCRIAN:								curScaleArr = SCALE_SUPER_LOCRIAN; break;
		case AEOLIAN_NATURAL_7:						curScaleArr = SCALE_AEOLIAN_NATURAL_7; break;
		case LOCRIAN_NATURAL_6:						curScaleArr = SCALE_LOCRIAN_NATURAL_6; break;
		case IONIAN_AUG:									curScaleArr = SCALE_IONIAN_AUG; break;
		case DORIAN_SHARP_4:							curScaleArr = SCALE_DORIAN_SHARP_4; break;
		case PHRYGIAN_DOM:								curScaleArr = SCALE_PHRYGIAN_DOM; break;
		case LYDIAN_SHARP_9:							curScaleArr = SCALE_LYDIAN_SHARP_9; break;
		case SUPER_LOCRIAN_DOUBLE_FLAT_7:	curScaleArr = SCALE_SUPER_LOCRIAN_DOUBLE_FLAT_7; break;
	}

	float closestVal = 0.0f;
	float closestDist = 1.0f;

	if (( inputNote>= 0.0f) || (inputNote == (int)inputNote)) {
		octaveInVolts = int(inputNote);
	}
	else {
		octaveInVolts = int(inputNote)-1;
	}

	octaveInVolts = clamp(octaveInVolts-1,-4,6);

	index1 = 0;
	for (int i = 0; i < 21; i++) {
		float scaleNoteInVolts = octaveInVolts + int(i/7) + (rootNote / 12.0f) + curScaleArr[i%7] / 12.0f;
		float distAway = fabs(inputs[NOTE_INPUT].getVoltage() - scaleNoteInVolts);
		if(distAway < closestDist) {
			index1 = i;
			closestVal = scaleNoteInVolts;
			closestDist = distAway;
		}
	}

	index2 = (index1+2)%7;
	index3 = (index1+4)%7;
	index4 = (index1+6)%7;

	offset2=(index1+2)/7;
	offset3=(index1+4)/7;
	offset4=(index1+6)/7;

	note1 = clamp(closestVal ,-4.0f,6.0f);
	note2 = clamp(curScaleArr[index2] / 12.0f + octaveInVolts + offset2 + (rootNote / 12.0f),-4.0f,6.0f);
	note3 = clamp(curScaleArr[index3] / 12.0f + octaveInVolts + offset3 + (rootNote / 12.0f),-4.0f,6.0f);
	note4 = clamp(curScaleArr[index4] / 12.0f + octaveInVolts + offset4 + (rootNote / 12.0f),-4.0f,6.0f);

	outputs[NOTE1_OUTPUT].setVoltage(note1);
	outputs[NOTE2_OUTPUT].setVoltage(note2);
	outputs[NOTE3_OUTPUT].setVoltage(note3);
	outputs[NOTE4_OUTPUT].setVoltage(note4);
}

struct DIKTATWidget : ModuleWidget {
  DIKTATWidget(DIKTAT *module);
};

struct RootNoteButton : OpaqueWidget {
	DIKTAT *module;
	int rootNote;
	std::string sRootNote="";
	shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));

	bool isInScale() {
		for (int i=0; i<7; i++) {
			if ((module->curScaleArr[i]+module->rootNote)%12 == rootNote) return true;
		}
		return false;
	}

	void draw(const DrawArgs &args) override {
		if (module && module->rootNote == rootNote) {
			nvgBeginPath(args.vg);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgRoundedRect(args.vg, 0, 0, box.size.x-mm2px(0.5f), box.size.y-mm2px(0.5f), 0);
  		nvgFillColor(args.vg, nvgRGB(107, 107, 107));
  		nvgFill(args.vg);

  		nvgFontSize(args.vg, 8.0f);
  		nvgFillColor(args.vg, YELLOW_BIDOO);
  		nvgText(args.vg, (box.size.x-mm2px(0.5f))/2, (box.size.y-mm2px(0.5f))/2, sRootNote.c_str(), NULL);
		}
		else if (module && isInScale()) {
			nvgBeginPath(args.vg);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgRoundedRect(args.vg, 0, 0, box.size.x-mm2px(0.5f), box.size.y-mm2px(0.5f), 0);
  		nvgFillColor(args.vg, nvgRGB(107, 107, 107));
  		nvgFill(args.vg);

  		nvgFontSize(args.vg, 8.0f);
  		nvgFillColor(args.vg, nvgRGB(255, 255, 255));
  		nvgText(args.vg, (box.size.x-mm2px(0.5f))/2, (box.size.y-mm2px(0.5f))/2, sRootNote.c_str(), NULL);
		}
		else {
			nvgBeginPath(args.vg);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgRoundedRect(args.vg, 0, 0, box.size.x-mm2px(0.5f), box.size.y-mm2px(0.5f), 0);
  		nvgFillColor(args.vg, nvgRGB(107, 107, 107));
  		nvgFill(args.vg);

  		nvgFontSize(args.vg, 8.0f);
  		nvgFillColor(args.vg, nvgRGB(150, 150, 150));
  		nvgText(args.vg, (box.size.x-mm2px(0.5f))/2, (box.size.y-mm2px(0.5f))/2, sRootNote.c_str(), NULL);
		}
	}

	void onButton(const event::Button &e) override {
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
			module->rootNote = 0;
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void onDragEnter(const event::DragEnter &e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			RootNoteButton *w = dynamic_cast<RootNoteButton*>(e.origin);
			if (w) {
				module->rootNote = rootNote;
			}
		}
	}
};

struct ScaleButton : OpaqueWidget {
	DIKTAT *module;
	int scale;
	std::string sScale="";
	shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));

	void draw(const DrawArgs &args) override {
		if (module && module->scale == scale) {
  		nvgBeginPath(args.vg);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
  		nvgRoundedRect(args.vg, 0, 0, box.size.x-mm2px(0.5f), box.size.y-mm2px(0.5f), 0);
  		nvgFillColor(args.vg, nvgRGB(107, 107, 107));
  		nvgFill(args.vg);

  		nvgFontSize(args.vg, 8.0f);
  		nvgFillColor(args.vg, YELLOW_BIDOO);
  		nvgText(args.vg, (box.size.x-mm2px(0.5f))/2, (box.size.y-mm2px(0.5f))/2, sScale.c_str(), NULL);
		}
		else {
			nvgBeginPath(args.vg);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgRoundedRect(args.vg, 0, 0, box.size.x-mm2px(0.5f), box.size.y-mm2px(0.5f), 0);
  		nvgFillColor(args.vg, nvgRGB(107, 107, 107));
  		nvgFill(args.vg);

  		nvgFontSize(args.vg, 8.0f);
  		nvgFillColor(args.vg, nvgRGB(255, 255, 255));
  		nvgText(args.vg, (box.size.x-mm2px(0.5f))/2, (box.size.y-mm2px(0.5f))/2, sScale.c_str(), NULL);
		}
	}

	void onButton(const event::Button &e) override {
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
			module->scale = 0;
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void onDragEnter(const event::DragEnter &e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			ScaleButton *w = dynamic_cast<ScaleButton*>(e.origin);
			if (w) {
				module->scale = scale;
			}
		}
	}
};

struct DiktatDisplay : OpaqueWidget {
	shared_ptr<Font> font;
	DIKTAT *module;

	DiktatDisplay() {
		box.size = mm2px(Vec(50, 87));
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void setModule(DIKTAT *module) {
		clearChildren();
		this->module = module;

		const int rootNotes = 12;
		const int scales = 21;
		const float margin = mm2px(2.0);
		float height = box.size.y - 2*margin;

		for (int i = 0; i < rootNotes; i++) {
			RootNoteButton *rootNoteButton = new RootNoteButton();
			rootNoteButton->box.pos = Vec(0, height / rootNotes * i);
			rootNoteButton->box.size = Vec(20, height / rootNotes);
			rootNoteButton->module = module;
			rootNoteButton->rootNote = i;
			switch(i){
				case 0:		rootNoteButton->sRootNote = "C"; break;
				case 1:		rootNoteButton->sRootNote = "C#"; break;
				case 2:		rootNoteButton->sRootNote = "D"; break;
				case 3:		rootNoteButton->sRootNote = "D#"; break;
				case 4:		rootNoteButton->sRootNote = "E"; break;
				case 5:		rootNoteButton->sRootNote = "F"; break;
				case 6:		rootNoteButton->sRootNote = "F#"; break;
				case 7:		rootNoteButton->sRootNote = "G"; break;
				case 8:		rootNoteButton->sRootNote = "G#"; break;
				case 9:		rootNoteButton->sRootNote = "A"; break;
				case 10:	rootNoteButton->sRootNote = "A#"; break;
				case 11:	rootNoteButton->sRootNote = "B"; break;
			}
			addChild(rootNoteButton);
		}

		for (int i = 0; i < scales; i++) {
			ScaleButton *scaleButton = new ScaleButton();
			scaleButton->box.pos = Vec(40, height / scales * i);
			scaleButton->box.size = Vec(80, height / scales);
			scaleButton->module = module;
			scaleButton->scale = i;
			switch(i){
				case 0:		scaleButton->sScale = "Ionian"; break;
				case 1:		scaleButton->sScale = "Dorian"; break;
				case 2:		scaleButton->sScale = "Phrygian"; break;
				case 3:		scaleButton->sScale = "Lydian"; break;
				case 4:		scaleButton->sScale = "Mixolydian"; break;
				case 5:		scaleButton->sScale = "Aeolian"; break;
				case 6:		scaleButton->sScale = "Locrian"; break;
				case 7:		scaleButton->sScale = "Ionian Min"; break;
				case 8:		scaleButton->sScale = "Dorian b9"; break;
				case 9:		scaleButton->sScale = "Lydian Aug"; break;
				case 10:	scaleButton->sScale = "Lydian Dom"; break;
				case 11:	scaleButton->sScale = "Mixolydian b6"; break;
				case 12:	scaleButton->sScale = "Aeolian Dim"; break;
				case 13:	scaleButton->sScale = "Super Locrian"; break;
				case 14:	scaleButton->sScale = "Aeolian ♮7"; break;
				case 15:	scaleButton->sScale = "Locrian ♮6"; break;
				case 16:	scaleButton->sScale = "Ionian Aug"; break;
				case 17:	scaleButton->sScale = "Dorian #4"; break;
				case 18:	scaleButton->sScale = "Phrygian Dom"; break;
				case 19:	scaleButton->sScale = "Lydian #9"; break;
				case 20:	scaleButton->sScale = "Super Locrian bb7"; break;
			}
			addChild(scaleButton);
		}
	}

	std::string displayNote(float value) {
		int octave = 0;
		if ((value >= 0.0f) || (value == (int)value)) {
			octave = int(value);
		}
		else {
			octave = int(value)-1;
		}
		int note = (value-octave)*1000;
		switch(note){
			case 0:  return "C" + to_string(octave+4);
			case 83: return "C#" + to_string(octave+4);
			case 166: return "D" + to_string(octave+4);
			case 250: return "D#" + to_string(octave+4);
			case 333: return "E" + to_string(octave+4);
			case 416: return "F" + to_string(octave+4);
			case 500: return "F#" + to_string(octave+4);
			case 583: return "G" + to_string(octave+4);
			case 666: return "G#" + to_string(octave+4);
			case 750: return "A" + to_string(octave+4);
			case 833: return "A#" + to_string(octave+4);
			case 916: return "B" + to_string(octave+4);
			case 1000: return "C" + to_string(octave+5);
			default: return to_string(octave+4);
		}
	}

	void draw(const DrawArgs &args) override {
		if (module) {
			nvgBeginPath(args.vg);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
  		nvgFontSize(args.vg, 10.0f);
  		nvgFillColor(args.vg, nvgRGB(255, 255, 255));
			nvgText(args.vg, 5, 257, displayNote(module->inputNote).c_str(), NULL);
  		nvgText(args.vg, 5, 303, displayNote(module->note1).c_str(), NULL);
			nvgText(args.vg, 42, 303, displayNote(module->note2).c_str(), NULL);
			nvgText(args.vg, 79, 303, displayNote(module->note3).c_str(), NULL);
			nvgText(args.vg, 116, 303, displayNote(module->note4).c_str(), NULL);
		}

		Widget::draw(args);
	}
};

DIKTATWidget::DIKTATWidget(DIKTAT *module) {
	setModule(module);
	setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DIKTAT.svg")));

	addChild(createWidget<ScrewSilver>(Vec(15, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createWidget<ScrewSilver>(Vec(15, 365)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

	DiktatDisplay *diktatDisplay = new DiktatDisplay();
	diktatDisplay->box.pos = mm2px(Vec(5, 7));
	diktatDisplay->setModule(module);
	addChild(diktatDisplay);

	addInput(createInput<PJ301MPort>(Vec(7, 283), module, DIKTAT::NOTE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(62.75f, 283), module, DIKTAT::ROOT_NOTE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(118.5f, 283), module, DIKTAT::SCALE_INPUT));

	addOutput(createOutput<PJ301MPort>(Vec(7.0f, 330), module, DIKTAT::NOTE1_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(44.0f, 330), module, DIKTAT::NOTE2_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(81.5f, 330), module, DIKTAT::NOTE3_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(118.5f, 330), module, DIKTAT::NOTE4_OUTPUT));
}

Model *modelDIKTAT = createModel<DIKTAT, DIKTATWidget>("DIKTAT");

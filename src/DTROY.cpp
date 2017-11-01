#include "Bidoo.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include <ctime>

struct DTROY : Module {
	enum ParamIds {
		CLOCK_PARAM,
		RUN_PARAM,
		RESET_PARAM,
		STEPS_PARAM,
		SLIDE_TIME_PARAM,
		GATE_TIME_PARAM,
		ROOT_NOTE_PARAM,
		SCALE_PARAM,
		TRIG_COUNT_PARAM = GATE_TIME_PARAM + 8,
		TRIG_TYPE_PARAM = TRIG_COUNT_PARAM + 8,
		TRIG_PITCH_PARAM = TRIG_TYPE_PARAM + 8,
		TRIG_SLIDE_PARAM = TRIG_PITCH_PARAM + 8,
		TRIG_SKIP_PARAM = TRIG_SLIDE_PARAM + 8,
		NUM_PARAMS = TRIG_SKIP_PARAM + 8
	};
	enum InputIds {
		CLOCK_INPUT,
		EXT_CLOCK_INPUT,
		RESET_INPUT,
		STEPS_INPUT,
		SLIDE_TIME_INPUT,
		GATE_TIME_INPUT,
		ROOT_NOTE_INPUT,
		SCALE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		GATE_OUTPUT,
		PITCH_OUTPUT,
		SCALE_OUTPUT,
		ROOT_NOTE_OUTPUT,
		INDEX_OUTPUT,
		FLOOR_OUTPUT,
		PULSE_OUTPUT,
		PHASE_OUTPUT,
		TRIG1COUNT_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		RUNNING_LIGHT,
		RESET_LIGHT,
		GATE_LIGHT,
		SLIDE_LIGHTS = GATE_LIGHT + 8,
		SKIP_LIGHTS = SLIDE_LIGHTS + 8,
		NUM_LIGHTS = SKIP_LIGHTS + 8
	};
	
	//copied from http://www.grantmuller.com/MidiReference/doc/midiReference/ScaleReference.html
	int SCALE_AEOLIAN        [7] = {0, 2, 3, 5, 7, 8, 10};
	int SCALE_BLUES          [9] = {0, 2, 3, 4, 5, 7, 9, 10, 11};
	int SCALE_CHROMATIC      [12]= {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
	int SCALE_DIATONIC_MINOR [7] = {0, 2, 3, 5, 7, 8, 10};
	int SCALE_DORIAN         [7] = {0, 2, 3, 5, 7, 9, 10};
	int SCALE_HARMONIC_MINOR [7] = {0, 2, 3, 5, 7, 8, 11};
	int SCALE_INDIAN         [7] = {0, 1, 1, 4, 5, 8, 10};
	int SCALE_LOCRIAN        [7] = {0, 1, 3, 5, 6, 8, 10};
	int SCALE_LYDIAN         [7] = {0, 2, 4, 6, 7, 9, 10};
	int SCALE_MAJOR          [7] = {0, 2, 4, 5, 7, 9, 11};
	int SCALE_MELODIC_MINOR  [9] = {0, 2, 3, 5, 7, 8, 9, 10, 11};
	int SCALE_MINOR          [7] = {0, 2, 3, 5, 7, 8, 10};
	int SCALE_MIXOLYDIAN     [7] = {0, 2, 4, 5, 7, 9, 10};
	int SCALE_NATURAL_MINOR  [7] = {0, 2, 3, 5, 7, 8, 10};
	int SCALE_PENTATONIC     [5] = {0, 2, 4, 7, 9};
	int SCALE_PHRYGIAN       [7] = {0, 1, 3, 5, 7, 8, 10};
	int SCALE_TURKISH        [7] = {0, 1, 3, 5, 7, 10, 11};

	bool running = true;
	SchmittTrigger clockTrigger; // for external clock
	// For buttons
	SchmittTrigger runningTrigger;
	SchmittTrigger resetTrigger;
	SchmittTrigger slideTriggers[8];
	SchmittTrigger skipTriggers[8];
	float phase = 0.0;
	int index = 0;
	int floor = 0;
	int rootNote = 0;
	int curScaleVal = 0;
	float pitch = 0;
	clock_t tCurrent;
	clock_t tLastTrig;
	clock_t tPreviousTrig;
	
		
	struct TrigState {
		float Pitch = 0;
		int Count = 1;
		int Type = 3; // 0 Empty 1 One Shot 2 Pulse 3 Full
		bool Slide = false;
		bool Skip = false;
	};
	
	TrigState trigs[8];
	
	float runningLight = 0.0;
	float resetLight = 0.0;
	float stepLights[8] = {};
	
	float slideLights[8] = {};
	float skipLights[8] = {};

	PulseGenerator gatePulse;

	//DTROY() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {}
	
	DTROY() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS) {}
	
	void step() override;

	json_t *toJson() override {
		json_t *rootJ = json_object();

		// running
		json_object_set_new(rootJ, "running", json_boolean(running));

		// trigs
		json_t *trigsJ = json_array();
		for (int i = 0; i < 8; i++) {
			json_t *trigJ = json_array();
			json_t *trigJPitch = json_real((float) trigs[i].Pitch);
			json_array_append_new(trigJ, trigJPitch);
			json_t *trigJCount = json_integer((int) trigs[i].Count);
			json_array_append_new(trigJ, trigJCount);
			json_t *trigJType = json_integer((int) trigs[i].Type);
			json_array_append_new(trigJ, trigJType);
			json_t *trigJSlide = json_boolean(trigs[i].Slide);
			json_array_append_new(trigJ, trigJSlide);
			json_t *trigJSkip = json_boolean(trigs[i].Skip);
			json_array_append_new(trigJ, trigJSkip);			
			
			json_object_set_new(trigsJ, "trig", trigJ);
		}
		json_object_set_new(rootJ, "trigs", trigsJ);

		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		// running
		json_t *runningJ = json_object_get(rootJ, "running");
		if (runningJ)
			running = json_is_true(runningJ);

		// trigs
		json_t *trigsJ = json_object_get(rootJ, "trigs");
		if (trigsJ) {
			for (int i = 0; i < 8; i++) {
				json_t *trigJ = json_array_get(trigsJ, i);
				if (trigJ)
				{
					trigs[i].Pitch = json_real_value(json_array_get(trigJ, 0));
					trigs[i].Count = json_integer_value(json_array_get(trigJ, 1));
					trigs[i].Type = json_integer_value(json_array_get(trigJ, 2));
					trigs[i].Slide = json_is_true(json_array_get(trigJ, 3));
					trigs[i].Skip = json_is_true(json_array_get(trigJ, 4));
				}
					
			}
		}
	}

	void reset() {
		for (int i = 0; i < 8; i++) {
			params[DTROY::TRIG_PITCH_PARAM + i].value = 0;
			params[DTROY::TRIG_COUNT_PARAM + i].value = 0;
			params[DTROY::TRIG_TYPE_PARAM + i].value = 0;
			params[DTROY::TRIG_SLIDE_PARAM + i].value = false;
			params[DTROY::TRIG_SKIP_PARAM + i].value = false;
		}
	}

	void randomize() override {
		for (int i = 0; i < 8; i++) {
			params[DTROY::TRIG_PITCH_PARAM + i].value = getOneRandomNoteInScale();
			params[DTROY::TRIG_COUNT_PARAM + i].value = static_cast<int>(std::round(randomf()*7));
			params[DTROY::TRIG_TYPE_PARAM + i].value = static_cast<int>(std::round(randomf()*3));
			params[DTROY::TRIG_SLIDE_PARAM + i].value = (randomf() > 0.7);
			params[DTROY::TRIG_SKIP_PARAM + i].value = (randomf() > 0.8);
		}
	}
	
	//inspired from  https://github.com/jeremywen/JW-Modules
	float getOneRandomNoteInScale(){
		rootNote = clampi(params[ROOT_NOTE_PARAM].value + inputs[ROOT_NOTE_INPUT].value, 0.0, DTROYWidget::NUM_NOTES-1);
		curScaleVal = clampi(params[SCALE_PARAM].value + inputs[SCALE_INPUT].value, 0.0, DTROYWidget::NUM_SCALES-1);
		int *curScaleArr;
		int notesInScale = 0;
		switch(curScaleVal){
			case DTROYWidget::AEOLIAN:        curScaleArr = SCALE_AEOLIAN;       notesInScale=LENGTHOF(SCALE_AEOLIAN); break;
			case DTROYWidget::BLUES:          curScaleArr = SCALE_BLUES;         notesInScale=LENGTHOF(SCALE_BLUES); break;
			case DTROYWidget::CHROMATIC:      curScaleArr = SCALE_CHROMATIC;     notesInScale=LENGTHOF(SCALE_CHROMATIC); break;
			case DTROYWidget::DIATONIC_MINOR: curScaleArr = SCALE_DIATONIC_MINOR;notesInScale=LENGTHOF(SCALE_DIATONIC_MINOR); break;
			case DTROYWidget::DORIAN:         curScaleArr = SCALE_DORIAN;        notesInScale=LENGTHOF(SCALE_DORIAN); break;
			case DTROYWidget::HARMONIC_MINOR: curScaleArr = SCALE_HARMONIC_MINOR;notesInScale=LENGTHOF(SCALE_HARMONIC_MINOR); break;
			case DTROYWidget::INDIAN:         curScaleArr = SCALE_INDIAN;        notesInScale=LENGTHOF(SCALE_INDIAN); break;
			case DTROYWidget::LOCRIAN:        curScaleArr = SCALE_LOCRIAN;       notesInScale=LENGTHOF(SCALE_LOCRIAN); break;
			case DTROYWidget::LYDIAN:         curScaleArr = SCALE_LYDIAN;        notesInScale=LENGTHOF(SCALE_LYDIAN); break;
			case DTROYWidget::MAJOR:          curScaleArr = SCALE_MAJOR;         notesInScale=LENGTHOF(SCALE_MAJOR); break;
			case DTROYWidget::MELODIC_MINOR:  curScaleArr = SCALE_MELODIC_MINOR; notesInScale=LENGTHOF(SCALE_MELODIC_MINOR); break;
			case DTROYWidget::MINOR:          curScaleArr = SCALE_MINOR;         notesInScale=LENGTHOF(SCALE_MINOR); break;
			case DTROYWidget::MIXOLYDIAN:     curScaleArr = SCALE_MIXOLYDIAN;    notesInScale=LENGTHOF(SCALE_MIXOLYDIAN); break;
			case DTROYWidget::NATURAL_MINOR:  curScaleArr = SCALE_NATURAL_MINOR; notesInScale=LENGTHOF(SCALE_NATURAL_MINOR); break;
			case DTROYWidget::PENTATONIC:     curScaleArr = SCALE_PENTATONIC;    notesInScale=LENGTHOF(SCALE_PENTATONIC); break;
			case DTROYWidget::PHRYGIAN:       curScaleArr = SCALE_PHRYGIAN;      notesInScale=LENGTHOF(SCALE_PHRYGIAN); break;
			case DTROYWidget::TURKISH:        curScaleArr = SCALE_TURKISH;       notesInScale=LENGTHOF(SCALE_TURKISH); break;
		}

		if(curScaleVal == DTROYWidget::NONE){
			return randomf() * 6.0;
		} else {
			float voltsOut = 0;
			int rndOctaveInVolts = int(5 * randomf());
			voltsOut += rndOctaveInVolts;
			voltsOut += rootNote / 12.0;
			voltsOut += curScaleArr[int(notesInScale * randomf())] / 12.0;
			return voltsOut;
		}
	}

	float closestVoltageInScale(float voltsIn){
		rootNote = clampi(params[ROOT_NOTE_PARAM].value + inputs[ROOT_NOTE_INPUT].value, 0.0, DTROYWidget::NUM_NOTES-1);
		curScaleVal = clampi(params[SCALE_PARAM].value + inputs[SCALE_INPUT].value, 0.0, DTROYWidget::NUM_SCALES-1);
		int *curScaleArr;
		int notesInScale = 0;
		switch(curScaleVal){
			case DTROYWidget::AEOLIAN:        curScaleArr = SCALE_AEOLIAN;       notesInScale=LENGTHOF(SCALE_AEOLIAN); break;
			case DTROYWidget::BLUES:          curScaleArr = SCALE_BLUES;         notesInScale=LENGTHOF(SCALE_BLUES); break;
			case DTROYWidget::CHROMATIC:      curScaleArr = SCALE_CHROMATIC;     notesInScale=LENGTHOF(SCALE_CHROMATIC); break;
			case DTROYWidget::DIATONIC_MINOR: curScaleArr = SCALE_DIATONIC_MINOR;notesInScale=LENGTHOF(SCALE_DIATONIC_MINOR); break;
			case DTROYWidget::DORIAN:         curScaleArr = SCALE_DORIAN;        notesInScale=LENGTHOF(SCALE_DORIAN); break;
			case DTROYWidget::HARMONIC_MINOR: curScaleArr = SCALE_HARMONIC_MINOR;notesInScale=LENGTHOF(SCALE_HARMONIC_MINOR); break;
			case DTROYWidget::INDIAN:         curScaleArr = SCALE_INDIAN;        notesInScale=LENGTHOF(SCALE_INDIAN); break;
			case DTROYWidget::LOCRIAN:        curScaleArr = SCALE_LOCRIAN;       notesInScale=LENGTHOF(SCALE_LOCRIAN); break;
			case DTROYWidget::LYDIAN:         curScaleArr = SCALE_LYDIAN;        notesInScale=LENGTHOF(SCALE_LYDIAN); break;
			case DTROYWidget::MAJOR:          curScaleArr = SCALE_MAJOR;         notesInScale=LENGTHOF(SCALE_MAJOR); break;
			case DTROYWidget::MELODIC_MINOR:  curScaleArr = SCALE_MELODIC_MINOR; notesInScale=LENGTHOF(SCALE_MELODIC_MINOR); break;
			case DTROYWidget::MINOR:          curScaleArr = SCALE_MINOR;         notesInScale=LENGTHOF(SCALE_MINOR); break;
			case DTROYWidget::MIXOLYDIAN:     curScaleArr = SCALE_MIXOLYDIAN;    notesInScale=LENGTHOF(SCALE_MIXOLYDIAN); break;
			case DTROYWidget::NATURAL_MINOR:  curScaleArr = SCALE_NATURAL_MINOR; notesInScale=LENGTHOF(SCALE_NATURAL_MINOR); break;
			case DTROYWidget::PENTATONIC:     curScaleArr = SCALE_PENTATONIC;    notesInScale=LENGTHOF(SCALE_PENTATONIC); break;
			case DTROYWidget::PHRYGIAN:       curScaleArr = SCALE_PHRYGIAN;      notesInScale=LENGTHOF(SCALE_PHRYGIAN); break;
			case DTROYWidget::TURKISH:        curScaleArr = SCALE_TURKISH;       notesInScale=LENGTHOF(SCALE_TURKISH); break;
			case DTROYWidget::NONE:           return voltsIn;
		}

		float closestVal = 10.0;
		float closestDist = 10.0;
		int octaveInVolts = int(voltsIn);
		for (int i = 0; i < notesInScale; i++) {
			float scaleNoteInVolts = octaveInVolts + ((rootNote + curScaleArr[i]) / 12.0);
			float distAway = fabs(voltsIn - scaleNoteInVolts);
			if(distAway < closestDist){
				closestVal = scaleNoteInVolts;
				closestDist = distAway;
			}
		}
		return closestVal;
	}
};


void DTROY::step() {
 	const float lightLambda = 0.075;
	// Run
	if (runningTrigger.process(params[RUN_PARAM].value)) {
		running = !running;
	}
	//lights[RUNNING_LIGHT].value = running ? 1.0 : 0.0;
	runningLight = running ? 1.0 : 0.0;
	

	bool nextStep = false;

	// Phase calculation
	if (running) {
		if (inputs[EXT_CLOCK_INPUT].active) {
			tCurrent = clock();
			float clockTime = powf(2.0, params[CLOCK_PARAM].value + inputs[CLOCK_INPUT].value);
			if (tLastTrig && tPreviousTrig) {
				phase = float(tCurrent - tLastTrig) / float(tLastTrig - tPreviousTrig);
			} 
			else {
				phase += clockTime / gSampleRate ; //engineGetSampleRate();
			}
			// External clock
			if (clockTrigger.process(inputs[EXT_CLOCK_INPUT].value)) {
				tPreviousTrig = tLastTrig;
				tLastTrig = clock();
				phase = 0.0;
				nextStep = true;
			}
		}
		else {
			// Internal clock
			float clockTime = powf(2.0, params[CLOCK_PARAM].value + inputs[CLOCK_INPUT].value);
			phase += clockTime / gSampleRate ; //engineGetSampleRate();
			if (phase >= 1.0) {
				phase -= 1.0;
				nextStep = true;
			}
		}
	}

	// Reset
	if (resetTrigger.process(params[RESET_PARAM].value + inputs[RESET_INPUT].value)) {
		phase = 0.0;
		floor = 0;
		index = 8;
		nextStep = true;
		resetLight = 1.0;
	}
	
	// Trigs Update
	for (int i = 0; i < 8; i++) {	
		trigs[i].Count = params[TRIG_COUNT_PARAM + i].value;	
		trigs[i].Type = params[TRIG_TYPE_PARAM + i].value;
		trigs[i].Pitch = params[TRIG_PITCH_PARAM + i].value;
		if (slideTriggers[i].process(params[TRIG_SLIDE_PARAM + i].value)) {
			trigs[i].Slide = !trigs[i].Slide;
		}
		if (skipTriggers[i].process(params[TRIG_SKIP_PARAM + i].value)) {
			trigs[i].Skip = !trigs[i].Skip;
		}
	}

	// Steps Floors Management
	if (nextStep) {
		// Advance step

		int numSteps = clampi(roundf(params[STEPS_PARAM].value + inputs[STEPS_INPUT].value), 1, 8);
		
		if ((index < numSteps) && (floor < trigs[index].Count)) {
			floor += 1;
		}
		else {
			floor = 0;
			index += 1;
			if (index >= numSteps) {
				index = 0;
			}
			int count = 8;
			while ((trigs[index].Skip) && (count > 0)) {
				index = (index + 1)%8;
				count -=1;;
			}
		}
		
		stepLights[index] = 1.0;
		gatePulse.trigger(1e-3);
	}

	// Lights
	for (int i = 0; i < 8; i++) {
		stepLights[i] -= stepLights[i] / lightLambda / gSampleRate ; //engineGetSampleRate();	
		slideLights[i] = trigs[i].Slide ? true - stepLights[i] : stepLights[i];
		skipLights[i] = trigs[i].Skip ? true - stepLights[i] : stepLights[i];		
		/* lights[SLIDE_LIGHTS + i].value = trigs[i].Slide ? true - stepLights[i] : stepLights[i];
		lights[SKIP_LIGHTS + i].value = trigs[i].Skip ? true - stepLights[i] : stepLights[i]; */
	}
	resetLight -= resetLight / lightLambda / gSampleRate ; //engineGetSampleRate();
	
	//lights[RESET_LIGHT].value = resetLight;

	// Caclulate Outputs
	bool pulse = gatePulse.process(1.0 / gSampleRate); //engineGetSampleRate());

	bool gateOn = running && !trigs[index].Skip;
	if (gateOn){
		if (trigs[index].Type == 0) {
			gateOn = false;
		}
		else if (((trigs[index].Type == 1) && (floor == 0)) 
				|| (trigs[index].Type == 2)
				|| ((trigs[index].Type == 3) && (floor == (trigs[index].Count)))) {
				float gateCoeff = clampf(params[GATE_TIME_PARAM].value - 0.02 + inputs[GATE_TIME_INPUT].value /10, 0.0, 0.99);
			gateOn = phase < gateCoeff;
		}
		else if (trigs[index].Type == 3) {
			gateOn = true;
		}
		else {
			gateOn = false;
		}
	}
	
	pitch = closestVoltageInScale(trigs[index].Pitch);
	if (trigs[index].Slide) {
		if (floor == 0) {
			float slideCoeff = clampf(params[SLIDE_TIME_PARAM].value - 0.01 + inputs[SLIDE_TIME_INPUT].value /10, -0.1, 0.99);
			float previousPitch = closestVoltageInScale(trigs[(index + 7)%8].Pitch);
			pitch = pitch - (1 - powf(phase, slideCoeff)) * (pitch - previousPitch);
		}
	}
			
	// Update Outputs
	outputs[GATE_OUTPUT].value = gateOn ? 10.0 : 0.0;
	outputs[PITCH_OUTPUT].value = pitch;
	outputs[SCALE_OUTPUT].value = clampi(params[SCALE_PARAM].value + inputs[SCALE_INPUT].value, 0.0, DTROYWidget::NUM_SCALES-1);
	outputs[ROOT_NOTE_OUTPUT].value = clampi(params[ROOT_NOTE_PARAM].value + inputs[ROOT_NOTE_INPUT].value, 0.0, DTROYWidget::NUM_NOTES-1);
	
	// Debug outputs
	/* outputs[INDEX_OUTPUT].value = index;
	outputs[FLOOR_OUTPUT].value = floor;
	outputs[PULSE_OUTPUT].value = pulse;
	outputs[PHASE_OUTPUT].value = phase;
	outputs[TRIG1COUNT_OUTPUT].value = trigs[0].Count; */
}

struct DTROYDisplay : TransparentWidget {
	DTROY *module;
	int frame = 0;
	std::shared_ptr<Font> font;

	std::string note, scale;

	DTROYDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void drawMessage(NVGcontext *vg, Vec pos, const char *title, std::string line1, std::string line2) {
		nvgFontSize(vg, 14);
		nvgFontFaceId(vg, font->handle);
		nvgTextLetterSpacing(vg, -2);
		nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
		nvgText(vg, pos.x + 7, pos.y + 7, line1.c_str(), NULL);
		nvgText(vg, pos.x + 7, pos.y + 24, line2.c_str(), NULL);
	}
	
	std::string calculateRootNote(int value) { 
		switch(value){
			case DTROYWidget::NOTE_C:       return "C";
			case DTROYWidget::NOTE_C_SHARP: return "C#";
			case DTROYWidget::NOTE_D:       return "D";
			case DTROYWidget::NOTE_D_SHARP: return "D#";
			case DTROYWidget::NOTE_E:       return "E";
			case DTROYWidget::NOTE_F:       return "F";
			case DTROYWidget::NOTE_F_SHARP: return "F#";
			case DTROYWidget::NOTE_G:       return "G";
			case DTROYWidget::NOTE_G_SHARP: return "G#";
			case DTROYWidget::NOTE_A:       return "A";
			case DTROYWidget::NOTE_A_SHARP: return "A#";
			case DTROYWidget::NOTE_B:       return "B";
			default: return "";
		}
	}
	
	std::string calculateScale(int value) { 
		switch(value){
			case DTROYWidget::AEOLIAN:        return "Aeolian";
			case DTROYWidget::BLUES:          return "Blues";
			case DTROYWidget::CHROMATIC:      return "Chromatic";
			case DTROYWidget::DIATONIC_MINOR: return "Diatonic Minor";
			case DTROYWidget::DORIAN:         return "Dorian";
			case DTROYWidget::HARMONIC_MINOR: return "Harmonic Minor";
			case DTROYWidget::INDIAN:         return "Indian";
			case DTROYWidget::LOCRIAN:        return "Locrian";
			case DTROYWidget::LYDIAN:         return "Lydian";
			case DTROYWidget::MAJOR:          return "Major";
			case DTROYWidget::MELODIC_MINOR:  return "Melodic Minor";
			case DTROYWidget::MINOR:          return "Minor";
			case DTROYWidget::MIXOLYDIAN:     return "Mixolydian";
			case DTROYWidget::NATURAL_MINOR:  return "Natural Minor";
			case DTROYWidget::PENTATONIC:     return "Pentatonic";
			case DTROYWidget::PHRYGIAN:       return "Phrygian";
			case DTROYWidget::TURKISH:        return "Turkish";
			case DTROYWidget::NONE:           return "None";
			default: return "";
		}
	}

	void draw(NVGcontext *vg) override {
		if (++frame >= 4) {
			frame = 0;
			note = calculateRootNote(module->rootNote);
			scale = calculateScale(module->curScaleVal);
		}
		drawMessage(vg, Vec(0, 20), "X", note, scale);
	}
};

DTROYWidget::DTROYWidget() {
	DTROY *module = new DTROY();
	setModule(module);
	box.size = Vec(15*34, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/DTROY.svg")));
		addChild(panel);
	}

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));
	
	{
		DTROYDisplay *display = new DTROYDisplay();
		display->module = module;
		display->box.pos = Vec(32, 222);
		display->box.size = Vec(100, 50);
		addChild(display);
	}
	
	addParam(createParam<RoundSmallBlackKnob>(Vec(18, 56), module, DTROY::CLOCK_PARAM, -2.0, 6.0, 2.0));
	addParam(createParam<LEDButton>(Vec(60, 61-1), module, DTROY::RUN_PARAM, 0.0, 1.0, 0.0));
	
	//addChild(createLight<SmallLight<GreenLight>>(Vec(65, 65), module, DTROY::RUNNING_LIGHT));
	addChild(createValueLight<SmallLight<GreenValueLight>>(Vec(65, 65), &module->runningLight));
	
	addParam(createParam<LEDButton>(Vec(99, 61-1), module, DTROY::RESET_PARAM, 0.0, 1.0, 0.0));
	
	//addChild(createLight<SmallLight<GreenLight>>(Vec(104, 65), module, DTROY::RESET_LIGHT));
	addChild(createValueLight<SmallLight<GreenValueLight>>(Vec(104, 65), &module->resetLight));
	
	addParam(createParam<RoundSmallBlackSnapKnob>(Vec(132, 56), module, DTROY::STEPS_PARAM, 1.0, 8.0, 8.0));
	
	static const float portX0[4] = {20, 58, 96, 135};
 	addInput(createInput<PJ301MPort>(Vec(portX0[0], 98), module, DTROY::CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 98), module, DTROY::EXT_CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 98), module, DTROY::RESET_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[3], 98), module, DTROY::STEPS_INPUT));
	
	addParam(createParam<RoundSmallBlackKnob>(Vec(portX0[0]-1, 150), module, DTROY::ROOT_NOTE_PARAM, 0.0, NUM_NOTES-1, 0));
	addParam(createParam<RoundSmallBlackKnob>(Vec(portX0[1]-1, 150), module, DTROY::SCALE_PARAM, 0.0, NUM_SCALES-1, 0));
		
	addParam(createParam<RoundSmallBlackKnob>(Vec(portX0[2]-1, 150), module, DTROY::GATE_TIME_PARAM, 0.1, 1.0, 0.2));
	addParam(createParam<RoundSmallBlackKnob>(Vec(portX0[3]-1, 150), module, DTROY::SLIDE_TIME_PARAM	, 0.1, 1.0, 0.2));
	
	addInput(createInput<PJ301MPort>(Vec(portX0[0], 192), module, DTROY::ROOT_NOTE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 192), module, DTROY::SCALE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 192), module, DTROY::GATE_TIME_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[3], 192), module, DTROY::SLIDE_TIME_INPUT));
	
	static const float portX1[8] = {200, 238, 276, 315, 353, 392, 430, 469};

	for (int i = 0; i < 8; i++) {
		addParam(createParam<RoundSmallBlackKnob>(Vec(portX1[i]-2, 56), module, DTROY::TRIG_PITCH_PARAM + i, 0.0, 10.0, 0.0));
		addParam(createParam<CKSS8>(Vec(portX1[i]+2, 110), module, DTROY::TRIG_COUNT_PARAM + i, 0.0, 7.0, 0.0));
		addParam(createParam<CKSS4>(Vec(portX1[i]+2, 230), module, DTROY::TRIG_TYPE_PARAM + i, 0.0, 3.0, 0.0));
		addParam(createParam<LEDButton>(Vec(portX1[i]+2, 302-1), module, DTROY::TRIG_SLIDE_PARAM + i, 0.0, 1.0, 0.0));
		
		//addChild(createLight<SmallLight<GreenLight>>(Vec(portX1[i]+7, 306), module, DTROY::SLIDE_LIGHTS + i));
		addChild(createValueLight<SmallLight<GreenValueLight>>(Vec(portX1[i]+7, 306), &module->slideLights[i]));
		
		addParam(createParam<LEDButton>(Vec(portX1[i]+2, 327-1), module, DTROY::TRIG_SKIP_PARAM + i, 0.0, 1.0, 0.0));
		
		//addChild(createLight<SmallLight<GreenLight>>(Vec(portX1[i]+7, 331), module, DTROY::SKIP_LIGHTS + i));
		addChild(createValueLight<SmallLight<GreenValueLight>>(Vec(portX1[i]+7, 331), &module->skipLights[i]));
		
		
	}
	
	addOutput(createOutput<PJ301MPort>(Vec(portX0[1]-1, 331), module, DTROY::GATE_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[2]-1, 331), module, DTROY::PITCH_OUTPUT));
	
	// Debug outputs
/*  	addOutput(createOutput<PJ301MPort>(Vec(portX0[0]-1, 220), module, DTROY::INDEX_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[1]-1, 220), module, DTROY::FLOOR_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[2]-1, 220), module, DTROY::PHASE_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[3]-1, 220), module, DTROY::PULSE_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[0]-1, 250), module, DTROY::TRIG1COUNT_OUTPUT));  */
	
}



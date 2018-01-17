#include "Bidoo.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include <ctime>
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>

using namespace std;

struct Step {
	int index = 0;
	int number = 0;
	bool skip = false;
	bool skipParam = false;
	bool slide = false;
	int pulses = 1;
	int pulsesParam = 1;
	float pitch = 3;
	int type = 2;
};

struct Pattern {
	int playMode = 0;
	int countMode = 0;
	int numberOfSteps = 8;
	int numberOfStepsParam = 8;
	int rootNote = 0;
	int rootNoteParam = 0;
	int scale = 0;
	int scaleParam = 0;
	float gateTime = 0.5;
	float slideTime = 0.2;
	float sensitivity = 1;
	int currentStep = 0;
	int currentPulse = 0;
	bool forward = true;
	std::vector<Step> steps {16};

	void Update(int playMode, int countMode, int numberOfSteps, int numberOfStepsParam, int rootNote, int scale, float gateTime, float slideTime, float sensitivity, std::vector<char> skips, std::vector<char> slides, std::vector<Param> pulses, std::vector<Param> pitches, std::vector<Param> types) {
		this->playMode = playMode;
		this->countMode = countMode;
		this->numberOfSteps = numberOfSteps;
		this->numberOfStepsParam = numberOfStepsParam;
		this->rootNote = rootNote;
		this->scale = scale;
		this->rootNoteParam = rootNote;
		this->scaleParam = scale;
		this->gateTime = gateTime;
		this->slideTime = slideTime;
		this->sensitivity = sensitivity;
		int pCount = 0;
		for (int i = 0; i < 16; i++) {
			steps[i].index = i%8;
			steps[i].number = i;
			if (((countMode == 0) && (i < numberOfSteps)) || ((countMode == 1) && (pCount < numberOfSteps))) {
				steps[i].skip = (skips[i%8] == 't');
			}	else {
				steps[i].skip = true;
			}
			steps[i].skipParam = (skips[i%8] == 't');
			steps[i].slide = (slides[i%8] == 't');
			if ((countMode == 1) && ((pCount + (int)pulses[i%8].value) >= numberOfSteps)) {
				steps[i].pulses = max(numberOfSteps - pCount, 0);
			}	else {
				steps[i].pulses = (int)pulses[i%8].value;
			}
			steps[i].pulsesParam = (int)pulses[i%8].value;
			pCount = pCount + steps[i].pulses;
			steps[i].pitch = pitches[i%8].value;
			steps[i].type = (int)types[i%8].value;
		}
	}

	std::tuple<int, int> GetNextStep(bool reset)
	{
		if (reset) {
			if (playMode != 1) {
				currentStep = GetFirstStep();
			} else {
				currentStep = GetLastStep();
			}
			currentPulse = 0;
			return std::make_tuple(currentStep,currentPulse);
		} else {
			if (currentPulse < steps[currentStep].pulses - 1) {
				currentPulse++;
				return std::make_tuple(steps[currentStep%16].index,currentPulse);
			} else {
				if (playMode == 0) {
					currentStep = GetNextStepForward(currentStep);
					currentPulse = 0;
					return std::make_tuple(steps[currentStep%16].index,currentPulse);
				} else if (playMode == 1) {
					currentStep = GetNextStepBackward(currentStep);
					currentPulse = 0;
					return std::make_tuple(steps[currentStep%16].index,currentPulse);
				} else if (playMode == 2) {
					if (currentStep == GetLastStep()) {
						forward = false;
					}
					if (currentStep == GetFirstStep()) {
						forward = true;
					}
					if (forward) {
						currentStep = GetNextStepForward(currentStep);
					} else {
						currentStep = GetNextStepBackward(currentStep);
					}
					currentPulse = 0;
					return std::make_tuple(steps[currentStep%16].index,currentPulse);
				} else if (playMode == 3) {
					std::vector<Step> tmp (steps.size());
				  auto it = std::copy_if (steps.begin(), steps.end(), tmp.begin(), [](Step i){return !(i.skip);} );
				  tmp.resize(std::distance(tmp.begin(),it));  // shrink container to new size
					Step tmpStep = *select_randomly(tmp.begin(), tmp.end());
					currentPulse = 0;
					currentStep = tmpStep.number;
					return std::make_tuple(steps[currentStep%16].index,currentPulse);
				} else if (playMode == 4) {
					int next = GetNextStepForward(currentStep);
					int prev = GetNextStepBackward(currentStep);
					vector<Step> subPattern;
					subPattern.push_back(steps[prev]);
					subPattern.push_back(steps[next]);
					Step choice = *select_randomly(subPattern.begin(), subPattern.end());
					currentPulse = 0;
					currentStep = choice.number;
					return std::make_tuple(steps[currentStep%16].index,currentPulse);
				} else {
					return std::make_tuple(0,0);
				}
			}
		}
	}

	Step CurrentStep() {
		return this->steps[currentStep];
	}

	int GetFirstStep()
	{
			for (int i = 0; i < 16; i++) {
				if (!steps[i].skip) {
					return i;
				}
			}
			return 0;
	}

	int GetLastStep()
	{
			for (int i = 15; i >= 0  ; i--) {
				if (!steps[i].skip) {
					return i;
				}
			}
			return 15;
	}

	int GetNextStepForward(int pos)
	{
			for (int i = pos + 1; i < pos + 16; i++) {
				if (!steps[i%16].skip) {
					return i%16;
				}
			}
			return pos;
	}

	int GetNextStepBackward(int pos)
	{
			for (int i = pos - 1; i > pos - 16; i--) {
				if (!steps[i%16 + (i<0?16:0)].skip) {
					return i%16 + (i<0?16:0);
				}
			}
			return pos;
	}

	template<typename Iter, typename RandomGenerator> Iter select_randomly(Iter start, Iter end, RandomGenerator& g) {
		std::uniform_int_distribution<> dis(0, std::distance(start, end) - 1);
		std::advance(start, dis(g));
		return start;
	}

	template<typename Iter> Iter select_randomly(Iter start, Iter end) {
		static std::random_device rd;
		static std::mt19937 gen(rd());
		return select_randomly(start, end, gen);
	}

};

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
		PLAY_MODE_PARAM,
		COUNT_MODE_PARAM,
		PATTERN_PARAM,
		SENSITIVITY_PARAM,
		TRIG_COUNT_PARAM = SENSITIVITY_PARAM + 8,
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
		EXTGATE1_INPUT,
		EXTGATE2_INPUT,
		PATTERN_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		GATE_OUTPUT,
		PITCH_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		RUNNING_LIGHT,
		RESET_LIGHT,
		GATE_LIGHT,
		STEPS_LIGHTS = GATE_LIGHT + 8,
		SLIDES_LIGHTS = STEPS_LIGHTS + 8,
		SKIPS_LIGHTS = SLIDES_LIGHTS + 8,
		NUM_LIGHTS = SKIPS_LIGHTS + 8
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
		AEOLIAN,
		BLUES,
		CHROMATIC,
		DIATONIC_MINOR,
		DORIAN,
		HARMONIC_MINOR,
		INDIAN,
		LOCRIAN,
		LYDIAN,
		MAJOR,
		MELODIC_MINOR,
		MINOR,
		MIXOLYDIAN,
		NATURAL_MINOR,
		PENTATONIC,
		PHRYGIAN,
		TURKISH,
		NONE,
		NUM_SCALES
	};

	bool running = true;
	SchmittTrigger clockTrigger;
	SchmittTrigger runningTrigger;
	SchmittTrigger resetTrigger;
	SchmittTrigger slideTriggers[8];
	SchmittTrigger skipTriggers[8];
	SchmittTrigger playModeTrigger;
	SchmittTrigger countModeTrigger;
	SchmittTrigger PatternTrigger;
	float phase = 0;
	int index = 0;
	bool reStart = true;
	int pulse = 0;
	int rootNote = 0;
	int curScaleVal = 0;
	float pitch = 0;
	float previousPitch = 0;
	clock_t tCurrent;
	clock_t tLastTrig;
	clock_t tPreviousTrig;
	std::vector<char> slideState = {'f','f','f','f','f','f','f','f'};
	std::vector<char> skipState = {'f','f','f','f','f','f','f','f'};
	int playMode = 0; // 0 forward, 1 backward, 2 pingpong, 3 random, 4 brownian
	int countMode = 0; // 0 steps, 1 pulses
	int numSteps = 8;
	int selectedPattern = 0;
	int playedPattern = 0;
	bool pitchMode = false;
	bool updateFlag = false;

	Pattern patterns[16];

	DTROY() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
		UpdatePattern();
	}

	void UpdatePattern() {
		std::vector<Param> pulses(&params[TRIG_COUNT_PARAM],&params[TRIG_COUNT_PARAM + 8]);
		std::vector<Param> pitches(&params[TRIG_PITCH_PARAM],&params[TRIG_PITCH_PARAM + 8]);
		std::vector<Param> types(&params[TRIG_TYPE_PARAM],&params[TRIG_TYPE_PARAM + 8]);
		patterns[selectedPattern].Update(playMode, countMode, numSteps, roundf(params[STEPS_PARAM].value), roundf(params[ROOT_NOTE_PARAM].value), roundf(params[SCALE_PARAM].value), params[GATE_TIME_PARAM].value, params[SLIDE_TIME_PARAM].value, params[SENSITIVITY_PARAM].value , skipState, slideState, pulses, pitches, types);
	}

	void step() override;

	// persistence, random & init

	json_t *toJson() override {
		json_t *rootJ = json_object();

		json_object_set_new(rootJ, "running", json_boolean(running));
		json_object_set_new(rootJ, "playMode", json_integer(playMode));
		json_object_set_new(rootJ, "countMode", json_integer(countMode));
		json_object_set_new(rootJ, "pitchMode", json_boolean(pitchMode));
		json_object_set_new(rootJ, "selectedPattern", json_integer(selectedPattern));
		json_object_set_new(rootJ, "playedPattern", json_integer(playedPattern));

		json_t *trigsJ = json_array();
		for (int i = 0; i < 8; i++) {
			json_t *trigJ = json_array();
			json_t *trigJSlide = json_boolean(slideState[i] == 't');
			json_array_append_new(trigJ, trigJSlide);
			json_t *trigJSkip = json_boolean(skipState[i] == 't');
			json_array_append_new(trigJ, trigJSkip);
			json_array_append_new(trigsJ, trigJ);
		}
		json_object_set_new(rootJ, "trigs", trigsJ);

		for (int i = 0; i<16; i++) {
			json_t *patternJ = json_object();
			json_object_set_new(patternJ, "playMode", json_integer(patterns[i].playMode));
			json_object_set_new(patternJ, "countMode", json_integer(patterns[i].countMode));
			json_object_set_new(patternJ, "numSteps", json_integer(patterns[i].numberOfStepsParam));
			json_object_set_new(patternJ, "rootNote", json_integer(patterns[i].rootNote));
			json_object_set_new(patternJ, "scale", json_integer(patterns[i].scale));
			json_object_set_new(patternJ, "gateTime", json_real(patterns[i].gateTime));
			json_object_set_new(patternJ, "slideTime", json_real(patterns[i].slideTime));
			json_object_set_new(patternJ, "sensitivity", json_real(patterns[i].sensitivity));
			for (int j = 0; j < 16; j++) {
				json_t *stepJ = json_object();
				json_object_set_new(stepJ, "index", json_integer(patterns[i].steps[j].index));
				json_object_set_new(stepJ, "number", json_integer(patterns[i].steps[j].number));
				json_object_set_new(stepJ, "skip", json_integer((int)patterns[i].steps[j].skip));
				json_object_set_new(stepJ, "skipParam", json_integer((int)patterns[i].steps[j].skipParam));
				json_object_set_new(stepJ, "slide", json_integer((int)patterns[i].steps[j].slide));
				json_object_set_new(stepJ, "pulses", json_integer(patterns[i].steps[j].pulses));
				json_object_set_new(stepJ, "pulsesParam", json_integer(patterns[i].steps[j].pulsesParam));
				json_object_set_new(stepJ, "pitch", json_real(patterns[i].steps[j].pitch));
				json_object_set_new(stepJ, "type", json_integer(patterns[i].steps[j].type));
				json_object_set_new(patternJ, ("step" + to_string(j)).c_str() , stepJ);
			}
			json_object_set_new(rootJ, ("pattern" + to_string(i)).c_str(), patternJ);
		}
		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		json_t *runningJ = json_object_get(rootJ, "running");
		if (runningJ)
			running = json_is_true(runningJ);
		json_t *playModeJ = json_object_get(rootJ, "playMode");
		if (playModeJ)
			playMode = json_integer_value(playModeJ);
		json_t *countModeJ = json_object_get(rootJ, "countMode");
		if (countModeJ)
			countMode = json_integer_value(countModeJ);
		json_t *selectedPatternJ = json_object_get(rootJ, "selectedPattern");
		if (selectedPatternJ)
			selectedPattern = json_integer_value(selectedPatternJ);
		json_t *playedPatternJ = json_object_get(rootJ, "playedPattern");
		if (playedPatternJ)
			playedPattern = json_integer_value(playedPatternJ);
		json_t *pitchModeJ = json_object_get(rootJ, "pitchMode");
		if (pitchModeJ)
			pitchMode = json_is_true(pitchModeJ);
		json_t *trigsJ = json_object_get(rootJ, "trigs");
		if (trigsJ) {
			for (int i = 0; i < 8; i++) {
				json_t *trigJ = json_array_get(trigsJ, i);
				if (trigJ)
				{
					slideState[i] = json_is_true(json_array_get(trigJ, 0)) ? 't' : 'f';
					skipState[i] = json_is_true(json_array_get(trigJ, 1)) ? 't' : 'f';
				}
			}
		}
		for (int i=0; i<16;i++) {
			json_t *patternJ = json_object_get(rootJ, ("pattern" + to_string(i)).c_str());
			if (patternJ){
				json_t *playModeJ = json_object_get(patternJ, "playMode");
				if (playModeJ)
					patterns[i].playMode = json_integer_value(playModeJ);
				json_t *countModeJ = json_object_get(patternJ, "countMode");
				if (countModeJ)
					patterns[i].countMode = json_integer_value(countModeJ);
				json_t *numStepsJ = json_object_get(patternJ, "numSteps");
				if (numStepsJ)
					patterns[i].numberOfStepsParam = json_integer_value(numStepsJ);
				json_t *rootNoteJ = json_object_get(patternJ, "rootNote");
				if (rootNoteJ)
					patterns[i].rootNote = json_integer_value(rootNoteJ);
				json_t *scaleJ = json_object_get(patternJ, "scale");
				if (scaleJ)
					patterns[i].scale = json_integer_value(scaleJ);
				json_t *gateTimeJ = json_object_get(patternJ, "gateTime");
				if (gateTimeJ)
					patterns[i].gateTime = json_real_value(gateTimeJ);
				json_t *slideTimeJ = json_object_get(patternJ, "slideTime");
				if (slideTimeJ)
					patterns[i].slideTime = json_real_value(slideTimeJ);
				json_t *sensitivityJ = json_object_get(patternJ, "sensitivity");
				if (sensitivityJ)
					patterns[i].sensitivity = json_real_value(sensitivityJ);
				for (int j = 0; j < 16; j++) {
					json_t *stepJ = json_object_get(patternJ, ("step" + to_string(j)).c_str());
					if (stepJ) {
						json_t *indexJ= json_object_get(stepJ, "index");
						if (indexJ)
							patterns[i].steps[j].index = json_integer_value(indexJ);
						json_t *numberJ= json_object_get(stepJ, "numer");
						if (numberJ)
							patterns[i].steps[j].number = json_integer_value(numberJ);
						json_t *skipJ= json_object_get(stepJ, "skip");
						if (skipJ)
							patterns[i].steps[j].skip = !!json_integer_value(skipJ);
						json_t *skipParamJ= json_object_get(stepJ, "skipParam");
						if (skipParamJ)
							patterns[i].steps[j].skipParam = !!json_integer_value(skipParamJ);
						json_t *slideJ= json_object_get(stepJ, "slide");
						if (slideJ)
							patterns[i].steps[j].slide = !!json_integer_value(slideJ);
						json_t *pulsesJ= json_object_get(stepJ, "pulses");
						if (pulsesJ)
							patterns[i].steps[j].pulses = json_integer_value(pulsesJ);
						json_t *pulsesParamJ= json_object_get(stepJ, "pulsesParam");
						if (pulsesParamJ)
							patterns[i].steps[j].pulsesParam = json_integer_value(pulsesParamJ);
						json_t *pitchJ= json_object_get(stepJ, "pitch");
						if (pitchJ)
							patterns[i].steps[j].pitch = json_real_value(pitchJ);
						json_t *typeJ= json_object_get(stepJ, "type");
						if (typeJ)
							patterns[i].steps[j].type = json_integer_value(typeJ);
					}
				}
			}
		}
	}

	void randomize() override {
		randomizeSlidesSkips();
	}

	void randomizeSlidesSkips() {
		for (int i = 0; i < 8; i++) {
			slideState[i] = (randomf() > 0.8) ? 't' : 'f';
			skipState[i] = (randomf() > 0.85) ? 't' : 'f';
		}
	}

	void reset() override {
		for (int i = 0; i < 8; i++) {
			slideState[i] = 'f';
			skipState[i] = 'f';
		}
	}

	// Quantization inspired from  https://github.com/jeremywen/JW-Modules

	float getOneRandomNoteInScale(){
		rootNote = clampi(patterns[playedPattern].rootNote + inputs[ROOT_NOTE_INPUT].value, 0.0, NUM_NOTES-1);
		curScaleVal = clampi(patterns[playedPattern].scale + inputs[SCALE_INPUT].value, 0.0, NUM_SCALES-1);
		int *curScaleArr;
		int notesInScale = 0;
		switch(curScaleVal){
			case AEOLIAN:        curScaleArr = SCALE_AEOLIAN;       notesInScale=LENGTHOF(SCALE_AEOLIAN); break;
			case BLUES:          curScaleArr = SCALE_BLUES;         notesInScale=LENGTHOF(SCALE_BLUES); break;
			case CHROMATIC:      curScaleArr = SCALE_CHROMATIC;     notesInScale=LENGTHOF(SCALE_CHROMATIC); break;
			case DIATONIC_MINOR: curScaleArr = SCALE_DIATONIC_MINOR;notesInScale=LENGTHOF(SCALE_DIATONIC_MINOR); break;
			case DORIAN:         curScaleArr = SCALE_DORIAN;        notesInScale=LENGTHOF(SCALE_DORIAN); break;
			case HARMONIC_MINOR: curScaleArr = SCALE_HARMONIC_MINOR;notesInScale=LENGTHOF(SCALE_HARMONIC_MINOR); break;
			case INDIAN:         curScaleArr = SCALE_INDIAN;        notesInScale=LENGTHOF(SCALE_INDIAN); break;
			case LOCRIAN:        curScaleArr = SCALE_LOCRIAN;       notesInScale=LENGTHOF(SCALE_LOCRIAN); break;
			case LYDIAN:         curScaleArr = SCALE_LYDIAN;        notesInScale=LENGTHOF(SCALE_LYDIAN); break;
			case MAJOR:          curScaleArr = SCALE_MAJOR;         notesInScale=LENGTHOF(SCALE_MAJOR); break;
			case MELODIC_MINOR:  curScaleArr = SCALE_MELODIC_MINOR; notesInScale=LENGTHOF(SCALE_MELODIC_MINOR); break;
			case MINOR:          curScaleArr = SCALE_MINOR;         notesInScale=LENGTHOF(SCALE_MINOR); break;
			case MIXOLYDIAN:     curScaleArr = SCALE_MIXOLYDIAN;    notesInScale=LENGTHOF(SCALE_MIXOLYDIAN); break;
			case NATURAL_MINOR:  curScaleArr = SCALE_NATURAL_MINOR; notesInScale=LENGTHOF(SCALE_NATURAL_MINOR); break;
			case PENTATONIC:     curScaleArr = SCALE_PENTATONIC;    notesInScale=LENGTHOF(SCALE_PENTATONIC); break;
			case PHRYGIAN:       curScaleArr = SCALE_PHRYGIAN;      notesInScale=LENGTHOF(SCALE_PHRYGIAN); break;
			case TURKISH:        curScaleArr = SCALE_TURKISH;       notesInScale=LENGTHOF(SCALE_TURKISH); break;
		}

		if(curScaleVal == NONE){
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
		rootNote = clampi(patterns[playedPattern].rootNote + inputs[ROOT_NOTE_INPUT].value, 0.0, DTROY::NUM_NOTES-1);
		curScaleVal = clampi(patterns[playedPattern].scale + inputs[SCALE_INPUT].value, 0.0, DTROY::NUM_SCALES-1);
		int *curScaleArr;
		int notesInScale = 0;
		switch(curScaleVal){
			case AEOLIAN:        curScaleArr = SCALE_AEOLIAN;       notesInScale=LENGTHOF(SCALE_AEOLIAN); break;
			case BLUES:          curScaleArr = SCALE_BLUES;         notesInScale=LENGTHOF(SCALE_BLUES); break;
			case CHROMATIC:      curScaleArr = SCALE_CHROMATIC;     notesInScale=LENGTHOF(SCALE_CHROMATIC); break;
			case DIATONIC_MINOR: curScaleArr = SCALE_DIATONIC_MINOR;notesInScale=LENGTHOF(SCALE_DIATONIC_MINOR); break;
			case DORIAN:         curScaleArr = SCALE_DORIAN;        notesInScale=LENGTHOF(SCALE_DORIAN); break;
			case HARMONIC_MINOR: curScaleArr = SCALE_HARMONIC_MINOR;notesInScale=LENGTHOF(SCALE_HARMONIC_MINOR); break;
			case INDIAN:         curScaleArr = SCALE_INDIAN;        notesInScale=LENGTHOF(SCALE_INDIAN); break;
			case LOCRIAN:        curScaleArr = SCALE_LOCRIAN;       notesInScale=LENGTHOF(SCALE_LOCRIAN); break;
			case LYDIAN:         curScaleArr = SCALE_LYDIAN;        notesInScale=LENGTHOF(SCALE_LYDIAN); break;
			case MAJOR:          curScaleArr = SCALE_MAJOR;         notesInScale=LENGTHOF(SCALE_MAJOR); break;
			case MELODIC_MINOR:  curScaleArr = SCALE_MELODIC_MINOR; notesInScale=LENGTHOF(SCALE_MELODIC_MINOR); break;
			case MINOR:          curScaleArr = SCALE_MINOR;         notesInScale=LENGTHOF(SCALE_MINOR); break;
			case MIXOLYDIAN:     curScaleArr = SCALE_MIXOLYDIAN;    notesInScale=LENGTHOF(SCALE_MIXOLYDIAN); break;
			case NATURAL_MINOR:  curScaleArr = SCALE_NATURAL_MINOR; notesInScale=LENGTHOF(SCALE_NATURAL_MINOR); break;
			case PENTATONIC:     curScaleArr = SCALE_PENTATONIC;    notesInScale=LENGTHOF(SCALE_PENTATONIC); break;
			case PHRYGIAN:       curScaleArr = SCALE_PHRYGIAN;      notesInScale=LENGTHOF(SCALE_PHRYGIAN); break;
			case TURKISH:        curScaleArr = SCALE_TURKISH;       notesInScale=LENGTHOF(SCALE_TURKISH); break;
			case NONE:           return voltsIn;
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
	lights[RUNNING_LIGHT].value = running ? 1.0 : 0.0;
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
				phase += clockTime / engineGetSampleRate();
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
			phase += clockTime / engineGetSampleRate();
			if (phase >= 1.0) {
				phase--;
				nextStep = true;
			}
		}
	}
	// Reset
	if (resetTrigger.process(params[RESET_PARAM].value + inputs[RESET_INPUT].value)) {
		phase = 0.0;
		reStart = true;
		nextStep = true;
		lights[RESET_LIGHT].value = 1.0;
	}
	//patternNumber
	playedPattern = clampi((inputs[PATTERN_INPUT].active ? rescalef(inputs[PATTERN_INPUT].value,0,10,1,16.1) : params[PATTERN_PARAM].value) - 1, 0, 15);
	// Update Pattern
	if (updateFlag) {
		// Trigs Update
		for (int i = 0; i < 8; i++) {
			if (slideTriggers[i].process(params[TRIG_SLIDE_PARAM + i].value)) {
				slideState[i] = slideState[i] == 't' ? 'f' : 't';
			}
			if (skipTriggers[i].process(params[TRIG_SKIP_PARAM + i].value)) {
				skipState[i] = skipState[i] == 't' ? 'f' : 't';
			}
		}
		// playMode
		if (playModeTrigger.process(params[PLAY_MODE_PARAM].value)) {
			playMode = (((int)playMode + 1) % 5);
		}
		// countMode
		if (countModeTrigger.process(params[COUNT_MODE_PARAM].value)) {
			countMode = (((int)countMode + 1) % 2);
		}
		// numSteps
		numSteps = clampi(roundf(params[STEPS_PARAM].value + inputs[STEPS_INPUT].value), 1, 16);
		UpdatePattern();
	}
	// Steps && Pulses Management
	if (nextStep) {
		// Advance step
		previousPitch = closestVoltageInScale(patterns[playedPattern].CurrentStep().pitch);
		auto nextT = patterns[playedPattern].GetNextStep(reStart);
		index = std::get<0>(nextT);
		pulse = std::get<1>(nextT);
		if (reStart) { reStart = false; }
		lights[STEPS_LIGHTS+index%8].value = 1.0;
	}
	// Lights
	for (int i = 0; i < 8; i++) {
		lights[STEPS_LIGHTS + i].value -= lights[STEPS_LIGHTS + i].value / lightLambda / engineGetSampleRate();
		lights[SLIDES_LIGHTS + i].value = slideState[i] == 't' ? 1 - lights[STEPS_LIGHTS + i].value : lights[STEPS_LIGHTS + i].value;
		lights[SKIPS_LIGHTS + i].value = skipState[i]== 't' ? 1 - lights[STEPS_LIGHTS + i].value : lights[STEPS_LIGHTS + i].value;
	}
	lights[RESET_LIGHT].value -= lights[RESET_LIGHT].value / lightLambda / engineGetSampleRate();

	// Caclulate Outputs
	bool gateOn = running && (!patterns[playedPattern].CurrentStep().skip);
	float gateValue = 0.0;
	if (gateOn){
		if (patterns[playedPattern].CurrentStep().type == 0) {
			gateOn = false;
		}
		else if (((patterns[playedPattern].CurrentStep().type == 1) && (pulse == 0))
				|| (patterns[playedPattern].CurrentStep().type == 2)
				|| ((patterns[playedPattern].CurrentStep().type == 3) && (pulse == patterns[playedPattern].CurrentStep().pulses))) {
				float gateCoeff = clampf(patterns[playedPattern].gateTime - 0.02 + inputs[GATE_TIME_INPUT].value /10, 0.0, 0.99);
			gateOn = phase < gateCoeff;
			gateValue = 10.0;
		}
		else if (patterns[playedPattern].CurrentStep().type == 3) {
			gateOn = true;
			gateValue = 10.0;
		}
		else if (patterns[playedPattern].CurrentStep().type == 4) {
			gateOn = true;
			gateValue = inputs[EXTGATE1_INPUT].value;
		}
		else if (patterns[playedPattern].CurrentStep().type == 5) {
			gateOn = true;
			gateValue = inputs[EXTGATE2_INPUT].value;
		}
		else {
			gateOn = false;
			gateValue = 0.0;
		}
	}
	//pitch management
	pitch = closestVoltageInScale(patterns[playedPattern].CurrentStep().pitch * patterns[playedPattern].sensitivity);
	if (patterns[playedPattern].CurrentStep().slide) {
		if (pulse == 0) {
			float slideCoeff = clampf(patterns[playedPattern].slideTime - 0.01 + inputs[SLIDE_TIME_INPUT].value /10, -0.1, 0.99);
			pitch = pitch - (1 - powf(phase, slideCoeff)) * (pitch - previousPitch);
		}
	}
	// Update Outputs
	outputs[GATE_OUTPUT].value = gateOn ? gateValue : 0.0;
	outputs[PITCH_OUTPUT].value = pitchMode ? pitch : (gateOn ? pitch : 0);
}

struct DTROYDisplay : TransparentWidget {
	DTROY *module;
	int frame = 0;
	shared_ptr<Font> font;

	string note, scale, steps, playMode, selectedPattern, playedPattern;

	DTROYDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void drawMessage(NVGcontext *vg, Vec pos, string note, string playMode, string selectedPattern, string playedPattern, string steps, string scale) {
		nvgFontSize(vg, 18);
		nvgFontFaceId(vg, font->handle);
		nvgTextLetterSpacing(vg, -2);
		nvgFillColor(vg, YELLOW_BIDOO);
		nvgText(vg, pos.x + 4, pos.y + 8, playMode.c_str(), NULL);
		nvgFontSize(vg, 14);
		nvgFillColor(vg, YELLOW_BIDOO);
		nvgText(vg, pos.x + 91, pos.y + 7, selectedPattern.c_str(), NULL);
		nvgText(vg, pos.x + 31, pos.y + 7, steps.c_str(), NULL);
		nvgText(vg, pos.x + 3, pos.y + 23, note.c_str(), NULL);
		nvgText(vg, pos.x + 25, pos.y + 23, scale.c_str(), NULL);
		nvgFillColor(vg, YELLOW_BIDOO);
		nvgText(vg, pos.x + 116, pos.y + 7, playedPattern.c_str(), NULL);
	}

	string displayRootNote(int value) {
		switch(value){
			case DTROY::NOTE_C:       return "C";
			case DTROY::NOTE_C_SHARP: return "C#";
			case DTROY::NOTE_D:       return "D";
			case DTROY::NOTE_D_SHARP: return "D#";
			case DTROY::NOTE_E:       return "E";
			case DTROY::NOTE_F:       return "F";
			case DTROY::NOTE_F_SHARP: return "F#";
			case DTROY::NOTE_G:       return "G";
			case DTROY::NOTE_G_SHARP: return "G#";
			case DTROY::NOTE_A:       return "A";
			case DTROY::NOTE_A_SHARP: return "A#";
			case DTROY::NOTE_B:       return "B";
			default: return "";
		}
	}

	string displayScale(int value) {
		switch(value){
			case DTROY::AEOLIAN:        return "Aeolian";
			case DTROY::BLUES:          return "Blues";
			case DTROY::CHROMATIC:      return "Chromatic";
			case DTROY::DIATONIC_MINOR: return "Diatonic Minor";
			case DTROY::DORIAN:         return "Dorian";
			case DTROY::HARMONIC_MINOR: return "Harmonic Minor";
			case DTROY::INDIAN:         return "Indian";
			case DTROY::LOCRIAN:        return "Locrian";
			case DTROY::LYDIAN:         return "Lydian";
			case DTROY::MAJOR:          return "Major";
			case DTROY::MELODIC_MINOR:  return "Melodic Minor";
			case DTROY::MINOR:          return "Minor";
			case DTROY::MIXOLYDIAN:     return "Mixolydian";
			case DTROY::NATURAL_MINOR:  return "Natural Minor";
			case DTROY::PENTATONIC:     return "Pentatonic";
			case DTROY::PHRYGIAN:       return "Phrygian";
			case DTROY::TURKISH:        return "Turkish";
			case DTROY::NONE:           return "None";
			default: return "";
		}
	}

	string displayPlayMode(int value) {
		switch(value){
			case 0: return "►";
			case 1: return "◄";
			case 2: return "►◄";
			case 3: return "►*";
			case 4: return "►?";
			default: return "";
		}
	}

	void draw(NVGcontext *vg) override {
		if (++frame >= 4) {
			frame = 0;
			note = displayRootNote(module->patterns[module->selectedPattern].rootNote);
			steps = (module->patterns[module->selectedPattern].countMode == 0 ? "steps:" : "pulses:" ) + to_string(module->patterns[module->selectedPattern].numberOfStepsParam);
			playMode = displayPlayMode(module->patterns[module->selectedPattern].playMode);
			scale = displayScale(module->patterns[module->selectedPattern].scale);
			selectedPattern = "P" + to_string(module->selectedPattern + 1);
			playedPattern = "P" + to_string(module->playedPattern + 1);
		}
		drawMessage(vg, Vec(0, 20), note, playMode, selectedPattern, playedPattern, steps, scale);
	}
};

struct DTROYPatternRoundSmallBlackSnapKnob : RoundSmallBlackSnapKnob {
	void onChange(EventChange &e) override {
			RoundSmallBlackSnapKnob::onChange(e);
			DTROY *module = dynamic_cast<DTROY*>(this->module);
			DTROYWidget *parent = dynamic_cast<DTROYWidget*>(this->parent);
			int target = clampi(value - 1, 0, 15);
			if (module && parent && (target != module->selectedPattern))
			{
				module->updateFlag = false;
				module->selectedPattern = value - 1;
				parent->stepsParam->setValue(module->patterns[target].numberOfStepsParam);
				parent->rootNoteParam->setValue(module->patterns[target].rootNote);
				parent->scaleParam->setValue(module->patterns[target].scale);
				parent->gateTimeParam->setValue(module->patterns[target].gateTime);
				parent->slideTimeParam->setValue(module->patterns[target].slideTime);
				parent->sensitivityParam->setValue(module->patterns[target].sensitivity);
				module->playMode = module->patterns[module->selectedPattern].playMode;
				module->countMode = module->patterns[module->selectedPattern].countMode;
				for (int i = 0; i < 8; i++) {
					parent->pitchParams[i]->setValue(module->patterns[target].steps[i].pitch);
					parent->pulseParams[i]->setValue(module->patterns[target].steps[i].pulsesParam);
					parent->typeParams[i]->setValue(module->patterns[target].steps[i].type);
					module->skipState[i] = module->patterns[target].steps[i].skipParam ? 't' : 'f';
					module->slideState[i] = module->patterns[target].steps[i].slide ? 't' : 'f';
				}
				module->updateFlag = true;
			}
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
		display->box.pos = Vec(20, 253);
		display->box.size = Vec(250, 60);
		addChild(display);
	}

	addParam(createParam<RoundSmallBlackKnob>(Vec(18, 52), module, DTROY::CLOCK_PARAM, -2.0, 6.0, 2.0));
	addParam(createParam<LEDButton>(Vec(61, 56), module, DTROY::RUN_PARAM, 0.0, 1.0, 0.0));
	addChild(createLight<SmallLight<GreenLight>>(Vec(67, 62), module, DTROY::RUNNING_LIGHT));
	addParam(createParam<LEDButton>(Vec(99, 56), module, DTROY::RESET_PARAM, 0.0, 1.0, 0.0));
	addChild(createLight<SmallLight<GreenLight>>(Vec(105, 62), module, DTROY::RESET_LIGHT));
	stepsParam = createParam<BidooBlueSnapKnob>(Vec(133, 52), module, DTROY::STEPS_PARAM, 1.0, 16.0, 8);
	addParam(stepsParam);

	static const float portX0[4] = {20, 58, 96, 135};
 	addInput(createInput<PJ301MPort>(Vec(portX0[0], 90), module, DTROY::CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 90), module, DTROY::EXT_CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 90), module, DTROY::RESET_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[3], 90), module, DTROY::STEPS_INPUT));

	rootNoteParam = createParam<BidooBlueSnapKnob>(Vec(portX0[0]-1, 140), module, DTROY::ROOT_NOTE_PARAM, 0.0, DTROY::NUM_NOTES-1 + 0.1, 0);
	addParam(rootNoteParam);
	scaleParam = createParam<BidooBlueSnapKnob>(Vec(portX0[1]-1, 140), module, DTROY::SCALE_PARAM, 0.0, DTROY::NUM_SCALES-1 + 0.1, 0);
	addParam(scaleParam);
	gateTimeParam = createParam<BidooBlueKnob>(Vec(portX0[2]-1, 140), module, DTROY::GATE_TIME_PARAM, 0.1, 1.0, 0.5);
	addParam(gateTimeParam);
	slideTimeParam = createParam<BidooBlueKnob>(Vec(portX0[3]-1, 140), module, DTROY::SLIDE_TIME_PARAM	, 0.1, 1.0, 0.2);
	addParam(slideTimeParam);

	addInput(createInput<PJ301MPort>(Vec(portX0[0], 180), module, DTROY::ROOT_NOTE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 180), module, DTROY::SCALE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 180), module, DTROY::GATE_TIME_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[3], 180), module, DTROY::SLIDE_TIME_INPUT));

	playModeParam = createParam<BlueCKD6>(Vec(portX0[0]-1, 230), module, DTROY::PLAY_MODE_PARAM, 0.0, 4.0, 0);
	addParam(playModeParam);
	countModeParam = createParam<BlueCKD6>(Vec(portX0[1]-1, 230), module, DTROY::COUNT_MODE_PARAM, 0.0, 4.0, 0);
	addParam(countModeParam);
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 232), module, DTROY::PATTERN_INPUT));
	patternParam = createParam<DTROYPatternRoundSmallBlackSnapKnob>(Vec(portX0[3],230), module, DTROY::PATTERN_PARAM, 1.0, 16.0, 1);
	addParam(patternParam);

	static const float portX1[8] = {200, 238, 276, 315, 353, 392, 430, 469};

	sensitivityParam = createParam<BidooBlueTrimpot>(Vec(portX1[6]+21, 31), module, DTROY::SENSITIVITY_PARAM, 0.1, 1, 1);
	addParam(sensitivityParam);

	for (int i = 0; i < 8; i++) {
		pitchParams[i] = createParam<BidooBlueKnob>(Vec(portX1[i]-2, 52), module, DTROY::TRIG_PITCH_PARAM + i, 0.0, 10.0, 3);
		addParam(pitchParams[i]);
		pulseParams[i] = createParam<BidooSlidePotLong>(Vec(portX1[i]+2, 103), module, DTROY::TRIG_COUNT_PARAM + i, 1.0, 8.0,  1);
		addParam(pulseParams[i]);
		typeParams[i] = createParam<BidooSlidePotShort>(Vec(portX1[i]+2, 220), module, DTROY::TRIG_TYPE_PARAM + i, 0.0, 5.0,  2);
		addParam(typeParams[i]);
		slideParams[i] = createParam<LEDButton>(Vec(portX1[i]+2, 313), module, DTROY::TRIG_SLIDE_PARAM + i, 0.0, 1.0,  0);
		addParam(slideParams[i]);
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[i]+8, 319), module, DTROY::SLIDES_LIGHTS + i));
		skipParams[i] = createParam<LEDButton>(Vec(portX1[i]+2, 338), module, DTROY::TRIG_SKIP_PARAM + i, 0.0, 1.0,  0);
		addParam(skipParams[i]);
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[i]+8, 344), module, DTROY::SKIPS_LIGHTS + i));
	}

	addInput(createInput<PJ301MPort>(Vec(portX0[0], 331), module, DTROY::EXTGATE1_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 331), module, DTROY::EXTGATE2_INPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[2]-1, 331), module, DTROY::GATE_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[3]-1, 331), module, DTROY::PITCH_OUTPUT));

	module->updateFlag = true;
}

struct DTROYRandPitchItem : MenuItem {
	DTROYWidget *dtroyWidget;
	void onAction(EventAction &e) override {
		for (int i = 0; i < 8; i++){
			int index = DTROY::TRIG_PITCH_PARAM + i;
			auto it = std::find_if(dtroyWidget->params.begin(), dtroyWidget->params.end(), [&index](const ParamWidget* m) -> bool { return m->paramId == index; });
			if (it != dtroyWidget->params.end())
			{
				auto index = std::distance(dtroyWidget->params.begin(), it);
				dtroyWidget->params[index]->randomize();
			}
		}
	}
};

struct DTROYRandGatesItem : MenuItem {
	DTROYWidget *dtroyWidget;
	void onAction(EventAction &e) override {
		for (int i = 0; i < 8; i++){
			int index = DTROY::TRIG_COUNT_PARAM + i;
			auto it = std::find_if(dtroyWidget->params.begin(), dtroyWidget->params.end(), [&index](const ParamWidget* m) -> bool { return m->paramId == index; });
			if (it != dtroyWidget->params.end())
			{
				auto index = std::distance(dtroyWidget->params.begin(), it);
				dtroyWidget->params[index]->randomize();
			}
		}
		for (int i = 0; i < 8; i++){
				int index = DTROY::TRIG_TYPE_PARAM + i;
				auto it = std::find_if(dtroyWidget->params.begin(), dtroyWidget->params.end(), [&index](const ParamWidget* m) -> bool { return m->paramId == index; });
				if (it != dtroyWidget->params.end())
				{
					auto index = std::distance(dtroyWidget->params.begin(), it);
				dtroyWidget->params[index]->randomize();
			}
		}
	}
};

struct DTROYRandSlideSkipItem : MenuItem {
	DTROY *dtroyModule;
	void onAction(EventAction &e) override {
		dtroyModule->randomizeSlidesSkips();
	}
};

struct DTROYPitchModeItem : MenuItem {
	DTROY *dtroyModule;
	void onAction(EventAction &e) override {
		dtroyModule->pitchMode = !dtroyModule->pitchMode;
	}
	void step() override {
		rightText = dtroyModule->pitchMode ? "✔" : "";
		MenuItem::step();
	}
};

struct DisconnectMenuItem : MenuItem {
	ModuleWidget *moduleWidget;
	void onAction(EventAction &e) override {
		moduleWidget->disconnect();
	}
};

struct ResetMenuItem : MenuItem {
	DTROYWidget *dtroyWidget;
	DTROY *dtroy;
	void onAction(EventAction &e) override {
		for (int i = 0; i < DTROY::NUM_PARAMS; i++){
			if (i != DTROY::PATTERN_PARAM) {
				auto it = std::find_if(dtroyWidget->params.begin(), dtroyWidget->params.end(), [&i](const ParamWidget* m) -> bool { return m->paramId == i; });
				if (it != dtroyWidget->params.end())
				{
					auto index = std::distance(dtroyWidget->params.begin(), it);
					dtroyWidget->params[index]->setValue(dtroyWidget->params[index]->defaultValue);
				}
			}
		}
		dtroy->updateFlag = false;
		dtroy->reset();
		dtroy->playMode = 0;
		dtroy->countMode = 0;
		dtroy->updateFlag = true;
	}
};

struct RandomizeMenuItem : MenuItem {
	ModuleWidget *moduleWidget;
	void onAction(EventAction &e) override {
		moduleWidget->randomize();
	}
};

struct CloneMenuItem : MenuItem {
	ModuleWidget *moduleWidget;
	void onAction(EventAction &e) override {
		gRackWidget->cloneModule(moduleWidget);
	}
};

struct DeleteMenuItem : MenuItem {
	ModuleWidget *moduleWidget;
	void onAction(EventAction &e) override {
		gRackWidget->deleteModule(moduleWidget);
		moduleWidget->finalizeEvents();
		delete moduleWidget;
	}
};

Menu *DTROYWidget::createContextMenu() {
	DTROYWidget *dtroyWidget = dynamic_cast<DTROYWidget*>(this);
	assert(dtroyWidget);

	DTROY *dtroyModule = dynamic_cast<DTROY*>(module);
	assert(dtroyModule);

	Menu *menu = gScene->createMenu();

	MenuLabel *menuLabel = new MenuLabel();
	menuLabel->text = model->manufacturer + " " + model->name;
	menu->addChild(menuLabel);

	ResetMenuItem *resetItem = new ResetMenuItem();
	resetItem->text = "Initialize";
	resetItem->rightText = GUI_MOD_KEY_NAME "+I";
	resetItem->dtroyWidget = this;
	resetItem->dtroy = dtroyModule;
	menu->addChild(resetItem);

	DisconnectMenuItem *disconnectItem = new DisconnectMenuItem();
	disconnectItem->text = "Disconnect cables";
	disconnectItem->moduleWidget = this;
	menu->addChild(disconnectItem);

	CloneMenuItem *cloneItem = new CloneMenuItem();
	cloneItem->text = "Duplicate";
	cloneItem->rightText = GUI_MOD_KEY_NAME "+D";
	cloneItem->moduleWidget = this;
	menu->addChild(cloneItem);

	DeleteMenuItem *deleteItem = new DeleteMenuItem();
	deleteItem->text = "Delete";
	deleteItem->rightText = "Backspace/Delete";
	deleteItem->moduleWidget = this;
	menu->addChild(deleteItem);

	MenuLabel *spacerLabel = new MenuLabel();
	menu->addChild(spacerLabel);

	DTROYRandPitchItem *randomizePitchItem = new DTROYRandPitchItem();
	randomizePitchItem->text = "Randomize pitch";
	randomizePitchItem->dtroyWidget = dtroyWidget;
	menu->addChild(randomizePitchItem);

	DTROYRandGatesItem *randomizeGatesItem = new DTROYRandGatesItem();
	randomizeGatesItem->text = "Randomize gates";
	randomizeGatesItem->dtroyWidget = dtroyWidget;
	menu->addChild(randomizeGatesItem);

	DTROYRandSlideSkipItem *randomizeSlideSkipItem = new DTROYRandSlideSkipItem();
	randomizeSlideSkipItem->text = "Randomize slides and skips";
	randomizeSlideSkipItem->dtroyModule = dtroyModule;
	menu->addChild(randomizeSlideSkipItem);

	MenuLabel *spacerLabel2 = new MenuLabel();
	menu->addChild(spacerLabel2);

	DTROYPitchModeItem *pitchModeItem = new DTROYPitchModeItem();
	pitchModeItem->text = "Pitch mode continuous/triggered";
	pitchModeItem->dtroyModule = dtroyModule;
	menu->addChild(pitchModeItem);

	return menu;
}

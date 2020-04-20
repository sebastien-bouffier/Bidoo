#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
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
	float pitch = 3.0f;
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
	float gateTime = 0.5f;
	float slideTime = 0.2f;
	float sensitivity = 1.0f;
	int currentStep = 0;
	int currentPulse = 0;
	bool forward = true;
	std::vector<Step> steps {16};

	void Update(int playMode, int countMode, int numberOfSteps, int numberOfStepsParam, int rootNote, int scale, float gateTime, float slideTime, float sensitivity, vector<char> skips, vector<char> slides, Param *pulses, Param *pitches, Param *types) {
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
				steps[i].skip = (skips[steps[i].index] == 't');
			}	else {
				steps[i].skip = true;
			}
			steps[i].skipParam = (skips[steps[i].index] == 't');
			steps[i].slide = (slides[steps[i].index] == 't');
			if ((countMode == 1) && ((pCount + (int)pulses[steps[i].index].getValue()) >= numberOfSteps)) {
				steps[i].pulses = max(numberOfSteps - pCount, 0);
			}	else {
				steps[i].pulses = (int)(pulses + steps[i].index)->getValue();
			}
			steps[i].pulsesParam = (int)(pulses + steps[i].index)->getValue();
			steps[i].pitch = (pitches + steps[i].index)->getValue();
			steps[i].type = (int)(types + steps[i].index)->getValue();

			pCount = pCount + steps[i].pulses;
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

	inline Step CurrentStep() {
		return this->steps[currentStep];
	}

	inline int GetFirstStep()
	{
			for (int i = 0; i < 16; i++) {
				if (!steps[i].skip) {
					return i;
				}
			}
			return 0;
	}

	inline int GetLastStep()
	{
			for (int i = 15; i >= 0  ; i--) {
				if (!steps[i].skip) {
					return i;
				}
			}
			return 15;
	}

	inline int GetNextStepForward(int pos)
	{
			for (int i = pos + 1; i < pos + 16; i++) {
				int j = i%16;
				if (!steps[j].skip) {
					return j;
				}
			}
			return pos;
	}

	inline int GetNextStepBackward(int pos)
	{
			for (int i = pos - 1; i > pos - 16; i--) {
				int j = i%16;
				if (!steps[j + (i<0?16:0)].skip) {
					return j + (i<0?16:0);
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
		LEFT_PARAM = TRIG_SKIP_PARAM + 8,
		RIGHT_PARAM,
		UP_PARAM,
		DOWN_PARAM,
		COPY_PARAM,
		NUM_PARAMS
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
		TRANSPOSE_INPUT,
		SENSITIVITY_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		GATE_OUTPUT,
		PITCH_OUTPUT,
		STEP_OUTPUT,
		NUM_OUTPUTS = STEP_OUTPUT + 8
	};
	enum LightIds {
		RUNNING_LIGHT,
		RESET_LIGHT,
		GATE_LIGHT,
		STEPS_LIGHTS = GATE_LIGHT + 8,
		SLIDES_LIGHTS = STEPS_LIGHTS + 8,
		SKIPS_LIGHTS = SLIDES_LIGHTS + 8,
		COPY_LIGHT = SKIPS_LIGHTS + 8,
		NUM_LIGHTS
	};

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
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger runningTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger slideTriggers[8];
	dsp::SchmittTrigger skipTriggers[8];
	dsp::SchmittTrigger playModeTrigger;
	dsp::SchmittTrigger countModeTrigger;
	dsp::SchmittTrigger copyTrigger;
	dsp::SchmittTrigger upTrigger;
	dsp::SchmittTrigger downTrigger;
	dsp::SchmittTrigger leftTrigger;
	dsp::SchmittTrigger rightTrigger;
	float phase = 0.0f;
	int index = 0;
	bool reStart = true;
	int pulse = 0;
	int rootNote = 0;
	int curScaleVal = 0;
	float pitch = 0.0f;
	float previousPitch = 0.0f;
	float candidateForPreviousPitch = 0.0f;
	float tCurrent = 0.0f;
	float tLastTrig = 0.0f;
	std::vector<char> slideState = {'f','f','f','f','f','f','f','f'};
	std::vector<char> skipState = {'f','f','f','f','f','f','f','f'};
	int playMode = 0; // 0 forward, 1 backward, 2 pingpong, 3 random, 4 brownian
	int countMode = 0; // 0 steps, 1 pulses
	int numSteps = 8;
	int selectedPattern = 0;
	int playedPattern = 0;
	bool updateFlag = false;
	bool first = true;
	bool loadedFromJson = false;
	int copyPattern = -1;
	dsp::PulseGenerator stepPulse[8];
	bool stepOutputsMode = false;
	bool gateOn = false;
	const float invLightLambda = 13.333333333333333333333f;
	bool copyState = false;

	Pattern patterns[16];

	DTROY() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CLOCK_PARAM, -2.0f, 6.0f, 2.0f);
		configParam(RUN_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(RESET_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(STEPS_PARAM, 1.0f, 16.0f, 8.0f);
		configParam(ROOT_NOTE_PARAM, 0.0f, DTROY::NUM_NOTES-1.0f, 0.0f);
		configParam(SCALE_PARAM, 0.0f, DTROY::NUM_SCALES-1.0f, 0.0f);
		configParam(GATE_TIME_PARAM, 0.1f, 1.0f, 0.5f);
		configParam(SLIDE_TIME_PARAM	, 0.1f, 1.0f, 0.2f);
		configParam(PLAY_MODE_PARAM, 0.0f, 4.0f, 0.0f);
		configParam(COUNT_MODE_PARAM, 0.0f, 4.0f, 0.0f);
		configParam(PATTERN_PARAM, 1.0f, 16.0f, 1.0f);
		configParam(SENSITIVITY_PARAM, 0.1f, 1.0f, 1.0f);

		configParam(COPY_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(LEFT_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(RIGHT_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(UP_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(DOWN_PARAM, 0.0f, 1.0f, 0.0f);

		for (int i = 0; i < 8; i++) {
			configParam(TRIG_PITCH_PARAM + i, -4.0f, 6.0f, 0.0f);
			configParam(TRIG_COUNT_PARAM + i, 1.0f, 8.0f,  1.0f);
			configParam(TRIG_TYPE_PARAM + i, 0.0f, 5.0f,  2.0f);
			configParam(TRIG_SLIDE_PARAM + i, 0.0f, 1.0f,  0.0f);
			configParam(TRIG_SKIP_PARAM + i, 0.0f, 1.0f, 0.0f);
		}
	}

	void UpdatePattern();

	void process(const ProcessArgs &args) override;

	// persistence, random & init

	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		json_object_set_new(rootJ, "running", json_boolean(running));
		json_object_set_new(rootJ, "playMode", json_integer(playMode));
		json_object_set_new(rootJ, "countMode", json_integer(countMode));
		json_object_set_new(rootJ, "stepOutputsMode", json_boolean(stepOutputsMode));
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

	void dataFromJson(json_t *rootJ) override {
		json_t *runningJ = json_object_get(rootJ, "running");
		if (runningJ)
			running = json_boolean_value(runningJ);
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
		json_t *stepOutputsModeJ = json_object_get(rootJ, "stepOutputsMode");
		if (stepOutputsModeJ)
			stepOutputsMode = json_boolean_value(stepOutputsModeJ);
		json_t *trigsJ = json_object_get(rootJ, "trigs");
		if (trigsJ) {
			for (int i = 0; i < 8; i++) {
				json_t *trigJ = json_array_get(trigsJ, i);
				if (trigJ)
				{
					slideState[i] = json_boolean_value(json_array_get(trigJ, 0)) ? 't' : 'f';
					skipState[i] = json_boolean_value(json_array_get(trigJ, 1)) ? 't' : 'f';
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
					patterns[i].gateTime = json_number_value(gateTimeJ);
				json_t *slideTimeJ = json_object_get(patternJ, "slideTime");
				if (slideTimeJ)
					patterns[i].slideTime = json_number_value(slideTimeJ);
				json_t *sensitivityJ = json_object_get(patternJ, "sensitivity");
				if (sensitivityJ)
					patterns[i].sensitivity = json_number_value(sensitivityJ);
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
							patterns[i].steps[j].pitch = json_number_value(pitchJ);
						json_t *typeJ= json_object_get(stepJ, "type");
						if (typeJ)
							patterns[i].steps[j].type = json_integer_value(typeJ);
					}
				}
			}
		}
		updateFlag = true;
		loadedFromJson = true;
	}

	void onRandomize() override {
		randomizeSlidesSkips();
	}

	void randomizeSlidesSkips() {
		for (int i = 0; i < 8; i++) {
			slideState[i] = (random::uniform() > 0.8f) ? 't' : 'f';
			skipState[i] = (random::uniform() > 0.85f) ? 't' : 'f';
		}
	}

	void randomizePitch() {
		random::init();
		for (int i = 0; i < 8; i++) {
			params[TRIG_PITCH_PARAM+i].setValue(random::uniform()*10.0f-4.0f);
		}
	}

	void randomizeGates() {
		random::init();
		for (int i = 0; i < 8; i++) {
			params[TRIG_COUNT_PARAM+i].setValue((int)(random::uniform()*7.0f+1.0f));
			params[TRIG_TYPE_PARAM+i].setValue((int)(random::uniform()*5.0f));
		}
	}

	void onReset() override {
		for (int i = 0; i < 8; i++) {
			slideState[i] = 'f';
			skipState[i] = 'f';
		}
	}

	float closestVoltageInScale(float voltsIn, int rootNote, float scaleVal){
		curScaleVal = scaleVal;
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
		float closestVal = 0.0f;
		float closestDist = 1.0f;
		int octaveInVolts = 0;
		if ((voltsIn >= 0.0f) || (voltsIn == (int)voltsIn)) {
			octaveInVolts = int(voltsIn);
		}
		else {
			octaveInVolts = int(voltsIn)-1;
		}
		for (int i = 0; i < notesInScale; i++) {
			float scaleNoteInVolts = octaveInVolts + curScaleArr[i] / 12.0f;
			float distAway = fabs(voltsIn - scaleNoteInVolts);
			if(distAway < closestDist) {
				closestVal = scaleNoteInVolts;
				closestDist = distAway;
			}
		}
		float transposeVoltage = inputs[TRANSPOSE_INPUT].isConnected() ? ((((int)rescale(clamp(inputs[TRANSPOSE_INPUT].getVoltage(),-10.0f,10.0f),-10.0f,10.0f,-48.0f,48.0f)) / 12.0f)) : 0.0f;
		return clamp(closestVal + (rootNote / 12.0f) + transposeVoltage,-4.0f,6.0f);
	}
};

void DTROY::UpdatePattern() {
	patterns[selectedPattern].Update(playMode, countMode, numSteps, roundf(params[STEPS_PARAM].getValue()),
		 roundf(params[ROOT_NOTE_PARAM].getValue()), roundf(params[SCALE_PARAM].getValue()), params[GATE_TIME_PARAM].getValue(),
		 params[SLIDE_TIME_PARAM].getValue(), params[SENSITIVITY_PARAM].getValue() , skipState, slideState,
		 &params[TRIG_COUNT_PARAM], &params[TRIG_PITCH_PARAM], &params[TRIG_TYPE_PARAM]);
}

void DTROY::process(const ProcessArgs &args) {

	float invESR = 1 / args.sampleRate;

	// Run
	if (runningTrigger.process(params[RUN_PARAM].getValue())) {
		running = !running;
	}
	lights[RUNNING_LIGHT].setBrightness(running ? 1.0f : 0.0f);
	bool nextStep = false;

	// Phase calculation
	if (running) {
		if (inputs[EXT_CLOCK_INPUT].isConnected()) {
			tCurrent += invESR;
			if (tLastTrig > 0.0f) {
				phase = tCurrent / tLastTrig;
			}
			else {
				float clockTime = powf(2.0f, params[CLOCK_PARAM].getValue() + inputs[CLOCK_INPUT].getVoltage());
				phase += clockTime * invESR;
			}
			// External clock
			if (clockTrigger.process(inputs[EXT_CLOCK_INPUT].getVoltage())) {
				tLastTrig = tCurrent;
				tCurrent = 0.0f;
				phase = 0.0f;
				nextStep = true;
			}
		}
		else {
			// Internal clock
			float clockTime = powf(2.0f, params[CLOCK_PARAM].getValue() + inputs[CLOCK_INPUT].getVoltage());
			phase += clockTime * invESR;
			if (phase >= 1.0f) {
				phase--;
				nextStep = true;
			}
		}
	}

	// Reset
	if (resetTrigger.process(params[RESET_PARAM].getValue() + inputs[RESET_INPUT].getVoltage())) {
		phase = 0.0f;
		reStart = true;
		nextStep = true;
		lights[RESET_LIGHT].setBrightness(1.0f);
	}

	//patternNumber
	int newPattern = clamp((inputs[PATTERN_INPUT].isConnected() ? (int)(rescale(inputs[PATTERN_INPUT].getVoltage(),0.0f,10.0f,1.0f,16.1f)) : (int)(params[PATTERN_PARAM].getValue())) - 1, 0, 15);
	if (newPattern != playedPattern) {
		int cStep = patterns[playedPattern].currentStep;
		int cPulse = patterns[playedPattern].currentPulse;
		playedPattern = newPattern;
		patterns[playedPattern].currentStep = cStep;
		patterns[playedPattern].currentPulse = cPulse;
	}

	int target = clamp((int)(params[PATTERN_PARAM].getValue())-1, 0, 15);
	if (target != selectedPattern) {
		selectedPattern = target;
		playMode = patterns[selectedPattern].playMode;
		countMode = patterns[selectedPattern].countMode;
		params[STEPS_PARAM].setValue(patterns[selectedPattern].numberOfStepsParam);
		params[ROOT_NOTE_PARAM].setValue(patterns[selectedPattern].rootNote);
		params[SCALE_PARAM].setValue(patterns[selectedPattern].scale);
		params[GATE_TIME_PARAM].setValue(patterns[selectedPattern].gateTime);
		params[SLIDE_TIME_PARAM].setValue(patterns[selectedPattern].slideTime);
		params[SENSITIVITY_PARAM].setValue(patterns[selectedPattern].sensitivity);
		for (int i = 0; i < 8; i++) {
			params[TRIG_PITCH_PARAM+i].setValue(patterns[selectedPattern].steps[i].pitch);
			params[TRIG_COUNT_PARAM+i].setValue(patterns[selectedPattern].steps[i].pulsesParam);
			params[TRIG_TYPE_PARAM+i].setValue(patterns[selectedPattern].steps[i].type);
			skipState[i] = patterns[selectedPattern].steps[i].skipParam ? 't' : 'f';
			slideState[i] = patterns[selectedPattern].steps[i].slide ? 't' : 'f';
		}
	}

	if (copyTrigger.process(params[COPY_PARAM].getValue())) {
		if (!copyState) {
			copyPattern = selectedPattern;
			copyState = true;
			lights[COPY_LIGHT].setBrightness(1.0f);
		}
		else if (copyPattern != selectedPattern)
		{
			params[STEPS_PARAM].setValue(patterns[copyPattern].numberOfStepsParam);
			params[ROOT_NOTE_PARAM].setValue(patterns[copyPattern].rootNote);
			params[SCALE_PARAM].setValue(patterns[copyPattern].scale);
			params[GATE_TIME_PARAM].setValue(patterns[copyPattern].gateTime);
			params[SLIDE_TIME_PARAM].setValue(patterns[copyPattern].slideTime);
			params[SENSITIVITY_PARAM].setValue(patterns[copyPattern].sensitivity);
			playMode = patterns[copyPattern].playMode;
			countMode = patterns[copyPattern].countMode;
			for (int i = 0; i < 8; i++) {
				params[TRIG_PITCH_PARAM+i].setValue(patterns[copyPattern].steps[i].pitch);
				params[TRIG_COUNT_PARAM+i].setValue(patterns[copyPattern].steps[i].pulsesParam);
				params[TRIG_TYPE_PARAM+i].setValue(patterns[copyPattern].steps[i].type);
				skipState[i] = patterns[copyPattern].steps[i].skipParam ? 't' : 'f';
				slideState[i] = patterns[copyPattern].steps[i].slide ? 't' : 'f';
			}
			copyState = false;
			copyPattern = -1;
			lights[COPY_LIGHT].setBrightness(0.0f);
		}
		else 		{
			copyState = false;
			copyPattern = -1;
			lights[COPY_LIGHT].setBrightness(0.0f);
		}
	}

	if (upTrigger.process(params[UP_PARAM].getValue())) {
		for (int i = 0; i < 8; i++) {
			params[TRIG_PITCH_PARAM+i].setValue(min(params[TRIG_PITCH_PARAM+i].getValue() + 1.f/12.f,6.0f));
		}
	}

	if (downTrigger.process(params[DOWN_PARAM].getValue())) {
		for (int i = 0; i < 8; i++) {
			params[TRIG_PITCH_PARAM+i].setValue(max(params[TRIG_PITCH_PARAM+i].getValue() - 1.f/12.f,-4.0f));
		}
	}

	if (leftTrigger.process(params[LEFT_PARAM].getValue())) {
			float pitch = params[TRIG_PITCH_PARAM].getValue();
			float pulse = params[TRIG_COUNT_PARAM].getValue();
			float type = params[TRIG_TYPE_PARAM].getValue();
			char skip = skipState[0];
			char slide = slideState[0];
			for (int i = 0; i < 7; i++) {
				params[TRIG_PITCH_PARAM+i].setValue(params[TRIG_PITCH_PARAM+i+1].getValue());
				params[TRIG_COUNT_PARAM+i].setValue(params[TRIG_COUNT_PARAM+i+1].getValue());
				params[TRIG_TYPE_PARAM+i].setValue(params[TRIG_TYPE_PARAM+i+1].getValue());
				skipState[i] = skipState[i+1];
				slideState[i] = slideState[i+1];
			}
			params[TRIG_PITCH_PARAM+7].setValue(pitch);
			params[TRIG_COUNT_PARAM+7].setValue(pulse);
			params[TRIG_TYPE_PARAM+7].setValue(type);
			skipState[7] = skip;
			slideState[7] = slide;
	}

	if (rightTrigger.process(params[RIGHT_PARAM].getValue())) {
			float pitch = params[TRIG_PITCH_PARAM+7].getValue();
			float pulse = params[TRIG_COUNT_PARAM+7].getValue();
			float type = params[TRIG_TYPE_PARAM+7].getValue();
			char skip = skipState[7];
			char slide = slideState[7];
			for (int i = 7; i > 0; i--) {
				params[TRIG_PITCH_PARAM+i].setValue(params[TRIG_PITCH_PARAM+i-1].getValue());
				params[TRIG_COUNT_PARAM+i].setValue(params[TRIG_COUNT_PARAM+i-1].getValue());
				params[TRIG_TYPE_PARAM+i].setValue(params[TRIG_TYPE_PARAM+i-1].getValue());
				skipState[i] = skipState[i-1];
				slideState[i] = slideState[i-1];
			}
			params[TRIG_PITCH_PARAM].setValue(pitch);
			params[TRIG_COUNT_PARAM].setValue(pulse);
			params[TRIG_TYPE_PARAM].setValue(type);
			skipState[0] = skip;
			slideState[0] = slide;
	}

	// Update Pattern
	if ((updateFlag) || (!loadedFromJson)) {
		// Trigs Update
		for (int i = 0; i < 8; i++) {
			if (slideTriggers[i].process(params[TRIG_SLIDE_PARAM + i].getValue())) {
				slideState[i] = slideState[i] == 't' ? 'f' : 't';
			}
			if (skipTriggers[i].process(params[TRIG_SKIP_PARAM + i].getValue())) {
				skipState[i] = skipState[i] == 't' ? 'f' : 't';
			}
		}
		// playMode
		if (playModeTrigger.process(params[PLAY_MODE_PARAM].getValue())) {
			playMode = (((int)playMode + 1) % 5);
		}
		// countMode
		if (countModeTrigger.process(params[COUNT_MODE_PARAM].getValue())) {
			countMode = (((int)countMode + 1) % 2);
		}
		// numSteps
		numSteps = clamp((int)(params[STEPS_PARAM].getValue() + inputs[STEPS_INPUT].getVoltage()), 1, 16);

		UpdatePattern();
		if (!loadedFromJson) {
			loadedFromJson = true;
			updateFlag = true;
		}
	}

	// Steps && Pulses Management
	if (nextStep) {
		candidateForPreviousPitch = closestVoltageInScale(clamp(patterns[playedPattern].CurrentStep().pitch,-4.0f,6.0f) * clamp(patterns[playedPattern].sensitivity + (inputs[SENSITIVITY_INPUT].isConnected() ? rescale(inputs[SENSITIVITY_INPUT].getVoltage(),0.f,10.f,0.1f,1.0f) : 0.0f),0.1f,1.0f),
		 clamp(patterns[playedPattern].rootNote + rescale(clamp(inputs[ROOT_NOTE_INPUT].getVoltage(), 0.0f,10.0f),0.0f,10.0f,0.0f,11.0f), 0.0f,11.0f), patterns[playedPattern].scale + inputs[SCALE_INPUT].getVoltage());


		auto nextT = patterns[playedPattern].GetNextStep(reStart);
		index = std::get<0>(nextT);
		pulse = std::get<1>(nextT);

		if (reStart)
			reStart = false;

		if (((!stepOutputsMode) && (pulse == 0)) || (stepOutputsMode))
			stepPulse[patterns[playedPattern].CurrentStep().index].trigger(10 * invESR);

		lights[STEPS_LIGHTS+patterns[playedPattern].CurrentStep().index].setBrightness(1.0f);
	}

	// Lights & steps outputs
	for (int i = 0; i < 8; i++) {
		lights[STEPS_LIGHTS + i].setBrightness(lights[STEPS_LIGHTS + i].getBrightness() - lights[STEPS_LIGHTS + i].getBrightness() * invLightLambda * invESR);
		lights[SLIDES_LIGHTS + i].setBrightness(slideState[i] == 't' ? 1.0f - lights[STEPS_LIGHTS + i].getBrightness() : lights[STEPS_LIGHTS + i].getBrightness());
		lights[SKIPS_LIGHTS + i].setBrightness(skipState[i]== 't' ? 1.0f - lights[STEPS_LIGHTS + i].getBrightness() : lights[STEPS_LIGHTS + i].getBrightness());
		outputs[STEP_OUTPUT+i].setVoltage(stepPulse[i].process(invESR) ? 10.0f : 0.0f);
	}
	lights[RESET_LIGHT].setBrightness(lights[RESET_LIGHT].getBrightness() -  lights[RESET_LIGHT].getBrightness() * invLightLambda * invESR);
	lights[COPY_LIGHT].setBrightness(copyPattern >= 0 ? 1 : 0);

	// Caclulate Outputs
	gateOn = running && (!patterns[playedPattern].CurrentStep().skip);
	float gateValue = 0.0f;
	if (gateOn){
		if (patterns[playedPattern].CurrentStep().type == 0) {
			gateOn = false;
		}
		else if (((patterns[playedPattern].CurrentStep().type == 1) && (pulse == 0))
				|| (patterns[playedPattern].CurrentStep().type == 2)
				|| ((patterns[playedPattern].CurrentStep().type == 3) && (pulse == patterns[playedPattern].CurrentStep().pulses))) {
				float gateCoeff = clamp(patterns[playedPattern].gateTime - 0.02f + inputs[GATE_TIME_INPUT].getVoltage() * 0.1f, 0.0f, 0.99f);
			gateOn = phase < gateCoeff;
			gateValue = 10.0f;
		}
		else if (patterns[playedPattern].CurrentStep().type == 3) {
			gateOn = true;
			gateValue = 10.0f;
		}
		else if (patterns[playedPattern].CurrentStep().type == 4) {
			gateOn = true;
			gateValue = inputs[EXTGATE1_INPUT].getVoltage();
		}
		else if (patterns[playedPattern].CurrentStep().type == 5) {
			gateOn = true;
			gateValue = inputs[EXTGATE2_INPUT].getVoltage();
		}
		else {
			gateOn = false;
			gateValue = 0.0f;
		}
	}

	//pitch management
	pitch = closestVoltageInScale(clamp(patterns[playedPattern].CurrentStep().pitch,-4.0f,6.0f) * clamp(patterns[playedPattern].sensitivity + (inputs[SENSITIVITY_INPUT].isConnected() ? rescale(inputs[SENSITIVITY_INPUT].getVoltage(),0.f,10.f,0.1f,1.0f) : 0.0f),0.1f,1.0f),
	clamp(patterns[playedPattern].rootNote + rescale(clamp(inputs[ROOT_NOTE_INPUT].getVoltage(), 0.0f,10.0f),0.0f,10.0f,0.0f,11.0f), 0.0f, 11.0f), patterns[playedPattern].scale + inputs[SCALE_INPUT].getVoltage());
	if (patterns[playedPattern].CurrentStep().slide) {
		if (pulse == 0) {
			float slideCoeff = clamp(patterns[playedPattern].slideTime - 0.01f + inputs[SLIDE_TIME_INPUT].getVoltage() * 0.1f, -0.1f, 0.99f);
			pitch = pitch - (1.0f - powf(phase, slideCoeff)) * (pitch - previousPitch);
		}
	}

	// Update Outputs
	outputs[GATE_OUTPUT].setVoltage(gateOn ? gateValue : 0.0f);
	outputs[PITCH_OUTPUT].setVoltage(pitch);

	if (nextStep && gateOn)
		previousPitch = candidateForPreviousPitch;
}

struct DTROYDisplay : TransparentWidget {
	DTROY *module;
	int frame = 0;
	shared_ptr<Font> font;

	std::string note, scale, steps, playMode, selectedPattern, playedPattern;

	DTROYDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void drawMessage(NVGcontext *vg, Vec pos, std::string note, std::string playMode, std::string selectedPattern, std::string playedPattern, std::string steps, std::string scale) {
		nvgFontSize(vg, 18.0f);
		// nvgFontFaceId(vg, font->handle);
		// nvgTextLetterSpacing(vg, -2.0f);
		nvgFillColor(vg, YELLOW_BIDOO);
		nvgText(vg, pos.x + 3.0f, pos.y + 8.0f, playMode.c_str(), NULL);
		nvgFontSize(vg, 14.0f);
		nvgText(vg, pos.x + 118.0f, pos.y + 7.0f, selectedPattern.c_str(), NULL);

		nvgText(vg, pos.x + 30.0f, pos.y + 7.0f, steps.c_str(), NULL);
		nvgText(vg, pos.x + 3.0f, pos.y + 21.0f, note.c_str(), NULL);
		nvgText(vg, pos.x + 25.0f, pos.y + 21.0f, scale.c_str(), NULL);

		if (++frame <= 30) {
			nvgText(vg, pos.x + 89.0f, pos.y + 7.0f, playedPattern.c_str(), NULL);
		}
		else if (++frame>60) {
			frame = 0;
		}
	}

	std::string displayRootNote(int value) {
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

	std::string displayScale(int value) {
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

	std::string displayPlayMode(int value) {
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
    if (module) {
      note = displayRootNote(module->patterns[module->selectedPattern].rootNote);
  		steps = (module->patterns[module->selectedPattern].countMode == 0 ? "steps:" : "pulses:" ) + to_string(module->patterns[module->selectedPattern].numberOfStepsParam);
  		playMode = displayPlayMode(module->patterns[module->selectedPattern].playMode);
  		scale = displayScale(module->patterns[module->selectedPattern].scale);
  		selectedPattern = "P" + to_string(module->selectedPattern + 1);
  		playedPattern = "P" + to_string(module->playedPattern + 1);
  		drawMessage(vg, Vec(0, 20), note, playMode, selectedPattern, playedPattern, steps, scale);
    }
	}
};

struct DTROYWidget : ModuleWidget {
	ParamWidget *stepsParam, *scaleParam, *rootNoteParam, *sensitivityParam,
	 *gateTimeParam, *slideTimeParam, *playModeParam, *countModeParam, *patternParam,
		*pitchParams[8], *pulseParams[8], *typeParams[8], *slideParams[8], *skipParams[8],
		 *pitchRndParams[8], *pulseProbParams[8], *accentParams[8], *rndAccentParams[8];

	void appendContextMenu(ui::Menu *menu) override;

	DTROYWidget(DTROY *module);
};

DTROYWidget::DTROYWidget(DTROY *module) {
	setModule(module);
	setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DTROY.svg")));

	addChild(createWidget<ScrewSilver>(Vec(15, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createWidget<ScrewSilver>(Vec(15, 365)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

	{
		DTROYDisplay *display = new DTROYDisplay();
		display->module = module;
		display->box.pos = Vec(20.0f, 217.0f);
		display->box.size = Vec(250.0f, 60.0f);
		addChild(display);
	}

	addParam(createParam<BidooRoundBlackKnob>(Vec(17.0f, 36.0f), module, DTROY::CLOCK_PARAM));
	addParam(createParam<LEDButton>(Vec(61.0f, 40.0f), module, DTROY::RUN_PARAM));
	addChild(createLight<SmallLight<GreenLight>>(Vec(67.0f, 46.0f), module, DTROY::RUNNING_LIGHT));
	addParam(createParam<LEDButton>(Vec(99.0f, 40.0f), module, DTROY::RESET_PARAM));
	addChild(createLight<SmallLight<GreenLight>>(Vec(105.0f, 46.0f), module, DTROY::RESET_LIGHT));
	stepsParam = createParam<BidooBlueSnapKnob>(Vec(132.0f, 36.0f), module, DTROY::STEPS_PARAM);
	addParam(stepsParam);

	static const float portX0[4] = {20.0f, 58.0f, 96.0f, 135.0f};
 	addInput(createInput<PJ301MPort>(Vec(portX0[0], 69.0f), module, DTROY::CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 69.0f), module, DTROY::EXT_CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 69.0f), module, DTROY::RESET_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[3], 69.0f), module, DTROY::STEPS_INPUT));

	rootNoteParam = createParam<BidooBlueSnapKnob>(Vec(portX0[0]-2.0f, 116.0f), module, DTROY::ROOT_NOTE_PARAM);
	addParam(rootNoteParam);
	scaleParam = createParam<BidooBlueSnapKnob>(Vec(portX0[1]-2.0f, 116.0f), module, DTROY::SCALE_PARAM);
	addParam(scaleParam);
	gateTimeParam = createParam<BidooBlueKnob>(Vec(portX0[2]-2.0f, 116.0f), module, DTROY::GATE_TIME_PARAM);
	addParam(gateTimeParam);
	slideTimeParam = createParam<BidooBlueKnob>(Vec(portX0[3]-2.0f, 116.0f), module, DTROY::SLIDE_TIME_PARAM);
	addParam(slideTimeParam);

	addInput(createInput<PJ301MPort>(Vec(portX0[0], 149.0f), module, DTROY::ROOT_NOTE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 149.0f), module, DTROY::SCALE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 149.0f), module, DTROY::GATE_TIME_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[3], 149.0f), module, DTROY::SLIDE_TIME_INPUT));

	playModeParam = createParam<BlueCKD6>(Vec(portX0[0]-1.0f, 196.0f), module, DTROY::PLAY_MODE_PARAM);
	addParam(playModeParam);
	countModeParam = createParam<BlueCKD6>(Vec(portX0[1]-1.0f, 196.0f), module, DTROY::COUNT_MODE_PARAM);
	addParam(countModeParam);
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 198.0f), module, DTROY::PATTERN_INPUT));
	patternParam = createParam<BidooRoundBlackSnapKnob>(Vec(portX0[3]-1,196.0f), module, DTROY::PATTERN_PARAM);
	addParam(patternParam);

	static const float portX1[8] = {200.0f, 238.0f, 276.0f, 315.0f, 353.0f, 392.0f, 430.0f, 469.0f};

	sensitivityParam = createParam<BidooBlueTrimpot>(Vec(portX1[6]+21.0f, 18.0f), module, DTROY::SENSITIVITY_PARAM);
	addParam(sensitivityParam);
	addInput(createInput<TinyPJ301MPort>(Vec(portX1[6]-10.f, 18.0f), module, DTROY::SENSITIVITY_INPUT));

	addInput(createInput<PJ301MPort>(Vec(portX0[0], 286.0f), module, DTROY::TRANSPOSE_INPUT));
	addParam(createParam<BlueCKD6>(Vec(portX0[1]-1.0f, 285.0f), module, DTROY::COPY_PARAM));
	addChild(createLight<SmallLight<GreenLight>>(Vec(portX0[1]+23.0f, 283.0f), module, DTROY::COPY_LIGHT));

	addParam(createParam<LeftBtn>(Vec(104.0f, 290.0f), module, DTROY::LEFT_PARAM));
	addParam(createParam<RightBtn>(Vec(134.0f, 290.0f), module, DTROY::RIGHT_PARAM));
	addParam(createParam<UpBtn>(Vec(119.0f, 282.0f), module, DTROY::UP_PARAM));
	addParam(createParam<DownBtn>(Vec(119.0f, 297.0f), module, DTROY::DOWN_PARAM));

	for (int i = 0; i < 8; i++) {
		pitchParams[i] = createParam<BidooBlueKnob>(Vec(portX1[i]-3.0f, 36.0f), module, DTROY::TRIG_PITCH_PARAM + i);
		addParam(pitchParams[i]);
		pulseParams[i] = createParam<BidooSlidePotLong>(Vec(portX1[i]+2.0f, 87.0f), module, DTROY::TRIG_COUNT_PARAM + i);
		addParam(pulseParams[i]);
		typeParams[i] = createParam<BidooSlidePotShort>(Vec(portX1[i]+2.0f, 204.0f), module, DTROY::TRIG_TYPE_PARAM + i);
		addParam(typeParams[i]);
		slideParams[i] = createParam<LEDButton>(Vec(portX1[i]+2.0f, 297.0f), module, DTROY::TRIG_SLIDE_PARAM + i);
		addParam(slideParams[i]);
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[i]+8.0f, 303.0f), module, DTROY::SLIDES_LIGHTS + i));
		skipParams[i] = createParam<LEDButton>(Vec(portX1[i]+2.0f, 321.0f), module, DTROY::TRIG_SKIP_PARAM + i);
		addParam(skipParams[i]);
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[i]+8.0f, 327.0f), module, DTROY::SKIPS_LIGHTS + i));
		addOutput(createOutput<TinyPJ301MPort>(Vec(portX1[i]+4.0f, 344.0f), module, DTROY::STEP_OUTPUT + i));
	}

	addInput(createInput<PJ301MPort>(Vec(portX0[0], 330.0f), module, DTROY::EXTGATE1_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 330.0f), module, DTROY::EXTGATE2_INPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[2]-1, 330.0f), module, DTROY::GATE_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[3]-1, 330.0f), module, DTROY::PITCH_OUTPUT));
}

struct DTROYRandPitchItem : MenuItem {
	DTROY *module;
	void onAction(const event::Action &e) override {
		module->randomizePitch();
	}
};

struct DTROYRandGatesItem : MenuItem {
	DTROY *module;
	void onAction(const event::Action &e) override {
		module->randomizeGates();
	}
};

struct DTROYRandSlideSkipItem : MenuItem {
	DTROY *module;
	void onAction(const event::Action &e) override {
		module->randomizeSlidesSkips();
	}
};

struct DTROYStepOutputsModeItem : MenuItem {
	DTROY *module;
	void onAction(const event::Action &e) override {
		module->stepOutputsMode = !module->stepOutputsMode;
	}
	void step() override {
		rightText = module->stepOutputsMode ? "✔" : "";
		MenuItem::step();
	}
};

void DTROYWidget::appendContextMenu(ui::Menu *menu) {
	DTROY *module = dynamic_cast<DTROY*>(this->module);
	assert(module);

	menu->addChild(construct<MenuLabel>());
	menu->addChild(construct<DTROYRandPitchItem>(&MenuItem::text, "Rand pitch", &DTROYRandPitchItem::module, module));
	menu->addChild(construct<DTROYRandGatesItem>(&MenuItem::text, "Rand gates", &DTROYRandGatesItem::module, module));
	menu->addChild(construct<DTROYRandSlideSkipItem>(&MenuItem::text, "Rand slides & skips", &DTROYRandSlideSkipItem::module, module));
	menu->addChild(construct<DTROYStepOutputsModeItem>(&MenuItem::text, "Step outputs mode", &DTROYStepOutputsModeItem::module, module));
}

Model *modelDTROY = createModel<DTROY, DTROYWidget>("dTrOY");

#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>

using namespace std;

struct StepExtended {
	int index = 0;
	int number = 0;
	bool skip = false;
	bool skipParam = false;
	bool slide = false;
	int pulses = 1;
	int pulsesParam = 1;
	float pitch = 3.0f;
	int type = 2;
	float gateProb = 1.0f;
	float pitchRnd = 0.0f;
	float accent = 0.0f;
	float accentRnd = 0.0f;
};

struct PatternExtended {
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
	std::vector<StepExtended> steps {16};

	void Update(int playMode, int countMode, int numberOfSteps, int numberOfStepsParam, int rootNote, int scale,
		 float gateTime, float slideTime, float sensitivity, std::vector<char> skips, std::vector<char> slides,
		  Param *pulses, Param *pitches, Param *types, Param *probGates,
			Param *rndPitches, Param *accents, Param *rndAccents)
			{
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
			steps[i].gateProb = (probGates + steps[i].index)->getValue();
			steps[i].pitchRnd = (rndPitches + steps[i].index)->getValue();
			steps[i].accent = (accents + steps[i].index)->getValue();
			steps[i].accentRnd = (rndAccents + steps[i].index)->getValue();

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
					std::vector<StepExtended> tmp (steps.size());
				  auto it = std::copy_if (steps.begin(), steps.end(), tmp.begin(), [](StepExtended i){return !(i.skip);} );
				  tmp.resize(std::distance(tmp.begin(),it));  // shrink container to new size
					StepExtended tmpStep = *select_randomly(tmp.begin(), tmp.end());
					currentPulse = 0;
					currentStep = tmpStep.number;
					return std::make_tuple(steps[currentStep%16].index,currentPulse);
				} else if (playMode == 4) {
					int next = GetNextStepForward(currentStep);
					int prev = GetNextStepBackward(currentStep);
					vector<StepExtended> subPattern;
					subPattern.push_back(steps[prev]);
					subPattern.push_back(steps[next]);
					StepExtended choice = *select_randomly(subPattern.begin(), subPattern.end());
					currentPulse = 0;
					currentStep = choice.number;
					return std::make_tuple(steps[currentStep%16].index,currentPulse);
				} else {
					return std::make_tuple(0,0);
				}
			}
		}
	}

	inline StepExtended CurrentStep() {
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
				if (!steps[i%16].skip) {
					return i%16;
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

	inline float MinVoltage() {
		float result = 0.0f;
		for (int i = 0; i < 16; i++) {
			if (steps[i].pitch<result) {
				result = steps[i].pitch;
			}
		}
		return result;
	}

	inline float MaxVoltage() {
		float result = 0.0f;
		for (int i = 0; i < 16; i++) {
			if (steps[i].pitch>result) {
				result = steps[i].pitch;
			}
		}
		return result;
	}

};

struct BORDL : Module {
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
		TRIG_GATEPROB_PARAM = TRIG_SKIP_PARAM + 8,
		TRIG_PITCHRND_PARAM = TRIG_GATEPROB_PARAM + 8,
		TRIG_ACCENT_PARAM = TRIG_PITCHRND_PARAM + 8,
		TRIG_RNDACCENT_PARAM = TRIG_ACCENT_PARAM + 8,
		LEFT_PARAM = TRIG_RNDACCENT_PARAM + 8,
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
		ACC_OUTPUT,
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
	int prevIndex = 0;
	bool reStart = true;
	int pulse = 0;
	int curScaleVal = 0;
	float pitch = 0.0f;
	float previousPitch = 0.0f;
	float candidateForPreviousPitch = 0.0f;
	float tCurrent;
	float tLastTrig;
	std::vector<char> slideState = {'f','f','f','f','f','f','f','f'};
	std::vector<char> skipState = {'f','f','f','f','f','f','f','f'};
	int playMode = 0; // 0 forward, 1 backward, 2 pingpong, 3 random, 4 brownian
	int countMode = 0; // 0 steps, 1 pulses
	int numSteps = 8;
	int selectedPattern = 0;
	int playedPattern = 0;
	bool pitchMode = false;
	bool updateFlag = false;
	bool probGate = true;
	float rndPitch = 0.0f;
	float accent = 5.0f;
	bool loadedFromJson = false;
	int copyPattern = -1;
	dsp::PulseGenerator stepPulse[8];
	bool stepOutputsMode = false;
	bool gateOn = false;
	const float invLightLambda = 13.333333333333333333333f;
	bool copyState = false;

	PatternExtended patterns[16];

	BORDL() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CLOCK_PARAM, -2.0f, 6.0f, 2.0f);
		configParam(RUN_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(RESET_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(STEPS_PARAM, 1.0f, 16.0f, 8.0f);
		configParam(ROOT_NOTE_PARAM, 0.0f, BORDL::NUM_NOTES-1.0f, 0.0f);
		configParam(SCALE_PARAM, 0.0f, BORDL::NUM_SCALES-1.0f, 0.0f);
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
			configParam(TRIG_PITCHRND_PARAM + i, 0.0f, 1.0f, 0.0f);
			configParam(TRIG_ACCENT_PARAM + i, 0.0f, 10.0f, 0.0f);
			configParam(TRIG_RNDACCENT_PARAM + i, 0.0f, 1.0f, 0.0f);
			configParam(TRIG_COUNT_PARAM + i, 1.0f, 8.0f,  1.0f);
			configParam(TRIG_GATEPROB_PARAM + i, 0.0f, 1.0f,  1.0f);
			configParam(TRIG_TYPE_PARAM + i, 0.0f, 5.0f,  2.0f);
			configParam(TRIG_SLIDE_PARAM + i, 0.0f, 1.0f,  0.0f);
			configParam(TRIG_SKIP_PARAM + i, 0.0f, 1.0f, 0.0f);
		}
	}

	void UpdatePattern();

	void process(const ProcessArgs &args) override;

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
			json_object_set_new(patternJ, "rootNote", json_integer(patterns[i].rootNoteParam));
			json_object_set_new(patternJ, "scale", json_integer(patterns[i].scaleParam));
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
				json_object_set_new(stepJ, "gateProb", json_real(patterns[i].steps[j].gateProb));
				json_object_set_new(stepJ, "pitchRnd", json_real(patterns[i].steps[j].pitchRnd));
				json_object_set_new(stepJ, "accent", json_real(patterns[i].steps[j].accent));
				json_object_set_new(stepJ, "accentRnd", json_real(patterns[i].steps[j].accentRnd));
				json_object_set_new(patternJ, ("step" + to_string(j)).c_str() , stepJ);
			}
			json_object_set_new(rootJ, ("pattern" + to_string(i)).c_str(), patternJ);
		}
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *runningJ = json_object_get(rootJ, "running");
		if (runningJ)
			running = json_is_true(runningJ);
		json_t *playModeJ = json_object_get(rootJ, "playMode");
		if (playModeJ)
			playMode = json_boolean_value(playModeJ);
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
						json_t *probGateJ= json_object_get(stepJ, "gateProb");
						if (probGateJ)
							patterns[i].steps[j].gateProb = json_number_value(probGateJ);
						json_t *rndPitchJ= json_object_get(stepJ, "pitchRnd");
						if (rndPitchJ)
							patterns[i].steps[j].pitchRnd = json_number_value(rndPitchJ);
						json_t *accentJ= json_object_get(stepJ, "accent");
						if (accentJ)
							patterns[i].steps[j].accent = json_number_value(accentJ);
						json_t *rndAccentJ= json_object_get(stepJ, "accentRnd");
						if (rndAccentJ)
							patterns[i].steps[j].accentRnd = json_number_value(rndAccentJ);
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
		int octaveInVolts = voltsIn >= 0.0f ? int(voltsIn) : (int(voltsIn)-1);
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

void BORDL::UpdatePattern() {
	patterns[selectedPattern].Update(playMode, countMode, numSteps, roundf(params[STEPS_PARAM].getValue()), roundf(params[ROOT_NOTE_PARAM].getValue()),
	 roundf(params[SCALE_PARAM].getValue()), params[GATE_TIME_PARAM].getValue(), params[SLIDE_TIME_PARAM].getValue(), params[SENSITIVITY_PARAM].getValue() ,
		skipState, slideState, &params[TRIG_COUNT_PARAM], &params[TRIG_PITCH_PARAM], &params[TRIG_TYPE_PARAM], &params[TRIG_GATEPROB_PARAM],
		 &params[TRIG_PITCHRND_PARAM], &params[TRIG_ACCENT_PARAM], &params[TRIG_RNDACCENT_PARAM]);
}

void BORDL::process(const ProcessArgs &args) {

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
			float clockTime = powf(2.0f, params[CLOCK_PARAM].getValue() + inputs[CLOCK_INPUT].getVoltage());
			if (tLastTrig > 0.0f) {
				phase = tCurrent / tLastTrig;
			}
			else {
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

	//copy/paste
	lights[COPY_LIGHT].setBrightness(copyState ? 1.0f : 0.0f);

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
			params[TRIG_PITCHRND_PARAM+i].setValue(patterns[selectedPattern].steps[i].pitchRnd);
			params[TRIG_ACCENT_PARAM+i].setValue(patterns[selectedPattern].steps[i].accent);
			params[TRIG_RNDACCENT_PARAM+i].setValue(patterns[selectedPattern].steps[i].accentRnd);
			params[TRIG_COUNT_PARAM+i].setValue(patterns[selectedPattern].steps[i].pulsesParam);
			params[TRIG_GATEPROB_PARAM+i].setValue(patterns[selectedPattern].steps[i].gateProb);
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
				params[TRIG_PITCHRND_PARAM+i].setValue(patterns[copyPattern].steps[i].pitchRnd);
				params[TRIG_ACCENT_PARAM+i].setValue(patterns[copyPattern].steps[i].accent);
				params[TRIG_RNDACCENT_PARAM+i].setValue(patterns[copyPattern].steps[i].accentRnd);
				params[TRIG_COUNT_PARAM+i].setValue(patterns[copyPattern].steps[i].pulsesParam);
				params[TRIG_GATEPROB_PARAM+i].setValue(patterns[copyPattern].steps[i].gateProb);
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
			float pitchRnd = params[TRIG_PITCHRND_PARAM].getValue();
			float accent = params[TRIG_ACCENT_PARAM].getValue();
			float accentRnd = params[TRIG_RNDACCENT_PARAM].getValue();
			float gateProb = params[TRIG_GATEPROB_PARAM].getValue();
			char skip = skipState[0];
			char slide = slideState[0];
			for (int i = 0; i < 7; i++) {
				params[TRIG_PITCH_PARAM+i].setValue(params[TRIG_PITCH_PARAM+i+1].getValue());
				params[TRIG_COUNT_PARAM+i].setValue(params[TRIG_COUNT_PARAM+i+1].getValue());
				params[TRIG_TYPE_PARAM+i].setValue(params[TRIG_TYPE_PARAM+i+1].getValue());
				params[TRIG_PITCHRND_PARAM+i].setValue(params[TRIG_PITCHRND_PARAM+i+1].getValue());
				params[TRIG_ACCENT_PARAM+i].setValue(params[TRIG_ACCENT_PARAM+i+1].getValue());
				params[TRIG_RNDACCENT_PARAM+i].setValue(params[TRIG_RNDACCENT_PARAM+i+1].getValue());
				params[TRIG_GATEPROB_PARAM+i].setValue(params[TRIG_GATEPROB_PARAM+i+1].getValue());
				skipState[i] = skipState[i+1];
				slideState[i] = slideState[i+1];
			}
			params[TRIG_PITCH_PARAM+7].setValue(pitch);
			params[TRIG_COUNT_PARAM+7].setValue(pulse);
			params[TRIG_TYPE_PARAM+7].setValue(type);
			params[TRIG_PITCHRND_PARAM+7].setValue(pitchRnd);
			params[TRIG_ACCENT_PARAM+7].setValue(accent);
			params[TRIG_RNDACCENT_PARAM+7].setValue(accentRnd);
			params[TRIG_GATEPROB_PARAM+7].setValue(gateProb);
			skipState[7] = skip;
			slideState[7] = slide;
	}

	if (rightTrigger.process(params[RIGHT_PARAM].getValue())) {
			float pitch = params[TRIG_PITCH_PARAM+7].getValue();
			float pulse = params[TRIG_COUNT_PARAM+7].getValue();
			float type = params[TRIG_TYPE_PARAM+7].getValue();
			float pitchRnd = params[TRIG_PITCHRND_PARAM+7].getValue();
			float accent = params[TRIG_ACCENT_PARAM+7].getValue();
			float accentRnd = params[TRIG_RNDACCENT_PARAM+7].getValue();
			float gateProb = params[TRIG_GATEPROB_PARAM+7].getValue();
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
			params[TRIG_PITCHRND_PARAM].setValue(pitchRnd);
			params[TRIG_ACCENT_PARAM].setValue(accent);
			params[TRIG_RNDACCENT_PARAM].setValue(accentRnd);
			params[TRIG_GATEPROB_PARAM].setValue(gateProb);
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
		numSteps = clamp(roundf(params[STEPS_PARAM].getValue() + inputs[STEPS_INPUT].getVoltage()), 1.0f, 16.0f);
		UpdatePattern();
		if (!loadedFromJson) {
			loadedFromJson = true;
			updateFlag = true;
		}
	}

	// Steps && Pulses Management
	if (nextStep) {
		// Advance step
		candidateForPreviousPitch = closestVoltageInScale(clamp(patterns[playedPattern].CurrentStep().pitch + rndPitch,-4.0f,6.0f) * clamp(patterns[playedPattern].sensitivity + (inputs[SENSITIVITY_INPUT].isConnected() ? rescale(inputs[SENSITIVITY_INPUT].getVoltage(),0.f,10.f,0.1f,1.0f) : 0.0f),0.1f,1.0f),
			clamp(patterns[playedPattern].rootNote + rescale(clamp(inputs[ROOT_NOTE_INPUT].getVoltage(), 0.0f,10.0f),0.0f,10.0f,0.0f,11.0f), 0.0f,11.0f), patterns[playedPattern].scale + inputs[SCALE_INPUT].getVoltage());

		prevIndex = index;
		auto nextT = patterns[playedPattern].GetNextStep(reStart);
		index = std::get<0>(nextT);
		pulse = std::get<1>(nextT);

		if (reStart)
			reStart = false;

		if (((!stepOutputsMode) && (pulse == 0)) || (stepOutputsMode))
			stepPulse[patterns[playedPattern].CurrentStep().index].trigger(10 * invESR);

		probGate = index != prevIndex ? random::uniform() <= patterns[playedPattern].CurrentStep().gateProb : probGate;
		rndPitch = index != prevIndex ? rescale(random::uniform(),0.0f,1.0f,patterns[playedPattern].CurrentStep().pitchRnd * -5.0f, patterns[playedPattern].CurrentStep().pitchRnd * 5.0f) : rndPitch;
		accent = index != prevIndex ? clamp(patterns[playedPattern].CurrentStep().accent + rescale(random::uniform(),0.0f,1.0f,patterns[playedPattern].CurrentStep().accentRnd * -5.0f,patterns[playedPattern].CurrentStep().accentRnd * 5.0f),0.0f,10.0f)  : accent;

		lights[STEPS_LIGHTS+patterns[playedPattern].CurrentStep().index].setBrightness(1.0f);
	}

	// Lights & steps outputs
	for (int i = 0; i < 8; i++) {
		lights[STEPS_LIGHTS + i].setBrightness(lights[STEPS_LIGHTS + i].getBrightness() - lights[STEPS_LIGHTS + i].getBrightness() * invLightLambda * invESR);
		lights[SLIDES_LIGHTS + i].setBrightness(slideState[i] == 't' ? 1.0f - lights[STEPS_LIGHTS + i].getBrightness() : lights[STEPS_LIGHTS + i].getBrightness());
		lights[SKIPS_LIGHTS + i].setBrightness(skipState[i] == 't' ? 1.0f - lights[STEPS_LIGHTS + i].getBrightness() : lights[STEPS_LIGHTS + i].getBrightness());

		outputs[STEP_OUTPUT+i].setVoltage(stepPulse[i].process(invESR) ? 10.0f : 0.0f);
	}
	lights[RESET_LIGHT].setBrightness(lights[RESET_LIGHT].getBrightness() - lights[RESET_LIGHT].getBrightness() * invLightLambda * invESR);
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
	pitch = closestVoltageInScale(clamp(patterns[playedPattern].CurrentStep().pitch + rndPitch,-4.0f,6.0f) * clamp(patterns[playedPattern].sensitivity + (inputs[SENSITIVITY_INPUT].isConnected() ? rescale(inputs[SENSITIVITY_INPUT].getVoltage(),0.f,10.f,0.1f,1.0f) : 0.0f),0.1f,1.0f),
		clamp(patterns[playedPattern].rootNote + rescale(clamp(inputs[ROOT_NOTE_INPUT].getVoltage(), 0.0f,10.0f),0.0f,10.0f,0.0f,11.0f), 0.0f, 11.0f), patterns[playedPattern].scale + inputs[SCALE_INPUT].getVoltage());
	if (patterns[playedPattern].CurrentStep().slide) {
		if (pulse == 0) {
			float slideCoeff = clamp(patterns[playedPattern].slideTime - 0.01f + inputs[SLIDE_TIME_INPUT].getVoltage() * 0.1f, -0.1f, 0.99f);
			pitch = pitch - (1.0f - powf(phase, slideCoeff)) * (pitch - previousPitch);
		}
	}

	// Update Outputs
	outputs[GATE_OUTPUT].setVoltage(gateOn ? (probGate ? gateValue : 0.0f) : 0.0f);
	outputs[PITCH_OUTPUT].setVoltage(pitch);
	outputs[ACC_OUTPUT].setVoltage(accent);

	if (nextStep && gateOn)
		previousPitch = candidateForPreviousPitch;
}

struct BORDLDisplay : TransparentWidget {
	BORDL *module;
	int frame = 0;
	shared_ptr<Font> font;

	std::string note, scale, steps, playMode, selectedPattern, playedPattern;

	BORDLDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void drawMessage(NVGcontext *vg, Vec pos, std::string note, std::string playMode, std::string selectedPattern, std::string playedPattern, std::string steps, std::string scale) {
		nvgFontSize(vg, 18.0f);
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
			case BORDL::NOTE_C:       return "C";
			case BORDL::NOTE_C_SHARP: return "C#";
			case BORDL::NOTE_D:       return "D";
			case BORDL::NOTE_D_SHARP: return "D#";
			case BORDL::NOTE_E:       return "E";
			case BORDL::NOTE_F:       return "F";
			case BORDL::NOTE_F_SHARP: return "F#";
			case BORDL::NOTE_G:       return "G";
			case BORDL::NOTE_G_SHARP: return "G#";
			case BORDL::NOTE_A:       return "A";
			case BORDL::NOTE_A_SHARP: return "A#";
			case BORDL::NOTE_B:       return "B";
			default: return "";
		}
	}

	std::string displayScale(int value) {
		switch(value){
			case BORDL::AEOLIAN:        return "Aeolian";
			case BORDL::BLUES:          return "Blues";
			case BORDL::CHROMATIC:      return "Chromatic";
			case BORDL::DIATONIC_MINOR: return "Diatonic Minor";
			case BORDL::DORIAN:         return "Dorian";
			case BORDL::HARMONIC_MINOR: return "Harmonic Minor";
			case BORDL::INDIAN:         return "Indian";
			case BORDL::LOCRIAN:        return "Locrian";
			case BORDL::LYDIAN:         return "Lydian";
			case BORDL::MAJOR:          return "Major";
			case BORDL::MELODIC_MINOR:  return "Melodic Minor";
			case BORDL::MINOR:          return "Minor";
			case BORDL::MIXOLYDIAN:     return "Mixolydian";
			case BORDL::NATURAL_MINOR:  return "Natural Minor";
			case BORDL::PENTATONIC:     return "Pentatonic";
			case BORDL::PHRYGIAN:       return "Phrygian";
			case BORDL::TURKISH:        return "Turkish";
			case BORDL::NONE:           return "None";
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
			note = displayRootNote(clamp(module->patterns[module->selectedPattern].rootNote + rescale(clamp(module->inputs[BORDL::ROOT_NOTE_INPUT].getVoltage(),0.0f,10.0f),0.0f,10.0f,0.0f,11.0f),0.0f, 11.0f));
			steps = (module->patterns[module->selectedPattern].countMode == 0 ? "steps:" : "pulses:" ) + to_string(module->patterns[module->selectedPattern].numberOfStepsParam);
			playMode = displayPlayMode(module->patterns[module->selectedPattern].playMode);
			scale = displayScale(module->patterns[module->selectedPattern].scale);
			selectedPattern = "P" + to_string(module->selectedPattern + 1);
			playedPattern = "P" + to_string(module->playedPattern + 1);
			drawMessage(vg, Vec(0.0f, 20.0f), note, playMode, selectedPattern, playedPattern, steps, scale);
		}
	}
};

struct BORDLGateDisplay : TransparentWidget {
	BORDL *module;
	shared_ptr<Font> font;

	int index;

	BORDLGateDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void drawGate(const DrawArgs &args, Vec pos) {
		if (module) {
			int gateType = (int)module->params[BORDL::TRIG_TYPE_PARAM+index].getValue();
			nvgStrokeWidth(args.vg, 1.0f);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgFontSize(args.vg, 16.0f);
			nvgFontFaceId(args.vg, font->handle);
			nvgTextLetterSpacing(args.vg, -2.0f);
			if (gateType == 0) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,22.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
			}
			else if (gateType == 1) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,22.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,6.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgFill(args.vg);
			}
			else if (gateType == 2) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,6.0f,6.0f,0.0f);
				nvgRoundedRect(args.vg,pos.x+8.0f,pos.y,6.0f,6.0f,0.0f);
				nvgRoundedRect(args.vg,pos.x+16.0f,pos.y,6.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgFill(args.vg);
			}
			else if (gateType == 3) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,22.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgFill(args.vg);
			}
			else if (gateType == 4) {
			  nvgText(args.vg, pos.x+11.0f, pos.y+8.0f, "G1", NULL);
			}
			else if (gateType == 5) {
			  nvgText(args.vg, pos.x+11.0f, pos.y+8.0f, "G2", NULL);
			}
		}
	}

	void draw(const DrawArgs &args) override {
		drawGate(args, Vec(0.0f, 0.0f));
	}
};

struct BORDLPulseDisplay : TransparentWidget {
	BORDL *module;
	shared_ptr<Font> font;

	int index;

	BORDLPulseDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void drawPulse(const DrawArgs &args, Vec pos) {
		if (module) {
			nvgStrokeWidth(args.vg, 1.0f);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgFontSize(args.vg, 16.0f);
			nvgFontFaceId(args.vg, font->handle);
			nvgTextLetterSpacing(args.vg, -2.0f);
			char tCount[128];
			snprintf(tCount, sizeof(tCount), "%1i", (int)module->params[BORDL::TRIG_COUNT_PARAM+index].getValue());
			nvgText(args.vg, pos.x, pos.y, tCount, NULL);
		}
	}

	void draw(const DrawArgs &args) override {
		drawPulse(args, Vec(0.0f, 0.0f));
	}
};

struct BORDLPitchDisplay : TransparentWidget {
	BORDL *module;
	shared_ptr<Font> font;

	int index;

	BORDLPitchDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	std::string displayNote(float value) {
		int octave = sgn(value) == 1.0f ? value : (value-1);
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

	void drawPitch(const DrawArgs &args, Vec pos) {
		if (module) {
			nvgStrokeWidth(args.vg, 3.0f);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgFontSize(args.vg, 16.0f);
			nvgText(args.vg, pos.x, pos.y-9.0f, displayNote(module->closestVoltageInScale(module->params[BORDL::TRIG_PITCH_PARAM+index].getValue() * clamp(module->patterns[module->playedPattern].sensitivity + (module->inputs[BORDL::SENSITIVITY_INPUT].isConnected() ? rescale(module->inputs[BORDL::SENSITIVITY_INPUT].getVoltage(),0.f,10.f,0.1f,1.0f) : 0.0f),0.1f,1.0f) ,
				clamp(module->patterns[module->selectedPattern].rootNote + rescale(clamp(module->inputs[BORDL::ROOT_NOTE_INPUT].getVoltage(), 0.0f,10.0f),0.0f,10.0f,0.0f,11.0f), 0.0f, 11.0f),
				module->patterns[module->selectedPattern].scale)).c_str(), NULL);
		}
	}

	void draw(const DrawArgs &args) override {
		drawPitch(args, Vec(0.0f, 0.0f));
	}
};

struct BORDLWidget : ModuleWidget {
	ParamWidget *stepsParam, *scaleParam, *rootNoteParam, *sensitivityParam,
	 *gateTimeParam, *slideTimeParam, *playModeParam, *countModeParam, *patternParam,
		*pitchParams[8], *pulseParams[8], *typeParams[8], *slideParams[8], *skipParams[8],
		 *pitchRndParams[8], *pulseProbParams[8], *accentParams[8], *rndAccentParams[8];

	void appendContextMenu(ui::Menu *menu) override;

	BORDLWidget(BORDL *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/BORDL.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		{
			BORDLDisplay *display = new BORDLDisplay();
			display->module = module;
			display->box.pos = Vec(20.0f, 217.0f);
			display->box.size = Vec(250.0f, 60.0f);
			addChild(display);
		}

		static const float portX0[4] = {20.0f, 58.0f, 96.0f, 135.0f};

		addParam(createParam<BidooRoundBlackKnob>(Vec(portX0[0]-2.0f, 36.0f), module, BORDL::CLOCK_PARAM));
		addParam(createParam<LEDButton>(Vec(61.0f, 40.0f), module, BORDL::RUN_PARAM));
		addChild(createLight<SmallLight<GreenLight>>(Vec(67.0f, 46.0f), module, BORDL::RUNNING_LIGHT));
		addParam(createParam<LEDButton>(Vec(99.0f, 40.0f), module, BORDL::RESET_PARAM));
		addChild(createLight<SmallLight<GreenLight>>(Vec(105.0f, 46.0f), module, BORDL::RESET_LIGHT));
		stepsParam = createParam<BidooBlueSnapKnob>(Vec(portX0[3]-2.0f, 36.0f), module, BORDL::STEPS_PARAM);
		addParam(stepsParam);


	 	addInput(createInput<PJ301MPort>(Vec(portX0[0], 69.0f),  module, BORDL::CLOCK_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1], 69.0f),  module, BORDL::EXT_CLOCK_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[2], 69.0f),  module, BORDL::RESET_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[3], 69.0f),  module, BORDL::STEPS_INPUT));

		rootNoteParam = createParam<BidooBlueSnapKnob>(Vec(portX0[0]-2.0f, 116.0f), module, BORDL::ROOT_NOTE_PARAM);
		addParam(rootNoteParam);
		scaleParam = createParam<BidooBlueSnapKnob>(Vec(portX0[1]-2.0f, 116.0f), module, BORDL::SCALE_PARAM);
		addParam(scaleParam);
		gateTimeParam = createParam<BidooBlueKnob>(Vec(portX0[2]-2.0f, 116.0f), module, BORDL::GATE_TIME_PARAM);
		addParam(gateTimeParam);
		slideTimeParam = createParam<BidooBlueKnob>(Vec(portX0[3]-2.0f, 116.0f), module, BORDL::SLIDE_TIME_PARAM);
		addParam(slideTimeParam);

		addInput(createInput<PJ301MPort>(Vec(portX0[0], 149.0f),  module, BORDL::ROOT_NOTE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1], 149.0f),  module, BORDL::SCALE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[2], 149.0f),  module, BORDL::GATE_TIME_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[3], 149.0f),  module, BORDL::SLIDE_TIME_INPUT));

		playModeParam = createParam<BlueCKD6>(Vec(portX0[0]-1.0f, 196.0f), module, BORDL::PLAY_MODE_PARAM);
		addParam(playModeParam);
		countModeParam = createParam<BlueCKD6>(Vec(portX0[1]-1.0f, 196.0f), module, BORDL::COUNT_MODE_PARAM);
		addParam(countModeParam);
		addInput(createInput<PJ301MPort>(Vec(portX0[2], 198.0f),  module, BORDL::PATTERN_INPUT));
		patternParam = createParam<BidooRoundBlackSnapKnob>(Vec(portX0[3],196.0f), module, BORDL::PATTERN_PARAM);
		addParam(patternParam);

		static const float portX1[8] = {200.0f, 241.0f, 282.0f, 323.0f, 364.0f, 405.0f, 446.0f, 487.0f};

		sensitivityParam = createParam<BidooBlueTrimpot>(Vec(portX1[0]-24.0f, 30.0f), module, BORDL::SENSITIVITY_PARAM);
		addParam(sensitivityParam);
		addInput(createInput<TinyPJ301MPort>(Vec(portX1[0]-22.0f, 52.0f), module, BORDL::SENSITIVITY_INPUT));

		addInput(createInput<PJ301MPort>(Vec(portX0[0], 286.0f),  module, BORDL::TRANSPOSE_INPUT));
		addParam(createParam<BlueCKD6>(Vec(portX0[1]-1.0f, 285.0f), module, BORDL::COPY_PARAM));
		addChild(createLight<SmallLight<GreenLight>>(Vec(portX0[1]+23.0f, 283.0f), module, BORDL::COPY_LIGHT));

		addParam(createParam<LeftBtn>(Vec(104.0f, 290.0f), module, BORDL::LEFT_PARAM));
		addParam(createParam<RightBtn>(Vec(134.0f, 290.0f), module, BORDL::RIGHT_PARAM));
		addParam(createParam<UpBtn>(Vec(119.0f, 282.0f), module, BORDL::UP_PARAM));
		addParam(createParam<DownBtn>(Vec(119.0f, 297.0f), module, BORDL::DOWN_PARAM));

		for (int i = 0; i < 8; i++) {
			pitchParams[i] = createParam<BidooBlueKnob>(Vec(portX1[i]+1.0f, 56.0f), module, BORDL::TRIG_PITCH_PARAM + i);
			addParam(pitchParams[i]);
			pitchRndParams[i] = createParam<BidooBlueTrimpot>(Vec(portX1[i]+27.0f, 81.0f), module, BORDL::TRIG_PITCHRND_PARAM + i);
			addParam(pitchRndParams[i]);
			accentParams[i] = createParam<BidooBlueKnob>(Vec(portX1[i]+1.0f, 110.0f), module, BORDL::TRIG_ACCENT_PARAM + i);
			addParam(accentParams[i]);
			rndAccentParams[i] = createParam<BidooBlueTrimpot>(Vec(portX1[i]+27.0f, 135.0f), module, BORDL::TRIG_RNDACCENT_PARAM + i);
			addParam(rndAccentParams[i]);
			{
				BORDLPitchDisplay *displayPitch = new BORDLPitchDisplay();
				displayPitch->module = module;
				displayPitch->box.pos = Vec(portX1[i]+16.0f, 52.0f);
				displayPitch->box.size = Vec(20.0f, 10.0f);
				displayPitch->index = i;
				addChild(displayPitch);
			}
			pulseParams[i] = createParam<BidooBlueSnapKnob>(Vec(portX1[i]+1.0f, 188.0f), module, BORDL::TRIG_COUNT_PARAM + i);
			addParam(pulseParams[i]);
			pulseProbParams[i] = createParam<BidooBlueTrimpot>(Vec(portX1[i]+27.0f, 213.0f), module, BORDL::TRIG_GATEPROB_PARAM + i);
			addParam(pulseProbParams[i]);
			{
				BORDLPulseDisplay *displayPulse = new BORDLPulseDisplay();
				displayPulse->module = module;
				displayPulse->box.pos = Vec(portX1[i]+15.0f, 179.0f);
				displayPulse->box.size = Vec(20.0f, 10.0f);
				displayPulse->index = i;
				addChild(displayPulse);
			}
			typeParams[i] = createParam<BidooBlueSnapTrimpot>(Vec(portX1[i]+6.5f, 267.0f), module, BORDL::TRIG_TYPE_PARAM + i);
			addParam(typeParams[i]);
			{
				BORDLGateDisplay *displayGate = new BORDLGateDisplay();
				displayGate->module = module;
				displayGate->box.pos = Vec(portX1[i]+5.0f, 250.0f);
				displayGate->box.size = Vec(20.0f, 10.0f);
				displayGate->index = i;
				addChild(displayGate);
			}
			slideParams[i] = createParam<LEDButton>(Vec(portX1[i]+7.0f, 295.0f), module, BORDL::TRIG_SLIDE_PARAM + i);
			addParam(slideParams[i]);
			addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[i]+13.0f, 301.0f), module, BORDL::SLIDES_LIGHTS + i));
			skipParams[i] = createParam<LEDButton>(Vec(portX1[i]+7.0f, 316.0f), module, BORDL::TRIG_SKIP_PARAM + i);
			addParam(skipParams[i]);
			addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[i]+13.0f, 322.0f), module, BORDL::SKIPS_LIGHTS + i));

			addOutput(createOutput<TinyPJ301MPort>(Vec(portX1[i]+9.0f, 344.0f), module, BORDL::STEP_OUTPUT + i));
		}

		addInput(createInput<PJ301MPort>(Vec(9.5f, 330.0f), module, BORDL::EXTGATE1_INPUT));
		addInput(createInput<PJ301MPort>(Vec(42.5f, 330.0f), module, BORDL::EXTGATE2_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(76.f, 330.0f), module, BORDL::GATE_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(109.f, 330.0f), module, BORDL::PITCH_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(142.0f, 330.0f), module, BORDL::ACC_OUTPUT));
	}
};

struct BORDLRandPitchItem : MenuItem {
	BORDL *module;
	void onAction(const event::Action &e) override {
		module->randomizePitch();
	}
};

struct BORDLRandGatesItem : MenuItem {
	BORDL *module;
	void onAction(const event::Action &e) override {
		module->randomizeGates();
	}
};

struct BORDLRandSlideSkipItem : MenuItem {
	BORDL *module;
	void onAction(const event::Action &e) override {
		module->randomizeSlidesSkips();
	}
};

struct BORDLStepOutputsModeItem : MenuItem {
	BORDL *module;
	void onAction(const event::Action &e) override {
		module->stepOutputsMode = !module->stepOutputsMode;
	}
	void step() override {
		rightText = module->stepOutputsMode ? "✔" : "";
		MenuItem::step();
	}
};

void BORDLWidget::appendContextMenu(ui::Menu *menu) {
	BORDL *module = dynamic_cast<BORDL*>(this->module);
	assert(module);

	menu->addChild(construct<MenuLabel>());
	menu->addChild(construct<BORDLRandPitchItem>(&MenuItem::text, "Rand pitch", &BORDLRandPitchItem::module, module));
	menu->addChild(construct<BORDLRandGatesItem>(&MenuItem::text, "Rand gates", &BORDLRandGatesItem::module, module));
	menu->addChild(construct<BORDLRandSlideSkipItem>(&MenuItem::text, "Rand slides & skips", &BORDLRandSlideSkipItem::module, module));
	menu->addChild(construct<BORDLStepOutputsModeItem>(&MenuItem::text, "Step outputs mode", &BORDLStepOutputsModeItem::module, module));
}


Model *modelBORDL = createModel<BORDL, BORDLWidget>("bordL");

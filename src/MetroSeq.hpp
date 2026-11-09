#pragma once
#include <rack.hpp>
#include "dep/quantizer.hpp"

using namespace rack;

// The engine shared by DTROY and BORDL: 8 stages played as up to 16 steps.
// Each stage has a pitch, a number of pulses (clocks it lasts), a gate type,
// a slide and a skip; BORDL adds a gate probability, a random pitch and an
// accent. 16 patterns: the panel edits the selected one, the pattern CV can
// play any other.

struct MetroStep {
	int index = 0;     // stage 0..7
	int number = 0;    // step 0..15
	bool skip = false;
	bool slide = false;
	int pulses = 1;
	float pitch = 0.f;
	int type = 2;
	float gateProb = 1.f;
	float pitchRnd = 0.f;
	float accent = 0.f;
	float accentRnd = 0.f;
};

struct MetroPattern {
	enum GateType { REST, SINGLE, MULTIPLE, HOLD, EXT1, EXT2 };

	// Settings, saved with the patch.
	int playMode = 0;    // 0 forward, 1 backward, 2 ping-pong, 3 random, 4 brownian
	int countMode = 0;   // 0 steps, 1 pulses
	int numberOfStepsParam = 8;
	int rootNote = 0;    // 0..11, C..B
	int scale = 0;       // 0 none
	float gateTime = 0.5f;
	float slideTime = 0.2f;
	float sensitivity = 1.f;
	float stagePitch[8] = {};
	int stagePulses[8] = {1, 1, 1, 1, 1, 1, 1, 1};
	int stageType[8] = {2, 2, 2, 2, 2, 2, 2, 2};
	bool stageSlide[8] = {};
	bool stageSkip[8] = {};
	float stageGateProb[8] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
	float stagePitchRnd[8] = {};
	float stageAccent[8] = {};
	float stageAccentRnd[8] = {};

	// Play position.
	int currentStep = 0;
	int currentPulse = 0;
	bool forward = true;

	// The steps, built from the settings.
	MetroStep steps[16];

	MetroPattern() {
		build(numberOfStepsParam);
	}

	void copySettings(const MetroPattern &o) {
		int s = currentStep, p = currentPulse;
		bool f = forward;
		*this = o;
		currentStep = s;
		currentPulse = p;
		forward = f;
	}

	// In steps mode the first n steps play; in pulses mode the steps play
	// until n pulses are counted, skipped stages counting none.
	void build(int n) {
		int pCount = 0;
		for (int i = 0; i < 16; i++) {
			MetroStep &s = steps[i];
			int idx = i % 8;
			s.index = idx;
			s.number = i;
			s.slide = stageSlide[idx];
			s.pitch = stagePitch[idx];
			s.type = stageType[idx];
			s.pulses = stagePulses[idx];
			s.gateProb = stageGateProb[idx];
			s.pitchRnd = stagePitchRnd[idx];
			s.accent = stageAccent[idx];
			s.accentRnd = stageAccentRnd[idx];
			if (countMode == 0)
				s.skip = stageSkip[idx] || i >= n;
			else if (stageSkip[idx] || pCount >= n)
				s.skip = true;
			else {
				s.skip = false;
				s.pulses = std::min(s.pulses, n - pCount);
				pCount += s.pulses;
			}
		}
	}

	int firstStep() {
		for (int i = 0; i < 16; i++)
			if (!steps[i].skip)
				return i;
		return 0;
	}

	int lastStep() {
		for (int i = 15; i >= 0; i--)
			if (!steps[i].skip)
				return i;
		return 15;
	}

	int nextForward(int pos) {
		for (int i = 1; i < 16; i++)
			if (!steps[(pos + i) % 16].skip)
				return (pos + i) % 16;
		return pos;
	}

	int nextBackward(int pos) {
		for (int i = 1; i < 16; i++)
			if (!steps[(pos + 16 - i) % 16].skip)
				return (pos + 16 - i) % 16;
		return pos;
	}

	void advance(bool reset) {
		if (reset) {
			currentStep = (playMode == 1) ? lastStep() : firstStep();
			currentPulse = 0;
			forward = true;
			return;
		}
		if (!steps[currentStep].skip && currentPulse < steps[currentStep].pulses - 1) {
			currentPulse++;
			return;
		}
		currentPulse = 0;
		switch (playMode) {
			case 0: currentStep = nextForward(currentStep); break;
			case 1: currentStep = nextBackward(currentStep); break;
			case 2: {
				if (currentStep == lastStep())
					forward = false;
				if (currentStep == firstStep())
					forward = true;
				currentStep = forward ? nextForward(currentStep) : nextBackward(currentStep);
				break;
			}
			case 3: {
				int count = 0;
				for (int i = 0; i < 16; i++)
					count += !steps[i].skip;
				if (count == 0)
					break;
				int k = random::u32() % count;
				for (int i = 0; i < 16; i++) {
					if (!steps[i].skip && k-- == 0) {
						currentStep = i;
						break;
					}
				}
				break;
			}
			case 4: currentStep = (random::uniform() < 0.5f) ? nextBackward(currentStep) : nextForward(currentStep); break;
			default: break;
		}
	}

	json_t *toJson(bool extended) const {
		json_t *pJ = json_object();
		json_object_set_new(pJ, "playMode", json_integer(playMode));
		json_object_set_new(pJ, "countMode", json_integer(countMode));
		json_object_set_new(pJ, "numSteps", json_integer(numberOfStepsParam));
		json_object_set_new(pJ, "rootNote", json_integer(rootNote));
		json_object_set_new(pJ, "scale", json_integer(scale));
		json_object_set_new(pJ, "gateTime", json_real(gateTime));
		json_object_set_new(pJ, "slideTime", json_real(slideTime));
		json_object_set_new(pJ, "sensitivity", json_real(sensitivity));
		json_t *stagesJ = json_array();
		for (int j = 0; j < 8; j++) {
			json_t *sJ = json_object();
			json_object_set_new(sJ, "pitch", json_real(stagePitch[j]));
			json_object_set_new(sJ, "pulses", json_integer(stagePulses[j]));
			json_object_set_new(sJ, "type", json_integer(stageType[j]));
			json_object_set_new(sJ, "slide", json_boolean(stageSlide[j]));
			json_object_set_new(sJ, "skip", json_boolean(stageSkip[j]));
			if (extended) {
				json_object_set_new(sJ, "gateProb", json_real(stageGateProb[j]));
				json_object_set_new(sJ, "pitchRnd", json_real(stagePitchRnd[j]));
				json_object_set_new(sJ, "accent", json_real(stageAccent[j]));
				json_object_set_new(sJ, "accentRnd", json_real(stageAccentRnd[j]));
			}
			json_array_append_new(stagesJ, sJ);
		}
		json_object_set_new(pJ, "stages", stagesJ);
		return pJ;
	}

	void fromJson(json_t *pJ) {
		*this = MetroPattern();
		if (!pJ)
			return;
		if (json_t *j = json_object_get(pJ, "playMode")) playMode = clamp((int)json_integer_value(j), 0, 4);
		if (json_t *j = json_object_get(pJ, "countMode")) countMode = clamp((int)json_integer_value(j), 0, 1);
		if (json_t *j = json_object_get(pJ, "numSteps")) numberOfStepsParam = clamp((int)json_integer_value(j), 1, 16);
		if (json_t *j = json_object_get(pJ, "rootNote")) rootNote = clamp((int)json_integer_value(j), 0, 11);
		if (json_t *j = json_object_get(pJ, "scale")) scale = clamp((int)json_integer_value(j), 0, quantizer::numScales - 1);
		if (json_t *j = json_object_get(pJ, "gateTime")) gateTime = json_number_value(j);
		if (json_t *j = json_object_get(pJ, "slideTime")) slideTime = json_number_value(j);
		if (json_t *j = json_object_get(pJ, "sensitivity")) sensitivity = json_number_value(j);
		json_t *stagesJ = json_object_get(pJ, "stages");
		for (int s = 0; s < 8 && stagesJ; s++) {
			json_t *sJ = json_array_get(stagesJ, s);
			if (!sJ)
				continue;
			if (json_t *j = json_object_get(sJ, "pitch")) stagePitch[s] = clamp((float)json_number_value(j), -4.f, 6.f);
			if (json_t *j = json_object_get(sJ, "pulses")) stagePulses[s] = clamp((int)json_integer_value(j), 1, 8);
			if (json_t *j = json_object_get(sJ, "type")) stageType[s] = clamp((int)json_integer_value(j), 0, 5);
			if (json_t *j = json_object_get(sJ, "slide")) stageSlide[s] = json_is_true(j);
			if (json_t *j = json_object_get(sJ, "skip")) stageSkip[s] = json_is_true(j);
			if (json_t *j = json_object_get(sJ, "gateProb")) stageGateProb[s] = clamp((float)json_number_value(j), 0.f, 1.f);
			if (json_t *j = json_object_get(sJ, "pitchRnd")) stagePitchRnd[s] = clamp((float)json_number_value(j), 0.f, 1.f);
			if (json_t *j = json_object_get(sJ, "accent")) stageAccent[s] = clamp((float)json_number_value(j), 0.f, 10.f);
			if (json_t *j = json_object_get(sJ, "accentRnd")) stageAccentRnd[s] = clamp((float)json_number_value(j), 0.f, 1.f);
		}
		build(numberOfStepsParam);
	}
};

// A pattern edit on its way to the undo history (audio thread to UI thread),
// or an undo on its way back: only what differs between the two is applied.
struct MetroEdit {
	int pattern = 0;
	const char *name = "";
	MetroPattern before, after;
};

// A knob's value on its way back to the pattern it was moved on.
struct MetroKnobUndo {
	int pattern = 0;
	int paramId = 0;
	float value = 0.f;
};

// The controls DTROY and BORDL share.
template <class M>
void configMetro(M &m) {
	m.configParam(M::CLOCK_PARAM, -3.f, 3.f, 0.f, "Clock", " BPM", 2.f, 120.f);
	m.configButton(M::RUN_PARAM, "Run");
	m.configButton(M::RESET_PARAM, "Reset (the next clock plays the first step)");
	m.configParam(M::STEPS_PARAM, 1.f, 16.f, 8.f, "Steps (or pulses in pulse count mode)");
	m.paramQuantities[M::STEPS_PARAM]->snapEnabled = true;
	std::vector<std::string> roots, scales;
	for (int i = 1; i < quantizer::numNotes; i++)
		roots.push_back(quantizer::rootNotes[i].label);
	for (int i = 0; i < quantizer::numScales; i++)
		scales.push_back(quantizer::scales[i].label);
	m.configSwitch(M::ROOT_NOTE_PARAM, 0.f, 11.f, 0.f, "Root note", roots);
	m.configSwitch(M::SCALE_PARAM, 0.f, quantizer::numScales - 1, 0.f, "Scale (No: not quantized)", scales);
	m.configParam(M::GATE_TIME_PARAM, 0.1f, 1.f, 0.5f, "Gate length", "%", 0.f, 100.f);
	m.configParam(M::SLIDE_TIME_PARAM, 0.1f, 1.f, 0.2f, "Slide curve", "%", 0.f, 100.f);
	m.configParam(M::PATTERN_PARAM, 1.f, 16.f, 1.f, "Edited pattern");
	m.paramQuantities[M::PATTERN_PARAM]->snapEnabled = true;
	m.paramQuantities[M::PATTERN_PARAM]->randomizeEnabled = false;
	m.configParam(M::SENSITIVITY_PARAM, 0.1f, 1.f, 1.f, "Pitch sensitivity", "%", 0.f, 100.f);

	m.configButton(M::PLAY_MODE_PARAM, "Play mode (forward, backward, ping-pong, random, brownian)");
	m.configButton(M::COUNT_MODE_PARAM, "Count mode (steps or pulses)");
	m.configButton(M::COPY_PARAM, "Copy, then paste on another pattern");
	m.configButton(M::LEFT_PARAM, "Rotate the stages left");
	m.configButton(M::RIGHT_PARAM, "Rotate the stages right");
	m.configButton(M::UP_PARAM, "Transpose up 1 semitone");
	m.configButton(M::DOWN_PARAM, "Transpose down 1 semitone");

	m.configInput(M::CLOCK_INPUT, "Clock tempo (1V/oct, 0 V = 120 BPM with the knob at 120)");
	m.configInput(M::EXT_CLOCK_INPUT, "External clock");
	m.configInput(M::RESET_INPUT, "Reset (the next clock plays the first step)");
	m.configInput(M::STEPS_INPUT, "Steps (1 V per step)");
	m.configInput(M::SLIDE_TIME_INPUT, "Slide curve (10 V = 100%)");
	m.configInput(M::GATE_TIME_INPUT, "Gate length (10 V = 100%)");
	m.configInput(M::ROOT_NOTE_INPUT, "Root note (0-10 V = 0-11 semitones)");
	m.configInput(M::SCALE_INPUT, "Scale (1 V per scale)");
	m.configInput(M::EXTGATE1_INPUT, "External gate 1");
	m.configInput(M::EXTGATE2_INPUT, "External gate 2");
	m.configInput(M::PATTERN_INPUT, "Played pattern (0.625 V per pattern: 0-10 V = 1-16)");
	m.configInput(M::TRANSPOSE_INPUT, "Transpose (1V/oct, before the quantizer)");
	m.configInput(M::SENSITIVITY_INPUT, "Sensitivity (10 V = 100%)");

	m.configOutput(M::GATE_OUTPUT, "Gate");
	m.configOutput(M::PITCH_OUTPUT, "Pitch (1V/oct)");

	for (int i = 0; i < 8; i++) {
		m.configParam(M::TRIG_PITCH_PARAM + i, -4.f, 6.f, 0.f, rack::string::f("Stage %d pitch", i + 1), " V");
		m.configParam(M::TRIG_COUNT_PARAM + i, 1.f, 8.f, 1.f, rack::string::f("Stage %d pulses", i + 1));
		m.paramQuantities[M::TRIG_COUNT_PARAM + i]->snapEnabled = true;
		m.configSwitch(M::TRIG_TYPE_PARAM + i, 0.f, 5.f, 2.f, rack::string::f("Stage %d gate", i + 1), {"Rest", "Single", "Multiple", "Hold", "External gate 1", "External gate 2"});
		m.configButton(M::TRIG_SLIDE_PARAM + i, rack::string::f("Stage %d slide", i + 1));
		m.configButton(M::TRIG_SKIP_PARAM + i, rack::string::f("Stage %d skip", i + 1));
		m.configOutput(M::STEP_OUTPUT + i, rack::string::f("Stage %d", i + 1));
	}
}

// M is the module. Its shared controls have the same ids in DTROY and BORDL.
// M::EXTENDED adds BORDL's probability, random pitch and accent. In both, the
// play, count, slide and skip controls are momentary buttons that toggle (or
// cycle) the pattern's value, so a MIDI-mapped button press flips them too.
template <class M>
struct MetroEngine {
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
	long stepPulse[8] = {0};     // samples left of the stage output pulses

	bool running = true;
	double phase = 0.0;          // position in the current pulse, 0..1
	// The internal clock from a count, not added up: n samples at `rate` from clockBase,
	// less the pulses already played.
	long long clockSamples = 0;
	double clockBase = 0.0;
	long long clockPulses = 0;
	long sinceClock = -1;        // samples since the last external clock
	long clockInterval = 0;      // samples between the last two external clocks
	bool resetArmed = true;      // the next clock plays the first step
	bool started = false;        // a step has been played since the start
	int pulse = 0;
	long sincePulse = 0;         // samples since the last clock
	float pitch = 0.f;
	float target = 0.f;          // the quantized pitch of the step
	float slideFrom = 0.f;
	bool lastGate = false;
	bool stepOn = true;          // the step passed its probability
	float rndPitch = 0.f;
	float accent = 0.f;
	int selectedPattern = 0;
	int playedPattern = 0;
	int copyPattern = -1;
	bool stepOutputsMode = false;

	// Control rate: the panel, the buttons and the lights run every CONTROL_DIV
	// samples; clock edges, gates and the pitch stay per sample.
	static constexpr int CONTROL_DIV = 16;
	int controlCounter = 0;
	// Caches, recomputed only when what they depend on changes.
	double rateVolts = NAN;      // clock knob + CV the rate was computed for
	double rate = 2.0;
	float quantIn = NAN;         // quantizer input, root and scale of 'quantOut'
	int quantRoot = -1, quantScale = -1;
	float quantOut = 0.f;

	MetroPattern patterns[16];
	quantizer::Quantizer quant;

	// The audio thread owns the patterns: the buttons edit them here at control
	// rate, an undo only asks for a change. No lock on either side.
	dsp::RingBuffer<MetroEdit, 8> editsToHistory;
	dsp::RingBuffer<MetroEdit, 8> editsToApply;
	dsp::RingBuffer<MetroKnobUndo, 32> knobsToApply;

	void pushEdit(const char *name, const MetroPattern &before) {
		if (editsToHistory.full())
			return;
		MetroEdit e;
		e.pattern = selectedPattern;
		e.name = name;
		e.before = before;
		e.after = patterns[selectedPattern];
		editsToHistory.push(e);
	}

	// What differs between from and to, written to p: knobs moved since stay.
	static void applyDifference(MetroPattern &p, const MetroPattern &from, const MetroPattern &to) {
		#define METRO_APPLY(f) if (from.f != to.f) p.f = to.f;
		METRO_APPLY(playMode) METRO_APPLY(countMode) METRO_APPLY(numberOfStepsParam) METRO_APPLY(rootNote) METRO_APPLY(scale)
		METRO_APPLY(gateTime) METRO_APPLY(slideTime) METRO_APPLY(sensitivity)
		for (int i = 0; i < 8; i++) {
			METRO_APPLY(stagePitch[i]) METRO_APPLY(stagePulses[i]) METRO_APPLY(stageType[i]) METRO_APPLY(stageSlide[i]) METRO_APPLY(stageSkip[i])
			METRO_APPLY(stageGateProb[i]) METRO_APPLY(stagePitchRnd[i]) METRO_APPLY(stageAccent[i]) METRO_APPLY(stageAccentRnd[i])
		}
		#undef METRO_APPLY
	}

	// The same conversions as savePanel, for one knob and any pattern.
	static void setKnob(MetroPattern &p, const int id, const float v) {
		if (id == M::STEPS_PARAM) p.numberOfStepsParam = clamp((int)std::round(v), 1, 16);
		else if (id == M::ROOT_NOTE_PARAM) p.rootNote = clamp((int)std::round(v), 0, 11);
		else if (id == M::SCALE_PARAM) p.scale = clamp((int)std::round(v), 0, quantizer::numScales - 1);
		else if (id == M::GATE_TIME_PARAM) p.gateTime = v;
		else if (id == M::SLIDE_TIME_PARAM) p.slideTime = v;
		else if (id == M::SENSITIVITY_PARAM) p.sensitivity = v;
		else if (id >= M::TRIG_PITCH_PARAM && id < M::TRIG_PITCH_PARAM + 8) p.stagePitch[id - M::TRIG_PITCH_PARAM] = v;
		else if (id >= M::TRIG_COUNT_PARAM && id < M::TRIG_COUNT_PARAM + 8) p.stagePulses[id - M::TRIG_COUNT_PARAM] = clamp((int)std::round(v), 1, 8);
		else if (id >= M::TRIG_TYPE_PARAM && id < M::TRIG_TYPE_PARAM + 8) p.stageType[id - M::TRIG_TYPE_PARAM] = clamp((int)std::round(v), 0, 5);
		else if constexpr (M::EXTENDED) {
			if (id >= M::TRIG_GATEPROB_PARAM && id < M::TRIG_GATEPROB_PARAM + 8) p.stageGateProb[id - M::TRIG_GATEPROB_PARAM] = v;
			else if (id >= M::TRIG_PITCHRND_PARAM && id < M::TRIG_PITCHRND_PARAM + 8) p.stagePitchRnd[id - M::TRIG_PITCHRND_PARAM] = v;
			else if (id >= M::TRIG_ACCENT_PARAM && id < M::TRIG_ACCENT_PARAM + 8) p.stageAccent[id - M::TRIG_ACCENT_PARAM] = v;
			else if (id >= M::TRIG_RNDACCENT_PARAM && id < M::TRIG_RNDACCENT_PARAM + 8) p.stageAccentRnd[id - M::TRIG_RNDACCENT_PARAM] = v;
		}
	}

	static bool isPatternKnob(const int id) {
		if (id == M::STEPS_PARAM || id == M::ROOT_NOTE_PARAM || id == M::SCALE_PARAM || id == M::GATE_TIME_PARAM || id == M::SLIDE_TIME_PARAM || id == M::SENSITIVITY_PARAM)
			return true;
		if ((id >= M::TRIG_PITCH_PARAM && id < M::TRIG_PITCH_PARAM + 8) || (id >= M::TRIG_COUNT_PARAM && id < M::TRIG_COUNT_PARAM + 8) || (id >= M::TRIG_TYPE_PARAM && id < M::TRIG_TYPE_PARAM + 8))
			return true;
		if constexpr (M::EXTENDED)
			return (id >= M::TRIG_GATEPROB_PARAM && id < M::TRIG_GATEPROB_PARAM + 8) || (id >= M::TRIG_PITCHRND_PARAM && id < M::TRIG_PITCHRND_PARAM + 8)
				|| (id >= M::TRIG_ACCENT_PARAM && id < M::TRIG_ACCENT_PARAM + 8) || (id >= M::TRIG_RNDACCENT_PARAM && id < M::TRIG_RNDACCENT_PARAM + 8);
		return false;
	}

	void applyUndos(M &m) {
		while (!editsToApply.empty()) {
			const MetroEdit e = editsToApply.shift();
			applyDifference(patterns[e.pattern], e.before, e.after);
			if (e.pattern == selectedPattern)
				loadPanel(m);
		}
		while (!knobsToApply.empty()) {
			const MetroKnobUndo k = knobsToApply.shift();
			setKnob(patterns[k.pattern], k.paramId, k.value);
			if (k.pattern == selectedPattern)
				m.params[k.paramId].setValue(k.value);
		}
	}

	void reset() {
		for (int i = 0; i < 16; i++)
			patterns[i] = MetroPattern();
		selectedPattern = playedPattern = 0;
		copyPattern = -1;
		resetArmed = true;
		started = false;
		running = true;
	}

	// The panel shows the selected pattern.
	void loadPanel(M &m) {
		const MetroPattern &p = patterns[selectedPattern];
		m.params[M::STEPS_PARAM].setValue(p.numberOfStepsParam);
		m.params[M::ROOT_NOTE_PARAM].setValue(p.rootNote);
		m.params[M::SCALE_PARAM].setValue(p.scale);
		m.params[M::GATE_TIME_PARAM].setValue(p.gateTime);
		m.params[M::SLIDE_TIME_PARAM].setValue(p.slideTime);
		m.params[M::SENSITIVITY_PARAM].setValue(p.sensitivity);
		for (int i = 0; i < 8; i++) {
			m.params[M::TRIG_PITCH_PARAM + i].setValue(p.stagePitch[i]);
			m.params[M::TRIG_COUNT_PARAM + i].setValue(p.stagePulses[i]);
			m.params[M::TRIG_TYPE_PARAM + i].setValue(p.stageType[i]);
		}
		if constexpr (M::EXTENDED) {
			for (int i = 0; i < 8; i++) {
				m.params[M::TRIG_GATEPROB_PARAM + i].setValue(p.stageGateProb[i]);
				m.params[M::TRIG_PITCHRND_PARAM + i].setValue(p.stagePitchRnd[i]);
				m.params[M::TRIG_ACCENT_PARAM + i].setValue(p.stageAccent[i]);
				m.params[M::TRIG_RNDACCENT_PARAM + i].setValue(p.stageAccentRnd[i]);
			}
		}
	}

	void savePanel(M &m) {
		MetroPattern &p = patterns[selectedPattern];
		p.numberOfStepsParam = clamp((int)std::round(m.params[M::STEPS_PARAM].getValue()), 1, 16);
		p.rootNote = clamp((int)std::round(m.params[M::ROOT_NOTE_PARAM].getValue()), 0, 11);
		p.scale = clamp((int)std::round(m.params[M::SCALE_PARAM].getValue()), 0, quantizer::numScales - 1);
		p.gateTime = m.params[M::GATE_TIME_PARAM].getValue();
		p.slideTime = m.params[M::SLIDE_TIME_PARAM].getValue();
		p.sensitivity = m.params[M::SENSITIVITY_PARAM].getValue();
		for (int i = 0; i < 8; i++) {
			p.stagePitch[i] = m.params[M::TRIG_PITCH_PARAM + i].getValue();
			p.stagePulses[i] = clamp((int)std::round(m.params[M::TRIG_COUNT_PARAM + i].getValue()), 1, 8);
			p.stageType[i] = clamp((int)std::round(m.params[M::TRIG_TYPE_PARAM + i].getValue()), 0, 5);
		}
		if constexpr (M::EXTENDED) {
			for (int i = 0; i < 8; i++) {
				p.stageGateProb[i] = m.params[M::TRIG_GATEPROB_PARAM + i].getValue();
				p.stagePitchRnd[i] = m.params[M::TRIG_PITCHRND_PARAM + i].getValue();
				p.stageAccent[i] = m.params[M::TRIG_ACCENT_PARAM + i].getValue();
				p.stageAccentRnd[i] = m.params[M::TRIG_RNDACCENT_PARAM + i].getValue();
			}
		}
	}

	// The quantizer input of a stage pitch.
	float quantizerInput(M &m, const MetroPattern &p, float stagePitch) {
		float sens = clamp(p.sensitivity + (m.inputs[M::SENSITIVITY_INPUT].isConnected() ? m.inputs[M::SENSITIVITY_INPUT].getVoltage() * 0.1f : 0.f), 0.1f, 1.f);
		return clamp(stagePitch, -4.f, 6.f) * sens + m.inputs[M::TRANSPOSE_INPUT].getVoltage();
	}

	// A stage's pitch as it plays, for the displays (UI thread: no cache).
	float quantize(M &m, const MetroPattern &p, float stagePitch) {
		return std::get<0>(quant.closestVoltageInScale(quantizerInput(m, p, stagePitch), rootNote(m, p), scale(m, p)));
	}

	// The same for the audio thread: the scale search only runs when its input changes.
	float quantizeCached(M &m, const MetroPattern &p, float stagePitch) {
		float v = quantizerInput(m, p, stagePitch);
		int r = rootNote(m, p);
		int sc = scale(m, p);
		if (v != quantIn || r != quantRoot || sc != quantScale) {
			quantIn = v;
			quantRoot = r;
			quantScale = sc;
			quantOut = std::get<0>(quant.closestVoltageInScale(v, r, sc));
		}
		return quantOut;
	}

	int rootNote(M &m, const MetroPattern &p) {
		return clamp((int)(p.rootNote + clamp(m.inputs[M::ROOT_NOTE_INPUT].getVoltage(), 0.f, 10.f) * 1.1f), 0, 11);
	}

	int scale(M &m, const MetroPattern &p) {
		return clamp((int)(p.scale + m.inputs[M::SCALE_INPUT].getVoltage()), 0, quantizer::numScales - 1);
	}

	template <typename T>
	static void rotate(T *a, bool left) {
		T t[8];
		for (int i = 0; i < 8; i++)
			t[i] = a[left ? (i + 1) % 8 : (i + 7) % 8];
		for (int i = 0; i < 8; i++)
			a[i] = t[i];
	}

	void processControls(M &m) {
		if (runningTrigger.process(m.params[M::RUN_PARAM].getValue()))
			running = !running;

		// Editing: the panel shows the selected pattern.
		int sel = clamp((int)m.params[M::PATTERN_PARAM].getValue() - 1, 0, 15);
		if (sel != selectedPattern) {
			selectedPattern = sel;
			loadPanel(m);
		}
		applyUndos(m);
		savePanel(m);
		MetroPattern &ed = patterns[selectedPattern];
		for (int i = 0; i < 8; i++) {
			if (slideTriggers[i].process(m.params[M::TRIG_SLIDE_PARAM + i].getValue())) {
				const MetroPattern before = ed;
				ed.stageSlide[i] = !ed.stageSlide[i];
				pushEdit("stage slide", before);
			}
			if (skipTriggers[i].process(m.params[M::TRIG_SKIP_PARAM + i].getValue())) {
				const MetroPattern before = ed;
				ed.stageSkip[i] = !ed.stageSkip[i];
				pushEdit("stage skip", before);
			}
		}
		if (playModeTrigger.process(m.params[M::PLAY_MODE_PARAM].getValue())) {
			const MetroPattern before = ed;
			ed.playMode = (ed.playMode + 1) % 5;
			pushEdit("play mode", before);
		}
		if (countModeTrigger.process(m.params[M::COUNT_MODE_PARAM].getValue())) {
			const MetroPattern before = ed;
			ed.countMode = (ed.countMode + 1) % 2;
			pushEdit("count mode", before);
		}
		bool reload = false;
		if (copyTrigger.process(m.params[M::COPY_PARAM].getValue())) {
			if (copyPattern < 0)
				copyPattern = selectedPattern;
			else {
				if (copyPattern != selectedPattern) {
					const MetroPattern before = ed;
					ed.copySettings(patterns[copyPattern]);
					pushEdit("paste pattern", before);
					reload = true;
				}
				copyPattern = -1;
			}
		}
		bool up = upTrigger.process(m.params[M::UP_PARAM].getValue());
		bool down = downTrigger.process(m.params[M::DOWN_PARAM].getValue());
		if (up || down) {
			const MetroPattern before = ed;
			for (int i = 0; i < 8; i++)
				ed.stagePitch[i] = clamp(ed.stagePitch[i] + (up ? 1.f : -1.f) / 12.f, -4.f, 6.f);
			pushEdit("transpose pattern", before);
			reload = true;
		}
		bool left = leftTrigger.process(m.params[M::LEFT_PARAM].getValue());
		bool right = rightTrigger.process(m.params[M::RIGHT_PARAM].getValue());
		if (left || right) {
			const MetroPattern before = ed;
			rotate(ed.stagePitch, left);
			rotate(ed.stagePulses, left);
			rotate(ed.stageType, left);
			rotate(ed.stageSlide, left);
			rotate(ed.stageSkip, left);
			rotate(ed.stageGateProb, left);
			rotate(ed.stagePitchRnd, left);
			rotate(ed.stageAccent, left);
			rotate(ed.stageAccentRnd, left);
			pushEdit("rotate pattern", before);
			reload = true;
		}
		if (reload)
			loadPanel(m);
	}

	void buildPlayed(M &m) {
		MetroPattern &pl = patterns[playedPattern];
		pl.build(clamp((int)(pl.numberOfStepsParam + m.inputs[M::STEPS_INPUT].getVoltage()), 1, 16));
	}

	void processLights(M &m, float sampleTime) {
		const MetroPattern &ed = patterns[selectedPattern];
		const float lambda = 13.333333f * sampleTime * CONTROL_DIV;
		for (int i = 0; i < 8; i++) {
			float b = m.lights[M::STEPS_LIGHTS + i].getBrightness();
			b -= b * lambda;
			m.lights[M::STEPS_LIGHTS + i].setBrightness(b);
			m.lights[M::SLIDES_LIGHTS + i].setBrightness(ed.stageSlide[i] ? 1.f - b : b);
			m.lights[M::SKIPS_LIGHTS + i].setBrightness(ed.stageSkip[i] ? 1.f - b : b);
		}
		float r = m.lights[M::RESET_LIGHT].getBrightness();
		m.lights[M::RESET_LIGHT].setBrightness(r - r * lambda);
		m.lights[M::RUNNING_LIGHT].setBrightness(running ? 1.f : 0.f);
		m.lights[M::COPY_LIGHT].setBrightness(copyPattern >= 0 ? 1.f : 0.f);
		if constexpr (M::EXTENDED) {
			m.lights[M::PLAYMODE_LIGHT].setBrightness(ed.playMode > 2 ? 1.f : 0.f);
			m.lights[M::PLAYMODE_LIGHT + 1].setBrightness(ed.playMode > 0 && ed.playMode < 4 ? 1.f : 0.f);
			m.lights[M::PLAYMODE_LIGHT + 2].setBrightness(ed.playMode < 2 ? 1.f : 0.f);
			m.lights[M::COUNTMODE_LIGHT].setBrightness(0.f);
			m.lights[M::COUNTMODE_LIGHT + 1].setBrightness(ed.countMode == 1 ? 1.f : 0.f);
			m.lights[M::COUNTMODE_LIGHT + 2].setBrightness(ed.countMode == 0 ? 1.f : 0.f);
		}
	}

	void process(M &m, const Module::ProcessArgs &args) {
		bool control = --controlCounter < 0;
		if (control) {
			controlCounter = CONTROL_DIV - 1;
			processControls(m);
		}

		// Playing: the pattern CV picks any pattern, the position carries over.
		int newPattern = m.inputs[M::PATTERN_INPUT].isConnected() ? clamp((int)std::floor(m.inputs[M::PATTERN_INPUT].getVoltage() * 1.6f), 0, 15) : selectedPattern;
		bool patternChanged = newPattern != playedPattern;
		if (patternChanged) {
			patterns[newPattern].currentStep = patterns[playedPattern].currentStep;
			patterns[newPattern].currentPulse = patterns[playedPattern].currentPulse;
			playedPattern = newPattern;
		}
		MetroPattern &pl = patterns[playedPattern];
		// The steps are rebuilt at control rate, on a pattern change and before each step.
		if (control || patternChanged)
			buildPlayed(m);

		// Clock: internal at 1V/oct (0 V = 120 BPM), or the external clock with
		// the phase measured from its last interval.
		bool tick = false;
		double rv = (double)m.params[M::CLOCK_PARAM].getValue() + m.inputs[M::CLOCK_INPUT].getVoltage();
		if (rv != rateVolts) {
			rateVolts = rv;
			rate = 2.0 * std::exp2(rv);
			clockBase = phase;
			clockSamples = 0;
			clockPulses = 0;
		}
		bool ext = m.inputs[M::EXT_CLOCK_INPUT].isConnected();
		bool extEdge = clockTrigger.process(ext ? m.inputs[M::EXT_CLOCK_INPUT].getVoltage() : 0.f, 0.1f, 1.f);
		if (ext) {
			if (sinceClock >= 0)
				sinceClock++;
			if (extEdge) {
				if (sinceClock > 0)
					clockInterval = sinceClock;
				sinceClock = 0;
			}
		}
		else {
			sinceClock = -1;
			clockInterval = 0;
		}
		const long ms = (std::lround(args.sampleRate) + 500) / 1000;
		if (resetTrigger.process(m.params[M::RESET_PARAM].getValue() + m.inputs[M::RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			resetArmed = true;
			m.lights[M::RESET_LIGHT].setBrightness(1.f);
			// The internal clock restarts on the reset: its first clock is now. A
			// reset up to 1 ms after an external clock belongs to that clock.
			if (!ext) {
				phase = 0.0;
				clockBase = 0.0;
				clockSamples = 0;
				clockPulses = 0;
				tick = running;
			}
			else if (running && started && sincePulse < ms)
				tick = true;
		}
		if (running) {
			if (ext) {
				if (extEdge) {
					tick = true;
					phase = 0.0;
				}
				else
					phase = clockInterval > 0 ? (double)sinceClock / clockInterval : phase + rate / args.sampleRate;
			}
			else if (!tick) {
				clockSamples++;
				phase = clockBase + (double)clockSamples * rate / std::lround(args.sampleRate) - (double)clockPulses;
				if (phase >= 1.0) {
					const long long whole = (long long)std::floor(phase);
					clockPulses += whole;
					phase -= (double)whole;
					tick = true;
				}
			}
		}

		if (tick) {
			slideFrom = pitch;
			if (!control && !patternChanged)
				buildPlayed(m);
			pl.advance(resetArmed);
			resetArmed = false;
			started = true;
			pulse = pl.currentPulse;
			sincePulse = 0;
			const MetroStep &s = pl.steps[pl.currentStep];
			if (!s.skip && (stepOutputsMode || pulse == 0))
				stepPulse[s.index] = std::max(ms, 1L);
			m.lights[M::STEPS_LIGHTS + s.index].setBrightness(1.f);
			if constexpr (M::EXTENDED) {
				// Drawn once per step, kept through its pulses.
				if (pulse == 0) {
					stepOn = random::uniform() < s.gateProb;
					rndPitch = (random::uniform() * 2.f - 1.f) * 5.f * s.pitchRnd;
					accent = clamp(s.accent + (random::uniform() * 2.f - 1.f) * 5.f * s.accentRnd, 0.f, 10.f);
				}
			}
		}

		// Gate.
		const MetroStep &s = pl.steps[pl.currentStep];
		bool active = running && started && !s.skip;
		// The gate length in millionths of a pulse, so no float decides the sample it falls on.
		const long long gateMillionths = clamp((long long)std::llround((double)pl.gateTime * 1e6 + (double)m.inputs[M::GATE_TIME_INPUT].getVoltage() * 1e5) - 20000LL, 0LL, 990000LL);
		const bool phaseLow = (ext && clockInterval > 0) ? (long long)sinceClock * 1000000 < gateMillionths * clockInterval : phase * 1e6 < (double)gateMillionths;
		bool timed = phaseLow || sincePulse < ms;
		bool gate = false;
		float gateValue = 10.f;
		if (active && stepOn) {
			switch (s.type) {
				case MetroPattern::SINGLE: gate = pulse == 0 && timed; break;
				case MetroPattern::MULTIPLE: gate = timed; break;
				case MetroPattern::HOLD: gate = pulse < s.pulses - 1 || timed; break;
				case MetroPattern::EXT1: gate = true; gateValue = m.inputs[M::EXTGATE1_INPUT].getVoltage(); break;
				case MetroPattern::EXT2: gate = true; gateValue = m.inputs[M::EXTGATE2_INPUT].getVoltage(); break;
				default: break;
			}
			// A new gate always gets a low sample before it, so it is a new edge.
			if (tick && lastGate && (pulse == 0 || s.type == MetroPattern::MULTIPLE) && s.type >= MetroPattern::SINGLE && s.type <= MetroPattern::HOLD)
				gate = false;
		}
		lastGate = gate;
		sincePulse++;

		// Pitch: quantized while the stage plays, held through rests.
		if (active && s.type != MetroPattern::REST)
			target = quantizeCached(m, pl, s.pitch + rndPitch);
		if (s.slide && pulse == 0 && started) {
			float curve = clamp(pl.slideTime + m.inputs[M::SLIDE_TIME_INPUT].getVoltage() * 0.1f, 0.f, 0.99f);
			pitch = slideFrom + (target - slideFrom) * std::pow((float)std::min(phase, 1.0), curve);
		}
		else
			pitch = target;

		m.outputs[M::GATE_OUTPUT].setVoltage(gate ? gateValue : 0.f);
		m.outputs[M::PITCH_OUTPUT].setVoltage(pitch);
		if constexpr (M::EXTENDED)
			m.outputs[M::ACC_OUTPUT].setVoltage(accent);

		// Stage outputs per sample, lights at control rate.
		for (int i = 0; i < 8; i++)
		{
			m.outputs[M::STEP_OUTPUT + i].setVoltage(stepPulse[i] > 0 ? 10.f : 0.f);
			if (stepPulse[i] > 0)
				stepPulse[i]--;
		}
		if (control)
			processLights(m, args.sampleTime);
	}

	json_t *toJson(json_t *rootJ) {
		json_object_set_new(rootJ, "running", json_boolean(running));
		json_object_set_new(rootJ, "stepOutputsMode", json_boolean(stepOutputsMode));
		json_object_set_new(rootJ, "selectedPattern", json_integer(selectedPattern));
		json_object_set_new(rootJ, "playedPattern", json_integer(playedPattern));
		json_t *patternsJ = json_array();
		for (int i = 0; i < 16; i++)
			json_array_append_new(patternsJ, patterns[i].toJson(M::EXTENDED));
		json_object_set_new(rootJ, "patterns", patternsJ);
		return rootJ;
	}

	void fromJson(json_t *rootJ) {
		if (json_t *j = json_object_get(rootJ, "running")) running = json_is_true(j);
		if (json_t *j = json_object_get(rootJ, "stepOutputsMode")) stepOutputsMode = json_is_true(j);
		if (json_t *j = json_object_get(rootJ, "selectedPattern")) selectedPattern = clamp((int)json_integer_value(j), 0, 15);
		if (json_t *j = json_object_get(rootJ, "playedPattern")) playedPattern = clamp((int)json_integer_value(j), 0, 15);
		json_t *patternsJ = json_object_get(rootJ, "patterns");
		for (int i = 0; i < 16; i++)
			patterns[i].fromJson(patternsJ ? json_array_get(patternsJ, i) : NULL);
		resetArmed = true;
		started = false;
	}
};

// The module part DTROY and BORDL share. M is the module itself.
template <class M>
struct MetroModule : BidooModule {
	MetroEngine<M> engine;

	void process(const ProcessArgs &args) override {
		engine.process(*static_cast<M*>(this), args);
	}

	json_t *dataToJson() override {
		return engine.toJson(BidooModule::dataToJson());
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		engine.fromJson(rootJ);
	}

	void onReset() override {
		engine.reset();
	}

	void randomizeSlidesSkips() {
		MetroPattern &p = engine.patterns[engine.selectedPattern];
		for (int i = 0; i < 8; i++) {
			p.stageSlide[i] = random::uniform() > 0.8f;
			p.stageSkip[i] = random::uniform() > 0.85f;
		}
	}

	// A change of the selected pattern from the menu: the audio thread applies
	// it, and Rack's undo gets it.
	template <typename F>
	void menuEdit(const char *name, F change) {
		if (engine.editsToApply.full())
			return;
		MetroEdit e;
		e.pattern = engine.selectedPattern;
		e.name = name;
		e.before = engine.patterns[e.pattern];
		e.after = e.before;
		change(e.after);
		engine.editsToApply.push(e);
		pushHistory(e);
	}

	void randomizeSlidesSkipsEdit() {
		menuEdit("randomize slides & skips", [](MetroPattern &p) {
			for (int i = 0; i < 8; i++) {
				p.stageSlide[i] = random::uniform() > 0.8f;
				p.stageSkip[i] = random::uniform() > 0.85f;
			}
		});
	}

	void randomizePitch() {
		menuEdit("randomize pitches", [](MetroPattern &p) {
			for (int i = 0; i < 8; i++)
				p.stagePitch[i] = random::uniform() * 10.f - 4.f;
		});
	}

	void randomizeGates() {
		menuEdit("randomize gates", [](MetroPattern &p) {
			for (int i = 0; i < 8; i++) {
				p.stagePulses[i] = (int)(random::uniform() * 7.f + 1.f);
				p.stageType[i] = (int)(random::uniform() * 6.f);
			}
		});
	}

	struct EditAction : history::ModuleAction {
		MetroEdit edit;

		void send(const bool undo) {
			M *m = dynamic_cast<M*>(APP->engine->getModule(moduleId));
			if (!m || m->engine.editsToApply.full())
				return;
			MetroEdit e = edit;
			if (undo)
				std::swap(e.before, e.after);
			m->engine.editsToApply.push(e);
		}
		void undo() override {send(true);}
		void redo() override {send(false);}
	};

	// Rack's undo of a knob only moves it back, which would write the old value to
	// whatever pattern is selected by then: replaced by this one, tied to the
	// pattern the knob was on.
	struct KnobAction : history::ModuleAction {
		int pattern, paramId;
		float oldValue, newValue;

		void send(const float v) {
			M *m = dynamic_cast<M*>(APP->engine->getModule(moduleId));
			if (!m || m->engine.knobsToApply.full())
				return;
			m->engine.knobsToApply.push({pattern, paramId, v});
		}
		void undo() override {send(oldValue);}
		void redo() override {send(newValue);}
	};

	void pushHistory(const MetroEdit &e) {
		if (!APP->history)
			return;
		EditAction *a = new EditAction;
		a->name = e.name;
		a->moduleId = id;
		a->edit = e;
		APP->history->push(a);
	}

	// UI thread, once a frame.
	void updateHistory() {
		while (!engine.editsToHistory.empty())
			pushHistory(engine.editsToHistory.shift());
		history::State *h = APP->history;
		if (!h || h->actionIndex <= 0 || h->actionIndex > (int)h->actions.size())
			return;
		history::ParamChange *pc = dynamic_cast<history::ParamChange*>(h->actions[h->actionIndex - 1]);
		if (!pc || pc->moduleId != id || !MetroEngine<M>::isPatternKnob(pc->paramId))
			return;
		KnobAction *a = new KnobAction;
		a->name = pc->name;
		a->moduleId = id;
		a->pattern = engine.selectedPattern;
		a->paramId = pc->paramId;
		a->oldValue = pc->oldValue;
		a->newValue = pc->newValue;
		h->actions[h->actionIndex - 1] = a;
		delete pc;
	}
};

// The widget part both share: the history runs in the UI thread.
template <class M>
struct MetroWidget : BidooWidget {
	void step() override {
		BidooWidget::step();
		if (module)
			static_cast<M*>(module)->updateHistory();
	}
};

// The small display: play mode, patterns, steps, root and scale.
template <class M>
struct MetroDisplay : TransparentWidget {
	M *module;

	// No strings built per frame: labels are static, numbers go through snprintf.
	void drawMessage(NVGcontext *vg, Vec pos, const char *note, const char *playMode, const char *selectedPattern, const char *playedPattern, const char *steps, const char *scale) {
		nvgFontSize(vg, 18.0f);
		nvgFillColor(vg, YELLOW_BIDOO);
		nvgText(vg, pos.x + 3.0f, pos.y + 8.0f, playMode, NULL);
		nvgFontSize(vg, 14.0f);
		nvgText(vg, pos.x + 114.0f, pos.y + 7.0f, selectedPattern, NULL);

		nvgText(vg, pos.x + 30.0f, pos.y + 7.0f, steps, NULL);
		nvgText(vg, pos.x + 3.0f, pos.y + 21.0f, note, NULL);
		nvgText(vg, pos.x + 25.0f, pos.y + 21.0f, scale, NULL);

		// The played pattern blinks: 0.5 s shown, 0.25 s hidden, whatever the frame rate.
		if (std::fmod(system::getTime(), 0.75) < 0.5)
			nvgText(vg, pos.x + 90.0f, pos.y + 7.0f, playedPattern, NULL);
	}

	static const char *displayRootNote(int value) {
		return quantizer::rootNotes[value+1].label.c_str();
	}

	static const char *displayScale(int value) {
		return quantizer::scales[value].label.c_str();
	}

	static const char *displayPlayMode(int value) {
		switch(value){
			case 0: return "►";
			case 1: return "◄";
			case 2: return "►◄";
			case 3: return "►*";
			case 4: return "►?";
			default: return "";
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module) {
				const MetroPattern &p = module->engine.patterns[module->engine.selectedPattern];
				char steps[16], selectedPattern[8], playedPattern[8];
				snprintf(steps, sizeof(steps), "%s%d", p.countMode == 0 ? "steps:" : "pulses:", p.numberOfStepsParam);
				snprintf(selectedPattern, sizeof(selectedPattern), "P%d", module->engine.selectedPattern + 1);
				snprintf(playedPattern, sizeof(playedPattern), "P%d", module->engine.playedPattern + 1);
				// BORDL shows the root with its CV, DTROY the knob's.
				int root;
				if constexpr (M::EXTENDED)
					root = module->engine.rootNote(*module, p);
				else
					root = p.rootNote;
				drawMessage(args.vg, Vec(0.0f, 20.0f), displayRootNote(root), displayPlayMode(p.playMode), selectedPattern, playedPattern, steps, displayScale(p.scale));
			}
		}
		Widget::drawLayer(args, layer);
	}
};

// The context menu lines both modules add.
template <class M>
void appendMetroMenu(ui::Menu *menu, M *m) {
	menu->addChild(new MenuSeparator());
	menu->addChild(createMenuItem("Randomize pitches", "", [=]() { m->randomizePitch(); }));
	menu->addChild(createMenuItem("Randomize gates", "", [=]() { m->randomizeGates(); }));
	menu->addChild(createMenuItem("Randomize slides & skips", "", [=]() { m->randomizeSlidesSkipsEdit(); }));
	menu->addChild(createBoolPtrMenuItem("Stage outputs on every pulse", "", &m->engine.stepOutputsMode));
}

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

// The controls DTROY and BORDL share. The play mode, count mode, slide and
// skip controls differ and are configured by each module.
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
	m.configInput(M::PATTERN_INPUT, "Played pattern (0-10 V = 1-16)");
	m.configInput(M::TRANSPOSE_INPUT, "Transpose (1V/oct, before the quantizer)");
	m.configInput(M::SENSITIVITY_INPUT, "Sensitivity (10 V = 100%)");

	m.configOutput(M::GATE_OUTPUT, "Gate");
	m.configOutput(M::PITCH_OUTPUT, "Pitch (1V/oct)");

	for (int i = 0; i < 8; i++) {
		m.configParam(M::TRIG_PITCH_PARAM + i, -4.f, 6.f, 0.f, string::f("Stage %d pitch", i + 1), " V");
		m.configParam(M::TRIG_COUNT_PARAM + i, 1.f, 8.f, 1.f, string::f("Stage %d pulses", i + 1));
		m.paramQuantities[M::TRIG_COUNT_PARAM + i]->snapEnabled = true;
		m.configSwitch(M::TRIG_TYPE_PARAM + i, 0.f, 5.f, 2.f, string::f("Stage %d gate", i + 1), {"Rest", "Single", "Multiple", "Hold", "External gate 1", "External gate 2"});
		m.configOutput(M::STEP_OUTPUT + i, string::f("Stage %d", i + 1));
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
	dsp::PulseGenerator stepPulse[8];

	bool running = true;
	double phase = 0.0;          // position in the current pulse, 0..1
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

	MetroPattern patterns[16];
	quantizer::Quantizer quant;

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

	// A stage's pitch as it plays, for the displays.
	float quantize(M &m, const MetroPattern &p, float stagePitch) {
		float sens = clamp(p.sensitivity + (m.inputs[M::SENSITIVITY_INPUT].isConnected() ? m.inputs[M::SENSITIVITY_INPUT].getVoltage() * 0.1f : 0.f), 0.1f, 1.f);
		float v = clamp(stagePitch, -4.f, 6.f) * sens + m.inputs[M::TRANSPOSE_INPUT].getVoltage();
		return std::get<0>(quant.closestVoltageInScale(v, rootNote(m, p), scale(m, p)));
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

	void process(M &m, const Module::ProcessArgs &args) {
		if (runningTrigger.process(m.params[M::RUN_PARAM].getValue()))
			running = !running;

		// Editing: the panel shows the selected pattern.
		int sel = clamp((int)m.params[M::PATTERN_PARAM].getValue() - 1, 0, 15);
		if (sel != selectedPattern) {
			selectedPattern = sel;
			loadPanel(m);
		}
		savePanel(m);
		MetroPattern &ed = patterns[selectedPattern];
		for (int i = 0; i < 8; i++) {
			if (slideTriggers[i].process(m.params[M::TRIG_SLIDE_PARAM + i].getValue()))
				ed.stageSlide[i] = !ed.stageSlide[i];
			if (skipTriggers[i].process(m.params[M::TRIG_SKIP_PARAM + i].getValue()))
				ed.stageSkip[i] = !ed.stageSkip[i];
		}
		if (playModeTrigger.process(m.params[M::PLAY_MODE_PARAM].getValue()))
			ed.playMode = (ed.playMode + 1) % 5;
		if (countModeTrigger.process(m.params[M::COUNT_MODE_PARAM].getValue()))
			ed.countMode = (ed.countMode + 1) % 2;
		bool reload = false;
		if (copyTrigger.process(m.params[M::COPY_PARAM].getValue())) {
			if (copyPattern < 0)
				copyPattern = selectedPattern;
			else {
				if (copyPattern != selectedPattern) {
					ed.copySettings(patterns[copyPattern]);
					reload = true;
				}
				copyPattern = -1;
			}
		}
		bool up = upTrigger.process(m.params[M::UP_PARAM].getValue());
		bool down = downTrigger.process(m.params[M::DOWN_PARAM].getValue());
		if (up || down) {
			for (int i = 0; i < 8; i++)
				ed.stagePitch[i] = clamp(ed.stagePitch[i] + (up ? 1.f : -1.f) / 12.f, -4.f, 6.f);
			reload = true;
		}
		bool left = leftTrigger.process(m.params[M::LEFT_PARAM].getValue());
		bool right = rightTrigger.process(m.params[M::RIGHT_PARAM].getValue());
		if (left || right) {
			rotate(ed.stagePitch, left);
			rotate(ed.stagePulses, left);
			rotate(ed.stageType, left);
			rotate(ed.stageSlide, left);
			rotate(ed.stageSkip, left);
			rotate(ed.stageGateProb, left);
			rotate(ed.stagePitchRnd, left);
			rotate(ed.stageAccent, left);
			rotate(ed.stageAccentRnd, left);
			reload = true;
		}
		if (reload)
			loadPanel(m);

		// Playing: the pattern CV picks any pattern, the position carries over.
		int newPattern = m.inputs[M::PATTERN_INPUT].isConnected() ? clamp((int)rescale(m.inputs[M::PATTERN_INPUT].getVoltage(), 0.f, 10.f, 1.f, 16.1f) - 1, 0, 15) : selectedPattern;
		if (newPattern != playedPattern) {
			patterns[newPattern].currentStep = patterns[playedPattern].currentStep;
			patterns[newPattern].currentPulse = patterns[playedPattern].currentPulse;
			playedPattern = newPattern;
		}
		MetroPattern &pl = patterns[playedPattern];
		pl.build(clamp((int)(pl.numberOfStepsParam + m.inputs[M::STEPS_INPUT].getVoltage()), 1, 16));

		// Clock: internal at 1V/oct (0 V = 120 BPM), or the external clock with
		// the phase measured from its last interval.
		bool tick = false;
		double rate = 2.0 * std::exp2((double)m.params[M::CLOCK_PARAM].getValue() + m.inputs[M::CLOCK_INPUT].getVoltage());
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
		const long ms = (long)std::round(1e-3f * args.sampleRate);
		if (resetTrigger.process(m.params[M::RESET_PARAM].getValue() + m.inputs[M::RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			resetArmed = true;
			m.lights[M::RESET_LIGHT].setBrightness(1.f);
			// The internal clock restarts on the reset: its first clock is now. A
			// reset up to 1 ms after an external clock belongs to that clock.
			if (!ext) {
				phase = 0.0;
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
					phase = clockInterval > 0 ? (double)sinceClock / clockInterval : phase + rate * args.sampleTime;
			}
			else if (!tick) {
				phase += rate * args.sampleTime;
				if (phase >= 1.0) {
					phase -= std::floor(phase);
					tick = true;
				}
			}
		}

		if (tick) {
			slideFrom = pitch;
			pl.advance(resetArmed);
			resetArmed = false;
			started = true;
			pulse = pl.currentPulse;
			sincePulse = 0;
			const MetroStep &s = pl.steps[pl.currentStep];
			if (!s.skip && (stepOutputsMode || pulse == 0))
				stepPulse[s.index].trigger(1e-3f);
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
		float gateCoeff = clamp(pl.gateTime - 0.02f + m.inputs[M::GATE_TIME_INPUT].getVoltage() * 0.1f, 0.f, 0.99f);
		bool timed = phase < gateCoeff || sincePulse < ms;
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
			target = quantize(m, pl, s.pitch + rndPitch);
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

		// Lights & stage outputs.
		const float lambda = 13.333333f * args.sampleTime;
		for (int i = 0; i < 8; i++) {
			float b = m.lights[M::STEPS_LIGHTS + i].getBrightness();
			b -= b * lambda;
			m.lights[M::STEPS_LIGHTS + i].setBrightness(b);
			m.lights[M::SLIDES_LIGHTS + i].setBrightness(ed.stageSlide[i] ? 1.f - b : b);
			m.lights[M::SKIPS_LIGHTS + i].setBrightness(ed.stageSkip[i] ? 1.f - b : b);
			m.outputs[M::STEP_OUTPUT + i].setVoltage(stepPulse[i].process(args.sampleTime) ? 10.f : 0.f);
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

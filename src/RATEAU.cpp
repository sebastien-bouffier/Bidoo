#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <cstdio>

using namespace std;

// Trigger generator after the Tiptop Audio Trigger Riot user guide.
//
// One counter counts clocks (16th notes) from the cycle start; the engine runs
// on 96 PPQ internal ticks (24 per clock), so a position A in ticks is count
// A / 24. Each of the 16 knobs drives a division: it fires when its own clock
// count (in Speed ticks) minus its shift reaches a multiple of Divide, plus an
// optional single Step. Matrix mode sums each knob into its row output (1-4)
// and its column output (A-D); independent mode keeps separate settings for
// rows and columns.

enum pageType { pDiv, pStep, pProb, pSpeed, pClockShift, pTimeShift, pPulseWidth };
enum playMode { indepEditA, indepEditB, matrix };

static const int RIOT_BANKS = 16;
static const int RIOT_SETS = 3;
static const int RIOT_CELLS = 16;
static const int TICKS_PER_CLOCK = 24;

struct RiotCell {
	int div = 0;            // 0 off, 1..255 clocks
	int step = 0;           // 0 off, 1..255: one trigger on that count
	float prob = 1.f;       // 0..1
	int speed = 24;         // 2..96 internal ticks per clock of this division
	int clockShift = 0;     // 0..div-1 whole clocks
	float timeShift = 0.f;  // 0..360 degrees of what remains of the division
	float pulseWidth = 0.25f; // 0..1 of the division, 0 = one internal tick

	// Delay of the division after its clock count, in its clocks.
	double offset() const {
		int cs = div > 0 ? std::min(clockShift, div - 1) : 0;
		return cs + timeShift / 360.0 * (div - cs);
	}
};

// Per cell lower bound of the next tick that can fire, so the exact test in
// fire() only runs near an event. It is keyed on the cell settings it was
// computed from and on the cycle epoch (the counter only moves backwards in
// beginCycle), so any edit or jump falls back to the exact test.
struct RiotNext {
	double tick = -1.0;
	unsigned epoch = 0;
	int div = -1, step = -1, speed = -1, clockShift = -1;
	float timeShift = -1.f;
	bool matches(const RiotCell &c, unsigned e) const {
		return epoch == e && div == c.div && step == c.step && speed == c.speed && clockShift == c.clockShift && timeShift == c.timeShift;
	}
};

struct RiotBank {
	RiotCell cells[RIOT_SETS][RIOT_CELLS];
	int cycleStart = 1;  // count the cycle resets to, 1..255
	int cycleEnd = 0;    // 0 = endless, else last count of the cycle
	float bpm = 120.f;   // internal clock
};

struct RATEAU : BidooModule {
	enum ParamIds {
		DIV_PARAM,
		STEP_PARAM,
		PROB_PARAM,
		SPEED_PARAM,
		CLOCKSHIFT_PARAM,
		TIMESHIFT_PARAM,
		PULSEWIDTH_PARAM,
		BANK_PARAM,
		INDEPEDITA_PARAM,
		INDEPEDITB_PARAM,
		MATRIX_PARAM,
		STARTSTOP_PARAM,
		MUTE_PARAMS,
		CONTROLS_PARAMS = MUTE_PARAMS + 8,
		NUM_PARAMS = CONTROLS_PARAMS + 16
	};
	enum InputIds {
		CLOCK_INPUT,
		RESET_INPUT,
		BANK_INPUT,
		MODE_INPUT,
		STARTSTOP_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		GATE_OUTPUTS,
		NUM_OUTPUTS = GATE_OUTPUTS + 8
	};
	enum LightIds {
		INDEPEDITA_LIGHT,
		INDEPEDITB_LIGHT,
		MATRIX_LIGHT,
		MUTE_LIGHTS,
		STARTSTOP_LIGHT = MUTE_LIGHTS + 8*3,
		NUM_LIGHTS
	};

	RiotBank banks[RIOT_BANKS];

	int page = pDiv;
	int bank = 0;
	int pendingBank = -1;
	int fCntrl = 0;
	int pMode = matrix;
	int previousMode = matrix;
	bool mutes[8] = {false};

	// Global settings (context menu)
	bool running = true;        // internal clock only
	bool clock24ppq = false;    // external clock: 16th notes or 24 PPQ
	bool beatSync = false;      // bank changes wait for the cycle start

	// Counter position in internal ticks; count = position / 24.
	double position = TICKS_PER_CLOCK;
	double lastEdgePosition = TICKS_PER_CLOCK;
	bool restart = true;
	float sinceEdge = 10.f;     // no previous edge yet
	float edgePeriod = 0.f;     // seconds between external clocks, 0 = unknown

	dsp::SchmittTrigger pageTriggers[7];
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger indepEditATrigger;
	dsp::SchmittTrigger indepEditBTrigger;
	dsp::SchmittTrigger matrixTrigger;
	dsp::SchmittTrigger startStopTrigger;
	dsp::SchmittTrigger modeInputTrigger;
	dsp::SchmittTrigger muteTriggers[8];
	dsp::PulseGenerator gatePulses[RIOT_CELLS][RIOT_SETS];
	bool gates[RIOT_CELLS][RIOT_SETS] = {{false}};
	RiotNext nextHits[RIOT_SETS][RIOT_CELLS];
	unsigned cycleEpoch = 1;
	dsp::ClockDivider controlDivider;

	RATEAU() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(BANK_PARAM, 0.0f, 15.0f, 0.0f, "Pattern", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		const char *pages[7] = {"Divide", "Steps", "Probability", "Speed", "Clock shift", "Time shift", "Pulse width"};
		for (int i = 0; i < 7; i++) {
			configSwitch(DIV_PARAM + i, 0.0f, 1.0f, i == 0 ? 1.0f : 0.0f, std::string(pages[i]) + " page", {"Off", "On"});
			getParamQuantity(DIV_PARAM + i)->randomizeEnabled = false;
		}
		configButton(INDEPEDITA_PARAM, "Independent mode A (rows: outputs A1-A4, right)");
		configButton(INDEPEDITB_PARAM, "Independent mode B (columns: outputs B1-B4, bottom)");
		configButton(MATRIX_PARAM, "Matrix mode");
		configButton(STARTSTOP_PARAM, "Start / stop the internal clock");
		configInput(STARTSTOP_INPUT, "Start / stop trigger (internal clock)");
		const char *outs[8] = {"A1", "A2", "A3", "A4", "B1", "B2", "B3", "B4"};
		for (int i = 0; i < 8; i++) {
			configButton(MUTE_PARAMS + i, std::string("Mute output ") + outs[i]);
			configOutput(GATE_OUTPUTS + i, std::string("Output ") + outs[i]);
		}
		for (int i = 0; i < 16; i++) {
			configParam(CONTROLS_PARAMS + i, 0.0f, 10.0f, 0.0f, "Knob " + std::to_string(i + 1));
		}
		configInput(CLOCK_INPUT, "Clock (16th notes or 24 PPQ, see menu; internal clock when unpatched)");
		configInput(RESET_INPUT, "Reset to the cycle start");
		configInput(BANK_INPUT, "Pattern (0.625 V per pattern)");
		configInput(MODE_INPUT, "Matrix / independent toggle");
		controlDivider.setDivision(16);
		updateControls();
	}

	RiotCell &cell(int i, int set) {
		return banks[bank].cells[set][i];
	}

	void clear(int b, int set) {
		for (int i = 0; i < RIOT_CELLS; i++)
			banks[b].cells[set][i] = RiotCell();
		banks[b].cycleStart = 1;
		banks[b].cycleEnd = 0;
	}

	void onReset() override {
		for (int b = 0; b < RIOT_BANKS; b++)
			banks[b] = RiotBank();
		bank = 0;
		pendingBank = -1;
		page = pDiv;
		pMode = previousMode = matrix;
		for (int i = 0; i < 8; i++)
			mutes[i] = false;
		running = true;
		clock24ppq = false;
		beatSync = false;
		restart = true;
		updateControls();
	}

	// Knob position (0..10) <-> value of the current page.
	static float toKnob(int page, const RiotCell &c) {
		switch (page) {
			case pDiv: return c.div * 10.f / 255.f;
			case pStep: return c.step * 10.f / 255.f;
			case pProb: return c.prob * 10.f;
			case pSpeed: return (c.speed - 2) * 10.f / 94.f;
			case pClockShift: return c.clockShift * 10.f / 255.f;
			case pTimeShift: return c.timeShift * 10.f / 360.f;
			default: return c.pulseWidth * 10.f;
		}
	}

	static void fromKnob(int page, RiotCell &c, float k) {
		float x = clamp(k / 10.f, 0.f, 1.f);
		switch (page) {
			case pDiv: c.div = (int)std::round(x * 255.f); break;
			case pStep: c.step = (int)std::round(x * 255.f); break;
			case pProb: c.prob = x; break;
			case pSpeed: c.speed = 2 + (int)std::round(x * 94.f); break;
			case pClockShift: c.clockShift = (int)std::round(x * 255.f); break;
			case pTimeShift: c.timeShift = x * 360.f; break;
			default: c.pulseWidth = x; break;
		}
	}

	void updateControls() {
		for (int i = 0; i < RIOT_CELLS; i++)
			params[CONTROLS_PARAMS + i].setValue(toKnob(page, cell(i, pMode)));
	}

	void setPage(int p) {
		page = p;
		for (int i = 0; i < 7; i++)
			params[DIV_PARAM + i].setValue(i == p ? 1.f : 0.f);
		updateControls();
	}

	void setBank(int b) {
		bank = b;
		pendingBank = -1;
		restart = true;  // a pattern change resets the cycle
		updateControls();
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_t *banksJ = json_array();
		for (int b = 0; b < RIOT_BANKS; b++) {
			json_t *bankJ = json_object();
			json_object_set_new(bankJ, "cycleStart", json_integer(banks[b].cycleStart));
			json_object_set_new(bankJ, "cycleEnd", json_integer(banks[b].cycleEnd));
			json_object_set_new(bankJ, "bpm", json_real(banks[b].bpm));
			json_t *setsJ = json_array();
			for (int s = 0; s < RIOT_SETS; s++) {
				json_t *cellsJ = json_array();
				for (int i = 0; i < RIOT_CELLS; i++) {
					const RiotCell &c = banks[b].cells[s][i];
					json_array_append_new(cellsJ, json_pack("[i, i, f, i, i, f, f]", c.div, c.step, (double)c.prob, c.speed, c.clockShift, (double)c.timeShift, (double)c.pulseWidth));
				}
				json_array_append_new(setsJ, cellsJ);
			}
			json_object_set_new(bankJ, "cells", setsJ);
			json_array_append_new(banksJ, bankJ);
		}
		json_object_set_new(rootJ, "riotBanks", banksJ);
		json_object_set_new(rootJ, "bank", json_integer(bank));
		json_object_set_new(rootJ, "mode", json_integer(pMode));
		json_object_set_new(rootJ, "previousMode", json_integer(previousMode));
		json_object_set_new(rootJ, "page", json_integer(page));
		json_t *mutesJ = json_array();
		for (int i = 0; i < 8; i++)
			json_array_append_new(mutesJ, json_boolean(mutes[i]));
		json_object_set_new(rootJ, "mutes", mutesJ);
		json_object_set_new(rootJ, "running", json_boolean(running));
		json_object_set_new(rootJ, "clock24ppq", json_boolean(clock24ppq));
		json_object_set_new(rootJ, "beatSync", json_boolean(beatSync));
		return rootJ;
	}

	static int jsonInt(json_t *j, int def, int lo, int hi) {
		return j ? clamp((int)json_number_value(j), lo, hi) : def;
	}

	static float jsonFloat(json_t *j, float def, float lo, float hi) {
		return j ? clamp((float)json_number_value(j), lo, hi) : def;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *banksJ = json_object_get(rootJ, "riotBanks");
		for (int b = 0; b < RIOT_BANKS; b++) {
			banks[b] = RiotBank();
			json_t *bankJ = json_array_get(banksJ, b);
			if (!bankJ)
				continue;
			banks[b].cycleStart = jsonInt(json_object_get(bankJ, "cycleStart"), 1, 1, 255);
			banks[b].cycleEnd = jsonInt(json_object_get(bankJ, "cycleEnd"), 0, 0, 255);
			banks[b].bpm = jsonFloat(json_object_get(bankJ, "bpm"), 120.f, 20.f, 300.f);
			json_t *setsJ = json_object_get(bankJ, "cells");
			for (int s = 0; s < RIOT_SETS; s++) {
				json_t *cellsJ = json_array_get(setsJ, s);
				for (int i = 0; i < RIOT_CELLS; i++) {
					json_t *c = json_array_get(cellsJ, i);
					if (!c)
						continue;
					RiotCell &cl = banks[b].cells[s][i];
					cl.div = jsonInt(json_array_get(c, 0), 0, 0, 255);
					cl.step = jsonInt(json_array_get(c, 1), 0, 0, 255);
					cl.prob = jsonFloat(json_array_get(c, 2), 1.f, 0.f, 1.f);
					cl.speed = jsonInt(json_array_get(c, 3), 24, 2, 96);
					cl.clockShift = jsonInt(json_array_get(c, 4), 0, 0, 255);
					cl.timeShift = jsonFloat(json_array_get(c, 5), 0.f, 0.f, 360.f);
					cl.pulseWidth = jsonFloat(json_array_get(c, 6), 0.25f, 0.f, 1.f);
				}
			}
		}
		bank = jsonInt(json_object_get(rootJ, "bank"), 0, 0, RIOT_BANKS - 1);
		pMode = jsonInt(json_object_get(rootJ, "mode"), matrix, 0, 2);
		previousMode = jsonInt(json_object_get(rootJ, "previousMode"), matrix, 0, 2);
		page = jsonInt(json_object_get(rootJ, "page"), pDiv, 0, 6);
		json_t *mutesJ = json_object_get(rootJ, "mutes");
		for (int i = 0; i < 8; i++)
			mutes[i] = json_is_true(json_array_get(mutesJ, i));
		json_t *runningJ = json_object_get(rootJ, "running");
		running = !runningJ || json_is_true(runningJ);
		clock24ppq = json_is_true(json_object_get(rootJ, "clock24ppq"));
		beatSync = json_is_true(json_object_get(rootJ, "beatSync"));
		pendingBank = -1;
		restart = true;
		setPage(page);
	}

	void process(const ProcessArgs &args) override;
	void advance(double from, double to, float tickSeconds);
	void fire(double from, double to, float tickSeconds);
	void restartCycle(float tickSeconds);
	void beginCycle(double startPosition);
};

// Fires the divisions and steps whose time falls in (from, to], in ticks.
// Only the sets that are heard are fired: the matrix set, or the row and column
// sets in independent mode.
void RATEAU::fire(double from, double to, float tickSeconds) {
	int setA = pMode == matrix ? (int)matrix : (int)indepEditA;
	int setB = pMode == matrix ? (int)matrix : (int)indepEditB;
	for (int s = setA; s <= setB; s++) {
		for (int i = 0; i < RIOT_CELLS; i++) {
			const RiotCell &c = cell(i, s);
			RiotNext &nx = nextHits[s][i];
			// Nothing can fire before the bound (the margin is far above rounding).
			if (nx.matches(c, cycleEpoch) && to < nx.tick - 1e-3)
				continue;
			bool hit = false;
			double base = c.div;
			if (c.div > 0) {
				double off = c.offset();
				double x0 = from / c.speed - off;
				double x1 = to / c.speed - off;
				// Events at counts k * div + offset, k >= 1.
				if (x1 >= c.div) {
					double k0 = std::max(std::floor(x0 / c.div), 0.0);
					double k1 = std::floor(x1 / c.div);
					hit = k1 > k0;
				}
				base = c.div + off;
			}
			if (!hit && c.step > 0) {
				double t = (double)c.step * c.speed;
				hit = from < t && t <= to;
				if (hit && c.div == 0)
					base = 1.0;
			}
			// Next possible event after 'to': the next division count, or the step.
			{
				double next = INFINITY;
				if (c.div > 0) {
					double off = c.offset();
					double x1 = to / c.speed - off;
					double k = std::max(std::floor(x1 / c.div), 0.0) + 1.0;
					next = (k * c.div + off) * c.speed;
				}
				if (c.step > 0) {
					double t = (double)c.step * c.speed;
					if (t > to)
						next = std::min(next, t);
				}
				nx.tick = next;
				nx.epoch = cycleEpoch;
				nx.div = c.div;
				nx.step = c.step;
				nx.speed = c.speed;
				nx.clockShift = c.clockShift;
				nx.timeShift = c.timeShift;
			}
			if (!hit || random::uniform() >= c.prob)
				continue;
			// Pulse width is a share of the division plus its shift; at 0 it is one tick.
			double ticks = c.pulseWidth > 0.f ? c.pulseWidth * base * c.speed : 1.0;
			if (c.div > 0)
				ticks = std::min(ticks, (double)c.div * c.speed);
			// End one internal tick early, so a trigger on the next clock is a new edge.
			if (ticks > 1.5)
				ticks -= 1.0;
			gatePulses[i][s].reset();
			gatePulses[i][s].trigger(std::max((float)(ticks * tickSeconds), 1e-4f));
		}
	}
}

// Puts the counter at startPosition (in ticks) and applies a waiting pattern change.
void RATEAU::beginCycle(double startPosition) {
	if (pendingBank >= 0) {
		bank = pendingBank;
		pendingBank = -1;
		updateControls();
	}
	position = startPosition;
	lastEdgePosition = position;
	cycleEpoch++;  // the counter may move backwards: drop the next-event bounds
	for (int s = 0; s < RIOT_SETS; s++)
		for (int i = 0; i < RIOT_CELLS; i++)
			gatePulses[i][s].reset();
}

void RATEAU::restartCycle(float tickSeconds) {
	beginCycle(0.0);
	position = lastEdgePosition = (double)banks[bank].cycleStart * TICKS_PER_CLOCK;
	// Everything due on the start count fires now.
	fire(position - 1e-6, position, tickSeconds);
}

// Moves the counter, wrapping at the cycle end.
void RATEAU::advance(double from, double to, float tickSeconds) {
	const RiotBank &b = banks[bank];
	if (b.cycleEnd > 0) {
		double endTicks = (double)(b.cycleEnd + 1) * TICKS_PER_CLOCK;
		if (b.cycleEnd >= b.cycleStart && to >= endTicks) {
			fire(from, endTicks - 1e-6, tickSeconds);
			double over = to - endTicks;
			restartCycle(tickSeconds);
			double start = position;
			position = start + over;
			fire(start, position, tickSeconds);
			return;
		}
	}
	fire(from, to, tickSeconds);
	position = to;
}

void RATEAU::process(const ProcessArgs &args) {
	int nBank = clamp((int)std::round(params[BANK_PARAM].getValue() + inputs[BANK_INPUT].getVoltage() * 1.6f), 0, RIOT_BANKS - 1);
	if (nBank != bank && nBank != pendingBank) {
		if (beatSync)
			pendingBank = nBank;
		else
			setBank(nBank);
	}

	// Modes
	auto setMode = [&](int m) {
		if (pMode != m) {
			previousMode = pMode;
			// the sets heard change: drop the pulses of the sets left behind
			for (int s = 0; s < RIOT_SETS; s++)
				for (int i = 0; i < RIOT_CELLS; i++)
					gatePulses[i][s].reset();
		}
		pMode = m;
		updateControls();
	};
	// Trigger inputs stay per sample; buttons, knobs, pages, mutes and lights at control rate.
	if (modeInputTrigger.process(inputs[MODE_INPUT].getVoltage())) {
		if (pMode != matrix)
			setMode(matrix);
		else
			setMode(previousMode != matrix ? previousMode : indepEditA);
	}
	if (startStopTrigger.process(params[STARTSTOP_PARAM].getValue() + inputs[STARTSTOP_INPUT].getVoltage()))
		running = !running;

	if (controlDivider.process()) {
		if (indepEditATrigger.process(params[INDEPEDITA_PARAM].getValue())) setMode(indepEditA);
		if (indepEditBTrigger.process(params[INDEPEDITB_PARAM].getValue())) setMode(indepEditB);
		if (matrixTrigger.process(params[MATRIX_PARAM].getValue())) setMode(matrix);
		lights[STARTSTOP_LIGHT].setBrightness(running ? 1 : 0);
		lights[INDEPEDITA_LIGHT].setBrightness(pMode == indepEditA ? 1 : 0);
		lights[INDEPEDITB_LIGHT].setBrightness(pMode == indepEditB ? 1 : 0);
		lights[MATRIX_LIGHT].setBrightness(pMode == matrix ? 1 : 0);

		// Pages: the 7 buttons work as a radio group.
		for (int p = 0; p < 7; p++) {
			if (pageTriggers[p].process(params[DIV_PARAM + p].getValue()) && p != page)
				setPage(p);
		}
		if (params[DIV_PARAM + page].getValue() < 0.5f)
			params[DIV_PARAM + page].setValue(1.f);

		// Knobs edit the current page of the current set.
		for (int i = 0; i < RIOT_CELLS; i++)
			fromKnob(page, cell(i, pMode), params[CONTROLS_PARAMS + i].getValue());

		for (int i = 0; i < 8; i++) {
			if (muteTriggers[i].process(params[MUTE_PARAMS + i].getValue()))
				mutes[i] = !mutes[i];
			lights[MUTE_LIGHTS + i*3].setBrightness(mutes[i] ? 1 : 0);
			lights[MUTE_LIGHTS + i*3 + 1].setBrightness(mutes[i] ? 0 : 1);
			lights[MUTE_LIGHTS + i*3 + 2].setBrightness(0);
		}
	}

	// Clock: external edges lock the counter; in between it moves at the
	// measured rate but never past the next edge, so it stops with the clock.
	bool external = inputs[CLOCK_INPUT].isConnected();
	double ticksPerEdge = clock24ppq ? 4.0 : (double)TICKS_PER_CLOCK;
	bool edge = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
	bool reset = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
	sinceEdge += args.sampleTime;
	float tickSeconds;
	if (external)
		tickSeconds = edgePeriod > 0.f ? (float)(edgePeriod / ticksPerEdge) : 60.f / (120.f * 96.f);
	else
		tickSeconds = 60.f / (banks[bank].bpm * 96.f);

	if (reset || restart) {
		restart = false;
		if (external) {
			// One clock before the start: the next clock edge (or this one) plays the start count.
			beginCycle(0.0);
			position = lastEdgePosition = (double)banks[bank].cycleStart * TICKS_PER_CLOCK - ticksPerEdge;
		}
		else {
			restartCycle(tickSeconds);
		}
	}
	if (external) {
		if (edge) {
			if (sinceEdge < 2.f)
				edgePeriod = sinceEdge;
			sinceEdge = 0.f;
			double target = lastEdgePosition + ticksPerEdge;
			advance(position, std::max(target, position), tickSeconds);
			lastEdgePosition = position;
		}
		else if (edgePeriod > 0.f) {
			double limit = lastEdgePosition + ticksPerEdge - 1e-6;
			double next = std::min(position + args.sampleTime / tickSeconds, limit);
			if (next > position)
				advance(position, next, tickSeconds);
		}
	}
	else if (running) {
		advance(position, position + args.sampleTime / tickSeconds, tickSeconds);
		lastEdgePosition = position;
	}

	// Rows 1-4 and columns A-D; independent mode reads the row and column sets.
	int rowSet = pMode == matrix ? matrix : indepEditA;
	int colSet = pMode == matrix ? matrix : indepEditB;
	for (int s = rowSet; s <= colSet; s++)
		for (int i = 0; i < RIOT_CELLS; i++)
			gates[i][s] = gatePulses[i][s].process(args.sampleTime);
	for (int r = 0; r < 4; r++) {
		bool on = gates[r*4][rowSet] || gates[r*4+1][rowSet] || gates[r*4+2][rowSet] || gates[r*4+3][rowSet];
		outputs[GATE_OUTPUTS + r].setVoltage(!mutes[r] && on ? 10.f : 0.f);
	}
	for (int c = 0; c < 4; c++) {
		bool on = gates[c][colSet] || gates[4+c][colSet] || gates[8+c][colSet] || gates[12+c][colSet];
		outputs[GATE_OUTPUTS + 4 + c].setVoltage(!mutes[4+c] && on ? 10.f : 0.f);
	}
}

struct RATEAUBlueKnob : BidooBlueKnob {
	RATEAUBlueKnob () {
		smooth = false;
	}

	void onButton(const ButtonEvent& e) override {
		RoundKnob::onButton(e);
		if (getParamQuantity()) {
			RATEAU *m = dynamic_cast<RATEAU*>(this->getParamQuantity()->module);
			if (m)
				m->fCntrl = this->getParamQuantity()->paramId - RATEAU::CONTROLS_PARAMS;
		}
	}

	// The selected knob gets a pulsing ring, drawn on top every frame (the
	// knob SVG is shared by every Bidoo blue knob, so it is not touched).
	void draw(const DrawArgs& args) override {
		BidooBlueKnob::draw(args);
		RATEAU *m = getParamQuantity() ? dynamic_cast<RATEAU*>(getParamQuantity()->module) : NULL;
		if (m && m->fCntrl == getParamQuantity()->paramId - RATEAU::CONTROLS_PARAMS) {
			float a = 0.5f + 0.5f * std::sin(2.f * M_PI * (float)system::getTime());
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, box.size.x * 0.5f + 1.5f);
			nvgStrokeColor(args.vg, nvgRGBAf(1.f, 0.913f, 0.f, 0.3f + 0.6f * a));
			nvgStrokeWidth(args.vg, 1.5f);
			nvgStroke(args.vg);
		}
	}
};

struct RATEAUDisplay : TransparentWidget {
	RATEAU *m;

	RATEAUDisplay() {

	}

	static const char *speedName(int s) {
		switch (s) {
			case 4: return " DIN";
			case 6: return " 64th";
			case 8: return " 32t";
			case 12: return " 32nd";
			case 16: return " 16t";
			case 18: return " 32d";
			case 24: return " 16th";
			case 32: return " 8t";
			case 36: return " 16d";
			case 48: return " 8th";
			case 64: return " 4t";
			case 72: return " 8d";
			case 96: return " 4th";
			default: return "";
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (m) {
	      nvgFontSize(args.vg, 16);
	  		nvgTextLetterSpacing(args.vg, 0);
	  		nvgFillColor(args.vg, YELLOW_BIDOO);
				char sBank[16], sFControl[16], sValue[32];
				snprintf(sBank, sizeof(sBank), "PTRN %d", m->bank + 1);
				snprintf(sFControl, sizeof(sFControl), "# %d", m->fCntrl + 1);
				const RiotCell &c = m->cell(m->fCntrl, m->pMode);
				switch(m->page) {
					case pDiv: snprintf(sValue, sizeof(sValue), " => %d", c.div); break;
					case pStep: snprintf(sValue, sizeof(sValue), " => %d", c.step); break;
					case pProb: snprintf(sValue, sizeof(sValue), " => %.0f %%", c.prob * 100.0f); break;
					case pSpeed: snprintf(sValue, sizeof(sValue), " => %d%s", c.speed, speedName(c.speed)); break;
					case pClockShift: snprintf(sValue, sizeof(sValue), " => %d", c.div > 0 ? std::min(c.clockShift, c.div - 1) : 0); break;
					case pTimeShift: snprintf(sValue, sizeof(sValue), " => %.0f deg", c.timeShift); break;
					default: snprintf(sValue, sizeof(sValue), " => %.0f %%", c.pulseWidth * 100.0f); break;
				}
				nvgText(args.vg, 135, 0, sBank, NULL);
				nvgText(args.vg, 0, 0, sFControl, NULL);
				nvgText(args.vg, 35, 0, sValue, NULL);
	    }
		}
		Widget::drawLayer(args, layer);
	}

};

struct divRateauBtn : app::SvgSwitch {
	divRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/divBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/divBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct stepRateauBtn : app::SvgSwitch {
	stepRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/stepBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/stepBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct probRateauBtn : app::SvgSwitch {
	probRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/probBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/probBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct speedRateauBtn : app::SvgSwitch {
	speedRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/speedBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/speedBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct clockshiftRateauBtn : app::SvgSwitch {
	clockshiftRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/clockshiftBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/clockshiftBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct timeshiftRateauBtn : app::SvgSwitch {
	timeshiftRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/timeshiftBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/timeshiftBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct pulsewidthRateauBtn : app::SvgSwitch {
	pulsewidthRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/pulsewidthBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/pulsewidthBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

// Slider for an int or float setting of the module, in a context menu.
struct RiotQuantity : Quantity {
	float *fValue = NULL;
	int *iValue = NULL;
	float minV, maxV, defV;
	std::string label, unit;
	RiotQuantity(float *v, float lo, float hi, float d, std::string l, std::string u) : fValue(v), minV(lo), maxV(hi), defV(d), label(l), unit(u) {}
	RiotQuantity(int *v, float lo, float hi, float d, std::string l, std::string u) : iValue(v), minV(lo), maxV(hi), defV(d), label(l), unit(u) {}
	void setValue(float v) override {
		v = clamp(v, minV, maxV);
		if (fValue) *fValue = v;
		else *iValue = (int)std::round(v);
	}
	float getValue() override { return fValue ? *fValue : (float)*iValue; }
	float getMinValue() override { return minV; }
	float getMaxValue() override { return maxV; }
	float getDefaultValue() override { return defV; }
	std::string getLabel() override { return label; }
	std::string getUnit() override { return unit; }
	int getDisplayPrecision() override { return fValue ? 4 : 3; }
};

struct RiotSlider : ui::Slider {
	RiotSlider(RiotQuantity *q) {
		quantity = q;
		box.size.x = 200.f;
	}
	~RiotSlider() {
		delete quantity;
	}
};

struct RATEAUWidget : BidooWidget {
	ParamWidget *controls[16];
  RATEAUWidget(RATEAU *module);
	void appendContextMenu(Menu *menu) override;
};


RATEAUWidget::RATEAUWidget(RATEAU *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/RATEAU.svg"));

	addChild(createWidget<ScrewSilver>(Vec(15, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createWidget<ScrewSilver>(Vec(15, 365)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

	const int controlsSpacer = 43;
	const int controlsXAnchor = 15;
	const int controlsYAnchor = 150;
	const float intputsAlign = 3.5f;

	const float btnXAnchor = 11.0f;
	const float btnOffset = 30.0f;
	const int ledOffset = 6;
	const int btnYAnchor = 116;

	addParam(createParam<divRateauBtn>(Vec(btnXAnchor, btnYAnchor), module, RATEAU::DIV_PARAM));
	addParam(createParam<stepRateauBtn>(Vec(btnXAnchor+btnOffset, btnYAnchor), module, RATEAU::STEP_PARAM));
	addParam(createParam<probRateauBtn>(Vec(btnXAnchor+2*btnOffset, btnYAnchor), module, RATEAU::PROB_PARAM));
	addParam(createParam<speedRateauBtn>(Vec(btnXAnchor+3*btnOffset, btnYAnchor), module, RATEAU::SPEED_PARAM));
	addParam(createParam<clockshiftRateauBtn>(Vec(btnXAnchor+4*btnOffset, btnYAnchor), module, RATEAU::CLOCKSHIFT_PARAM));
	addParam(createParam<timeshiftRateauBtn>(Vec(btnXAnchor+5*btnOffset, btnYAnchor), module, RATEAU::TIMESHIFT_PARAM));
	addParam(createParam<pulsewidthRateauBtn>(Vec(btnXAnchor+6*btnOffset, btnYAnchor), module, RATEAU::PULSEWIDTH_PARAM));

	addInput(createInput<PJ301MPort>(Vec(6.0f, 45.0f+intputsAlign), module, RATEAU::CLOCK_INPUT));
	// RUN: the internal clock's start / stop, in the CLK box
	addParam(createParam<MiniLEDButton>(Vec(36.85f, 46.3f), module, RATEAU::STARTSTOP_PARAM));
	addChild(createLight<SmallLight<BlueLight>>(Vec(36.55f, 46.0f), module, RATEAU::STARTSTOP_LIGHT));
	addInput(createInput<TinyPJ301MPort>(Vec(32.0f, 57.0f), module, RATEAU::STARTSTOP_INPUT));
	addInput(createInput<PJ301MPort>(Vec(52.0f, 45.0f+intputsAlign), module, RATEAU::RESET_INPUT));

	addParam(createParam<BidooLEDButton>(Vec(80.0f,45.0f), module, RATEAU::INDEPEDITA_PARAM));
	addChild(createLight<SmallLight<BlueLight>>(Vec(80.0f+ledOffset,45.0f+ledOffset), module, RATEAU::INDEPEDITA_LIGHT));

	addParam(createParam<BidooLEDButton>(Vec(102.5f,45.0f), module, RATEAU::INDEPEDITB_PARAM));
	addChild(createLight<SmallLight<BlueLight>>(Vec(102.5f+ledOffset,45.0f+ledOffset), module, RATEAU::INDEPEDITB_LIGHT));

	addParam(createParam<BidooLEDButton>(Vec(125.0f,45.0f), module, RATEAU::MATRIX_PARAM));
	addChild(createLight<SmallLight<BlueLight>>(Vec(125.0f+ledOffset,45.0f+ledOffset), module, RATEAU::MATRIX_LIGHT));

		addInput(createInput<TinyPJ301MPort>(Vec(126.5f,67.0f), module, RATEAU::MODE_INPUT));

	addParam(createParam<BidooBlueSnapKnob>(Vec(149.0f, 45.0f), module, RATEAU::BANK_PARAM));
	addInput(createInput<PJ301MPort>(Vec(controlsXAnchor + controlsSpacer * 4, 45.0f+intputsAlign), module, RATEAU::BANK_INPUT));

	RATEAUDisplay *pDisp = new RATEAUDisplay();
	pDisp->box.pos = Vec(17,103);
	pDisp->box.size = Vec(150, 20);
	pDisp->m = module ? module : NULL;
	addChild(pDisp);

	for (int i = 0; i < 16; i++) {
		controls[i] = createParam<RATEAUBlueKnob>(Vec(controlsXAnchor + controlsSpacer *(i%4), controlsYAnchor + controlsSpacer * floor(i/4)), module, RATEAU::CONTROLS_PARAMS + i);
		addParam(controls[i]);

		if (i%4==0) {
			addOutput(createOutput<PJ301MPort>(Vec(controlsXAnchor + controlsSpacer * 4, controlsYAnchor + controlsSpacer * floor(i/4) + intputsAlign), module, RATEAU::GATE_OUTPUTS+i/4));

			addParam(createParam<MiniLEDButton>(Vec(controlsXAnchor - 11, controlsYAnchor + controlsSpacer * floor(i/4) + intputsAlign + 8), module, RATEAU::MUTE_PARAMS+i/4));
			addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(controlsXAnchor - 11 , controlsYAnchor + controlsSpacer * floor(i/4) + intputsAlign + 8), module, RATEAU::MUTE_LIGHTS+3*i/4));
		}
	}
	for (int i = 0; i < 4; i++) {
			addOutput(createOutput<PJ301MPort>(Vec(controlsXAnchor + controlsSpacer * i + intputsAlign, controlsYAnchor + controlsSpacer * 4), module, RATEAU::GATE_OUTPUTS+4+i));

			addParam(createParam<MiniLEDButton>(Vec(controlsXAnchor + controlsSpacer * i + intputsAlign + 8, controlsYAnchor - 11), module, RATEAU::MUTE_PARAMS+4+i));
			addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(controlsXAnchor + controlsSpacer * i + intputsAlign + 8, controlsYAnchor - 11 ), module, RATEAU::MUTE_LIGHTS+(4+i)*3));
	}
}

void RATEAUWidget::appendContextMenu(Menu *menu) {
	BidooWidget::appendContextMenu(menu);
	RATEAU *m = dynamic_cast<RATEAU*>(this->module);
	if (!m)
		return;
	RiotBank &b = m->banks[m->bank];
	menu->addChild(new MenuSeparator());
	menu->addChild(createMenuLabel("Pattern " + std::to_string(m->bank + 1)));
	menu->addChild(new RiotSlider(new RiotQuantity(&b.bpm, 20.f, 300.f, 120.f, "Internal clock", " BPM")));
	menu->addChild(new RiotSlider(new RiotQuantity(&b.cycleStart, 1.f, 255.f, 1.f, "Cycle start", "")));
	menu->addChild(new RiotSlider(new RiotQuantity(&b.cycleEnd, 0.f, 255.f, 0.f, "Cycle end (0 = endless)", "")));
	menu->addChild(createMenuItem("Clear this mode of the pattern", "", [=]() { m->clear(m->bank, m->pMode); m->updateControls(); }));
	menu->addChild(new MenuSeparator());
	menu->addChild(createBoolPtrMenuItem("Internal clock running", "", &m->running));
	menu->addChild(createBoolPtrMenuItem("Clock input at 24 PPQ (DIN sync)", "", &m->clock24ppq));
	menu->addChild(createBoolPtrMenuItem("Pattern changes wait for the cycle start", "", &m->beatSync));
}

Model *modelRATEAU = createModel<RATEAU, RATEAUWidget>("RATEAU");

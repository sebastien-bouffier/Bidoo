#pragma once
#include "plugin.hpp"

// What differs between ZOUMAI and ENCORE, read by ZouSeq.hpp at compile time. Kept in a header
// so that tools/tests can build both engines with the modules' own values.

// One clock per step: the playhead moves between clocks at the measured clock
// rate. Units of 1/1008000 step.
struct ZouMaiTraits {
	static constexpr bool INTERPOLATE = true;
	static constexpr int TICKS = 1008000;
	static constexpr int UNITS_PER_CLOCK = TICKS;
	static constexpr int KNOB_PER_STEP = 1;
	static constexpr int MAX_SPEED = 4;
	static constexpr int MIN_LENGTH = 0, MAX_LENGTH = 64 * TICKS, DEFAULT_LENGTH = TICKS / 10 * 9, SHORTEST_NOTE = TICKS / 100;
	static constexpr int MAX_DISTANCE = 64 * TICKS, DEFAULT_DISTANCE = TICKS / 2;
	static constexpr int MAX_TRIM = TICKS;
	static constexpr int OLD_TICKS = TICKS;  // its times are saved in steps
	static constexpr float PREVIEW_LENGTH = 0.99f, PREVIEW_DISTANCE = 1.0f;
	static constexpr const char *QUANTIZED_LABEL = "Quant";
	static constexpr const char *PANEL = "res/ZOUMAI.svg";
	static Model *expanderModel() {return modelZOUMAIExpander;}
};

// A finer grid: each step is 96 ticks (32nds and triplets exact) and the head only moves
// on a clock, 3 x Speed ticks at a time (32 clocks a step), so it follows a fast or
// modulated clock. Patches saved at 32 ticks a step are scaled on load.
struct EncoreTraits {
	static constexpr bool INTERPOLATE = false;
	static constexpr int TICKS = 96;
	static constexpr int UNITS_PER_CLOCK = 3;
	static constexpr int KNOB_PER_STEP = 32;
	static constexpr int MAX_SPEED = 8;
	static constexpr int MIN_LENGTH = 3, MAX_LENGTH = 6144, DEFAULT_LENGTH = 45, SHORTEST_NOTE = 3;
	static constexpr int MAX_DISTANCE = 96, DEFAULT_DISTANCE = 3;
	static constexpr int MAX_TRIM = 95;
	static constexpr int OLD_TICKS = 32;  // patches without "ticksPerStep"
	static constexpr float PREVIEW_LENGTH = 0.5f, PREVIEW_DISTANCE = 0.03f;
	static constexpr const char *QUANTIZED_LABEL = "Qnt.";
	static constexpr const char *PANEL = "res/ENCORE.svg";
	static Model *expanderModel() {return modelENCOREExpander;}
};

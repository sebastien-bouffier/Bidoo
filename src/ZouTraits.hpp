#pragma once
#include "plugin.hpp"

// zOu MAi's constants, read by ZouSeq.hpp at compile time. Kept in a header so that tools/tests
// build the engine with the module's own values.

// One clock per step: the playhead moves between clocks at the measured clock rate (Step
// mode; the enCORE clock mode is in ZouSeq.hpp). Units of 1/1008000 step.
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

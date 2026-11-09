#include "ZouSeq.hpp"

// A finer grid: each step is 32 ticks and the head only moves on a clock, Speed
// ticks at a time, so it follows a fast or modulated clock. Everything else is
// in ZouSeq.hpp.
struct EncoreTraits {
	static constexpr bool INTERPOLATE = false;
	static constexpr int TICKS = 32;
	static constexpr int UNITS_PER_CLOCK = 1;
	static constexpr int KNOB_PER_STEP = 32;
	static constexpr int MAX_SPEED = 8;
	static constexpr int MIN_LENGTH = 1, MAX_LENGTH = 2048, DEFAULT_LENGTH = 15, SHORTEST_NOTE = 1;
	static constexpr int MAX_DISTANCE = 32, DEFAULT_DISTANCE = 1;
	static constexpr int MAX_TRIM = 31;
	static constexpr float PREVIEW_LENGTH = 0.5f, PREVIEW_DISTANCE = 0.03f;
	static constexpr const char *QUANTIZED_LABEL = "Qnt.";
	static constexpr const char *PANEL = "res/ENCORE.svg";
	static Model *expanderModel() {return modelENCOREExpander;}
};

using ENCORE = ZouSeq<EncoreTraits>;

Model *modelENCORE = createModel<ENCORE, ZouSeqWidget<ENCORE>>("ENCORE");

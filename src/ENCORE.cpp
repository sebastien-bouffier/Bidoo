#include "ZouSeq.hpp"

// A finer grid: each step is 96 ticks (32nds and triplets exact) and the head only moves
// on a clock, 3 x Speed ticks at a time (32 clocks a step), so it follows a fast or
// modulated clock. Patches saved at 32 ticks a step are scaled on load. Everything else
// is in ZouSeq.hpp.
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

using ENCORE = ZouSeq<EncoreTraits>;

Model *modelENCORE = createModel<ENCORE, ZouSeqWidget<ENCORE>>("ENCORE");

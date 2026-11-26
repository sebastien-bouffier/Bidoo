#include "ZouSeq.hpp"
#include "ZouTraits.hpp"

// Hidden since Bidoo 2.7.8 (zOu MAi's fast clock does what it did): kept so that patches made
// with it open and play as they did. A finer grid: each step is 96 ticks and the head only
// moves on a clock, 3 x Speed ticks at a time (32 clocks a step). Everything else is in
// ZouSeq.hpp; the traits are in ZouTraits.hpp.
using ENCORE = ZouSeq<EncoreTraits>;

Model *modelENCORE = createModel<ENCORE, ZouSeqWidget<ENCORE>>("ENCORE");

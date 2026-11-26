#include "ZouSeq.hpp"
#include "ZouTraits.hpp"

// Two clock modes (context menu): Step, one clock a step and the playhead moving between clocks;
// Fast, 32 clocks a step (tOCAnTe's /128 output) and the head moving on clocks only.
// Everything else is in ZouSeq.hpp; the traits are in ZouTraits.hpp.
using ZOUMAI = ZouSeq<ZouMaiTraits>;

Model *modelZOUMAI = createModel<ZOUMAI, ZouSeqWidget<ZOUMAI>>("ZOUMAI");

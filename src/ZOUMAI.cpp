#include "ZouSeq.hpp"
#include "ZouTraits.hpp"

// One clock per step (the playhead moves between clocks), or 32 (fast clock, context menu).
// Everything else is in ZouSeq.hpp; the traits are in ZouTraits.hpp.
using ZOUMAI = ZouSeq<ZouMaiTraits>;

Model *modelZOUMAI = createModel<ZOUMAI, ZouSeqWidget<ZOUMAI>>("ZOUMAI");

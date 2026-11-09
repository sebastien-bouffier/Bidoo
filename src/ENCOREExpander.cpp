#include "ZouSeqExpander.hpp"

struct ENCOREExpanderTraits {
	static constexpr const char *PANEL = "res/ENCOREExpander.svg";
	static Model *parentModel() {return modelENCORE;}
};

using ENCOREExpander = ZouSeqExpander<ENCOREExpanderTraits>;

Model *modelENCOREExpander = createModel<ENCOREExpander, ZouSeqExpanderWidget<ENCOREExpander>>("ENCORE-Expander");

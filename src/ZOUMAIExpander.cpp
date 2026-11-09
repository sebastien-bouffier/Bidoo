#include "ZouSeqExpander.hpp"

struct ZOUMAIExpanderTraits {
	static constexpr const char *PANEL = "res/ZOUMAIExpander.svg";
	static Model *parentModel() {return modelZOUMAI;}
};

using ZOUMAIExpander = ZouSeqExpander<ZOUMAIExpanderTraits>;

Model *modelZOUMAIExpander = createModel<ZOUMAIExpander, ZouSeqExpanderWidget<ZOUMAIExpander>>("ZOUMAI-Expander");

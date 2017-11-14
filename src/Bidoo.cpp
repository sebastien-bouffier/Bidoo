#include "Bidoo.hpp"


Plugin *plugin;

void init(rack::Plugin *p) {
	plugin = p;
	p->slug = "Bidoo";
#ifdef VERSION
	p->version = TOSTRING(VERSION);
#endif
	p->addModel(createModel<DTROYWidget>("Bidoo", "dTRoY", "dTRoY sequencer", SEQUENCER_TAG));
	p->addModel(createModel<OUAIVEWidget>("Bidoo", "OUAIve", "OUAIve player", SAMPLER_TAG));
	p->addModel(createModel<CHUTEWidget>("Bidoo", "ChUTE", "ChUTE trigger", SEQUENCER_TAG));
	p->addModel(createModel<DUKEWidget>("Bidoo", "dUKe", "dUKe controller", CONTROLLER_TAG));
	p->addModel(createModel<VOIDWidget>("Bidoo", "vOId", "vOId machine", BLANK_TAG));
}

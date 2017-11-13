#include "Bidoo.hpp"


Plugin *plugin;

void init(rack::Plugin *p) {
	plugin = p;
	p->slug = "Bidoo";
#ifdef VERSION
	p->version = TOSTRING(VERSION);
#endif
	p->addModel(createModel<DTROYWidget>("Bidoo", "DTROY", "DTROY sequencer", SEQUENCER_TAG));
	p->addModel(createModel<OUAIVEWidget>("Bidoo", "OUAIVE", "OUAIVE player", SAMPLER_TAG));
	p->addModel(createModel<CHUTEWidget>("Bidoo", "CHUTE", "CHUTE trigger", SEQUENCER_TAG));
}

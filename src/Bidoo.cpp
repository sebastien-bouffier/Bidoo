#include "Bidoo.hpp"


Plugin *plugin;

void init(rack::Plugin *p) {
	plugin = p;
	p->slug = "Bidoo";
#ifdef VERSION
	p->version = TOSTRING(VERSION);
#endif
	p->addModel(createModel<DTROYWidget>("Bidoo", "DTROY", "DTROY sequencer"));
	p->addModel(createModel<OUAIVEWidget>("Bidoo", "OUAIVE", "OUAIVE player"));
	p->addModel(createModel<CHUTEWidget>("Bidoo", "CHUTE", "CHUTE trigger"));
}

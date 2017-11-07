#include "Bidoo.hpp"


Plugin *plugin;

void init(rack::Plugin *p) {
	plugin = p;
	plugin->slug = "Bidoo";
 #ifdef VERSION
	p->version = TOSTRING(VERSION);
#endif 
	p->addModel(createModel<DTROYWidget>("Bidoo", "Bidoo", "DTROY", "DTROY sequencer"));
	p->addModel(createModel<OUAIVEWidget>("Bidoo", "Bidoo", "OUAIVE", "OUAIVE player"));
}		

#include "Bidoo.hpp"


Plugin *plugin;

void init(rack::Plugin *p) {
	plugin = p;
	plugin->slug = "Bidoo";
 #ifdef VERSION
	p->version = TOSTRING(VERSION);
#endif 
	p->addModel(createModel<DTROYWidget>("Bidoo", "Bidoo", "DTROY", "DTROY Sequencer"));
}		

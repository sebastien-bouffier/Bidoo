#include "Bidoo.hpp"


Plugin *plugin;

void init(rack::Plugin *p) {
	plugin = p;
	plugin->slug = "Bidoo";
	plugin->name = "Bidoo";
	plugin->homepageUrl = "https://github.com/sebastien-bouffier/Bidoo";
/* #ifdef VERSION
	p->version = TOSTRING(VERSION);
#endif */
	//p->addModel(createModel<DTROYWidget>("Bidoo", "Bidoo", "DTROY", "DTROY Sequencer"));
	createModel<DTROYWidget>(plugin, "DTROY", "DTROY Sequencer");
}		

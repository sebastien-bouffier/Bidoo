#include "Bidoo.hpp"


Plugin *plugin;

void init(rack::Plugin *p) {
	plugin = p;
	p->slug = "Bidoo";
#ifdef VERSION
	p->version = TOSTRING(VERSION);
#endif
	p->website = "https://github.com/sebastien-bouffier/Bidoo";
	p->manual = "https://github.com/sebastien-bouffier/Bidoo/wiki";

	p->addModel(createModel<DTROYWidget>("Bidoo", "dTRoY", "dTrOY sequencer", SEQUENCER_TAG));
	p->addModel(createModel<OUAIVEWidget>("Bidoo", "OUAIve", "OUAIve player", SAMPLER_TAG));
	p->addModel(createModel<CHUTEWidget>("Bidoo", "ChUTE", "ChUTE trigger", SEQUENCER_TAG));
	p->addModel(createModel<DUKEWidget>("Bidoo", "dUKe", "dUKe controller", CONTROLLER_TAG));
	p->addModel(createModel<VOIDWidget>("Bidoo", "vOId", "vOId machine", BLANK_TAG));
	p->addModel(createModel<ACNEWidget>("Bidoo", "ACnE", "ACnE mixer", MIXER_TAG));
	p->addModel(createModel<MOIREWidget>("Bidoo", "MOiRE", "MOiRE controller", CONTROLLER_TAG));
	p->addModel(createModel<LATEWidget>("Bidoo", "lATe", "lATe clock", CLOCK_TAG));
	p->addModel(createModel<FORKWidget>("Bidoo", "ForK", "ForK oscillator", OSCILLATOR_TAG));
	p->addModel(createModel<TIAREWidget>("Bidoo", "TiARE", "TiARE oscillator", OSCILLATOR_TAG));
}

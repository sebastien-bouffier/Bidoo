#include "Bidoo.hpp"

Plugin *plugin;

void init(rack::Plugin *p) {
	plugin = p;
	p->slug = TOSTRING(SLUG);
	p->version = TOSTRING(VERSION);

	p->addModel(modelDTROY);
	p->addModel(modelBORDL);
	p->addModel(modelMU);
	p->addModel(modelTOCANTE);
	p->addModel(modelCHUTE);
	p->addModel(modelLATE);
	p->addModel(modelLOURDE);
	p->addModel(modelACNE);
	p->addModel(modelMS);
	p->addModel(modelOUAIVE);
	p->addModel(modelCANARD);
	p->addModel(modelEMILE);
	p->addModel(modelDUKE);
	p->addModel(modelMOIRE);
	p->addModel(modelFORK);
	p->addModel(modelTIARE);
	p->addModel(modelCLACOS);
	p->addModel(modelANTN);
	p->addModel(modelLIMBO);
	p->addModel(modelPERCO);
	p->addModel(modelBAR);
	p->addModel(modelZINC);
	p->addModel(modelDFUZE);
	p->addModel(modelRABBIT);
	p->addModel(modelBISTROT);
	p->addModel(modelSIGMA);
	p->addModel(modelVOID);
}

#include "plugin.hpp"


Plugin *pluginInstance;

void init(rack::Plugin *p) {
	pluginInstance = p;

	p->addModel(modelDTROY);
	p->addModel(modelBORDL);
	// p->addModel(modelZOUMAI);
	// p->addModel(modelMU);
	p->addModel(modelTOCANTE);
  p->addModel(modelCHUTE);
	p->addModel(modelLATE);
	// p->addModel(modelLOURDE);
	p->addModel(modelACNE);
	p->addModel(modelMS);
	// p->addModel(modelOUAIVE);
	p->addModel(modelCANARD);
	// p->addModel(modelEMILE);
	p->addModel(modelDUKE);
	// p->addModel(modelMOIRE);
	// p->addModel(modelFORK);
	// p->addModel(modelGARCON);
	p->addModel(modelTIARE);
	p->addModel(modelCLACOS);
	p->addModel(modelANTN);
	// p->addModel(modelPENEQUE);
	// p->addModel(modelLIMONADE);
	p->addModel(modelLIMBO);
	p->addModel(modelPERCO);
	p->addModel(modelFFILTR);
	p->addModel(modelBAR);
	p->addModel(modelZINC);
	p->addModel(modelHCTIP);
	p->addModel(modelCURT);
	p->addModel(modelDFUZE);
	p->addModel(modelREI);
	p->addModel(modelRABBIT);
	p->addModel(modelBISTROT);
	p->addModel(modelSIGMA);
	p->addModel(modelVOID);
}

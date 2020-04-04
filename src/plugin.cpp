#include "plugin.hpp"


Plugin *pluginInstance;

void init(rack::Plugin *p) {
	pluginInstance = p;

	p->addModel(modelTOCANTE);
	p->addModel(modelLATE);
	p->addModel(modelDTROY);
	p->addModel(modelBORDL);
	p->addModel(modelZOUMAI);
	p->addModel(modelMU);
  p->addModel(modelCHUTE);
	p->addModel(modelLOURDE);
	p->addModel(modelACNE);
	p->addModel(modelMS);
	p->addModel(modelDUKE);
	p->addModel(modelMOIRE);
	p->addModel(modelHUITRE);
	p->addModel(modelOUAIVE);
	p->addModel(modelPOUPRE);
	p->addModel(modelMAGMA);
	p->addModel(modelOAI);
	p->addModel(modelCANARD);
	p->addModel(modelEMILE);
	p->addModel(modelFORK);
	p->addModel(modelTIARE);
	p->addModel(modelCLACOS);
	p->addModel(modelANTN);
	p->addModel(modelPENEQUE);
	p->addModel(modelLIMONADE);
	p->addModel(modelLIMBO);
	p->addModel(modelPERCO);
	p->addModel(modelBAFIS);
	p->addModel(modelFFILTR);
	p->addModel(modelBAR);
	p->addModel(modelMINIBAR);
	p->addModel(modelZINC);
	p->addModel(modelHCTIP);
	p->addModel(modelCURT);
	p->addModel(modelDFUZE);
	p->addModel(modelREI);
	p->addModel(modelRABBIT);
	p->addModel(modelBISTROT);
	p->addModel(modelSIGMA);
	p->addModel(modelGARCON);
	p->addModel(modelVOID);
}

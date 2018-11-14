#include "Bidoo.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "window.hpp"
#include <thread>
#include "dsp/resampler.hpp"
#include "dsp/fir.hpp"
#include "osdialog.h"
#include "dep/dr_wav/dr_wav.h"
#include "dep/osc/wtOsc.h"
#include "../pffft/pffft.h"
#include <iostream>
#include <fstream>

using namespace std;

#define frameSize 2048
#define aCoeff 2.0*M_PI/frameSize
#define frameSize2 1024

struct threadData {
	wtOscillator *osc;
	int index;
	float *sample;
	size_t sc;
};

void * tSynthetize(threadData data) {
	if (data.index>=0)
		data.osc->table.frames[data.index].calcWav();
	else {
		for (size_t i=0; i<data.osc->table.frames.size(); i++) {
			data.osc->table.frames[i].calcWav();
		}
	}
  return 0;
}

void * tChopSample(threadData data) {
	data.osc->table.loadSample(data.sc, 2048, false, data.sample);
  return 0;
}

void * tWindowSample(threadData data) {
	data.osc->table.window();
  return 0;
}

void * tSmoothSample(threadData data) {
	data.osc->table.smooth();
  return 0;
}

void * tNormalizeFrame(threadData data) {
	data.osc->table.frames[data.index].normalize();
  return 0;
}

void * tNormalizeWt(threadData data) {
	data.osc->table.normalize();
  return 0;
}

void * tFFTSample(threadData data) {
	data.osc->table.frames[data.index].calcFFT();
  return 0;
}

void * tIFFTSample(threadData data) {
	data.osc->table.frames[data.index].calcIFFT();
  return 0;
}

void * tMorphSample(threadData data) {
	data.osc->table.morphFrames();
  return 0;
}

void * tMorphSpectrum(threadData data) {
	data.osc->table.morphSpectrum();
	return 0;
}

struct LIMONADE : Module {
	enum ParamIds {
		RESET_PARAM,
		SYNC_PARAM,
		FREQ_PARAM,
		FINE_PARAM,
		FM_PARAM,
		INDEX_PARAM,
		wtIndex_PARAM,
		wtIndexATT_PARAM,
		COPYPASTE_PARAM,
		ADDFRAME_PARAM,
		DELETEFRAME_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PITCH_INPUT,
		FM_INPUT = PITCH_INPUT+4,
		SYNC_INPUT,
		SYNCMODE_INPUT,
		wtIndex_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS = OUT+4
	};
	enum LightIds {
		COPYPASTE_LIGHT,
		NUM_LIGHTS
	};

	thread sThread;
	threadData sData;
	mutex sLock;
	SchmittTrigger resetTrigger;
	SchmittTrigger copyTrigger;
	SchmittTrigger addFrameTrigger;
	SchmittTrigger deleteFrameTrigger;

	size_t index = 0;
	size_t wtIndex = 0;
	int cpyPstIndex = -1;
	string lastPath;

	wtOscillator osc;

	LIMONADE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
		syncThreadData();
	}

  ~LIMONADE() {

	}

	void step() override;
	void syncThreadData();
	void loadSample(std::string path);
	void windowSample();
	void fftSample(size_t i);
	void ifftSample(size_t i);
	void morphSample();
	void morphSpectrum();
	void updateWaveTable(size_t i);
	void addFrame(size_t i);
	void deleteFrame(size_t i);
	void normalizeFrame(size_t i);
	void normalizeWt();

	json_t *toJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));

		// json_t *magnSJ = json_array();
		// json_t *phasSJ = json_array();
		// //json_t *wavSJ = json_array();
		//
		// for (size_t i=0; i<osc.table.frames.size(); i++) {
		// 	json_t *magnI = json_array();
		// 	json_t *phasI = json_array();
		// 	//json_t *wavI = json_array();
		// 	for (size_t j=0; j<frameSize2; j++) {
		// 		json_t *magnJ = json_real(magn[i][j]);
		// 		json_array_append_new(magnI, magnJ);
		// 		json_t *phasJ = json_real(phas[i][j]);
		// 		json_array_append_new(phasI, phasJ);
		// 		// json_t *wavJ = json_real(wav[i][j]);
		// 		// json_array_append_new(wavI, wavJ);
		// 	}
		// 	json_array_append_new(magnSJ, magnI);
		// 	json_array_append_new(phasSJ, phasI);
		// 	//json_array_append_new(wavSJ, wavI);
		// }
		//
		// json_object_set_new(rootJ, "magnS", magnSJ);
		// json_object_set_new(rootJ, "phasS", phasSJ);
		// //json_object_set_new(rootJ, "wavS", wavSJ);
		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (lastPathJ) {
			lastPath = json_string_value(lastPathJ);
			loadSample(lastPath);
		}
		else {
			// json_t *nFramesJ = json_object_get(rootJ, "osc.table.frames.size()");
			// if (nFramesJ) {
			// 	osc.table.frames.size() = json_integer_value(nFramesJ);
			// }
			//
			// json_t *magnSJ = json_object_get(rootJ, "magnS");
			// json_t *phasSJ = json_object_get(rootJ, "phasS");
			// //json_t *wavSJ = json_object_get(rootJ, "wavS");
			// if (magnSJ && phasSJ) {
			// 	for (size_t i=0; i<osc.table.frames.size(); i++) {
			// 		json_t *magnI = json_array_get(magnSJ, i);
			// 		json_t *phasI = json_array_get(phasSJ, i);
			// 		//json_t *wavI = json_array_get(wavSJ, i);
			// 		if (magnI && phasI) {
			// 			for (size_t j=0; j<frameSize2; j++) {
			// 				magn[i][j] = json_number_value(json_array_get(magnI, j));
			// 				phas[i][j] = json_number_value(json_array_get(phasI, j));
			// 				//wav[i][j] = json_number_value(json_array_get(wavI, j));
			// 			}
			// 		}
			// 	}
			// }
		}
	}

	void randomize() override {
		// for (size_t i=0; i<frameSize; i++) {
		// 	magn[index][i]=randomUniform()*100.0f;
		// 	phas[index][i]=randomUniform()*M_PI;
		// }
		// updateWaveTable(index);
	}

	void reset() override {
		// for (size_t i=0; i<256; i++) {
		// 	memset(magn[i], 0, frameSize2*sizeof(float));
		// 	memset(phas[i], 0, frameSize2*sizeof(float));
		// 	memset(wav[i], 0, frameSize*sizeof(float));
		// }
		// lastPath = "";
		// osc.table.frames.size() = 1;
	}
};

inline void LIMONADE::syncThreadData() {
	sData.osc = &osc;
}

inline void LIMONADE::updateWaveTable(size_t i) {
	syncThreadData();
	sData.index = i;
	sThread = thread(tSynthetize, std::ref(sData));
	sThread.detach();
}

inline void LIMONADE::fftSample(size_t i) {
	sData.index = i;
	sThread = thread(tFFTSample, std::ref(sData));
	sThread.detach();
}

inline void LIMONADE::ifftSample(size_t i) {
	sData.index = i;
	sThread = thread(tIFFTSample, std::ref(sData));
	sThread.detach();
}

inline void LIMONADE::morphSample() {
	sThread = thread(tMorphSample, std::ref(sData));
	sThread.detach();
}

inline void LIMONADE::morphSpectrum() {
	sThread = thread(tMorphSpectrum, std::ref(sData));
	sThread.detach();
}

void LIMONADE::addFrame(size_t i) {
	osc.table.addFrame(i);
}

void LIMONADE::deleteFrame(size_t i) {
	osc.table.removeFrame(i);
}

void LIMONADE::loadSample(std::string path) {
	unsigned int c;
  unsigned int sr;
  drwav_uint64 sc;
	float* pSampleData;
  pSampleData = drwav_open_and_read_file_f32(path.c_str(), &c, &sr, &sc);
  if (pSampleData != NULL)  {
		if (sData.sample != NULL) {
			delete(sData.sample);
		}
		if (c == 1) {
			sData.sample = (float*)calloc(sc,sizeof(float));
			for (unsigned int i=0; i < sc; i++) {
				sData.sample[i] = pSampleData[i];
			}
			sData.sc = sc;
			sThread = thread(tChopSample, std::ref(sData));
			sThread.detach();
		}
		else if (c == 2) {
			sData.sc = sc/2;
			sData.sample = (float*)calloc(sData.sc,sizeof(float));
			for (unsigned int i=0; i<sData.sc; i++) {
				sData.sample[i] = 0.5f*(pSampleData[2*i]+pSampleData[2*i+1]);
			}
			sThread = thread(tChopSample, std::ref(sData));
			sThread.detach();
		}
		drwav_free(pSampleData);
	}
}

void LIMONADE::windowSample() {
	sThread = thread(tWindowSample, std::ref(sData));
	sThread.detach();
}

void LIMONADE::normalizeFrame(size_t i) {
	sData.index = i;
	sThread = thread(tNormalizeFrame, std::ref(sData));
	sThread.detach();
}

void LIMONADE::normalizeWt() {
	sThread = thread(tNormalizeWt, std::ref(sData));
	sThread.detach();
}

void LIMONADE::step() {
	if (addFrameTrigger.process(params[ADDFRAME_PARAM].value))
	{
		addFrame(index);
	}

	if (deleteFrameTrigger.process(params[DELETEFRAME_PARAM].value))
	{
		deleteFrame(index);
	}

	if (resetTrigger.process(params[RESET_PARAM].value))
	{
		osc.table.reset();
	}

	index = max((int)rescale(params[INDEX_PARAM].value,0.0f,10.0f,0.0f,osc.table.frames.size()-1),0);
	wtIndex = max((int)(rescale(params[wtIndex_PARAM].value,0.0f,10.0f,0.0f,osc.table.frames.size()-1) + rescale(inputs[wtIndex_INPUT].value*params[wtIndexATT_PARAM].value,0.0f,10.0f,0.0f,osc.table.frames.size()-1)),0);

	osc.soft = (params[SYNC_PARAM].value + inputs[SYNCMODE_INPUT].value) <= 0.0f;
	float pitchFine = 3.0f * quadraticBipolar(params[FINE_PARAM].value);
	float pitchCv = 12.0f * inputs[PITCH_INPUT].value;
	if (inputs[FM_INPUT].active) {
		pitchCv += quadraticBipolar(params[FM_PARAM].value) * 12.0f * inputs[FM_INPUT].value;
	}
	osc.setPitch(params[FREQ_PARAM].value, pitchFine + pitchCv);
	osc.syncEnabled = inputs[SYNC_INPUT].active;

	if (outputs[OUT].active) {
		osc.process(engineGetSampleTime(), inputs[SYNC_INPUT].value, wtIndex);
		outputs[OUT].value = 5.0f * osc.out();
	}
}

struct LIMONADEWidget : ModuleWidget {
	LIMONADEWidget(LIMONADE *module);
	Menu *createContextMenu() override;
};

struct LIMONADEBinsDisplay : OpaqueWidget {
	LIMONADE *module;
	shared_ptr<Font> font;
	const float width = 400.0f;
	const float heightMagn = 70.0f;
	const float heightPhas = 50.0f;
	const float graphGap = 30.0f;
	float zoomWidth = width * 28.0f;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refY = 0.0f;
	float refX = 0.0f;
	bool write = false;

	LIMONADEBinsDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void onMouseDown(EventMouseDown &e) override {
		refX = e.pos.x;
		refY = e.pos.y;
		refIdx = ((e.pos.x - zoomLeftAnchor)/zoomWidth)*(float)frameSize2;
		OpaqueWidget::onMouseDown(e);
	}

	void onDragStart(EventDragStart &e) override {
		windowCursorLock();
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(EventDragMove &e) override {
		if (!windowIsShiftPressed() && (module->osc.table.frames.size()>0)) {
			if (refY<=heightMagn) {
				if (windowIsModPressed()) {
					module->osc.table.frames[module->index].magnitude[refIdx] = 0.0f;
				}
				else {
					module->osc.table.frames[module->index].magnitude[refIdx] -= e.mouseRel.y/500.0f;
					module->osc.table.frames[module->index].magnitude[refIdx] = clamp(module->osc.table.frames[module->index].magnitude[refIdx],0.0f, 1.0f);
				}
			}
			else if (refY>=heightMagn+graphGap) {
				if (windowIsModPressed()) {
					module->osc.table.frames[module->index].phase[refIdx] = 0.0f;
				}
				else {
					module->osc.table.frames[module->index].phase[refIdx] -= e.mouseRel.y/500.0f;
					module->osc.table.frames[module->index].phase[refIdx] = clamp(module->osc.table.frames[module->index].phase[refIdx],-1.0f*M_PI, M_PI);
				}
			}
			module->updateWaveTable(module->index);
		}
		else {
			zoomLeftAnchor = clamp(refX - (refX - zoomLeftAnchor) + e.mouseRel.x, width - zoomWidth,0.0f);
		}
		OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(EventDragEnd &e) override {
		windowCursorUnlock();
		OpaqueWidget::onDragEnd(e);
	}

	void draw(NVGcontext *vg) override {
		nvgSave(vg);
		Rect b = Rect(Vec(zoomLeftAnchor, 0), Vec(zoomWidth, heightMagn + graphGap + heightPhas));
		nvgScissor(vg, 0, b.pos.y, width, heightMagn + graphGap + heightPhas);
		float invframeSize = 1.0f/FS2;
		size_t tag=1;

		nvgFillColor(vg, YELLOW_BIDOO);
		nvgFontSize(vg, 16.0f);
		nvgFontFaceId(vg, font->handle);
		nvgTextLetterSpacing(vg, -2.0f);
		nvgFillColor(vg, YELLOW_BIDOO);
		nvgText(vg, 0.0f, heightMagn + graphGap/2+4, ("Frame " + to_string(module->index+1) + " / " + to_string(module->osc.table.frames.size())).c_str(), NULL);
		nvgText(vg, 130.0f, heightMagn + graphGap/2+4, "▲ Magnitude ▼ Phase", NULL);

		if (module->index<module->osc.table.frames.size()) {
			module->osc.table.mtx.lock();
			wtFrame frame = module->osc.table.frames[module->index];
			module->osc.table.mtx.unlock();
			for (size_t i = 0; i < FS2; i++) {
				float x, y;
				x = (float)i * invframeSize;
				y = frame.magnitude[i];
				Vec p;
				p.x = b.pos.x + b.size.x * x;
				p.y = heightMagn * y;

				if (i==tag){
					nvgBeginPath(vg);
					nvgFillColor(vg, nvgRGBA(45, 114, 143, 100));
					nvgRect(vg, p.x, 0, b.size.x * invframeSize, heightMagn);
					nvgRect(vg, p.x, heightMagn + graphGap, b.size.x * invframeSize, heightPhas);
					nvgClosePath(vg);
					nvgLineCap(vg, NVG_MITER);
					nvgStrokeWidth(vg, 0);
					//nvgStroke(vg);
					nvgFill(vg);
					tag *=2;
				}

				if (p.x < width) {
					nvgStrokeColor(vg, YELLOW_BIDOO);
					nvgFillColor(vg, YELLOW_BIDOO);
					nvgLineCap(vg, NVG_MITER);
					nvgStrokeWidth(vg, 1);
					nvgBeginPath(vg);
					nvgRect(vg, p.x, heightMagn - p.y, b.size.x * invframeSize, p.y);
					y = module->osc.table.frames[module->index].phase[i]/M_PI;
					p.y = heightPhas * 0.5f * y;
					nvgRect(vg, p.x, heightMagn + graphGap + heightPhas * 0.5f - p.y, b.size.x * invframeSize, p.y);
					nvgClosePath(vg);
					nvgStroke(vg);
					nvgFill(vg);
				}
			}
		}

		nvgResetScissor(vg);
		nvgRestore(vg);
	}
};

struct LIMONADEWavDisplay : OpaqueWidget {
	LIMONADE *module;
	shared_ptr<Font> font;
	const float width = 150.0f;
	const float height = 100.0f;
	int refIdx = 0;
	float alpha1 = 25.0f;
	float alpha2 = 35.0f;

	LIMONADEWavDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void onMouseDown(EventMouseDown &e) override {
			OpaqueWidget::onMouseDown(e);
	}

	void onDragStart(EventDragStart &e) override {
		windowCursorLock();
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(EventDragMove &e) override {
		alpha1+=e.mouseRel.y;
		alpha2-=e.mouseRel.x;
		if (alpha1>90) alpha1=90;
		if (alpha1<-90) alpha1=-90;
		if (alpha2>360) alpha2-=360;
		if (alpha2<0) alpha2+=360;
		OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(EventDragEnd &e) override {
		windowCursorUnlock();
		OpaqueWidget::onDragEnd(e);
	}

	void point2D(float x3D, float y3D, float z3D, float w, float h, float a1, float a2, float &x2D, float &y2D) {
		a1 *= M_PI/180.0f;
		a2 *= M_PI/180.0f;
		y2D = z3D*cos(a1)-(cos(a2)*y3D-sin(a2)*x3D)*sin(a1)+h/2.0f;
		x2D = cos(a2)*x3D+sin(a2)*y3D+w/2.0f;
	}

	void draw(NVGcontext *vg) override {
		module->osc.table.mtx.lock();
		wtTable table;
		for (size_t i=0; i<module->osc.table.frames.size(); i++) {
			wtFrame frame;
			frame.morphed = module->osc.table.frames[i].morphed;
			frame.sample=module->osc.table.frames[i].sample;
			table.frames.push_back(frame);
		}
		module->osc.table.mtx.unlock();

		nvgSave(vg);
		float invNbSample = 1.0f/FS;

		for (size_t n=0; n<table.frames.size(); n++) {
			nvgBeginPath(vg);
			for (size_t i=0; i<FS; i++) {
				float x3D, y3D, z3D, x2D, y2D;
				y3D = 10.0f * (table.frames.size()-n)/table.frames.size()-5.0f;
				x3D = 10.0f * i * invNbSample-5.0f;
				z3D = table.frames[table.frames.size()-1-n].sample[i];
				point2D(x3D, y3D, z3D, 15, 10, alpha1, alpha2, x2D, y2D);
				if (i == 0) {
					nvgMoveTo(vg, 10.0f * x2D, 10.0f * y2D);
				}
				else {
					nvgLineTo(vg, 10.0f * x2D, 10.0f * y2D);
				}
			}
			if (table.frames[table.frames.size()-1-n].morphed) {
				nvgStrokeColor(vg, YELLOW_BIDOO_LIGHT);
				nvgStrokeWidth(vg, 1.0f);
			}
			else {
				nvgStrokeColor(vg, YELLOW_BIDOO);
				nvgStrokeWidth(vg, 1.0f);
			}
			nvgStroke(vg);
		}

		if (table.frames.size()>0) {
			nvgBeginPath(vg);
			for (size_t i=0; i<FS; i++) {
				float x3D, y3D, z3D, x2D, y2D;
				y3D = 10.0f * (module->index)/table.frames.size()-5.0f;
				x3D = 10.0f * i * invNbSample-5.0f;
				z3D = table.frames[module->index].sample[i];
				point2D(x3D, y3D, z3D, 15, 10, alpha1, alpha2, x2D, y2D);
				if (i == 0) {
					nvgMoveTo(vg, 10.0f * x2D, 10.0f * y2D);
				}
				else {
					nvgLineTo(vg, 10.0f * x2D, 10.0f * y2D);
				}
			}
			nvgStrokeColor(vg, GREEN_BIDOO);
			nvgStrokeWidth(vg, 1.0f);
			nvgStroke(vg);

			nvgBeginPath(vg);
			for (size_t i=0; i<FS; i++) {
				float x3D, y3D, z3D, x2D, y2D;
				y3D = 10.0f * (module->wtIndex)/table.frames.size()-5.0f;
				x3D = 10.0f * i * invNbSample-5.0f;
				z3D = table.frames[module->wtIndex].sample[i];
				point2D(x3D, y3D, z3D, 15, 10, alpha1, alpha2, x2D, y2D);
				if (i == 0) {
					nvgMoveTo(vg, 10.0f * x2D, 10.0f * y2D);
				}
				else {
					nvgLineTo(vg, 10.0f * x2D, 10.0f * y2D);
				}
			}
			nvgStrokeColor(vg, RED_BIDOO);
			nvgStrokeWidth(vg, 1.0f);
			nvgStroke(vg);
		}



		nvgRestore(vg);
 	}
};

LIMONADEWidget::LIMONADEWidget(LIMONADE *module) : ModuleWidget(module) {
	setPanel(SVG::load(assetPlugin(plugin, "res/LIMONADE.svg")));

	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

	{
		LIMONADEBinsDisplay *display = new LIMONADEBinsDisplay();
		display->module = module;
		display->box.pos = Vec(24, 50);
		display->box.size = Vec(500, 160);
		addChild(display);
	}

	{
		LIMONADEWavDisplay *display = new LIMONADEWavDisplay();
		display->module = module;
		display->box.pos = Vec(23, 220);
		display->box.size = Vec(150, 110);
		addChild(display);
	}

	addParam(ParamWidget::create<CKSS>(Vec(255, 240), module, LIMONADE::SYNC_PARAM, 0.0f, 1.0f, 1.0f));
	addParam(ParamWidget::create<BidooBlueKnob>(Vec(287, 235), module, LIMONADE::FREQ_PARAM, -54.0f, 54.0f, 0.0f));
	addParam(ParamWidget::create<BidooBlueKnob>(Vec(327, 235), module, LIMONADE::FINE_PARAM, -1.0f, 1.0f, 0.0f));
	addParam(ParamWidget::create<BidooBlueKnob>(Vec(367, 235), module, LIMONADE::FM_PARAM, 0.0f, 1.0f, 0.0f));


	addParam(ParamWidget::create<BidooBlueKnob>(Vec(445.0f, 42), module, LIMONADE::INDEX_PARAM, 0.0f, 10.0f, 0.0f));
	addParam(ParamWidget::create<BlueCKD6>(Vec(446.0f, 90), module, LIMONADE::COPYPASTE_PARAM, 0.0f, 1.0f, 0.0f));
	addChild(ModuleLightWidget::create<SmallLight<BlueLight>>(Vec(470.0f, 88.0f), module, LIMONADE::COPYPASTE_LIGHT));
	addParam(ParamWidget::create<BlueCKD6>(Vec(446.0f, 125), module, LIMONADE::ADDFRAME_PARAM, 0.0f, 1.0f, 0.0f));
	addParam(ParamWidget::create<BlueCKD6>(Vec(446.0f, 160), module, LIMONADE::DELETEFRAME_PARAM, 0.0f, 1.0f, 0.0f));
	addParam(ParamWidget::create<BlueCKD6>(Vec(446.0f, 195), module, LIMONADE::RESET_PARAM, 0.0f, 1.0f, 0.0f));

	addParam(ParamWidget::create<BidooBlueKnob>(Vec(407.0f, 235), module, LIMONADE::wtIndex_PARAM, 0.0f, 10.0f, 0.0f));
	addParam(ParamWidget::create<BidooBlueTrimpot>(Vec(412.0f, 275), module, LIMONADE::wtIndexATT_PARAM, -1.0f, 1, 0.0f));
	addInput(Port::create<PJ301MPort>(Vec(410, 300), Port::INPUT, module, LIMONADE::wtIndex_INPUT));

	addInput(Port::create<PJ301MPort>(Vec(250, 300), Port::INPUT, module, LIMONADE::SYNCMODE_INPUT));
	addInput(Port::create<PJ301MPort>(Vec(290, 300), Port::INPUT, module, LIMONADE::PITCH_INPUT));
	addInput(Port::create<PJ301MPort>(Vec(330, 300), Port::INPUT, module, LIMONADE::SYNC_INPUT));
	addInput(Port::create<PJ301MPort>(Vec(370, 300), Port::INPUT, module, LIMONADE::FM_INPUT));

	addOutput(Port::create<PJ301MPort>(Vec(447, 300), Port::OUTPUT, module, LIMONADE::OUT));
}

struct LIMONADELoadSample : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
		char *path = osdialog_file(OSDIALOG_OPEN, "", NULL, NULL);
		if (path) {
			limonadeModule->lastPath=path;
			limonadeModule->loadSample(path);
			free(path);
		}
	}
};

struct LIMONADEWindowSample : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->windowSample();
	}
};

struct LIMONADEFFTSample : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->fftSample(limonadeModule->index);
	}
};

struct LIMONADEIFFTSample : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->ifftSample(limonadeModule->index);
	}
};

struct LIMONADEMorphSample : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->morphSample();
	}
};

struct LIMONADEMorphSpectrum : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->morphSpectrum();
	}
};

struct LIMONADENormalizeFrame : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->normalizeFrame(limonadeModule->index);
	}
};

struct LIMONADENormalizeWt : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->normalizeWt();
	}
};

Menu *LIMONADEWidget::createContextMenu() {
	LIMONADE *limonadeModule = dynamic_cast<LIMONADE*>(module);
	assert(limonadeModule);
	Menu *menu = ModuleWidget::createContextMenu();

	LIMONADELoadSample *loadItem = new LIMONADELoadSample();
	loadItem->text = "Load sample";
	loadItem->limonadeModule = limonadeModule;
	menu->addChild(loadItem);

	LIMONADEWindowSample *windowItem = new LIMONADEWindowSample();
	windowItem->text = "Window sample";
	windowItem->limonadeModule = limonadeModule;
	menu->addChild(windowItem);

	LIMONADEFFTSample *fftItem = new LIMONADEFFTSample();
	fftItem->text = "FFT sample";
	fftItem->limonadeModule = limonadeModule;
	menu->addChild(fftItem);

	LIMONADEIFFTSample *ifftItem = new LIMONADEIFFTSample();
	ifftItem->text = "IFFT sample";
	ifftItem->limonadeModule = limonadeModule;
	menu->addChild(ifftItem);

	LIMONADEMorphSample *morphSItem = new LIMONADEMorphSample();
	morphSItem->text = "Morph sample";
	morphSItem->limonadeModule = limonadeModule;
	menu->addChild(morphSItem);

	LIMONADEMorphSpectrum *morphSpItem = new LIMONADEMorphSpectrum();
	morphSpItem->text = "Morph spectrum";
	morphSpItem->limonadeModule = limonadeModule;
	menu->addChild(morphSpItem);

	LIMONADENormalizeFrame *normFItem = new LIMONADENormalizeFrame();
	normFItem->text = "Normalize frame";
	normFItem->limonadeModule = limonadeModule;
	menu->addChild(normFItem);

	LIMONADENormalizeWt *normWtItem = new LIMONADENormalizeWt();
	normWtItem->text = "Normalize wavetable";
	normWtItem->limonadeModule = limonadeModule;
	menu->addChild(normWtItem);

	return menu;
}

Model *modelLIMONADE = Model::create<LIMONADE, LIMONADEWidget>("Bidoo","liMonADe", "liMonADe additive osc", OSCILLATOR_TAG);

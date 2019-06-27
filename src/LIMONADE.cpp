#include "plugin.hpp"
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
#include "dep/lodepng/lodepng.h"

using namespace std;

void tUpdateWaveTable(wtTable &table, float index) {
	size_t i = index*(table.nFrames-1);
	table.frames[i].calcWav();
}

void tLoadSample(wtTable &table, std::string path, size_t frameLen, bool interpolate) {
	unsigned int c;
  unsigned int sr;
  drwav_uint64 sc;
	float *pSampleData;
	float *sample;

  pSampleData = drwav_open_and_read_file_f32(path.c_str(), &c, &sr, &sc);
  if (pSampleData != NULL)  {
		sc = sc/c;
		sample = (float*)calloc(sc,sizeof(float));
		for (unsigned int i=0; i < sc; i++) {
			if (c == 1) sample[i] = pSampleData[i];
			else sample[i] = 0.5f*(pSampleData[2*i]+pSampleData[2*i+1]);
		}
		drwav_free(pSampleData);
		table.loadSample(sc, frameLen, interpolate, sample);
		free(sample);
	}
}

void tLoadISample(wtTable &table, float *iRec, size_t sc, size_t frameLen, bool interpolate) {
	table.loadSample(sc, frameLen, interpolate, iRec);
}

void tLoadIFrame(wtTable &table, float *iRec, float index, size_t frameLen, bool interpolate) {
	size_t i = index*(table.nFrames-1);
	if (i<table.nFrames) {
		table.frames[i].loadSample(frameLen, interpolate, iRec);
	}
	else if (table.nFrames==0) {
		table.addFrame(0);
		table.frames[0].loadSample(frameLen, interpolate, iRec);
	}
}

void tLoadFrame(wtTable &table, std::string path, float index, bool interpolate) {
	unsigned int c;
  unsigned int sr;
  drwav_uint64 sc;
	float *pSampleData;
	float *sample;

  pSampleData = drwav_open_and_read_file_f32(path.c_str(), &c, &sr, &sc);
  if (pSampleData != NULL)  {
		sc = sc/c;
		sample = (float*)calloc(sc,sizeof(float));
		for (unsigned int i=0; i < sc; i++) {
			if (c == 1) sample[i] = pSampleData[i];
			else sample[i] = 0.5f*(pSampleData[2*i]+pSampleData[2*i+1]);
		}
		drwav_free(pSampleData);
		size_t i = index*(table.nFrames-1);
		if (i<table.nFrames) {
			table.frames[i].loadSample(sc, interpolate, sample);
		}
		else if (table.nFrames==0) {
			table.addFrame(0);
			table.frames[0].loadSample(sc, interpolate, sample);
		}
		free(sample);
	}
}

void tLoadPNG(wtTable &table, std::string path) {
	std::vector<unsigned char> image;
	float *sample;
	unsigned width = 0;
	unsigned height = 0;
	size_t sc = 0;
	unsigned error = lodepng::decode(image, width, height, path, LCT_RGB);
	if(error != 0)
  {
    std::cout << "error " << error << ": " << lodepng_error_text(error) << std::endl;
	}
  else {
		sc = width*height;
		sample = (float*)calloc(width*height,sizeof(float));
		for(size_t i=0; i<height; i++) {
			for (size_t j=0; j<width; j++) {
				sample[i*width+j] = (0.299f*image[3*j + 3*(height-i-1)*width] + 0.587f*image[3*j + 3*(height-i-1)*width +1] +  0.114f*image[3*j + 3*(height-i-1)*width +2])/255.0f - 0.5f;
			}
		}
		table.loadSample(sc, width, true, sample);
		free(sample);
  }
}

void tWindowWt(wtTable &table) {
	table.window();
}

void tSmoothWt(wtTable &table) {
	table.smooth();
}

void tWindowFrame(wtTable &table, float index) {
	size_t i = index*(table.nFrames-1);
	table.windowFrame(i);
}

void tSmoothFrame(wtTable &table, float index) {
	size_t i = index*(table.nFrames-1);
	table.smoothFrame(i);
}

void tRemoveDCOffset(wtTable &table) {
	table.removeDCOffset();
}

void tNormalizeFrame(wtTable &table, float index) {
	size_t i = index*(table.nFrames-1);
	table.frames[i].normalize();
}

void tNormalizeWt(wtTable &table) {
	table.normalize();
}

void tNormalizeAllFrames(wtTable &table) {
	table.normalizeAllFrames();
}

void tFFTSample(wtTable &table, float index) {
	size_t i = index*(table.nFrames-1);
	table.frames[i].calcFFT();
}

void tIFFTSample(wtTable &table, float index) {
	size_t i = index*(table.nFrames-1);
	table.frames[i].calcIFFT();
}

void tMorphWaveTable(wtTable &table) {
	table.morphFrames();
}

void tMorphSpectrum(wtTable &table) {
	table.morphSpectrum();
}

void tMorphSpectrumConstantPhase(wtTable &table) {
	table.morphSpectrumConstantPhase();
}

void tAddFrame(wtTable &table, float index) {
	size_t i = index*(table.nFrames-1);
	table.addFrame(i);
}

void tDeleteFrame(wtTable &table, float index) {
	size_t i = index*(table.nFrames-1);
	table.removeFrame(i);
}

void tResetWaveTable(wtTable &table) {
	table.reset();
}

void tDeleteMorphing(wtTable &table) {
	table.deleteMorphing();
}

struct LIMONADE : Module {
	enum ParamIds {
		RESET_PARAM,
		SYNC_PARAM,
		FREQ_PARAM,
		FINE_PARAM,
		FM_PARAM,
		INDEX_PARAM,
		WTINDEX_PARAM,
		WTINDEXATT_PARAM,
		UNISSON_PARAM,
		UNISSONRANGE_PARAM,
		RECWT_PARAM,
		RECFRAME_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PITCH_INPUT,
		FM_INPUT = PITCH_INPUT+4,
		SYNC_INPUT,
		SYNCMODE_INPUT,
		WTINDEX_INPUT,
		IN,
		UNISSONRANGE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS = OUT+4
	};
	enum LightIds {
		RECWT_LIGHT,
		RECFRAME_LIGHT,
		NUM_LIGHTS
	};

	std::string lastPath;
	size_t frameSize = FS;
	int morphType = -1;
	bool recWt = false;
	bool recFrame = false;
	float *iRec;
	dsp::SchmittTrigger recTrigger;
	size_t recIndex=0;


	wtTable table;
	wtOscillator osc[3];

	LIMONADE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(INDEX_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(UNISSON_PARAM, 1.0f, 3.0f, 1.0f);
		configParam(FREQ_PARAM, -108.0f, 54.0f, 0.0f);
    configParam(FINE_PARAM, -1.0f, 1.0f, 0.0f);
    configParam(FM_PARAM, 0.0f, 1.0f, 0.0f);
    configParam(WTINDEX_PARAM, 0.0f, 1.0f, 0.0f);
    configParam(WTINDEXATT_PARAM, -1.0f, 1.0f, 0.0f);
    configParam(SYNC_PARAM, 0.0f, 1.0f, 1.0f);
    configParam(RECWT_PARAM, 0.0f, 10.0f, 0.0f);
    configParam(RECFRAME_PARAM, 0.0f, 10.0f, 0.0f);

		for(size_t i=0; i<3; i++) {
			osc[i].table = &table;
		}
		iRec=(float*)calloc(4*NF*FS,sizeof(float));
	}

  ~LIMONADE() {
		delete(iRec);
	}

	void process(const ProcessArgs &args) override;
	void syncThreadData();
	void loadSample();
	void loadFrame();
	void loadPNG();
	void windowWt();
	void smoothWt();
	void windowFrame();
	void smoothFrame();
	void removeDCOffset();
	void fftSample();
	void ifftSample();
	void morphWavetable();
	void morphSpectrum();
	void morphSpectrumConstantPhase();
	void deleteMorphing();
	void updateWaveTable();
	void addFrame();
	void deleteFrame();
	void resetWaveTable();
	void normalizeFrame();
	void normalizeAllFrames();
	void normalizeWt();

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_t *framesJ = json_array();
		size_t nFrames = 0;
		for (size_t i=0; i<table.nFrames; i++) {
			if (!table.frames[i].morphed) {
				json_t *frameI = json_array();
				for (size_t j=0; j<FS; j++) {
					json_t *frameJ = json_real(table.frames[i].sample[j]);
					json_array_append_new(frameI, frameJ);
				}
				json_array_append_new(framesJ, frameI);
				nFrames++;
			}
		}
		json_object_set_new(rootJ, "nFrames", json_integer(nFrames));
		json_object_set_new(rootJ, "morphType", json_integer(morphType));
		json_object_set_new(rootJ, "frames", framesJ);
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {

		size_t nFrames = 0;
		json_t *nFramesJ = json_object_get(rootJ, "nFrames");
		if (nFramesJ)
			nFrames = json_integer_value(nFramesJ);

		json_t *morphTypeJ = json_object_get(rootJ, "morphType");
		if (morphTypeJ)
			morphType = json_integer_value(morphTypeJ);

		if (nFrames>0)
		{
			float *wav = (float*)calloc(nFrames*FS, sizeof(float));
			json_t *framesJ = json_object_get(rootJ, "frames");
			for (size_t i = 0; i < nFrames; i++) {
				json_t *frameJ = json_array_get(framesJ, i);
				for (size_t j=0; j<FS; j++) {
					wav[i*FS+j] = json_number_value(json_array_get(frameJ, j));
				}
			}
			table.loadSample(nFrames*FS, FS, false, wav);
			if (morphType==0) {
				morphWavetable();
			}
			else if (morphType==1) {
				morphSpectrum();
			}
			else if (morphType==2) {
				morphSpectrumConstantPhase();
			}
			delete(wav);
		}
	}

	void onRandomize() override {

	}

	void onReset() override {
		table.reset();
		lastPath = "";
	}
};

inline void LIMONADE::updateWaveTable() {
	tUpdateWaveTable(table, params[INDEX_PARAM].value);
	// thread t = thread(tUpdateWaveTable, std::ref(table), params[INDEX_PARAM].value);
	// t.detach();
}

inline void LIMONADE::fftSample() {
	tFFTSample(table, params[INDEX_PARAM].value);
	// thread t = thread(tFFTSample, std::ref(table), params[INDEX_PARAM].value);
	// t.detach();
}

inline void LIMONADE::ifftSample() {
	tIFFTSample(table, params[INDEX_PARAM].value);
	// thread t = thread(tIFFTSample, std::ref(table), params[INDEX_PARAM].value);
	// t.detach();
}

inline void LIMONADE::morphWavetable() {
	//thread t = thread(tMorphWaveTable, std::ref(table));
	morphType = 0;
	tMorphWaveTable(table);
	//t.detach();
}

inline void LIMONADE::morphSpectrum() {
	//thread t = thread(tMorphSpectrum, std::ref(table));
	morphType = 1;
	tMorphSpectrum(table);
	//t.detach();
}

inline void LIMONADE::morphSpectrumConstantPhase() {
	//thread t = thread(tMorphSpectrumConstantPhase, std::ref(table));
	morphType = 2;
	tMorphSpectrumConstantPhase(table);
	//t.detach();
}

inline void LIMONADE::deleteMorphing() {
	//thread t = thread(tDeleteMorphing, std::ref(table));
	morphType = -1;
	tDeleteMorphing(table);
	//t.detach();
}

void LIMONADE::addFrame() {
	thread t = thread(tAddFrame, std::ref(table), params[INDEX_PARAM].value);
	t.detach();
}

void LIMONADE::deleteFrame() {
	tDeleteFrame(table, params[INDEX_PARAM].value);
	// thread t = thread(tDeleteFrame, std::ref(table), params[INDEX_PARAM].value);
	// t.detach();
}

void LIMONADE::resetWaveTable() {
	tResetWaveTable(table);
	// thread t = thread(tResetWaveTable, std::ref(table));
	// t.detach();
}

void LIMONADE::loadSample() {
	char *path = osdialog_file(OSDIALOG_OPEN, "", NULL, NULL);
	if (path) {
		lastPath=path;
		tLoadSample(table, path, frameSize, true);
		// thread t = thread(tLoadSample, std::ref(table), path, frameSize, true);
		// t.detach();
		free(path);
	}
}

void LIMONADE::loadFrame() {
	char *path = osdialog_file(OSDIALOG_OPEN, "", NULL, NULL);
	if (path) {
		lastPath=path;
		tLoadFrame(table, path, params[INDEX_PARAM].value, true);
		// thread t = thread(tLoadFrame, std::ref(table), path, params[INDEX_PARAM].value, true);
		// t.detach();
		free(path);
	}
}

void LIMONADE::loadPNG() {
	char *path = osdialog_file(OSDIALOG_OPEN, "", NULL, NULL);
	if (path) {
		lastPath=path;
		tLoadPNG(table, path);
		// thread t = thread(tLoadPNG, std::ref(table), path);
		// t.detach();
		free(path);
	}
}

void LIMONADE::windowWt() {
	tWindowWt(table);
	// thread t = thread(tWindowWt, std::ref(table));
	// t.detach();
}

void LIMONADE::smoothWt() {
	tSmoothWt(table);
	// thread t = thread(tSmoothWt, std::ref(table));
	// t.detach();
}

void LIMONADE::windowFrame() {
	tWindowFrame(table, params[INDEX_PARAM].value);
	// thread t = thread(tWindowFrame, std::ref(table), params[INDEX_PARAM].value);
	// t.detach();
}

void LIMONADE::smoothFrame() {
	tSmoothFrame(table, params[INDEX_PARAM].value);
	// thread t = thread(tSmoothFrame, std::ref(table), params[INDEX_PARAM].value);
	// t.detach();
}

void LIMONADE::removeDCOffset() {
	tRemoveDCOffset(table);
	// thread t = thread(tRemoveDCOffset, std::ref(table));
	// t.detach();
}


void LIMONADE::normalizeFrame() {
	tNormalizeFrame(table, params[INDEX_PARAM].value);
	// thread t = thread(tNormalizeFrame, std::ref(table), params[INDEX_PARAM].value);
	// t.detach();
}

void LIMONADE::normalizeWt() {
	tNormalizeWt(table);
	// thread t = thread(tNormalizeWt, std::ref(table));
	// t.detach();
}

void LIMONADE::normalizeAllFrames() {
	tNormalizeAllFrames(table);
	// thread t = thread(tNormalizeAllFrames, std::ref(table));
	// t.detach();
}

void LIMONADE::process(const ProcessArgs &args) {
	if (recTrigger.process(params[RECWT_PARAM].value) && !recWt && !recFrame) {
		recWt = true;
		recIndex=0;
		lights[RECWT_LIGHT].value=10.0f;
	}

	if (recTrigger.process(params[RECFRAME_PARAM].value) && !recWt && !recFrame) {
		recFrame = true;
		recIndex=0;
		lights[RECFRAME_LIGHT].value=10.0f;
	}

	if (recWt || recFrame) {
		iRec[recIndex]=inputs[IN].value/10.0f;
		recIndex++;

		if (recWt && (recIndex==frameSize*NF)) {
			thread t = thread(tLoadISample, std::ref(table), std::ref(iRec), frameSize*NF, frameSize, true);
			t.detach();
			recWt = false;
			recIndex = 0;
			lights[RECWT_LIGHT].value=00.0f;
		}
		else if (recFrame && (recIndex==frameSize)) {
			thread t = thread(tLoadIFrame, std::ref(table), std::ref(iRec), params[INDEX_PARAM].value, frameSize, true);
			t.detach();
			recFrame = false;
			recIndex = 0;
			lights[RECFRAME_LIGHT].value=0.0f;
		}
	}

	float pitchCv = 12.0f * inputs[PITCH_INPUT].value;
	float pitchFine = 3.0f * dsp::quadraticBipolar(params[FINE_PARAM].value);
	if (inputs[FM_INPUT].active) {
		pitchCv += dsp::quadraticBipolar(params[FM_PARAM].value) * 12.0f * inputs[FM_INPUT].value;
	}
	float idx=clamp(params[WTINDEX_PARAM].value+inputs[WTINDEX_INPUT].value*0.1f*params[WTINDEXATT_PARAM].value,0.0f,1.0f);
	outputs[OUT].value = 0.0f;

	float ur = clamp(params[UNISSONRANGE_PARAM].value + inputs[UNISSONRANGE_INPUT].value / 10.0f,0.0f,0.1f);
	for (size_t i=0; i<(size_t)params[UNISSON_PARAM].value; i++) {
		float pFC = pitchFine;
		if (params[UNISSON_PARAM].value>2) {
			if (i==1) {	pFC += ur;} else if (i==2) { pFC -= ur;}
		}
		else if (params[UNISSON_PARAM].value>1) {
			i==0 ? pFC+=ur : pFC-=ur;
		}
		osc[i].soft = (params[SYNC_PARAM].value + inputs[SYNCMODE_INPUT].value) <= 0.0f;
		osc[i].setPitch(params[FREQ_PARAM].value, pFC + pitchCv);
		osc[i].syncEnabled = inputs[SYNC_INPUT].active;
		osc[i].prepare(args.sampleTime, inputs[SYNC_INPUT].value);
	}

	for(size_t i=0; i<(size_t)params[UNISSON_PARAM].value; i++) {
		osc[i].updateBuffer(idx);
	}

	for(size_t i=0; i<(size_t)params[UNISSON_PARAM].value; i++) {
			outputs[OUT].value += osc[i].out();
	}

	outputs[OUT].value *= 3.0 / sqrt(params[UNISSON_PARAM].value);
}

struct LIMONADEBinsDisplay : OpaqueWidget {
	LIMONADE *module;
	shared_ptr<Font> font;
	const float width = 420.0f;
	const float heightMagn = 70.0f;
	const float heightPhas = 50.0f;
	const float graphGap = 30.0f;
	float zoomWidth = width * 28.0f;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refY = 0.0f;
	float refX = 0.0f;
	bool write = false;
	float scrollLeftAnchor = 0.0f;
	bool scroll = false;
	float globalZoom = 1.0f;

	LIMONADEBinsDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void onButton(const event::Button &e) override {
		refX = e.pos.x;
		refY = e.pos.y;
		refIdx = ((e.pos.x - zoomLeftAnchor)/zoomWidth)*(float)FS2;
		if (refY<(heightMagn + heightPhas + graphGap)) {
			scroll = false;
		}
		else {
			if ((refX>scrollLeftAnchor) && (refX<scrollLeftAnchor+20)) scroll = true;
		}
		OpaqueWidget::onButton(e);
	}

  void onDragStart(const event::DragStart &e) override {
		appGet()->window->cursorLock();
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(const event::DragMove &e) override {
		if ((!scroll) && (module->table.nFrames>0)) {
			size_t i = module->params[LIMONADE::INDEX_PARAM].value*(module->table.nFrames-1);
			if (refY<=heightMagn) {
				if ((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_CONTROL)) {
					module->table.frames[i].magnitude[refIdx] = 0.0f;
				}
				else {
					module->table.frames[i].magnitude[refIdx] -= e.mouseDelta.y/(250/appGet()->scene->rackScroll->zoomWidget->zoom);
					module->table.frames[i].magnitude[refIdx] = clamp(module->table.frames[i].magnitude[refIdx],0.0f, 1.0f);
				}
			}
			else if (refY>=heightMagn+graphGap) {
				if ((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_CONTROL)) {
					module->table.frames[i].phase[refIdx] = 0.0f;
				}
				else {
					module->table.frames[i].phase[refIdx] -= e.mouseDelta.y / (250 / appGet()->scene->rackScroll->zoomWidget->zoom);
					module->table.frames[i].phase[refIdx] = clamp(module->table.frames[i].phase[refIdx],-1.0f*M_PI, M_PI);
				}
			}
			module->table.frames[i].morphed = false;
			module->updateWaveTable();
		}
		else {
				scrollLeftAnchor = clamp(scrollLeftAnchor + e.mouseDelta.x / appGet()->scene->rackScroll->zoomWidget->zoom, 0.0f,width-20.0f);
				zoomLeftAnchor = rescale(scrollLeftAnchor, 0.0f, width-20.0f, 0.0f, width - zoomWidth);
		}
		OpaqueWidget::onDragMove(e);
	}

  void onDragEnd(const event::DragEnd &e) override {
    appGet()->window->cursorUnlock();
    OpaqueWidget::onDragEnd(e);
  }

	void draw(const DrawArgs &args) override {
    if (module) {
      wtFrame frame;
  		size_t fs = 0;
  		size_t idx = 0;
  		size_t tag=1;

  		fs = module->table.nFrames;
  		if (fs>0) {
  			idx = module->params[LIMONADE::INDEX_PARAM].value*(fs-1);
  			frame.magnitude = module->table.frames[idx].magnitude;
  			frame.phase = module->table.frames[idx].phase;
  		}

  		nvgSave(args.vg);
  		Rect b = Rect(Vec(zoomLeftAnchor, 0), Vec(zoomWidth, heightMagn + graphGap + heightPhas));
  		nvgScissor(args.vg, 0, b.pos.y, width, heightMagn + graphGap + heightPhas+12);

  		nvgBeginPath(args.vg);
  		nvgRoundedRect(args.vg, 0, heightMagn + heightPhas + graphGap + 4, width, 8, 2);
  		nvgFillColor(args.vg, nvgRGBA(220,220,220,80));
  		nvgFill(args.vg);

  		nvgBeginPath(args.vg);
  		nvgRoundedRect(args.vg, scrollLeftAnchor, heightMagn + heightPhas + graphGap + 4, 20, 8, 2);
  		nvgFillColor(args.vg, nvgRGBA(220,220,220,255));
  		nvgFill(args.vg);

  		nvgFontSize(args.vg, 16.0f);
  		nvgFontFaceId(args.vg, font->handle);
  		nvgTextLetterSpacing(args.vg, -2.0f);
  		nvgFillColor(args.vg, YELLOW_BIDOO);

  		nvgText(args.vg, 130.0f, heightMagn + graphGap/2+4, "▲ Magnitude ▼ Phase", NULL);

  		if (fs>0) {
  			nvgText(args.vg, 0.0f, heightMagn + graphGap/2+4, ("Frame " + to_string(idx+1) + " / " + to_string(fs)).c_str(), NULL);
  			for (size_t i = 0; i < FS2; i++) {
  				float x, y;
  				x = (float)i * IFS2;
  				y = frame.magnitude[i];
  				Vec p;
  				p.x = b.pos.x + b.size.x * x;
  				p.y = heightMagn * y;

  				if (i==tag){
  					nvgBeginPath(args.vg);
  					nvgFillColor(args.vg, nvgRGBA(45, 114, 143, 100));
  					nvgRect(args.vg, p.x, 0, b.size.x * IFS2, heightMagn);
  					nvgRect(args.vg, p.x, heightMagn + graphGap, b.size.x * IFS2, heightPhas);
  					nvgLineCap(args.vg, NVG_MITER);
  					nvgStrokeWidth(args.vg, 0);
  					nvgFill(args.vg);
  					tag *=2;
  				}

  				if (p.x < width) {
  					nvgStrokeColor(args.vg, YELLOW_BIDOO);
  					nvgFillColor(args.vg, YELLOW_BIDOO);
  					nvgLineCap(args.vg, NVG_MITER);
  					nvgStrokeWidth(args.vg, 1);
  					nvgBeginPath(args.vg);
  					nvgRect(args.vg, p.x, heightMagn - p.y, b.size.x * IFS2, p.y);
  					y = module->table.frames[idx].phase[i]*IM_PI;
  					p.y = heightPhas * 0.5f * y;
  					nvgRect(args.vg, p.x, heightMagn + graphGap + heightPhas * 0.5f - p.y, b.size.x * IFS2, p.y);
  					nvgStroke(args.vg);
  					nvgFill(args.vg);
  				}
  			}
  		}

  		nvgResetScissor(args.vg);
  		nvgRestore(args.vg);
    }
	}
};

struct LIMONADEWavDisplay : OpaqueWidget {
	LIMONADE *module;
	shared_ptr<Font> font;
	const float width = 130.0f;
	const float height = 130.0f;
	int refIdx = 0;
	float alpha1 = 25.0f;
	float alpha2 = 35.0f;
	float a1 = alpha1 * M_PI/180.0f;
	float a2 = alpha2 * M_PI/180.0f;
	float ca1 = cos(a1);
	float sa1 = sin(a1);
	float ca2 = cos(a2);
	float sa2 = sin(a2);
	float x3D, y3D, z3D, x2D, y2D;

	LIMONADEWavDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void onButton(const event::Button &e) override {
			OpaqueWidget::onButton(e);
	}

	void onDragStart(const event::DragStart &e) override {
		appGet()->window->cursorLock();
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(const event::DragMove &e) override {
		alpha1+=e.mouseDelta.y;
		alpha2-=e.mouseDelta.x;
		if (alpha1>90) alpha1=90;
		if (alpha1<-90) alpha1=-90;
		if (alpha2>360) alpha2-=360;
		if (alpha2<0) alpha2+=360;

		a1 = alpha1 * M_PI/180.0f;
		a2 = alpha2 * M_PI/180.0f;
		ca1 = cos(a1);
		sa1 = sin(a1);
		ca2 = cos(a2);
		sa2 = sin(a2);
		OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const event::DragEnd &e) override {
		appGet()->window->cursorUnlock();
		OpaqueWidget::onDragEnd(e);
	}

	void draw(const DrawArgs &args) override {
    if (module) {
      size_t fs = module->table.nFrames;
  		size_t idx = 0;
  		size_t wtidx = 0;
  		if (fs>0) {
  			idx = module->params[LIMONADE::INDEX_PARAM].value*(fs-1);
  			wtidx = clamp(module->params[LIMONADE::WTINDEX_PARAM].value+module->inputs[LIMONADE::WTINDEX_INPUT].value*0.1f*module->params[LIMONADE::WTINDEXATT_PARAM].value,0.0f,1.0f)*(fs-1);
  		}

  		nvgSave(args.vg);

  		nvgFontSize(args.vg, 8.0f);
  		nvgFontFaceId(args.vg, font->handle);
  		nvgTextLetterSpacing(args.vg, -2.0f);
  		nvgFillColor(args.vg, YELLOW_BIDOO);

  		nvgText(args.vg, width+6, height-10, ("V=" + to_string((int)module->params[LIMONADE::UNISSON_PARAM].value)).c_str(), NULL);


  		for (size_t n=0; n<fs; n++) {
  			nvgBeginPath(args.vg);
  			for (size_t i=0; i<FS; i++) {
  				y3D = 10.0f * (fs-n-1)/fs-5.0f;
  				x3D = 10.0f * i * IFS -5.0f;
  				z3D = module->table.frames[fs-1-n].sample[i];
  				y2D = z3D*ca1-(ca2*y3D-sa2*x3D)*sa1+5.0f;
  				x2D = ca2*x3D+sa2*y3D+7.5f;
  				if (i == 0) {
  					nvgMoveTo(args.vg, 10.0f * x2D, 10.0f * y2D);
  				}
  				else {
  					nvgLineTo(args.vg, 10.0f * x2D, 10.0f * y2D);
  				}
  			}
  			if (module->table.frames[fs-1-n].morphed) {
  				nvgStrokeColor(args.vg, nvgRGBA(255, 233, 0, 15));
  				nvgStrokeWidth(args.vg, 1.0f);
  			}
  			else {
  				nvgStrokeColor(args.vg, nvgRGBA(255, 233, 0, 50));
  				nvgStrokeWidth(args.vg, 1.0f);
  			}
  			nvgStroke(args.vg);
  		}

  		if (fs>0) {
  			nvgBeginPath(args.vg);
  			for (size_t i=0; i<FS; i++) {
  				float x3D, y3D, z3D, x2D, y2D;
  				y3D = 10.0f * idx/fs -5.0f;
  				x3D = 10.0f * i * IFS -5.0f;
  				z3D = module->table.frames[idx].sample[i];
  				y2D = z3D*ca1-(ca2*y3D-sa2*x3D)*sa1+5.0f;
  				x2D = ca2*x3D+sa2*y3D+7.5f;
  				if (i == 0) {
  					nvgMoveTo(args.vg, 10.0f * x2D, 10.0f * y2D);
  				}
  				else {
  					nvgLineTo(args.vg, 10.0f * x2D, 10.0f * y2D);
  				}
  			}
  			nvgStrokeColor(args.vg, GREEN_BIDOO);
  			nvgStrokeWidth(args.vg, 1.0f);
  			nvgStroke(args.vg);

  			nvgBeginPath(args.vg);
  			for (size_t i=0; i<FS; i++) {
  				float x3D, y3D, z3D, x2D, y2D;
  				y3D = 10.0f * wtidx/fs -5.0f;
  				x3D = 10.0f * i * IFS -5.0f;
  				z3D = module->table.frames[wtidx].sample[i];
  				y2D = z3D*ca1-(ca2*y3D-sa2*x3D)*sa1+5.0f;
  				x2D = ca2*x3D+sa2*y3D+7.5f;
  				if (i == 0) {
  					nvgMoveTo(args.vg, 10.0f * x2D, 10.0f * y2D);
  				}
  				else {
  					nvgLineTo(args.vg, 10.0f * x2D, 10.0f * y2D);
  				}
  			}
  			nvgStrokeColor(args.vg, RED_BIDOO);
  			nvgStrokeWidth(args.vg, 1.0f);
  			nvgStroke(args.vg);
  		}

  		nvgRestore(args.vg);
    }
 	}
};

struct LIMONADETextField : LedDisplayTextField {
  LIMONADETextField(LIMONADE *mod) {
    module = mod;
    font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
  	color = YELLOW_BIDOO;
  	textOffset = Vec(2,0);
    text = "2048";
  }
	void onButton(const event::Button &e) override;
	LIMONADE *module;
};

void LIMONADETextField::onButton(const event::Button &e) {
	if (text.size() > 0) {
      std::string tText = text;
      tText.erase(std::remove_if(tText.begin(), tText.end(), [](unsigned char x){return std::isspace(x);}), tText.end());
      module->frameSize = std::stoi(tText);
	}
  LedDisplayTextField::onButton(e);
}

struct LimonadeBlueBtnLoadSample : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->loadSample();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnLoadPNG : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->loadPNG();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnLoadFrame : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->loadFrame();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnMorphWavetable : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->morphWavetable();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnMorphSpectrum : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->morphSpectrum();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnMorphSpectrumConstantPhase : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->morphSpectrumConstantPhase();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnDeleteMorphing : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->deleteMorphing();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnNormalizeAllFrames : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->normalizeAllFrames();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnRemoveDCOffset : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->removeDCOffset();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnNormalizeWt : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->normalizeWt();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnNormalizeFrame : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->normalizeFrame();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnWindowWt : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->windowWt();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnSmoothWt : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->smoothWt();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnWindowFrame : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->windowFrame();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnSmoothFrame : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->smoothFrame();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnAddFrame : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->addFrame();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnDeleteFrame : BlueBtn {
  LIMONADE *module;
	virtual void onButton(const event::Button &e) override {
		dynamic_cast<LIMONADE*>(this->module)->deleteFrame();
		BlueBtn::onButton(e);
	}
};

struct LIMONADEWidget : ModuleWidget {
	LIMONADEWidget(LIMONADE *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LIMONADE.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

  	{
  		LIMONADEBinsDisplay *display = new LIMONADEBinsDisplay();
  		display->module = module;
  		display->box.pos = Vec(15, 33);
  		display->box.size = Vec(500, 164);
  		addChild(display);
  	}

  	{
  		LIMONADEWavDisplay *display = new LIMONADEWavDisplay();
  		display->module = module;
  		display->box.pos = Vec(11, 235);
  		display->box.size = Vec(150, 110);
  		addChild(display);
  	}

  	{
  		LIMONADETextField *textField = new LIMONADETextField(module);
  		textField->box.pos = Vec(170, 208);
  		textField->box.size = Vec(38, 19);
  		textField->multiline = false;
  		addChild(textField);
  	}

  	{
  		LimonadeBlueBtnLoadSample *btn = new LimonadeBlueBtnLoadSample();
  		btn->box.pos = Vec(170, 240);
  		btn->caption = "≈";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnLoadPNG *btn = new LimonadeBlueBtnLoadPNG();
  		btn->box.pos = Vec(190, 240);
  		btn->caption = "☺";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnLoadFrame *btn = new LimonadeBlueBtnLoadFrame();
  		btn->box.pos = Vec(210, 240);
  		btn->caption = "~";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnMorphWavetable *btn = new LimonadeBlueBtnMorphWavetable();
  		btn->box.pos = Vec(170, 260);
  		btn->caption = "≡";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnMorphSpectrum *btn = new LimonadeBlueBtnMorphSpectrum();
  		btn->box.pos = Vec(190, 260);
  		btn->caption = "∞";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnMorphSpectrumConstantPhase *btn = new LimonadeBlueBtnMorphSpectrumConstantPhase();
  		btn->box.pos = Vec(210, 260);
  		btn->caption = "Փ";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnDeleteMorphing *btn = new LimonadeBlueBtnDeleteMorphing();
  		btn->box.pos = Vec(230, 260);
  		btn->caption = "Ø";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnNormalizeAllFrames *btn = new LimonadeBlueBtnNormalizeAllFrames();
  		btn->box.pos = Vec(170, 280);
  		btn->caption = "↕";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnRemoveDCOffset *btn = new LimonadeBlueBtnRemoveDCOffset();
  		btn->box.pos = Vec(190, 280);
  		btn->caption = "↨";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnNormalizeWt *btn = new LimonadeBlueBtnNormalizeWt();
  		btn->box.pos = Vec(210, 280);
  		btn->caption = "↕≈";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnNormalizeFrame *btn = new LimonadeBlueBtnNormalizeFrame();
  		btn->box.pos = Vec(230, 280);
  		btn->caption = "↕~";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnWindowWt *btn = new LimonadeBlueBtnWindowWt();
  		btn->box.pos = Vec(170, 300);
  		btn->caption = "∩≈";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnSmoothWt *btn = new LimonadeBlueBtnSmoothWt();
  		btn->box.pos = Vec(190, 300);
  		btn->caption = "ƒ≈";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnWindowFrame *btn = new LimonadeBlueBtnWindowFrame();
  		btn->box.pos = Vec(210, 300);
  		btn->caption = "∩~";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnSmoothFrame *btn = new LimonadeBlueBtnSmoothFrame();
  		btn->box.pos = Vec(230, 300);
  		btn->caption = "ƒ~";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnAddFrame *btn = new LimonadeBlueBtnAddFrame();
  		btn->box.pos = Vec(170, 320);
  		btn->caption = "+";
  		btn->module = module;
  		addChild(btn);
  	}
  	{
  		LimonadeBlueBtnDeleteFrame *btn = new LimonadeBlueBtnDeleteFrame();
  		btn->box.pos = Vec(190, 320);
  		btn->caption = "-";
  		btn->module = module;
  		addChild(btn);
  	}

  	addParam(createParam<BidooGreenKnob>(Vec(230.0f, 208.0f), module, LIMONADE::INDEX_PARAM));

  	addParam(createParam<BidooBlueKnob>(Vec(274.0f, 219.0f), module, LIMONADE::UNISSON_PARAM));
  	addParam(createParam<BidooBlueKnob>(Vec(274.0f, 262.0f), module, LIMONADE::UNISSONRANGE_PARAM));
  	addParam(createParam<BidooBlueKnob>(Vec(309, 219), module, LIMONADE::FREQ_PARAM));
  	addParam(createParam<BidooBlueKnob>(Vec(344, 219), module, LIMONADE::FINE_PARAM));
  	addParam(createParam<BidooBlueKnob>(Vec(379, 219), module, LIMONADE::FM_PARAM));
  	addParam(createParam<BidooRedKnob>(Vec(414.0f, 219), module, LIMONADE::WTINDEX_PARAM));
  	addParam(createParam<BidooBlueTrimpot>(Vec(419.0f, 265), module, LIMONADE::WTINDEXATT_PARAM));

  	addParam(createParam<CKSS>(Vec(352, 336), module, LIMONADE::SYNC_PARAM));
  	addInput(createInput<TinyPJ301MPort>(Vec(317, 339), module, LIMONADE::SYNCMODE_INPUT));

  	{
  		RedBtn *btn = createParam<RedBtn>(Vec(254.0f, 324), module, LIMONADE::RECWT_PARAM);
  		btn->caption = "≈";
  		addParam(btn);
  	}
  	addChild(createLight<SmallLight<RedLight>>(Vec(244.0f, 329), module, LIMONADE::RECWT_LIGHT));

  	{
  		RedBtn *btn = createParam<RedBtn>(Vec(254.0f, 344), module, LIMONADE::RECFRAME_PARAM);
  		btn->caption = "~";
  		addParam(btn);
  	}
  	addChild(createLight<SmallLight<RedLight>>(Vec(244.0f, 349), module, LIMONADE::RECFRAME_LIGHT));

  	addInput(createInput<PJ301MPort>(Vec(277, 295), module, LIMONADE::UNISSONRANGE_INPUT));
  	addInput(createInput<PJ301MPort>(Vec(312, 295), module, LIMONADE::PITCH_INPUT));
  	addInput(createInput<PJ301MPort>(Vec(347, 295), module, LIMONADE::SYNC_INPUT));
  	addInput(createInput<PJ301MPort>(Vec(382, 295), module, LIMONADE::FM_INPUT));
  	addInput(createInput<PJ301MPort>(Vec(417, 295), module, LIMONADE::WTINDEX_INPUT));

  	addInput(createInput<PJ301MPort>(Vec(277, 335), module, LIMONADE::IN));
  	addOutput(createOutput<PJ301MPort>(Vec(417, 335), module, LIMONADE::OUT));
  }
};

Model *modelLIMONADE = createModel<LIMONADE, LIMONADEWidget>("liMonADe");

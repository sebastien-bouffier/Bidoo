#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
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
#include "dep/AudioFile/AudioFile.h"

using namespace std;

static const char WAV_FILTERS[] = "wav:wav";
static const char PNG_FILTERS[] = "png:png";

void tUpdateWaveTable(wtTable &table, float index) {
	size_t i = index*(table.nFrames-1);
	table.frames[i].calcWav();
}

void tSaveWaveTableAsWave(wtTable &table, int sampleRate, std::string path) {
	drwav_data_format format;
	format.container = drwav_container_riff;
	format.format = DR_WAVE_FORMAT_PCM;
	format.channels = 1;
	format.sampleRate = sampleRate;
	format.bitsPerSample = 32;

	int *pSamples = (int*)calloc(NF*FS,sizeof(int));
	memset(pSamples, 0, NF*FS*sizeof(int));
	for (unsigned int i = 0; i < NF; i++) {
		for (unsigned int j = 0; j < FS; j++) {
			*(pSamples+i*FS+j)=floor(table.frames[i].sample[j]*1990000000);
		}
	}

	drwav wav;
	drwav_init_file_write(&wav, path.c_str(), &format, NULL);
	drwav_uint64 framesWritten = drwav_write_pcm_frames(&wav, table.frames.size()*table.frames[0].sample.size(), pSamples);
	drwav_uninit(&wav);

	free(pSamples);
}

void tSaveFrameAsWave(wtTable &table, int sampleRate, std::string path, int frameIndex) {
	drwav_data_format format;
	format.container = drwav_container_riff;
	format.format = DR_WAVE_FORMAT_PCM;
	format.channels = 1;
	format.sampleRate = sampleRate;
	format.bitsPerSample = 32;

	int *pSamples = (int*)calloc(FS,sizeof(int));
	memset(pSamples, 0, FS*sizeof(int));
	for (unsigned int i = 0; i < FS; i++) {
		*(pSamples+i)= floor(table.frames[frameIndex].sample[i]*1990000000);
	}

	drwav wav;
	drwav_init_file_write(&wav, path.c_str(), &format, NULL);
	drwav_uint64 framesWritten = drwav_write_pcm_frames(&wav, table.frames[frameIndex].sample.size(), pSamples);
	drwav_uninit(&wav);

	free(pSamples);
}

void tSaveWaveTableAsPng(wtTable &table, int sampleRate, std::string path) {
	unsigned width = FS;
	unsigned height = NF;
	std::vector<unsigned char> image;
	for(size_t i=0; i<NF; i++) {
		for (size_t j=0; j<FS; j++) {
			image.push_back(floor(table.frames[i].sample[j]*1000000000) + 1000000000);
			image.push_back(floor(table.frames[i].sample[j]*1000000000) + 1000000000);
			image.push_back(floor(table.frames[i].sample[j]*1000000000) + 1000000000);
			image.push_back(floor(table.frames[i].sample[j]*1000000000) + 1000000000);
		}
	}
	unsigned error = lodepng::encode(path, image, width, height);
	if(error != 0)
  {
    std::cout << "error " << error << ": " << lodepng_error_text(error) << std::endl;
	}
}

void tLoadSample(wtTable &table, std::string path, size_t frameLen, bool interpolate) {
	std::string waveExtension = rack::system::getExtension(rack::system::getFilename(path));
	if (waveExtension == ".wav") {
		unsigned int c;
	  unsigned int sr;
	  drwav_uint64 sc;
		float *pSampleData;
		float *sample;
	  pSampleData = drwav_open_file_and_read_pcm_frames_f32(path.c_str(), &c, &sr, &sc, NULL);
	  if (pSampleData != NULL)  {
			sc = sc/c;
			sample = (float*)calloc(sc,sizeof(float));
			for (unsigned int i=0; i < sc; i++) {
				if (c == 1) sample[i] = pSampleData[i];
				else sample[i] = 0.5f*(pSampleData[2*i]+pSampleData[2*i+1]);
			}
			drwav_free(pSampleData, NULL);
			table.loadSample(sc, frameLen, interpolate, sample);
			free(sample);
			table.calcFFT();
		}
	}
	else if (waveExtension == ".aiff") {
		float *sample;
		AudioFile<float> audioFile;
		if (audioFile.load(path.c_str()))  {
			sample = (float*)calloc(audioFile.getNumSamplesPerChannel(),sizeof(float));
			for (int i=0; i < audioFile.getNumSamplesPerChannel(); i++) {
				if (audioFile.isMono()) {
					sample[i] = audioFile.samples[0][i];
				}
				else {
					sample[i] = 0.5f*(audioFile.samples[0][i]+audioFile.samples[1][i]);
				}
			}
			table.loadSample(audioFile.getNumSamplesPerChannel(), frameLen, interpolate, sample);
			free(sample);
			table.calcFFT();
		}
	}
}

void tLoadISample(wtTable &table, float *iRec, size_t sc, size_t frameLen, bool interpolate) {
	table.loadSample(sc, frameLen, interpolate, iRec);
	table.calcFFT();
}

void tLoadIFrame(wtTable &table, float *iRec, float index, size_t frameLen, bool interpolate) {
	size_t i = index*(table.nFrames-1);
	if (i<table.nFrames) {
		table.frames[i].loadSample(frameLen, interpolate, iRec);
	}
	else if (table.nFrames==0) {
		table.addFrame(0);
		table.frames[0].loadSample(frameLen, interpolate, iRec);
		table.calcFFT();
	}
}

void tLoadFrame(wtTable &table, std::string path, float index, bool interpolate) {
	std::string waveExtension = rack::system::getExtension(rack::system::getFilename(path));
	if (waveExtension == ".wav") {
		unsigned int c;
	  unsigned int sr;
	  drwav_uint64 sc;
		float *pSampleData;
		float *sample;
	  pSampleData = drwav_open_file_and_read_pcm_frames_f32(path.c_str(), &c, &sr, &sc, NULL);
	  if (pSampleData != NULL)  {
			sc = sc/c;
			sample = (float*)calloc(sc,sizeof(float));
			for (unsigned int i=0; i < sc; i++) {
				if (c == 1) sample[i] = pSampleData[i];
				else sample[i] = 0.5f*(pSampleData[2*i]+pSampleData[2*i+1]);
			}
			drwav_free(pSampleData, NULL);
			size_t i = index*(table.nFrames-1);
			if (i<table.nFrames) {
				table.frames[i].loadSample(sc, interpolate, sample);
			}
			else if (table.nFrames==0) {
				table.addFrame(0);
				table.frames[0].loadSample(sc, interpolate, sample);
			}
			free(sample);
			table.calcFFT();
		}
	}
	else if (waveExtension == ".aiff") {
		float *sample;
			AudioFile<float> audioFile;
			if (audioFile.load(path.c_str()))  {
				sample = (float*)calloc(audioFile.getNumSamplesPerChannel(),sizeof(float));
				for (int i=0; i < audioFile.getNumSamplesPerChannel(); i++) {
					if (audioFile.isMono()) sample[i] = audioFile.samples[0][i];
					else sample[i] = 0.5f*(audioFile.samples[0][i]+audioFile.samples[1][i]);
				}
				size_t i = index*(table.nFrames-1);
				if (i<table.nFrames) {
					table.frames[i].loadSample(audioFile.getNumSamplesPerChannel(), interpolate, sample);
				}
				else if (table.nFrames==0) {
					table.addFrame(0);
					table.frames[0].loadSample(audioFile.getNumSamplesPerChannel(), interpolate, sample);
				}
				free(sample);
				table.calcFFT();
			}
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
		table.calcFFT();
  }
}

void tCalcFFT(wtTable &table) {
	table.calcFFT();
}

void tWindowWt(wtTable &table) {
	table.window();
	table.calcFFT();
}

void tSmoothWt(wtTable &table) {
	table.smooth();
	table.calcFFT();
}

void tWindowFrame(wtTable &table, float index) {
	size_t i = index*(table.nFrames-1);
	table.windowFrame(i);
	table.frames[i].calcFFT();
}

void tSmoothFrame(wtTable &table, float index) {
	size_t i = index*(table.nFrames-1);
	table.smoothFrame(i);
	table.frames[i].calcFFT();
}

void tRemoveDCOffset(wtTable &table) {
	table.removeDCOffset();
}

void tNormalizeFrame(wtTable &table, float index) {
	size_t i = index*(table.nFrames-1);
	table.frames[i].normalize();
	table.frames[i].calcFFT();
}

void tNormalizeWt(wtTable &table) {
	table.normalize();
	table.calcFFT();
}

void tNormalizeAllFrames(wtTable &table) {
	table.normalizeAllFrames();
	table.calcFFT();
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

void tRemoveFrame(wtTable &table, float index) {
	size_t i = index*(table.nFrames-1);
	table.removeFrame(i);
}

void tResetWaveTable(wtTable &table) {
	table.reset();
}

void tDeleteMorphing(wtTable &table) {
	table.deleteMorphing();
}

struct LIMONADE : BidooModule {
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
		DISPLAYMODE_PARAM,
		LOADSAMPLE_PARAM,
		LOADFRAME_PARAM,
		LOADPNG_PARAM,
		MORPHWT_PARAM,
		MORPHSPECTRUM_PARAM,
		MORPHSPECTRUMCONSTANTPHASE_PARAM,
		REMOVEMORPH_PARAM,
		NORMALIZEWT_PARAM,
		NORMALIZEFRAME_PARAM,
		NORMALIZEALLFRAMES_PARAM,
		REMOVEDC_PARAM,
		WINDOWWT_PARAM,
		WINDOWFRAME_PARAM,
		SMOOTHWT_PARAM,
		SMOOTHFRAME_PARAM,
		ADDFRAME_PARAM,
		REMOVEFRAME_PARAM,
		DISPLAYEDITEDFRAME_PARAM,
		DISPLAYPLAYEDFRAME_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PITCH_INPUT,
		FM_INPUT,
		SYNC_INPUT,
		SYNCMODE_INPUT,
		WTINDEX_INPUT,
		IN,
		UNISSONRANGE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		RECWT_LIGHT,
		RECFRAME_LIGHT,
		NUM_LIGHTS
	};

	std::string lastPath;
	size_t frameSize=FS;
	int morphType = -1;
	bool recWt = false;
	bool recFrame = false;
	float *iRec;
	dsp::SchmittTrigger recTrigger, displayModeTrigger, loadSampleTrigger, loadPngTrigger, loadFrameTrigger, morphWtTrigger, morphSpectrumTrigger, morphSpectrumPhaseConstantTrigger,
	removeMorphingTrigger, normalizeWtTrigger, normalizeFrameTrigger, normalizeAllFramesTrigger, removeDCTrigger, windowWtTrigger, windowFrameTrigger, smoothWtTrigger, smoothFrameTrigger,
	addFrameTrigger, removeFrameTrigger, displayEditedFrameTrigger, displayPlayedFrameTrigger;
	size_t recIndex = 0;
	int displayMode = 0;
	int displayEditedFrame = 1;
	int displayPlayedFrame = 1;
	size_t index = 0;
	bool dirty = true;

	wtTable table;
	wtOscillator<16, 16, float_4> oscillators[4];
	wtOscillator<16, 16, float_4> oscillatorsUp[4];
	wtOscillator<16, 16, float_4> oscillatorsDown[4];

	LIMONADE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(INDEX_PARAM, 0.0f, 1.0f, 0.0f ,"Edited frame");
		configParam(UNISSON_PARAM, 1.0f, 3.0f, 1.0f, "Voices number");
		configParam(UNISSONRANGE_PARAM, 0.0f, .02f, 0.0f, "Voices range");

		configSwitch(SYNC_PARAM, 0.f, 1.f, 1.f, "Sync mode", {"Soft", "Hard"});
		configParam(FREQ_PARAM, -54.f, 54.f, 0.f, "Frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
		configParam(FINE_PARAM, -1.f, 1.f, 0.f, "Fine frequency");
		configParam(FM_PARAM, 0.f, 1.f, 0.f, "Frequency modulation", "%", 0.f, 100.f);
		configInput(PITCH_INPUT, "1V/oct pitch");
		configInput(FM_INPUT, "Frequency modulation");
		configInput(SYNC_INPUT, "Sync");

    configParam(WTINDEX_PARAM, 0.0f, 1.0f, 0.0f, "Played frame");
    configParam(WTINDEXATT_PARAM, -1.0f, 1.0f, 1.0f, "Played frame attenuation");
    configParam(RECWT_PARAM, 0.0f, 10.0f, 0.0f, "Rec. wavetable");
    configParam(RECFRAME_PARAM, 0.0f, 10.0f, 0.0f, "Rec. frame");
		configParam(DISPLAYMODE_PARAM, 0.0f, 1.0f, 0.0f, "3D Wavetable display");
		configParam(DISPLAYEDITEDFRAME_PARAM, 0.0f, 1.0f, 0.0f, "Edited frame display");
		configParam(DISPLAYPLAYEDFRAME_PARAM, 0.0f, 1.0f, 0.0f, "Played frame display");
		configParam(LOADSAMPLE_PARAM, 0.0f, 1.0f, 0.0f, "Load wavetable");
		configParam(LOADFRAME_PARAM, 0.0f, 1.0f, 0.0f, "Load frame");
		configParam(LOADPNG_PARAM, 0.0f, 1.0f, 0.0f, "Load png");
		configParam(MORPHWT_PARAM, 0.0f, 1.0f, 0.0f, "Morph wavetable");
		configParam(MORPHSPECTRUM_PARAM, 0.0f, 1.0f, 0.0f, "Morph spectrum amplitude and phase");
		configParam(MORPHSPECTRUMCONSTANTPHASE_PARAM, 0.0f, 1.0f, 0.0f, "Morph spectrum amplitude constant phase");
		configParam(REMOVEMORPH_PARAM, 0.0f, 1.0f, 0.0f, "Remove morphing");
		configParam(NORMALIZEWT_PARAM, 0.0f, 1.0f, 0.0f, "Normalize wavetable");
		configParam(NORMALIZEFRAME_PARAM, 0.0f, 1.0f, 0.0f, "Normalize frame");
		configParam(NORMALIZEALLFRAMES_PARAM, 0.0f, 1.0f, 0.0f, "Normalize all frames");
		configParam(REMOVEDC_PARAM, 0.0f, 1.0f, 0.0f, "Remove DC offset");
		configParam(WINDOWWT_PARAM, 0.0f, 1.0f, 0.0f, "Window (Blackman Harris) wavetable");
		configParam(WINDOWFRAME_PARAM, 0.0f, 1.0f, 0.0f, "Window (Blackman Harris) frame");
		configParam(SMOOTHWT_PARAM, 0.0f, 1.0f, 0.0f, "Smooth wavetable (decaying avg)");
		configParam(SMOOTHFRAME_PARAM, 0.0f, 1.0f, 0.0f, "Smooth frame (decaying avg)");
		configParam(ADDFRAME_PARAM, 0.0f, 1.0f, 0.0f, "Add frame");
		configParam(REMOVEFRAME_PARAM, 0.0f, 1.0f, 0.0f, "Remove frame");

		for(size_t i=0; i<4; i++) {
			oscillators[i].table = &table;
			oscillatorsUp[i].table = &table;
			oscillatorsDown[i].table = &table;
		}
		iRec=(float*)calloc(4*NF*FS,sizeof(float));
	}

  ~LIMONADE() {
		delete(iRec);
	}

	void process(const ProcessArgs &args) override;
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
	void removeMorphing();
	void updateWaveTable();
	void addFrame();
	void removeFrame();
	void resetWaveTable();
	void normalizeFrame();
	void normalizeAllFrames();
	void normalizeWt();

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
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
		json_object_set_new(rootJ, "displayMode", json_integer(displayMode));
		json_object_set_new(rootJ, "displayEditedFrame", json_integer(displayEditedFrame));
		json_object_set_new(rootJ, "displayPlayedFrame", json_integer(displayPlayedFrame));
		json_object_set_new(rootJ, "frameSize", json_integer(frameSize));
		json_object_set_new(rootJ, "frames", framesJ);
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		size_t nFrames = 0;
		json_t *nFramesJ = json_object_get(rootJ, "nFrames");
		if (nFramesJ)	nFrames = json_integer_value(nFramesJ);

		json_t *morphTypeJ = json_object_get(rootJ, "morphType");
		if (morphTypeJ)	morphType = json_integer_value(morphTypeJ);

		json_t *displayModeJ = json_object_get(rootJ, "displayMode");
		if (displayModeJ)	displayMode = json_integer_value(displayModeJ);

		json_t *displayEditedFrameJ = json_object_get(rootJ, "displayEditedFrame");
		if (displayEditedFrameJ) displayEditedFrame = json_integer_value(displayEditedFrameJ);

		json_t *displayPlayedFrameJ = json_object_get(rootJ, "displayPlayedFrame");
		if (displayPlayedFrameJ) displayPlayedFrame = json_integer_value(displayPlayedFrameJ);

		json_t *frameSizeJ = json_object_get(rootJ, "frameSize");
		if (frameSizeJ)	frameSize = json_integer_value(frameSizeJ);

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
			free(wav);
		}
		table.calcFFT();
		dirty = true;
	}

	void onRandomize() override {

	}

	void onReset() override {
		table.reset();
		lastPath = "";
		dirty = true;
	}
};

inline void LIMONADE::updateWaveTable() {
	tUpdateWaveTable(table, params[INDEX_PARAM].getValue());
}

inline void LIMONADE::fftSample() {
	tFFTSample(table, params[INDEX_PARAM].getValue());
}

inline void LIMONADE::ifftSample() {
	tIFFTSample(table, params[INDEX_PARAM].getValue());
}

inline void LIMONADE::morphWavetable() {
	morphType = 0;
	tMorphWaveTable(table);
}

inline void LIMONADE::morphSpectrum() {
	morphType = 1;
	tMorphSpectrum(table);
}

inline void LIMONADE::morphSpectrumConstantPhase() {
	morphType = 2;
	tMorphSpectrumConstantPhase(table);
}

inline void LIMONADE::removeMorphing() {
	morphType = -1;
	tDeleteMorphing(table);
}

void LIMONADE::addFrame() {
	thread t = thread(tAddFrame, std::ref(table), params[INDEX_PARAM].getValue());
	t.detach();
}

void LIMONADE::removeFrame() {
	tRemoveFrame(table, params[INDEX_PARAM].getValue());
}

void LIMONADE::resetWaveTable() {
	tResetWaveTable(table);
}

void LIMONADE::loadSample() {
	osdialog_filters* filters = osdialog_filters_parse(WAV_FILTERS);
	char *path = osdialog_file(OSDIALOG_OPEN, "", NULL, filters);
	if (path) {
		lastPath=path;
		tLoadSample(table, path, frameSize, true);
		free(path);
		morphType = -1;
	}
	osdialog_filters_free(filters);
}

void LIMONADE::loadFrame() {
	osdialog_filters* filters = osdialog_filters_parse(WAV_FILTERS);
	char *path = osdialog_file(OSDIALOG_OPEN, "", NULL, filters);
	if (path) {
		lastPath=path;
		tLoadFrame(table, path, params[INDEX_PARAM].getValue(), true);
		free(path);
	}
	osdialog_filters_free(filters);
}

void LIMONADE::loadPNG() {
	osdialog_filters* filters = osdialog_filters_parse(PNG_FILTERS);
	char *path = osdialog_file(OSDIALOG_OPEN, "", NULL, filters);
	if (path) {
		lastPath=path;
		tLoadPNG(table, path);
		free(path);
	}
	osdialog_filters_free(filters);
}

void LIMONADE::windowWt() {
	tWindowWt(table);
}

void LIMONADE::smoothWt() {
	tSmoothWt(table);
}

void LIMONADE::windowFrame() {
	tWindowFrame(table, params[INDEX_PARAM].getValue());
}

void LIMONADE::smoothFrame() {
	tSmoothFrame(table, params[INDEX_PARAM].getValue());
}

void LIMONADE::removeDCOffset() {
	tRemoveDCOffset(table);
}


void LIMONADE::normalizeFrame() {
	tNormalizeFrame(table, params[INDEX_PARAM].getValue());
}

void LIMONADE::normalizeWt() {
	tNormalizeWt(table);
}

void LIMONADE::normalizeAllFrames() {
	tNormalizeAllFrames(table);
}

void LIMONADE::process(const ProcessArgs &args) {

	if (displayModeTrigger.process(params[DISPLAYMODE_PARAM].getValue())) {
		displayMode = (displayMode == 0) ? 1 : 0;
	}

	if (displayEditedFrameTrigger.process(params[DISPLAYEDITEDFRAME_PARAM].getValue())) {
		displayEditedFrame = (displayEditedFrame == 0) ? 1 : 0;
	}

	if (displayPlayedFrameTrigger.process(params[DISPLAYPLAYEDFRAME_PARAM].getValue())) {
		displayPlayedFrame = (displayPlayedFrame == 0) ? 1 : 0;
	}

	if (morphWtTrigger.process(params[MORPHWT_PARAM].getValue())) {
		morphWavetable();
	}

	if (morphSpectrumTrigger.process(params[MORPHSPECTRUM_PARAM].getValue())) {
		morphSpectrum();
	}

	if (morphSpectrumPhaseConstantTrigger.process(params[MORPHSPECTRUMCONSTANTPHASE_PARAM].getValue())) {
		morphSpectrumConstantPhase();
	}

	if (removeMorphingTrigger.process(params[REMOVEMORPH_PARAM].getValue())) {
		removeMorphing();
	}

	if (normalizeWtTrigger.process(params[NORMALIZEWT_PARAM].getValue())) {
		normalizeWt();
	}

	if (normalizeFrameTrigger.process(params[NORMALIZEFRAME_PARAM].getValue())) {
		normalizeFrame();
	}

	if (normalizeAllFramesTrigger.process(params[NORMALIZEALLFRAMES_PARAM].getValue())) {
		normalizeAllFrames();
	}

	if (removeDCTrigger.process(params[REMOVEDC_PARAM].getValue())) {
		removeDCOffset();
	}

	if (windowWtTrigger.process(params[WINDOWWT_PARAM].getValue())) {
		windowWt();
	}

	if (windowFrameTrigger.process(params[WINDOWFRAME_PARAM].getValue())) {
		windowFrame();
	}

	if (smoothWtTrigger.process(params[SMOOTHWT_PARAM].getValue())) {
		smoothWt();
	}

	if (smoothFrameTrigger.process(params[SMOOTHFRAME_PARAM].getValue())) {
		smoothFrame();
	}

	if (addFrameTrigger.process(params[ADDFRAME_PARAM].getValue())) {
		addFrame();
	}

	if (removeFrameTrigger.process(params[REMOVEFRAME_PARAM].getValue())) {
		removeFrame();
	}

	if (recTrigger.process(params[RECWT_PARAM].getValue()) && !recWt && !recFrame) {
		recWt = true;
		recIndex=0;
		lights[RECWT_LIGHT].setBrightness(1.0f);
	}

	if (recTrigger.process(params[RECFRAME_PARAM].getValue()) && !recWt && !recFrame) {
		recFrame = true;
		recIndex=0;
		lights[RECFRAME_LIGHT].setBrightness(1.0f);
	}

	if (recWt || recFrame) {
		iRec[recIndex]=inputs[IN].getVoltage()*0.1f;
		recIndex++;

		if (recWt && (recIndex==frameSize*NF)) {
			thread t = thread(tLoadISample, std::ref(table), std::ref(iRec), frameSize*NF, frameSize, true);
			t.detach();
			recWt = false;
			recIndex = 0;
			lights[RECWT_LIGHT].setBrightness(0.0f);
		}
		else if (recFrame && (recIndex==frameSize)) {
			thread t = thread(tLoadIFrame, std::ref(table), std::ref(iRec), params[INDEX_PARAM].getValue(), frameSize, true);
			t.detach();
			recFrame = false;
			recIndex = 0;
			lights[RECFRAME_LIGHT].setBrightness(0.0f);
		}
	}

	float freqParam = params[FREQ_PARAM].getValue() / 12.f;
	freqParam += dsp::quadraticBipolar(params[FINE_PARAM].getValue()) * 0.25f;
	float fmParam = dsp::quadraticBipolar(params[FM_PARAM].getValue());

	int channels = std::max(inputs[PITCH_INPUT].getChannels(), 1);
	index = clamp(params[WTINDEX_PARAM].getValue() + inputs[WTINDEX_INPUT].getVoltage() * 0.1f * params[WTINDEXATT_PARAM].getValue(),0.0f,1.0f)*(float)(table.nFrames == 0 ? 0 : table.nFrames - 1);
	float ur = clamp(params[UNISSONRANGE_PARAM].getValue() + rescale(inputs[UNISSONRANGE_INPUT].getVoltage(),0.0f,10.0f,0.0f,0.02f),0.0f,0.02f);

	for (int c = 0; c < channels; c += 4) {
		if ((size_t)params[UNISSON_PARAM].getValue()==1) {
			auto* oscillator = &oscillators[c / 4];
			oscillator->channels = std::min(channels - c, 4);
			oscillator->soft = params[SYNC_PARAM].getValue() <= 0.f;
			float_4 pitch = freqParam;
			pitch += inputs[PITCH_INPUT].getVoltageSimd<float_4>(c);
			if (inputs[FM_INPUT].isConnected()) {
				pitch += fmParam * inputs[FM_INPUT].getPolyVoltageSimd<float_4>(c);
			}
			oscillator->setPitch(pitch);
			oscillator->syncEnabled = inputs[SYNC_INPUT].isConnected();
			oscillator->process(args.sampleTime, inputs[SYNC_INPUT].getPolyVoltageSimd<float_4>(c),index);

			if (outputs[OUT].isConnected())
				outputs[OUT].setVoltageSimd(5.f * oscillator->out(), c);
		}
		else if ((size_t)params[UNISSON_PARAM].getValue()==2) {
			auto* oscillator = &oscillators[c / 4];
			oscillator->channels = std::min(channels - c, 4);
			oscillator->soft = params[SYNC_PARAM].getValue() <= 0.f;
			float_4 pitch = freqParam-ur;
			pitch += inputs[PITCH_INPUT].getVoltageSimd<float_4>(c);
			if (inputs[FM_INPUT].isConnected()) {
				pitch += fmParam * inputs[FM_INPUT].getPolyVoltageSimd<float_4>(c);
			}
			oscillator->setPitch(pitch);
			oscillator->syncEnabled = inputs[SYNC_INPUT].isConnected();
			oscillator->process(args.sampleTime, inputs[SYNC_INPUT].getPolyVoltageSimd<float_4>(c),index);

			auto* oscillatorUp = &oscillatorsUp[c / 4];
			oscillatorUp->channels = std::min(channels - c, 4);
			oscillatorUp->soft = params[SYNC_PARAM].getValue() <= 0.f;
			float_4 pitchUp = freqParam+ur;
			pitchUp += inputs[PITCH_INPUT].getVoltageSimd<float_4>(c);
			if (inputs[FM_INPUT].isConnected()) {
				pitchUp += fmParam * inputs[FM_INPUT].getPolyVoltageSimd<float_4>(c);
			}
			oscillatorUp->setPitch(pitchUp);
			oscillatorUp->syncEnabled = inputs[SYNC_INPUT].isConnected();
			oscillatorUp->process(args.sampleTime, inputs[SYNC_INPUT].getPolyVoltageSimd<float_4>(c),index);

			if (outputs[OUT].isConnected())
				outputs[OUT].setVoltageSimd(2.5f * (oscillator->out() + oscillatorUp->out()), c);
		}
		else {
			auto* oscillator = &oscillators[c / 4];
			oscillator->channels = std::min(channels - c, 4);
			oscillator->soft = params[SYNC_PARAM].getValue() <= 0.f;
			float_4 pitch = freqParam;
			pitch += inputs[PITCH_INPUT].getVoltageSimd<float_4>(c);
			if (inputs[FM_INPUT].isConnected()) {
				pitch += fmParam * inputs[FM_INPUT].getPolyVoltageSimd<float_4>(c);
			}
			oscillator->setPitch(pitch);
			oscillator->syncEnabled = inputs[SYNC_INPUT].isConnected();
			oscillator->process(args.sampleTime, inputs[SYNC_INPUT].getPolyVoltageSimd<float_4>(c),index);

			auto* oscillatorUp = &oscillatorsUp[c / 4];
			oscillatorUp->channels = std::min(channels - c, 4);
			oscillatorUp->soft = params[SYNC_PARAM].getValue() <= 0.f;
			float_4 pitchUp = freqParam+ur;
			pitchUp += inputs[PITCH_INPUT].getVoltageSimd<float_4>(c);
			if (inputs[FM_INPUT].isConnected()) {
				pitchUp += fmParam * inputs[FM_INPUT].getPolyVoltageSimd<float_4>(c);
			}
			oscillatorUp->setPitch(pitchUp);
			oscillatorUp->syncEnabled = inputs[SYNC_INPUT].isConnected();
			oscillatorUp->process(args.sampleTime, inputs[SYNC_INPUT].getPolyVoltageSimd<float_4>(c),index);

			auto* oscillatorDown = &oscillatorsDown[c / 4];
			oscillatorDown->channels = std::min(channels - c, 4);
			oscillatorDown->soft = params[SYNC_PARAM].getValue() <= 0.f;
			float_4 pitchDown = freqParam-ur;
			pitchDown += inputs[PITCH_INPUT].getVoltageSimd<float_4>(c);
			if (inputs[FM_INPUT].isConnected()) {
				pitchDown += fmParam * inputs[FM_INPUT].getPolyVoltageSimd<float_4>(c);
			}
			oscillatorDown->setPitch(pitchDown);
			oscillatorDown->syncEnabled = inputs[SYNC_INPUT].isConnected();
			oscillatorDown->process(args.sampleTime, inputs[SYNC_INPUT].getPolyVoltageSimd<float_4>(c),index);

			if (outputs[OUT].isConnected())
				outputs[OUT].setVoltageSimd(1.6666666f * (oscillator->out() + oscillatorUp->out() + oscillatorDown->out()), c);
		}
	}

	outputs[OUT].setChannels(channels);
}

struct LIMONADEBinsDisplay : OpaqueWidget {
	LIMONADE *module;
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

	LIMONADEBinsDisplay() {

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
		APP->window->cursorLock();
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(const event::DragMove &e) override {
		if ((!scroll) && (module->table.nFrames>0)) {
			size_t i = module->params[LIMONADE::INDEX_PARAM].getValue()*(module->table.nFrames-1);
			if (refY<=heightMagn) {
				if ((APP->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_CONTROL)) {
					module->table.frames[i].magnitude[refIdx] = 0.0f;
				}
				else {
					module->table.frames[i].magnitude[refIdx] -= e.mouseDelta.y/(250/APP->scene->rackScroll->zoomWidget->zoom);
					module->table.frames[i].magnitude[refIdx] = clamp(module->table.frames[i].magnitude[refIdx],0.0f, 1.0f);
				}
			}
			else if (refY>=heightMagn+graphGap) {
				if ((APP->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_CONTROL)) {
					module->table.frames[i].phase[refIdx] = 0.0f;
				}
				else {
					module->table.frames[i].phase[refIdx] -= e.mouseDelta.y / (250 / APP->scene->rackScroll->zoomWidget->zoom);
					module->table.frames[i].phase[refIdx] = clamp(module->table.frames[i].phase[refIdx],-1.0f*M_PI, M_PI);
				}
			}
			module->table.frames[i].morphed = false;
			module->updateWaveTable();
		}
		else {
				scrollLeftAnchor = clamp(scrollLeftAnchor + e.mouseDelta.x / APP->scene->rackScroll->zoomWidget->zoom, 0.0f,width-20.0f);
				zoomLeftAnchor = rescale(scrollLeftAnchor, 0.0f, width-20.0f, 0.0f, (width - zoomWidth)*0.5f);
		}
		OpaqueWidget::onDragMove(e);
	}

  void onDragEnd(const event::DragEnd &e) override {
    APP->window->cursorUnlock();
    OpaqueWidget::onDragEnd(e);
  }

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module) {
				nvgSave(args.vg);
				wtFrame frame, playedFrame;
				size_t tag=1;

				if (module->table.nFrames>0) {
					frame.magnitude = module->table.frames[(size_t)(module->params[LIMONADE::INDEX_PARAM].getValue()*(module->table.nFrames - 1))].magnitude;
					frame.phase = module->table.frames[(size_t)(module->params[LIMONADE::INDEX_PARAM].getValue()*(module->table.nFrames - 1))].phase;
					frame.sample = module->table.frames[(size_t)(module->params[LIMONADE::INDEX_PARAM].getValue()*(module->table.nFrames - 1))].sample;
					playedFrame.sample = module->table.frames[module->index].sample;
				}

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
				nvgFillColor(args.vg, YELLOW_BIDOO);

				nvgText(args.vg, 130.0f, heightMagn + graphGap * 0.5f + 4, "▲ Magnitude ▼ Phase", NULL);

				if (module->table.nFrames>0) {
					nvgText(args.vg, 0.0f, heightMagn + graphGap * 0.5f + 4, ("Frame " + to_string((int)(module->params[LIMONADE::INDEX_PARAM].getValue()*(module->table.nFrames-1) + 1)) + " / " + to_string(module->table.nFrames)).c_str(), NULL);
					for (size_t i = 0; i < FS2/2; i++) {
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
							nvgStrokeColor(args.vg, nvgRGBA(45, 114, 143, 100));
							nvgFillColor(args.vg, YELLOW_BIDOO);
							nvgLineCap(args.vg, NVG_MITER);
							nvgStrokeWidth(args.vg, 2);
							nvgBeginPath(args.vg);
							nvgRect(args.vg, p.x+1, heightMagn - p.y, b.size.x * IFS2 - 2, p.y);
							y = frame.phase[i]*IM_PI;
							p.y = heightPhas * 0.5f * y;
							nvgRect(args.vg, p.x+1, heightMagn + graphGap + heightPhas * 0.5f - p.y, b.size.x * IFS2-2, p.y);
							nvgStroke(args.vg);
							nvgFill(args.vg);
						}
					}
				}

				nvgResetScissor(args.vg);

				if ((module->displayPlayedFrame == 0) && (playedFrame.sample.size()>0)) {
					nvgStrokeColor(args.vg, RED_BIDOO);
					float invNbSample = 1.f / (float)playedFrame.sample.size();
					nvgBeginPath(args.vg);
					for (size_t i = 0; i < playedFrame.sample.size(); i++) {
						float x, y;
						x = (float)i * invNbSample  * 420.f;
						y = (-1.f)*playedFrame.sample[i] * 18.f + 35.f;
						if (i == 0) {
							nvgMoveTo(args.vg, x, y);
						}
						else {
							nvgLineTo(args.vg, x, y);
						}
					}
					nvgLineCap(args.vg, NVG_MITER);
					nvgStrokeWidth(args.vg, 1);
					nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
					nvgStroke(args.vg);
				}

				if ((module->displayEditedFrame == 0) && (frame.sample.size()>0)) {
					nvgStrokeColor(args.vg, GREEN_BIDOO);
					float invNbSample = 1.f / (float)frame.sample.size();
					nvgBeginPath(args.vg);
					for (size_t i = 0; i < frame.sample.size(); i++) {
						float x, y;
						x = (float)i * invNbSample * 420.f;
						y = (-1.f)*frame.sample[i] * 18.f + 35.f;
						if (i == 0) {
							nvgMoveTo(args.vg, x, y);
						}
						else {
							nvgLineTo(args.vg, x, y);
						}
					}
					nvgLineCap(args.vg, NVG_MITER);
					nvgStrokeWidth(args.vg, 1);
					nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
					nvgStroke(args.vg);
				}
				nvgRestore(args.vg);
			}
		}
		Widget::drawLayer(args, layer);
	}

};

struct LIMONADEWavDisplay : OpaqueWidget {
	LIMONADE *module;
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

	}

	void onButton(const event::Button &e) override {
			OpaqueWidget::onButton(e);
	}

	void onDragStart(const event::DragStart &e) override {
		APP->window->cursorLock();
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
		APP->window->cursorUnlock();
		OpaqueWidget::onDragEnd(e);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module && (module->displayMode == 0)) {
				size_t fs = module->table.nFrames;
				size_t idx = 0;
				size_t wtidx = 0;
				if (fs>0) {
					idx = module->params[LIMONADE::INDEX_PARAM].getValue()*(fs-1);
					wtidx = module->index;
				}

				nvgSave(args.vg);

				nvgFontSize(args.vg, 8.0f);
				nvgFillColor(args.vg, YELLOW_BIDOO);
				nvgStrokeWidth(args.vg, 1.0f);

				nvgText(args.vg, width+2, height-10, ("V=" + to_string((int)module->params[LIMONADE::UNISSON_PARAM].getValue())).c_str(), NULL);

				for (size_t n=0; n<fs; n++) {
					size_t fid = fs-n-1;
					y3D = 10.0f * fid/fs-5.0f;
					nvgBeginPath(args.vg);
					for (size_t i=0; i<FS2; i+=2) {
						x3D = 20.0f * i * IFS -5.0f;
						z3D = (-1.f)*module->table.frames[fid].sample[2*i];
						y2D = z3D*ca1-(ca2*y3D-sa2*x3D)*sa1+5.0f;
						x2D = ca2*x3D+sa2*y3D+7.5f;
						if (i == 0) {
							nvgMoveTo(args.vg, 10.0f * x2D, 10.0f * y2D);
						}
						else {
							nvgLineTo(args.vg, 10.0f * x2D, 10.0f * y2D);
						}
					}

					nvgStrokeColor(args.vg, nvgRGBA(255, 233, 0, module->table.frames[fid].morphed ? 15 : 50));
					nvgStroke(args.vg);
				}

				if (fs>0) {
					nvgBeginPath(args.vg);
					y3D = 10.0f * idx/fs -5.0f;
					for (size_t i=0; i<FS; i++) {
						x3D = 10.0f * i * IFS -5.0f;
						z3D = (-1.f)*module->table.frames[idx].sample[i];
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
					nvgStroke(args.vg);

					nvgBeginPath(args.vg);
					y3D = 10.0f * wtidx/fs -5.0f;
					for (size_t i=0; i<FS; i++) {
						x3D = 10.0f * i * IFS -5.0f;
						z3D = (-1.f)*module->table.frames[wtidx].sample[i];
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
					nvgStroke(args.vg);
				}

				nvgRestore(args.vg);
			}
		}
		Widget::drawLayer(args, layer);
	}

};

struct LIMONADEFrameSizeTextField : LedDisplayTextField {
	LIMONADE *module;

	void step() override {
		LedDisplayTextField::step();
		if (module && module->dirty) {
			setText(std::to_string(module->frameSize));
			module->dirty = false;
		}
	}

	void onChange(const event::Change &e) override {
		LedDisplayTextField::onChange(e);
		if ((getText().size() > 0) && (getText() != "")) {
			size_t val;
			std::istringstream(getText())>>val;
	    module->frameSize = val;
		}
	};

};

struct LIMONADEFrameSizeDisplay : LedDisplay {
	void setModule(LIMONADE* module) {
		LIMONADEFrameSizeTextField* textField = createWidget<LIMONADEFrameSizeTextField>(Vec(2.0f, -6.0f));
		textField->box.size = box.size;
		textField->multiline = false;
		textField->module = module;
		addChild(textField);
	}
};

struct moduleDisplayModeItem : MenuItem {
	LIMONADE *module;
	void onAction(const event::Action &e) override {
		module->displayMode = (module->displayMode == 0) ? 1 : 0;
	}
};

struct moduleDisplayEditedFrameItem : MenuItem {
	LIMONADE *module;
	void onAction(const event::Action &e) override {
		module->displayEditedFrame = (module->displayEditedFrame == 0) ? 1 : 0;
	}
};

struct moduleDisplayPlayedFrameItem : MenuItem {
	LIMONADE *module;
	void onAction(const event::Action &e) override {
		module->displayPlayedFrame = (module->displayPlayedFrame == 0) ? 1 : 0;
	}
};

struct moduleSaveWavetableAsWavItem : MenuItem {
	LIMONADE *module;
	void onAction(const event::Action &e) override {
		std::string dir = module->lastPath.empty() ? asset::user("") : rack::system::getDirectory(module->lastPath);
		osdialog_filters* filters = osdialog_filters_parse(WAV_FILTERS);
		char *path = osdialog_file(OSDIALOG_SAVE, dir.c_str(), "Untitled", filters);
		if (path) {
			tSaveWaveTableAsWave(module->table, APP->engine->getSampleRate(), path);
			free(path);
		}
		osdialog_filters_free(filters);
	}
};

struct moduleSaveFrameAsWavItem : MenuItem {
	LIMONADE *module;
	void onAction(const event::Action &e) override {
		std::string dir = module->lastPath.empty() ? asset::user("") : rack::system::getDirectory(module->lastPath);
		osdialog_filters* filters = osdialog_filters_parse(WAV_FILTERS);
		char *path = osdialog_file(OSDIALOG_SAVE, dir.c_str(), "Untitled", filters);
		if (path) {
			tSaveFrameAsWave(module->table, APP->engine->getSampleRate(), path, (size_t)(module->params[LIMONADE::INDEX_PARAM].getValue()*(module->table.nFrames - 1)));
			free(path);
		}
		osdialog_filters_free(filters);
	}
};

struct moduleSaveWavetableAsPngItem : MenuItem {
	LIMONADE *module;
	void onAction(const event::Action &e) override {
		std::string dir = module->lastPath.empty() ? asset::user("") : rack::system::getDirectory(module->lastPath);
		osdialog_filters* filters = osdialog_filters_parse(PNG_FILTERS);
		char *path = osdialog_file(OSDIALOG_SAVE, dir.c_str(), "Untitled", filters);
		if (path) {
			tSaveWaveTableAsPng(module->table, APP->engine->getSampleRate(), path);
			free(path);
		}
		osdialog_filters_free(filters);
	}
};

struct LimonadeBlueBtnLoadSample : BlueBtn {
	virtual void onButton(const event::Button &e) override {
		if ((e.button == GLFW_MOUSE_BUTTON_LEFT) && (e.action == GLFW_PRESS)) dynamic_cast<LIMONADE*>(this->getParamQuantity()->module)->loadSample();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnLoadPNG : BlueBtn {
	virtual void onButton(const event::Button &e) override {
		if ((e.button == GLFW_MOUSE_BUTTON_LEFT) && (e.action == GLFW_PRESS)) dynamic_cast<LIMONADE*>(this->getParamQuantity()->module)->loadPNG();
		BlueBtn::onButton(e);
	}
};

struct LimonadeBlueBtnLoadFrame : BlueBtn {
	virtual void onButton(const event::Button &e) override {
		if ((e.button == GLFW_MOUSE_BUTTON_LEFT) && (e.action == GLFW_PRESS)) dynamic_cast<LIMONADE*>(this->getParamQuantity()->module)->loadFrame();
		BlueBtn::onButton(e);
	}
};

struct LIMONADEWidget : BidooWidget {
	LIMONADEWidget(LIMONADE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/LIMONADE.svg"));

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
			LIMONADEFrameSizeDisplay* frameSizeDisplay = createWidget<LIMONADEFrameSizeDisplay>(Vec(170, 208));
			frameSizeDisplay->box.size = Vec(45, 19);
			frameSizeDisplay->setModule(module);
			addChild(frameSizeDisplay);
  	}

		BlueBtn *loadSampleParam = createParam<LimonadeBlueBtnLoadSample>(Vec(170, 240), module, LIMONADE::LOADSAMPLE_PARAM);
		loadSampleParam->caption = "≈";
		addParam(loadSampleParam);

		BlueBtn *loadPngParam = createParam<LimonadeBlueBtnLoadPNG>(Vec(190, 240), module, LIMONADE::LOADPNG_PARAM);
		loadPngParam->caption = "☺";
		addParam(loadPngParam);

		BlueBtn *loadFrameParam = createParam<LimonadeBlueBtnLoadFrame>(Vec(210, 240), module, LIMONADE::LOADFRAME_PARAM);
		loadFrameParam->caption = "~";
		addParam(loadFrameParam);

		BlueBtn *morphWtParam = createParam<BlueBtn>(Vec(170, 260), module, LIMONADE::MORPHWT_PARAM);
		morphWtParam->caption = "≡";
		addParam(morphWtParam);

		BlueBtn *morphSpectrumParam = createParam<BlueBtn>(Vec(190, 260), module, LIMONADE::MORPHSPECTRUM_PARAM);
		morphSpectrumParam->caption = "∞";
		addParam(morphSpectrumParam);

		BlueBtn *morphSpectrumPhaseConstantParam = createParam<BlueBtn>(Vec(210, 260), module, LIMONADE::MORPHSPECTRUMCONSTANTPHASE_PARAM);
		morphSpectrumPhaseConstantParam->caption = "Փ";
		addParam(morphSpectrumPhaseConstantParam);

		BlueBtn *removeMorphingParam = createParam<BlueBtn>(Vec(230, 260), module, LIMONADE::REMOVEMORPH_PARAM);
		removeMorphingParam->caption = "Ø";
		addParam(removeMorphingParam);

		BlueBtn *normalizeAllFramesParam = createParam<BlueBtn>(Vec(170, 280), module, LIMONADE::NORMALIZEALLFRAMES_PARAM);
		normalizeAllFramesParam->caption = "↕";
		addParam(normalizeAllFramesParam);

		BlueBtn *removeDCParam = createParam<BlueBtn>(Vec(190, 280), module, LIMONADE::REMOVEDC_PARAM);
		removeDCParam->caption = "↨";
		addParam(removeDCParam);

		BlueBtn *normalizeWtParam = createParam<BlueBtn>(Vec(210, 280), module, LIMONADE::NORMALIZEWT_PARAM);
		normalizeWtParam->caption = "↕≈";
		addParam(normalizeWtParam);

		BlueBtn *normalizeFrameParam = createParam<BlueBtn>(Vec(230, 280), module, LIMONADE::NORMALIZEFRAME_PARAM);
		normalizeFrameParam->caption = "↕~";
		addParam(normalizeFrameParam);

		BlueBtn *windowWtParam = createParam<BlueBtn>(Vec(170, 300), module, LIMONADE::WINDOWWT_PARAM);
		windowWtParam->caption = "∩≈";
		addParam(windowWtParam);

		BlueBtn *smoothWtParam = createParam<BlueBtn>(Vec(190, 300), module, LIMONADE::SMOOTHWT_PARAM);
		smoothWtParam->caption = "ƒ≈";
		addParam(smoothWtParam);

		BlueBtn *windowFrameParam = createParam<BlueBtn>(Vec(210, 300), module, LIMONADE::WINDOWFRAME_PARAM);
		windowFrameParam->caption = "∩~";
		addParam(windowFrameParam);

		BlueBtn *smoothFrameParam = createParam<BlueBtn>(Vec(230, 300), module, LIMONADE::SMOOTHFRAME_PARAM);
		smoothFrameParam->caption = "ƒ~";
		addParam(smoothFrameParam);

		BlueBtn *addFrameParam = createParam<BlueBtn>(Vec(170, 320), module, LIMONADE::ADDFRAME_PARAM);
		addFrameParam->caption = "+";
		addParam(addFrameParam);

		BlueBtn *removeFrameParam = createParam<BlueBtn>(Vec(190, 320), module, LIMONADE::REMOVEFRAME_PARAM);
		removeFrameParam->caption = "-";
		addParam(removeFrameParam);

		BlueBtn *displayModeParam = createParam<BlueBtn>(Vec(355, 8), module, LIMONADE::DISPLAYMODE_PARAM);
		displayModeParam->caption = "d";
		addParam(displayModeParam);

		BlueBtn *displayEditedFrameParam = createParam<BlueBtn>(Vec(375, 8), module, LIMONADE::DISPLAYEDITEDFRAME_PARAM);
		displayEditedFrameParam->caption = "e";
		addParam(displayEditedFrameParam);

		BlueBtn *displayPlayedFrameParam = createParam<BlueBtn>(Vec(395, 8), module, LIMONADE::DISPLAYPLAYEDFRAME_PARAM);
		displayPlayedFrameParam->caption = "p";
		addParam(displayPlayedFrameParam);

  	addParam(createParam<BidooGreenKnob>(Vec(230.0f, 208.0f), module, LIMONADE::INDEX_PARAM));

  	addParam(createParam<BidooBlueSnapKnob>(Vec(274.0f, 219.0f), module, LIMONADE::UNISSON_PARAM));
  	addParam(createParam<BidooBlueTrimpot>(Vec(279.0f, 262.0f), module, LIMONADE::UNISSONRANGE_PARAM));
  	addParam(createParam<BidooBlueKnob>(Vec(309, 219), module, LIMONADE::FREQ_PARAM));
  	addParam(createParam<BidooBlueKnob>(Vec(344, 219), module, LIMONADE::FINE_PARAM));
  	addParam(createParam<BidooBlueKnob>(Vec(379, 219), module, LIMONADE::FM_PARAM));
  	addParam(createParam<BidooRedKnob>(Vec(414.0f, 219), module, LIMONADE::WTINDEX_PARAM));
  	addParam(createParam<BidooBlueTrimpot>(Vec(420.0f, 257), module, LIMONADE::WTINDEXATT_PARAM));

  	addParam(createParam<CKSS>(Vec(352, 327), module, LIMONADE::SYNC_PARAM));
  	addInput(createInput<TinyPJ301MPort>(Vec(317, 330), module, LIMONADE::SYNCMODE_INPUT));


		RedBtn *btnRecWt = createParam<RedBtn>(Vec(254.0f, 320), module, LIMONADE::RECWT_PARAM);
		btnRecWt->caption = "≈";
		addParam(btnRecWt);

  	addChild(createLight<SmallLight<RedLight>>(Vec(244.0f, 325), module, LIMONADE::RECWT_LIGHT));

		RedBtn *btnRecFrame = createParam<RedBtn>(Vec(254.0f, 340), module, LIMONADE::RECFRAME_PARAM);
		btnRecFrame->caption = "~";
		addParam(btnRecFrame);

  	addChild(createLight<SmallLight<RedLight>>(Vec(244.0f, 345), module, LIMONADE::RECFRAME_LIGHT));

  	addInput(createInput<PJ301MPort>(Vec(277, 283), module, LIMONADE::UNISSONRANGE_INPUT));
  	addInput(createInput<PJ301MPort>(Vec(312, 283), module, LIMONADE::PITCH_INPUT));
  	addInput(createInput<PJ301MPort>(Vec(347, 283), module, LIMONADE::SYNC_INPUT));
  	addInput(createInput<PJ301MPort>(Vec(382, 283), module, LIMONADE::FM_INPUT));
  	addInput(createInput<PJ301MPort>(Vec(417, 283), module, LIMONADE::WTINDEX_INPUT));

  	addInput(createInput<PJ301MPort>(Vec(277, 330), module, LIMONADE::IN));
  	addOutput(createOutput<PJ301MPort>(Vec(417, 330), module, LIMONADE::OUT));
  }

	void appendContextMenu(Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		LIMONADE *m = dynamic_cast<LIMONADE*>(this->module);
		menu->addChild(new MenuSeparator());
		moduleDisplayModeItem *modeItem = new moduleDisplayModeItem;
		modeItem->text = "Wavetable display: ";
		modeItem->rightText = (m->displayMode == 0) ? "ON✔ OFF" : "ON  OFF✔";
		modeItem->module = m;
		menu->addChild(modeItem);
		moduleDisplayEditedFrameItem *editedFrameItem = new moduleDisplayEditedFrameItem;
		editedFrameItem->text = "Edited frame display: ";
		editedFrameItem->rightText = (m->displayEditedFrame == 0) ? "ON✔ OFF" : "ON  OFF✔";
		editedFrameItem->module = m;
		menu->addChild(editedFrameItem);
		moduleDisplayPlayedFrameItem *playedFrameItem = new moduleDisplayPlayedFrameItem;
		playedFrameItem->text = "Played frame display: ";
		playedFrameItem->rightText = (m->displayPlayedFrame == 0) ? "ON✔ OFF" : "ON  OFF✔";
		playedFrameItem->module = m;
		menu->addChild(playedFrameItem);
		moduleSaveWavetableAsWavItem *saveWavetableAsWavItem = new moduleSaveWavetableAsWavItem;
		saveWavetableAsWavItem->text = "Save wavetable as wav";
		saveWavetableAsWavItem->module = m;
		menu->addChild(saveWavetableAsWavItem);
		moduleSaveFrameAsWavItem *saveFrameAsWavItem = new moduleSaveFrameAsWavItem;
		saveFrameAsWavItem->text = "Save frame as wav";
		saveFrameAsWavItem->module = m;
		menu->addChild(saveFrameAsWavItem);
		moduleSaveWavetableAsPngItem *saveWavetableAsPngItem = new moduleSaveWavetableAsPngItem;
		saveWavetableAsPngItem->text = "Save wavetable as png";
		saveWavetableAsPngItem->module = m;
		menu->addChild(saveWavetableAsPngItem);
	}


	void onPathDrop(const PathDropEvent& e) override {
		Widget::onPathDrop(e);
		LIMONADE *module = dynamic_cast<LIMONADE*>(this->module);
		module->lastPath=e.paths[0];
		tLoadSample(module->table, e.paths[0], module->frameSize, true);
		module->morphType = -1;
	}
};

Model *modelLIMONADE = createModel<LIMONADE, LIMONADEWidget>("liMonADe");

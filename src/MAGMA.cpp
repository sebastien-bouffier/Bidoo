#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "dep/dr_wav/dr_wav.h"
#include "dep/AudioFile/AudioFile.h"
#include <iomanip>
#include "osdialog.h"
#include "dsp/resampler.hpp"

using namespace std;


#define pi 3.14159265359

struct MultiFilter {
	float q;
	float freq;
	float smpRate;
	float hp = 0.0f, bp = 0.0f, lp = 0.0f, mem1 = 0.0f, mem2 = 0.0f;

	void setParams(float freq, float q, float smpRate) {
		this->freq = freq;
		this->q = q;
		this->smpRate = smpRate;
	}

	void calcOutput(float sample) {
		float g = tan(pi*freq / smpRate);
		float R = 1.0f / (2.0f*q);
		hp = (sample - (2.0f*R + g)*mem1 - mem2) / (1.0f + 2.0f * R * g + g * g);
		bp = g * hp + mem1;
		lp = g * bp + mem2;
		mem1 = g * hp + bp;
		mem2 = g * bp + lp;
	}
};

struct channel {
	float start=0.0f;
	float len=1.0f;
	bool loop=false;
	float speed=1.0f;
	float head=0.0f;
	int gate=0;
	int filterType=0;
	float q;
	float freq;
	MultiFilter filter;
};

struct MAGMA : Module {
	enum ParamIds {
		CHANNEL_PARAM,
		START_PARAM,
		LEN_PARAM,
		LOOP_PARAM,
		SPEED_PARAM,
		GATE_PARAM,
		Q_PARAM,
		FREQ_PARAM,
		FILTERTYPE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		TRIG_INPUT,
		GATE_INPUT,
		START_INPUT,
		LEN_INPUT,
		LOOP_INPUT,
		SPEED_INPUT,
		Q_INPUT,
		FREQ_INPUT,
		FILTERTYPE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		POLY_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		SAMPLE_LIGHT,
		NUM_LIGHTS = SAMPLE_LIGHT+3
	};

	channel channels[16];
	int currentChannel=0;
	dsp::SchmittTrigger triggers[16];
	bool active[16]={false};
	bool loading=false;
	unsigned int sampleChannels;
	unsigned int sampleRate;
	drwav_uint64 totalSampleCount;
	AudioFile<float> audioFile;
	vector<float> playBuffer;
	bool play = false;
	std::string lastPath;
	std::string waveFileName;
	std::string waveExtension;

	MAGMA() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CHANNEL_PARAM, 0.0f, 15.0f, 0.0f);
		configParam(START_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(LEN_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(LOOP_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(GATE_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(SPEED_PARAM, 0.0f, 10.0f, 1.0f);
		configParam(FILTERTYPE_PARAM, 0.0f, 3.0f, 0.0f);
		configParam(Q_PARAM, 0.1f, 1.0f, 0.1f);
		configParam(FREQ_PARAM, 0.0f, 1.0f, 1.0f);

		playBuffer.resize(0);
	}

	void process(const ProcessArgs &args) override;

	void loadSample(std::string path);
	void saveSample();

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
		for (size_t i = 0; i<16 ; i++) {
			json_t *channelJ = json_object();
			json_object_set_new(channelJ, "start", json_real(channels[i].start));
			json_object_set_new(channelJ, "len", json_real(channels[i].len));
			json_object_set_new(channelJ, "speed", json_real(channels[i].speed));
			json_object_set_new(channelJ, "loop", json_boolean(channels[i].loop));
			json_object_set_new(channelJ, "gate", json_integer(channels[i].gate));
			json_object_set_new(channelJ, "filterType", json_integer(channels[i].filterType));
			json_object_set_new(channelJ, "q", json_real(channels[i].q));
			json_object_set_new(channelJ, "freq", json_real(channels[i].freq));
			json_object_set_new(rootJ, ("channel"+ to_string(i)).c_str(), channelJ);
		}
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
			json_t *lastPathJ = json_object_get(rootJ, "lastPath");
			if (lastPathJ) {
				lastPath = json_string_value(lastPathJ);
				waveFileName = rack::string::filename(lastPath);
				waveExtension = rack::string::filenameBase(lastPath);
				loadSample(lastPath);
				for (size_t i = 0; i<16 ; i++) {
					json_t *channelJ = json_object_get(rootJ, ("channel" + to_string(i)).c_str());
					if (channelJ){
						json_t *startJ= json_object_get(channelJ, "start");
						if (startJ)
							channels[i].start = json_number_value(startJ);
						json_t *lenJ= json_object_get(channelJ, "len");
						if (lenJ)
							channels[i].len = json_number_value(lenJ);
						json_t *speedJ= json_object_get(channelJ, "speed");
						if (speedJ)
							channels[i].speed = json_number_value(speedJ);
						json_t *loopJ= json_object_get(channelJ, "loop");
						if (loopJ)
							channels[i].loop = !!json_integer_value(loopJ);
						json_t *gateJ= json_object_get(channelJ, "gate");
						if (gateJ)
							channels[i].gate = json_integer_value(gateJ);
						json_t *filterTypeJ= json_object_get(channelJ, "filterType");
						if (filterTypeJ)
							channels[i].filterType = json_integer_value(filterTypeJ);
						json_t *qJ= json_object_get(channelJ, "q");
						if (qJ)
							channels[i].q = json_number_value(qJ);
						json_t *freqJ= json_object_get(channelJ, "freq");
						if (freqJ)
							channels[i].freq = json_number_value(freqJ);
				}
			}
		}
	}
};

void MAGMA::loadSample(std::string path) {
	waveFileName = rack::string::filename(path);
	waveExtension = rack::string::filenameExtension(rack::string::filename(lastPath));
	lastPath = path;
	if (waveExtension == "wav") {
		loading = true;
		unsigned int c;
		unsigned int sr;
		drwav_uint64 sc;
		float* pSampleData;
		pSampleData = drwav_open_file_and_read_f32(path.c_str(), &c, &sr, &sc);
		if (pSampleData != NULL)  {
			sampleChannels = c;
			sampleRate = sr;
			playBuffer.clear();
			for (unsigned int i=0; i < sc; i = i + c) {
				if (sampleChannels == 2) {
					playBuffer.push_back((pSampleData[i] + pSampleData[i+1])/2.0f);
				}
				else {
					playBuffer.push_back(pSampleData[i]);
				}
			}
			totalSampleCount = playBuffer.size();
			drwav_free(pSampleData);
		}
		loading = false;
	}
	else if (waveExtension == "aiff") {
		loading = true;
		if (audioFile.load (path.c_str()))  {
			sampleChannels = audioFile.getNumChannels();
			sampleRate = audioFile.getSampleRate();
			totalSampleCount = audioFile.getNumSamplesPerChannel();
			playBuffer.clear();
			for (unsigned int i=0; i < totalSampleCount; i++) {
				if (sampleChannels == 2) {
					playBuffer.push_back((audioFile.samples[0][i] + audioFile.samples[1][i])/2.0f);
				}
				else {
					playBuffer.push_back(audioFile.samples[0][i]);
				}
			}
		}
		loading = false;
	}
}

void MAGMA::saveSample() {
	drwav_data_format format;
	format.container = drwav_container_riff;
	format.format = DR_WAVE_FORMAT_PCM;
	format.channels = 2;
	format.sampleRate = sampleRate;
	format.bitsPerSample = 32;
	int *pSamples = (int*)calloc(2*totalSampleCount,sizeof(int));
	memset(pSamples, 0, 2*totalSampleCount*sizeof(int));
	for (unsigned int i = 0; i < totalSampleCount; i++) {
		*(pSamples+2*i)= floor(playBuffer[i]*2147483647);
		*(pSamples+2*i+1)= floor(playBuffer[i]*2147483647);
	}
	drwav* pWav = drwav_open_file_write(lastPath.c_str(), &format);
	drwav_write(pWav, 2*totalSampleCount, pSamples);
	drwav_close(pWav);
	free(pSamples);
}

void MAGMA::process(const ProcessArgs &args) {
	if (playBuffer.size()==0) {
		lights[SAMPLE_LIGHT].setBrightness(1.0f);
		lights[SAMPLE_LIGHT+1].setBrightness(0.0f);
		lights[SAMPLE_LIGHT+2].setBrightness(0.0f);
	}
	else {
		lights[SAMPLE_LIGHT].setBrightness(0.0f);
		lights[SAMPLE_LIGHT+1].setBrightness(1.0f);
		lights[SAMPLE_LIGHT+2].setBrightness(0.0f);
	}

	if (currentChannel != (int)params[CHANNEL_PARAM].getValue()) {
		currentChannel = params[CHANNEL_PARAM].getValue();
		params[START_PARAM].setValue(channels[currentChannel].start);
		params[LEN_PARAM].setValue(channels[currentChannel].len);
		params[SPEED_PARAM].setValue(channels[currentChannel].speed);
		params[LOOP_PARAM].setValue(channels[currentChannel].loop ? 1.0f : 0.0f);
		params[GATE_PARAM].setValue(channels[currentChannel].gate);
		params[FILTERTYPE_PARAM].setValue(channels[currentChannel].filterType);
		params[Q_PARAM].setValue(channels[currentChannel].q);
		params[FREQ_PARAM].setValue(channels[currentChannel].freq);
	}

	channels[currentChannel].start = params[START_PARAM].getValue();
	channels[currentChannel].len = params[LEN_PARAM].getValue();
	channels[currentChannel].speed = params[SPEED_PARAM].getValue();
	channels[currentChannel].loop = params[LOOP_PARAM].getValue() == 1.0f;
	channels[currentChannel].gate = params[GATE_PARAM].getValue();
	channels[currentChannel].filterType = params[FILTERTYPE_PARAM].getValue();
	channels[currentChannel].freq = params[FREQ_PARAM].getValue();
	channels[currentChannel].q = params[Q_PARAM].getValue();


	int c = std::max(inputs[TRIG_INPUT].getChannels(), 1);

	outputs[POLY_OUTPUT].setChannels(c);

	for (int i=0;i<c;i++) {
		float start = clamp(channels[i].start + (inputs[START_INPUT].isConnected() ? rescale(inputs[START_INPUT].getVoltage(i),0.0f,10.0f,0.0f,1.0f) : 0.0f), 0.0f, 1.0f);
		float len = clamp(channels[i].len + (inputs[LEN_INPUT].isConnected() ? rescale(inputs[LEN_INPUT].getVoltage(i),0.0f,10.0f,0.0f,1.0f) : 0.0f), 0.0f, 1.0f);
		float speed = clamp(channels[i].speed + (inputs[SPEED_INPUT].isConnected() ? rescale(inputs[SPEED_INPUT].getVoltage(i),0.0f,10.0f,0.0f,1.0f) : 0.0f), 0.0f, 10.0f);
		bool loop = channels[i].loop && (clamp(inputs[LOOP_INPUT].isConnected() ? inputs[LOOP_INPUT].getVoltage(i) : 1.0f, 0.0f, 1.0f) != 0.0f);
		int gate = inputs[GATE_INPUT].isConnected() ? rescale(inputs[SPEED_INPUT].getVoltage(i),0.0f,10.0f,0.0f,1.0f) : channels[i].gate;
		int filterType = inputs[FILTERTYPE_INPUT].isConnected() ? rescale(inputs[FILTERTYPE_INPUT].getVoltage(i),0.0f,10.0f,0.0f,3.0f) : channels[i].filterType;
		float q = 10.0f *clamp(channels[i].q + (inputs[Q_INPUT].isConnected() ? rescale(inputs[Q_INPUT].getVoltage(i),0.0f,10.0f,0.1f,1.0f) : 0.0f), 0.1f, 1.0f);
		float freq = std::pow(2.0f, rescale(clamp(channels[i].freq + (inputs[FREQ_INPUT].isConnected() ? rescale(inputs[FREQ_INPUT].getVoltage(i),0.0f,10.0f,0.0f,1.0f) : 0.0f), 0.0f, 1.0f), 0.0f, 1.0f, 4.5f, 14.0f));

		if ((!active[i] || (gate==1.0f)) && (triggers[i].process(inputs[TRIG_INPUT].getVoltage(i)))) {
			active[i] = true;
			channels[i].head = start * playBuffer.size();
		}
		else if ((gate==0.0f) && (inputs[TRIG_INPUT].getVoltage(i) == 0.0f)) {
			active[i] = false;
		}

		if (active[i]) {
			int xi = channels[i].head;
			float xf = channels[i].head - xi;
			float crossfaded = crossfade(playBuffer[xi], playBuffer[xi + 1], xf);
			channels[i].filter.setParams(freq, q, args.sampleRate);
			channels[i].filter.calcOutput(crossfaded);
			if (channels[i].filterType == 0.0f) {
				outputs[POLY_OUTPUT].setVoltage(5.0f * crossfaded,i);
			}
			else if (channels[i].filterType == 1.0f) {
				outputs[POLY_OUTPUT].setVoltage(5.0f * channels[i].filter.lp,i);
			}
			else if (channels[i].filterType == 2.0f) {
				outputs[POLY_OUTPUT].setVoltage(5.0f * channels[i].filter.bp,i);
			}
			else {
				outputs[POLY_OUTPUT].setVoltage(5.0f * channels[i].filter.hp,i);
			}

			channels[i].head += speed;
			if ((channels[i].head >= (playBuffer.size()-1)) || (channels[i].head > ((start+len)*playBuffer.size()))) {
				if (loop && (gate==0.0f)) {
					channels[i].head = start*playBuffer.size();
				}
				else {
					active[i]=false;
				}
			}
		}
		else {
			outputs[POLY_OUTPUT].setVoltage(0.0f,i);
		}
	}
}

struct MAGMAWidget : ModuleWidget {
	MAGMAWidget(MAGMA *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MAGMA.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(56.5f, 20.0f), module, MAGMA::SAMPLE_LIGHT));

		addParam(createParam<BidooBlueSnapKnob>(Vec(45.0f,35.0f), module, MAGMA::CHANNEL_PARAM));

		addParam(createParam<BidooBlueKnob>(Vec(7.0f,100.0f), module, MAGMA::START_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(45.0f,100.0f), module, MAGMA::LEN_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(83.0f,100.0f), module, MAGMA::SPEED_PARAM));

		addParam(createParam<BidooBlueSnapKnob>(Vec(7.0f,150.0f), module, MAGMA::FILTERTYPE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(45.0f,150.0f), module, MAGMA::FREQ_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(83.0f,150.0f), module, MAGMA::Q_PARAM));

		addParam(createParam<CKSS>(Vec(38.0f,191.5f), module, MAGMA::LOOP_PARAM));
		addParam(createParam<CKSS>(Vec(97.0f,191.5f), module, MAGMA::GATE_PARAM));

		addInput(createInput<PJ301MPort>(Vec(4.0f, 236.0f), module, MAGMA::START_INPUT));
		addInput(createInput<PJ301MPort>(Vec(33.0f, 236.0f), module, MAGMA::LEN_INPUT));
		addInput(createInput<PJ301MPort>(Vec(62.5f, 236.0f), module, MAGMA::SPEED_INPUT));
		addInput(createInput<PJ301MPort>(Vec(91.5f, 236.0f), module, MAGMA::LOOP_INPUT));

		addInput(createInput<PJ301MPort>(Vec(4.0f, 283.0f), module, MAGMA::FILTERTYPE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(33.0f, 283.0f), module, MAGMA::FREQ_INPUT));
		addInput(createInput<PJ301MPort>(Vec(62.5f, 283.0f), module, MAGMA::Q_INPUT));
		addInput(createInput<PJ301MPort>(Vec(91.5f, 283.0f), module, MAGMA::GATE_INPUT));

		addInput(createInput<PJ301MPort>(Vec(7.0f, 330.0f), module, MAGMA::TRIG_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(88.0f, 330.0f), module, MAGMA::POLY_OUTPUT));
	}

	struct MAGMAItem : MenuItem {
  	MAGMA *module;
  	void onAction(const event::Action &e) override {

  		std::string dir = module->lastPath.empty() ? asset::user("") : rack::string::directory(module->lastPath);
  		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
  		if (path) {
				module->play = false;
  			module->loadSample(path);
  			module->lastPath = path;
  			free(path);
  		}
  	}
  };

  void appendContextMenu(ui::Menu *menu) override {
		MAGMA *module = dynamic_cast<MAGMA*>(this->module);
		assert(module);

		menu->addChild(construct<MenuLabel>());
		menu->addChild(construct<MAGMAItem>(&MenuItem::text, "Load sample", &MAGMAItem::module, module));
	}
};

Model *modelMAGMA = createModel<MAGMA, MAGMAWidget>("MAGMA");

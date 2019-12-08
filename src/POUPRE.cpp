#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "dep/dr_wav/dr_wav.h"
#include "dep/AudioFile/AudioFile.h"
#include <iomanip>
#include "osdialog.h"

using namespace std;

struct channel {
	float start=0.0f;
	float len=1.0f;
	bool loop=false;
	float speed=1.0f;
	float head=0.0f;
	int gate=0;
};

struct POUPRE : Module {
	enum ParamIds {
		CHANNEL_PARAM,
		START_PARAM,
		LEN_PARAM,
		LOOP_PARAM,
		SPEED_PARAM,
		GATE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		TRIG_INPUT,
		GATE_INPUT,
		START_INPUT,
		LEN_INPUT,
		LOOP_INPUT,
		SPEED_INPUT,
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

	POUPRE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CHANNEL_PARAM, 0.0f, 15.0f, 0.0f);
		configParam(START_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(LEN_PARAM, 0.0f, 1.0f, 1.0f);
		configParam(LOOP_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(GATE_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(SPEED_PARAM, 0.0f, 10.0f, 1.0f);

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
							channels[i].loop = json_boolean_value(loopJ);
						json_t *gateJ= json_object_get(channelJ, "gate");
						if (gateJ)
							channels[i].gate = json_integer_value(gateJ);
				}
			}
		}
	}
};

void POUPRE::loadSample(std::string path) {
	waveFileName = rack::string::filename(path);
	waveExtension = rack::string::filenameExtension(rack::string::filename(path));
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

void POUPRE::saveSample() {
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

void POUPRE::process(const ProcessArgs &args) {
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
	}

	channels[currentChannel].start = params[START_PARAM].getValue();
	channels[currentChannel].len = params[LEN_PARAM].getValue();
	channels[currentChannel].speed = params[SPEED_PARAM].getValue();
	channels[currentChannel].loop = params[LOOP_PARAM].getValue() == 1.0f;
	channels[currentChannel].gate = params[GATE_PARAM].getValue();

	int c = std::max(inputs[TRIG_INPUT].getChannels(), 1);

	outputs[POLY_OUTPUT].setChannels(c);

	for (int i=0;i<c;i++) {
		float start = clamp(channels[i].start + (inputs[START_INPUT].isConnected() ? rescale(inputs[START_INPUT].getVoltage(i),0.0f,10.0f,0.0f,1.0f) : 0.0f), 0.0f, 1.0f);
		float len = clamp(channels[i].len + (inputs[LEN_INPUT].isConnected() ? rescale(inputs[LEN_INPUT].getVoltage(i),0.0f,10.0f,0.0f,1.0f) : 0.0f), 0.0f, 1.0f);
		float speed = clamp(channels[i].speed + (inputs[SPEED_INPUT].isConnected() ? rescale(inputs[SPEED_INPUT].getVoltage(i),0.0f,10.0f,0.0f,1.0f) : 0.0f), 0.0f, 10.0f);
		bool loop = channels[i].loop && (clamp(inputs[LOOP_INPUT].isConnected() ? inputs[LOOP_INPUT].getVoltage(i) : 1.0f, 0.0f, 1.0f) != 0.0f);
		int gate = inputs[GATE_INPUT].isConnected() ? rescale(inputs[SPEED_INPUT].getVoltage(i),0.0f,10.0f,0.0f,1.0f) : channels[i].gate;

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
			outputs[POLY_OUTPUT].setVoltage(5.0f * crossfade(playBuffer[xi], playBuffer[xi + 1], xf),i);
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

struct POUPREWidget : ModuleWidget {
	POUPREWidget(POUPRE *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/POUPRE.svg")));

		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(34.0f, 20.0f), module, POUPRE::SAMPLE_LIGHT));

		addParam(createParam<BidooBlueSnapKnob>(Vec(23.0f,35.0f), module, POUPRE::CHANNEL_PARAM));

		addParam(createParam<CKSS>(Vec(49.0f,92.5f), module, POUPRE::GATE_PARAM));
		addParam(createParam<CKSS>(Vec(49.0f,139.5f), module, POUPRE::LOOP_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(41.0f,182.0f), module, POUPRE::START_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(41.0f,229.0f), module, POUPRE::LEN_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(41.0f,276.0f), module, POUPRE::SPEED_PARAM));

		addInput(createInput<PJ301MPort>(Vec(7.0f, 95.0f), module, POUPRE::GATE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(7.0f, 142.0f), module, POUPRE::LOOP_INPUT));
		addInput(createInput<PJ301MPort>(Vec(7.0f, 189.0f), module, POUPRE::START_INPUT));
		addInput(createInput<PJ301MPort>(Vec(7.0f, 236.0f), module, POUPRE::LEN_INPUT));
		addInput(createInput<PJ301MPort>(Vec(7.0f, 283.0f), module, POUPRE::SPEED_INPUT));

		addInput(createInput<PJ301MPort>(Vec(7.0f, 330.0f), module, POUPRE::TRIG_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(43.5f, 330.0f), module, POUPRE::POLY_OUTPUT));
	}

	struct POUPREItem : MenuItem {
  	POUPRE *module;
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
		POUPRE *module = dynamic_cast<POUPRE*>(this->module);
		assert(module);

		menu->addChild(construct<MenuLabel>());
		menu->addChild(construct<POUPREItem>(&MenuItem::text, "Load sample", &POUPREItem::module, module));
	}
};

Model *modelPOUPRE = createModel<POUPRE, POUPREWidget>("POUPRE");

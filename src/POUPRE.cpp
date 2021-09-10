#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <iomanip>
#include "osdialog.h"
#include "dep/waves.hpp"
#include <mutex>

using namespace std;

struct channel {
	float start=0.0f;
	float len=1.0f;
	bool loop=false;
	float speed=1.0f;
	float head=0.0f;
	int gate=1;
};

struct POUPRE : Module {
	enum ParamIds {
		CHANNEL_PARAM,
		START_PARAM,
		LEN_PARAM,
		LOOP_PARAM,
		SPEED_PARAM,
		GATE_PARAM,
		PRESET_PARAM,
		NUM_PARAMS = PRESET_PARAM+4
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
		PRESET_LIGHT = SAMPLE_LIGHT+3,
		NUM_LIGHTS = PRESET_LIGHT+4
	};

	channel channels[16];
	int currentChannel=0;
	dsp::SchmittTrigger triggers[16];
	bool active[16]={false};
	bool loading=false;
	int sampleChannels;
	int sampleRate;
	int totalSampleCount;
	vector<dsp::Frame<1>> playBuffer;
	bool play = false;
	std::string lastPath;
	std::string waveFileName;
	std::string waveExtension;
	dsp::SchmittTrigger presetTriggers[4];
	std::mutex mylock;

	POUPRE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CHANNEL_PARAM, 0.0f, 15.0f, 0.0f);
		configParam(START_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(LEN_PARAM, 0.0f, 1.0f, 1.0f);
		configParam(LOOP_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(GATE_PARAM, 0.0f, 1.0f, 1.0f);
		configParam(SPEED_PARAM, 0.0f, 10.0f, 1.0f);
		configParam(PRESET_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(PRESET_PARAM+1, 0.0f, 1.0f, 0.0f);
		configParam(PRESET_PARAM+2, 0.0f, 1.0f, 0.0f);
		configParam(PRESET_PARAM+3, 0.0f, 1.0f, 0.0f);

		playBuffer.resize(0);
	}

	void process(const ProcessArgs &args) override;

	void loadSample();
	void saveSample();

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
		json_object_set_new(rootJ, "currentChannel", json_integer(currentChannel));
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
		json_t *currentChannelJ = json_object_get(rootJ, "currentChannel");
		if (currentChannelJ) {
			currentChannel = json_integer_value(currentChannelJ);
		}
		if (lastPathJ) {
			lastPath = json_string_value(lastPathJ);
			waveFileName = rack::string::filename(lastPath);
			waveExtension = rack::string::filenameBase(lastPath);
			if (!lastPath.empty()) loadSample();
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
		params[START_PARAM].setValue(channels[currentChannel].start);
		params[LEN_PARAM].setValue(channels[currentChannel].len);
		params[SPEED_PARAM].setValue(channels[currentChannel].speed);
		params[LOOP_PARAM].setValue(channels[currentChannel].loop ? 1.0f : 0.0f);
		params[GATE_PARAM].setValue(channels[currentChannel].gate);
	}

	void onSampleRateChange() override {
		if (!lastPath.empty()) loadSample();
	}
};

void POUPRE::loadSample() {
	appGet()->engine->yieldWorkers();
	playBuffer = waves::getMonoWav(lastPath, appGet()->engine->getSampleRate(), waveFileName, waveExtension, sampleChannels, sampleRate, totalSampleCount);
	loading = false;
}

void POUPRE::process(const ProcessArgs &args) {
	mylock.lock();
	if (loading) {
		loadSample();
	}
	mylock.unlock();
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

	lights[PRESET_LIGHT].setBrightness(lights[PRESET_LIGHT].getBrightness() -  lights[PRESET_LIGHT].getBrightness() * 0.0001f);
	lights[PRESET_LIGHT+1].setBrightness(lights[PRESET_LIGHT+1].getBrightness() -  lights[PRESET_LIGHT+1].getBrightness() * 0.0001f);
	lights[PRESET_LIGHT+2].setBrightness(lights[PRESET_LIGHT+2].getBrightness() -  lights[PRESET_LIGHT+2].getBrightness() * 0.0001f);
	lights[PRESET_LIGHT+3].setBrightness(lights[PRESET_LIGHT+3].getBrightness() -  lights[PRESET_LIGHT+3].getBrightness() * 0.0001f);

	for (int i=0;i<4;i++) {
		if (presetTriggers[i].process(params[PRESET_PARAM+i].getValue())) {
			if (i==0) {
				for (int j=0;j<8;j++) {
					channels[j].start = j*0.125f;
					channels[j].len = 0.125f;
					channels[j].speed = 1.0f;
					channels[j].loop = false;
					channels[j].gate = 1;
				}
				lights[PRESET_LIGHT].setBrightness(1.0f);
			}
			else if (i==1) {
				for (int j=0;j<16;j++) {
					channels[j].start = j*0.0625f;
					channels[j].len = 0.0625f;
					channels[j].speed = 1.0f;
					channels[j].loop = false;
					channels[j].gate = 1;
				}
				lights[PRESET_LIGHT+1].setBrightness(1.0f);
			}
			else if (i==2) {
				for (int j=0;j<16;j++) {
					channels[j].start = j*0.03125f;
					channels[j].len = 0.03125f;
					channels[j].speed = 1.0f;
					channels[j].loop = false;
					channels[j].gate = 1;
				}
				lights[PRESET_LIGHT+2].setBrightness(1.0f);
			}
			else {
				for (int j=0;j<16;j++) {
					channels[j].start = j*0.015625f;
					channels[j].len = 0.015625f;
					channels[j].speed = 1.0f;
					channels[j].loop = false;
					channels[j].gate = 1;
				}
				lights[PRESET_LIGHT+3].setBrightness(1.0f);
			}
			params[START_PARAM].setValue(channels[currentChannel].start);
			params[LEN_PARAM].setValue(channels[currentChannel].len);
			params[SPEED_PARAM].setValue(channels[currentChannel].speed);
			params[LOOP_PARAM].setValue(channels[currentChannel].loop ? 1.0f : 0.0f);
			params[GATE_PARAM].setValue(channels[currentChannel].gate);
		}
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

		if (active[i] && (playBuffer.size()!=0)) {
			int xi = channels[i].head;
			float xf = channels[i].head - xi;
			outputs[POLY_OUTPUT].setVoltage(5.0f * crossfade(playBuffer[xi].samples[0], playBuffer[xi + 1].samples[0], xf),i);
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

		addParam(createParam<MiniLEDButton>(Vec(66.0f, 20.0f), module, POUPRE::PRESET_PARAM));
		addParam(createParam<MiniLEDButton>(Vec(66.0f, 30.0f), module, POUPRE::PRESET_PARAM+1));
		addParam(createParam<MiniLEDButton>(Vec(66.0f, 62.0f), module, POUPRE::PRESET_PARAM+2));
		addParam(createParam<MiniLEDButton>(Vec(66.0f, 72.0f), module, POUPRE::PRESET_PARAM+3));
		addChild(createLight<SmallLight<BlueLight>>(Vec(66.0f, 20.0f), module, POUPRE::PRESET_LIGHT));
		addChild(createLight<SmallLight<BlueLight>>(Vec(66.0f, 30.0f), module, POUPRE::PRESET_LIGHT+1));
		addChild(createLight<SmallLight<BlueLight>>(Vec(66.0f, 62.0f), module, POUPRE::PRESET_LIGHT+2));
		addChild(createLight<SmallLight<BlueLight>>(Vec(66.0f, 72.0f), module, POUPRE::PRESET_LIGHT+3));

		addInput(createInput<PJ301MPort>(Vec(7.0f, 330.0f), module, POUPRE::TRIG_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(43.5f, 330.0f), module, POUPRE::POLY_OUTPUT));
	}

	struct POUPREItem : MenuItem {
  	POUPRE *module;
  	void onAction(const event::Action &e) override {
  		std::string dir = module->lastPath.empty() ? asset::user("") : rack::string::directory(module->lastPath);
  		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
  		if (path) {
				module->mylock.lock();
				module->lastPath = path;
				module->loading = true;
				module->mylock.unlock();
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

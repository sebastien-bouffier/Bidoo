#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <iomanip>
#include "osdialog.h"
#include "dep/waves.hpp"

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
	int gate=1;
	int filterType=0;
	float q=0.1f;
	float freq=1.0f;
	MultiFilter filter;
	int kill=-1;
	bool active=false;

	void randomize() {
		q=random::uniform();
		freq=random::uniform();
		filterType=random::uniform()*3;
		gate=random::uniform();
		loop=random::uniform();
		start=random::uniform();
		len=random::uniform();
		speed=random::uniform();
		kill=random::uniform()*16.0f-1.0f;
	}

	void reset() {
		q=0.1f;
		freq=1.0f;
		filterType=0;
		gate=1;
		loop=false;
		start=0.0f;
		len=1.0f;
		speed=1.0f;
		kill=-1.0f;
	}
};

struct MAGMA : Module {
	enum ParamIds {
		START_PARAM,
		LEN_PARAM,
		LOOP_PARAM,
		SPEED_PARAM,
		GATE_PARAM,
		Q_PARAM,
		FREQ_PARAM,
		FILTERTYPE_PARAM,
		CHANNEL_PARAM,
		KILL_PARAM,
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
		Q_INPUT,
		FREQ_INPUT,
		FILTERTYPE_INPUT,
		KILL_INPUT,
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

	MAGMA() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(START_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(LEN_PARAM, 0.0f, 1.0f, 1.0f);
		configParam(LOOP_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(GATE_PARAM, 0.0f, 1.0f, 1.0f);
		configParam(SPEED_PARAM, 0.0f, 10.0f, 1.0f);
		configParam(FILTERTYPE_PARAM, 0.0f, 3.0f, 0.0f);
		configParam(Q_PARAM, 0.1f, 1.0f, 0.1f);
		configParam(FREQ_PARAM, 0.0f, 1.0f, 1.0f);
		configParam(CHANNEL_PARAM, 0.0f, 15.0f, 0.0f);
		configParam(KILL_PARAM, -1.0f, 15.0f, -1.0f);
		configParam(PRESET_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(PRESET_PARAM+1, 0.0f, 1.0f, 0.0f);
		configParam(PRESET_PARAM+2, 0.0f, 1.0f, 0.0f);
		configParam(PRESET_PARAM+3, 0.0f, 1.0f, 0.0f);
		playBuffer.resize(0);
	}

	void process(const ProcessArgs &args) override;

	void loadSample(std::string path);
	void saveSample();

	void onRandomize() override {
		params[START_PARAM].setValue(random::uniform());
		params[LEN_PARAM].setValue(random::uniform());
		params[SPEED_PARAM].setValue(random::uniform());
		params[LOOP_PARAM].setValue(random::uniform());
		params[GATE_PARAM].setValue(random::uniform());
		params[FILTERTYPE_PARAM].setValue((int)(random::uniform()*3.0f));
		params[Q_PARAM].setValue(random::uniform());
		params[FREQ_PARAM].setValue(random::uniform());
		params[KILL_PARAM].setValue(random::uniform()*16-1);
		for (size_t i = 0; i<16 ; i++) {
			channels[i].randomize();
		}
	}

	void onReset() override {
		params[START_PARAM].setValue(0.0f);
		params[LEN_PARAM].setValue(1.0f);
		params[SPEED_PARAM].setValue(1.0f);
		params[LOOP_PARAM].setValue(0.0f);
		params[GATE_PARAM].setValue(0.0f);
		params[FILTERTYPE_PARAM].setValue(0.0f);
		params[Q_PARAM].setValue(0.1f);
		params[FREQ_PARAM].setValue(1.0f);
		params[KILL_PARAM].setValue(-1.0f);
		for (size_t i = 0; i<16 ; i++) {
			channels[i].reset();
		}
	}

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
			json_object_set_new(channelJ, "filterType", json_integer(channels[i].filterType));
			json_object_set_new(channelJ, "q", json_real(channels[i].q));
			json_object_set_new(channelJ, "freq", json_real(channels[i].freq));
			json_object_set_new(channelJ, "kill", json_integer(channels[i].kill));
			json_object_set_new(rootJ, ("channel"+ to_string(i)).c_str(), channelJ);
		}
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *currentChannelJ = json_object_get(rootJ, "currentChannel");
		if (currentChannelJ) {
			currentChannel = json_integer_value(currentChannelJ);
		}
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (lastPathJ) {
			lastPath = json_string_value(lastPathJ);
			waveFileName = rack::string::filename(lastPath);
			waveExtension = rack::string::filenameBase(lastPath);
			if (!lastPath.empty()) loadSample(lastPath);
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
					json_t *filterTypeJ= json_object_get(channelJ, "filterType");
					if (filterTypeJ)
						channels[i].filterType = json_integer_value(filterTypeJ);
					json_t *qJ= json_object_get(channelJ, "q");
					if (qJ)
						channels[i].q = json_number_value(qJ);
					json_t *freqJ= json_object_get(channelJ, "freq");
					if (freqJ)
						channels[i].freq = json_number_value(freqJ);
					json_t *killJ= json_object_get(channelJ, "kill");
					if (killJ)
						channels[i].kill = json_integer_value(killJ);
				}
			}
		}
		currentChannel=0;
		params[START_PARAM].setValue(channels[currentChannel].start);
		params[LEN_PARAM].setValue(channels[currentChannel].len);
		params[SPEED_PARAM].setValue(channels[currentChannel].speed);
		params[LOOP_PARAM].setValue(channels[currentChannel].loop ? 1.0f : 0.0f);
		params[GATE_PARAM].setValue(channels[currentChannel].gate);
		params[FILTERTYPE_PARAM].setValue(channels[currentChannel].filterType);
		params[Q_PARAM].setValue(channels[currentChannel].q);
		params[FREQ_PARAM].setValue(channels[currentChannel].freq);
		params[KILL_PARAM].setValue(channels[currentChannel].kill);
	}

	void onSampleRateChange() override {
		if (!lastPath.empty()) loadSample(lastPath);
	}
};

void MAGMA::loadSample(std::string path) {
	lastPath = path;
	float spmRate = appGet()->engine->getSampleRate();
	loading = true;
	appGet()->engine->yieldWorkers();
	playBuffer = waves::getMonoWav(path, spmRate, waveFileName, waveExtension, sampleChannels, sampleRate, totalSampleCount);
	loading = false;
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
			params[FILTERTYPE_PARAM].setValue(channels[currentChannel].filterType);
			params[Q_PARAM].setValue(channels[currentChannel].q);
			params[FREQ_PARAM].setValue(channels[currentChannel].freq);
			params[KILL_PARAM].setValue(channels[currentChannel].kill);
		}
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
		params[KILL_PARAM].setValue(channels[currentChannel].kill);
	}

	channels[currentChannel].start = params[START_PARAM].getValue();
	channels[currentChannel].len = params[LEN_PARAM].getValue();
	channels[currentChannel].speed = params[SPEED_PARAM].getValue();
	channels[currentChannel].loop = params[LOOP_PARAM].getValue() == 1.0f;
	channels[currentChannel].gate = params[GATE_PARAM].getValue();
	channels[currentChannel].filterType = params[FILTERTYPE_PARAM].getValue();
	channels[currentChannel].freq = params[FREQ_PARAM].getValue();
	channels[currentChannel].q = params[Q_PARAM].getValue();
	channels[currentChannel].kill = params[KILL_PARAM].getValue();


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

		if ((!channels[i].active || (gate==1.0f)) && (triggers[i].process(inputs[TRIG_INPUT].getVoltage(i)))) {
			channels[i].active = true;
			channels[i].head = start * playBuffer.size();
		}
		else if ((gate==0.0f) && (inputs[TRIG_INPUT].getVoltage(i) == 0.0f)) {
			channels[i].active = false;
		}

		for (int j=0;j<16;j++) {
			int kill = inputs[KILL_INPUT].isConnected() ? rescale(inputs[KILL_INPUT].getVoltage(j),0.0f,10.0f,-1.0f,15.0f) : channels[j].kill;
			if ((j!=i) && (kill==i) && channels[j].active) {
				channels[i].active = false;
				break;
			}
		}

		if (channels[i].active) {
			int xi = channels[i].head;
			float xf = channels[i].head - xi;
			float crossfaded = crossfade(playBuffer[xi].samples[0], playBuffer[xi + 1].samples[0], xf);
			channels[i].filter.setParams(freq, q, args.sampleRate);
			channels[i].filter.calcOutput(crossfaded);
			if (filterType == 0.0f) {
				outputs[POLY_OUTPUT].setVoltage(5.0f * crossfaded,i);
			}
			else if (filterType == 1.0f) {
				outputs[POLY_OUTPUT].setVoltage(5.0f * channels[i].filter.lp,i);
			}
			else if (filterType == 2.0f) {
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
					channels[i].active=false;
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

		addParam(createParam<BidooBlueKnob>(Vec(7.0f,85.0f), module, MAGMA::START_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(45.0f,85.0f), module, MAGMA::LEN_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(83.0f,85.0f), module, MAGMA::SPEED_PARAM));

		addParam(createParam<BidooBlueSnapKnob>(Vec(7.0f,135.0f), module, MAGMA::FILTERTYPE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(45.0f,135.0f), module, MAGMA::FREQ_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(83.0f,135.0f), module, MAGMA::Q_PARAM));

		addParam(createParam<CKSS>(Vec(14.5f,190.f), module, MAGMA::LOOP_PARAM));
		addParam(createParam<CKSS>(Vec(53.0f,190.f), module, MAGMA::GATE_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(83.0f,185.0f), module, MAGMA::KILL_PARAM));

		addInput(createInput<PJ301MPort>(Vec(4.0f, 236.0f), module, MAGMA::START_INPUT));
		addInput(createInput<PJ301MPort>(Vec(33.0f, 236.0f), module, MAGMA::LEN_INPUT));
		addInput(createInput<PJ301MPort>(Vec(62.5f, 236.0f), module, MAGMA::SPEED_INPUT));
		addInput(createInput<PJ301MPort>(Vec(91.5f, 236.0f), module, MAGMA::LOOP_INPUT));

		addInput(createInput<PJ301MPort>(Vec(4.0f, 283.0f), module, MAGMA::FILTERTYPE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(33.0f, 283.0f), module, MAGMA::FREQ_INPUT));
		addInput(createInput<PJ301MPort>(Vec(62.5f, 283.0f), module, MAGMA::Q_INPUT));
		addInput(createInput<PJ301MPort>(Vec(91.5f, 283.0f), module, MAGMA::GATE_INPUT));

		addInput(createInput<PJ301MPort>(Vec(7.0f, 330.0f), module, MAGMA::TRIG_INPUT));
		addInput(createInput<PJ301MPort>(Vec(47.5f, 330.0f), module, MAGMA::KILL_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(88.5f, 330.0f), module, MAGMA::POLY_OUTPUT));

		addParam(createParam<MiniLEDButton>(Vec(109.0f, 20.0f), module, MAGMA::PRESET_PARAM));
		addParam(createParam<MiniLEDButton>(Vec(109.0f, 30.0f), module, MAGMA::PRESET_PARAM+1));
		addParam(createParam<MiniLEDButton>(Vec(109.0f, 40.0f), module, MAGMA::PRESET_PARAM+2));
		addParam(createParam<MiniLEDButton>(Vec(109.0f, 50.0f), module, MAGMA::PRESET_PARAM+3));
		addChild(createLight<SmallLight<BlueLight>>(Vec(109.0f, 20.0f), module, MAGMA::PRESET_LIGHT));
		addChild(createLight<SmallLight<BlueLight>>(Vec(109.0f, 30.0f), module, MAGMA::PRESET_LIGHT+1));
		addChild(createLight<SmallLight<BlueLight>>(Vec(109.0f, 40.0f), module, MAGMA::PRESET_LIGHT+2));
		addChild(createLight<SmallLight<BlueLight>>(Vec(109.0f, 50.0f), module, MAGMA::PRESET_LIGHT+3));
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

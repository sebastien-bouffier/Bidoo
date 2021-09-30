#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "dsp/resampler.hpp"
#include "BidooComponents.hpp"
#include "dsp/ringbuffer.hpp"
#include <algorithm>
#include <cctype>
#include <atomic>
#include <sstream>
#include <thread>
#include "curl/curl.h"
#define MINIMP3_IMPLEMENTATION
#include "dep/minimp3/minimp3.h"


using namespace std;

struct threadReadData {
  dsp::DoubleRingBuffer<char,262144> *dataToDecodeRingBuffer;
  std::string url;
  std::string secUrl;
  std::atomic<bool> *dl;
  std::atomic<bool> *free;
  float sr;
};

struct threadDecodeData {
  dsp::DoubleRingBuffer<char,262144> *dataToDecodeRingBuffer;
  dsp::DoubleRingBuffer<dsp::Frame<2>,2097152> *dataAudioRingBuffer;
  mp3dec_t mp3d;
  std::atomic<bool> *dc;
  std::atomic<bool> *free;
  float sr;
};

size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  struct threadReadData *pData = (struct threadReadData *) userp;
  size_t realsize = size * nmemb;
  if ((pData->dl->load()) && (realsize < pData->dataToDecodeRingBuffer->capacity()))
  {
    memcpy(pData->dataToDecodeRingBuffer->endData(), contents, realsize);
    pData->dataToDecodeRingBuffer->endIncr(realsize);
    return realsize;
  }
  return 0;
}

size_t WriteUrlCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  struct threadReadData *pData = (struct threadReadData *) userp;
  size_t realsize = size * nmemb;
  pData->secUrl += (const char*) contents;
  return realsize;
}

void * threadDecodeTask(threadDecodeData data)
{
  data.free->store(false);
  mp3dec_frame_info_t info;
  dsp::DoubleRingBuffer<dsp::Frame<2>,4096> *tmpBuffer = new dsp::DoubleRingBuffer<dsp::Frame<2>,4096>();
  dsp::SampleRateConverter<2> conv;
  int inSize;
  int outSize;
  while (data.dc->load()) {
    short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    if (data.dataToDecodeRingBuffer->size() >= 65536) {
      int samples = mp3dec_decode_frame(&data.mp3d, (const uint8_t*)data.dataToDecodeRingBuffer->startData(), data.dataToDecodeRingBuffer->size(), pcm, &info);

      if (info.frame_bytes > 0) {
        data.dataToDecodeRingBuffer->startIncr(info.frame_bytes);
      }

      if (samples > 0) {
        if (info.channels == 1) {
          for(int i = 0; i < samples; i++) {
            if (!data.dc->load()) break;
            dsp::Frame<2> newFrame;
            newFrame.samples[0]=(float)pcm[i] * 30517578125e-15f;
            newFrame.samples[1]=(float)pcm[i] * 30517578125e-15f;
            tmpBuffer->push(newFrame);
          }
        }
        else {
          for(int i = 0; i < 2 * samples; i=i+2) {
            if (!data.dc->load()) break;
            dsp::Frame<2> newFrame;
            newFrame.samples[0]=(float)pcm[i] * 30517578125e-15f;
            newFrame.samples[1]=(float)pcm[i+1] * 30517578125e-15f;
            tmpBuffer->push(newFrame);
          }
        }

        conv.setRates(info.hz, data.sr);
        conv.setQuality(10);
        inSize = tmpBuffer->size();
        outSize = data.dataAudioRingBuffer->capacity();
        conv.process(tmpBuffer->startData(), &inSize, data.dataAudioRingBuffer->endData(), &outSize);
        tmpBuffer->startIncr(inSize);
        data.dataAudioRingBuffer->endIncr((size_t)outSize);
      }
    }
  }
  data.free->store(true);
  return 0;
}

void * threadReadTask(threadReadData data)
{
  data.free->store(false);

  CURL *curl;
  curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  std::string url;
  data.secUrl == "" ? url = data.url : url = data.secUrl;

  if ((rack::system::getExtension(data.url) == ".pls") || (rack::system::getExtension(data.url) == ".m3u")) {
    istringstream iss(url);
    for (std::string line; std::getline(iss, line); )
    {
      std::size_t found=line.find("http");
      if (found!=std::string::npos) {
        url = line.substr(found);
        url.erase(std::remove_if(url.begin(), url.end(), [](unsigned char x){return std::isspace(x);}), url.end());
        break;
      }
    }
  }

  std::cout << url << '\n';

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
  curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  data.free->store(true);

  return 0;
}


void * urlTask(threadReadData data)
{
  data.free->store(false);

  CURL *curl;
  curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_URL, data.url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteUrlCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
  data.secUrl = "";
  curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  data.free->store(true);

  thread iThread = thread(threadReadTask, data);
  iThread.detach();

  return 0;
}

struct ANTN : Module {
	enum ParamIds {
		URL_PARAM,
		TRIG_PARAM,
    GAIN_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		OUTL_OUTPUT,
		OUTR_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

  std::string url;
	dsp::SchmittTrigger trigTrigger;
  bool read = false;
  dsp::DoubleRingBuffer<dsp::Frame<2>,2097152> dataAudioRingBuffer;
  dsp::DoubleRingBuffer<char,262144> dataToDecodeRingBuffer;
  thread rThread, dThread;
  threadReadData rData;
  threadDecodeData dData;
  std::atomic<bool> tDl;
  std::atomic<bool> tDc;
  std::atomic<bool> trFree;
  std::atomic<bool> tdFree;

	ANTN() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(GAIN_PARAM, 0.0f, 3.f, 1.f, "Gain");
    configParam(TRIG_PARAM, 0.f, 1.f, 0.f, "Trig");

    tDl.store(true);
    tDc.store(true);
    trFree.store(true);
    tdFree.store(true);

    rData.dataToDecodeRingBuffer = &dataToDecodeRingBuffer;
    rData.dl = &tDl;
    rData.free = &trFree;

    dData.dataToDecodeRingBuffer = &dataToDecodeRingBuffer;
    dData.dataAudioRingBuffer = &dataAudioRingBuffer;
    dData.dc = &tDc;
    dData.free = &tdFree;
    mp3dec_init(&dData.mp3d);
	}

  ~ANTN() {
    tDc.store(false);
    while(!tdFree) {
    }

    tDl.store(false);
    while(!trFree) {
    }
  }

  json_t *dataToJson() override {
    json_t *rootJ = json_object();
    json_object_set_new(rootJ, "url", json_string(url.c_str()));
    return rootJ;
  }

  void dataFromJson(json_t *rootJ) override {
    json_t *urlJ = json_object_get(rootJ, "url");
  	if (urlJ)
  		url = json_string_value(urlJ);
  }

	void process(const ProcessArgs &args) override;

  void onSampleRateChange() override;
};

void ANTN::onSampleRateChange() {
  read = false;
  dataAudioRingBuffer.clear();
}

void ANTN::process(const ProcessArgs &args) {
	if (trigTrigger.process(params[TRIG_PARAM].value)) {
    tDc.store(false);
    while(!tdFree) {
    }

    tDl.store(false);
    while(!trFree) {
    }
    read = false;
    dataToDecodeRingBuffer.clear();
    dataAudioRingBuffer.clear();

    tDl.store(true);
    rData.url = url;
    rData.sr = args.sampleRate;
    if ((rack::system::getExtension(rData.url) == ".m3u") || (rack::system::getExtension(rData.url) == ".pls")) {
      rThread = thread(urlTask, std::ref(rData));
    }
    else {
      rThread = thread(threadReadTask, std::ref(rData));
    }
    rThread.detach();

    tDc.store(true);
    dData.sr = args.sampleRate;
    mp3dec_init(&dData.mp3d);
    dThread = thread(threadDecodeTask, std::ref(dData));
    dThread.detach();
	}

  if ((dataAudioRingBuffer.size()>64000) && (args.sampleRate<96000)) {
    read = true;
  }
  if ((dataAudioRingBuffer.size()>128000) && (args.sampleRate>=96000)) {
    read = true;
  }

  if (read) {
    dsp::Frame<2> currentFrame = *dataAudioRingBuffer.startData();
    outputs[OUTL_OUTPUT].setVoltage(5.0f*currentFrame.samples[0]*params[GAIN_PARAM].getValue());
    outputs[OUTR_OUTPUT].setVoltage(5.0f*currentFrame.samples[1]*params[GAIN_PARAM].getValue());
    dataAudioRingBuffer.startIncr(1);
  }
}

struct ANTNTextField : LedDisplayTextField {
  ANTN *module;

  ANTNTextField(ANTN *mod) {
    module = mod;
    std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
  	color = GREEN_BIDOO;
  	textOffset = Vec(3, 3);
    if (module != NULL) text = module->url;
  }

	void onChange(const event::Change &e) override {
    if (text.size() > 0) {
      std::string tText = text;
      tText.erase(std::remove_if(tText.begin(), tText.end(), [](unsigned char x){return std::isspace(x);}), tText.end());
      if (module != NULL) module->url = tText;
	  }
    LedDisplayTextField::onChange(e);
  };
};


struct ANTNDisplay : TransparentWidget {
	ANTN *module;

	ANTNDisplay() {

	}

void draw(NVGcontext *vg) override {
  if (module) {
    std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
    nvgSave(vg);
  	nvgStrokeWidth(vg, 1.0f);
    nvgStrokeColor(vg, BLUE_BIDOO);
    nvgFillColor(vg, BLUE_BIDOO);
  	nvgBeginPath(vg);
    nvgRoundedRect(vg,0,0,115.f * module->dataToDecodeRingBuffer.size()/262144.f,5.f,0.0f);
    nvgRoundedRect(vg,0,10.f,115.f * module->dataAudioRingBuffer.size()/2097152.f,5.f,0.0f);
  	nvgClosePath(vg);
    nvgStroke(vg);
  	nvgFill(vg);
  	nvgRestore(vg);
  }
}
};

struct ANTNWidget : ModuleWidget {
  TextField *textField;

	ANTNWidget(ANTN *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ANTN.svg")));

    if (module) {
			ANTNDisplay *display = new ANTNDisplay();
			display->module = module;
			display->box.pos = Vec(10.0f, 140.0f);
			display->box.size = Vec(70.0f, 70.0f);
			addChild(display);
		}

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

  	textField = new ANTNTextField(module);
  	textField->box.pos = Vec(5, 25);
  	textField->box.size = Vec(125, 100);
  	textField->multiline = true;
  	addChild(textField);

    addParam(createParam<BidooBlueKnob>(Vec(54, 183), module, ANTN::GAIN_PARAM));
  	addParam(createParam<BlueCKD6>(Vec(54, 245), module, ANTN::TRIG_PARAM));

  	static const float portX0[4] = {34, 67, 101};

  	addOutput(createOutput<TinyPJ301MPort>(Vec(portX0[1]-18, 340), module, ANTN::OUTL_OUTPUT));
  	addOutput(createOutput<TinyPJ301MPort>(Vec(portX0[1]+4, 340), module, ANTN::OUTR_OUTPUT));
  }
};

Model *modelANTN = createModel<ANTN, ANTNWidget>("antN");

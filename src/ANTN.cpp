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

const int DATASIZE = 220000000;
const int AUDIOSIZE = 524288;

struct threadReadData {
  dsp::DoubleRingBuffer<char,DATASIZE> *dataToDecodeRingBuffer;
  std::string url;
  std::string secUrl;
  std::atomic<bool> *dl;
  std::atomic<bool> *free;
  float sr;
};

struct threadDecodeData {
  dsp::DoubleRingBuffer<char,DATASIZE> *dataToDecodeRingBuffer;
  dsp::DoubleRingBuffer<dsp::Frame<2>,AUDIOSIZE> *dataAudioRingBuffer;
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
  dsp::DoubleRingBuffer<dsp::Frame<2>,AUDIOSIZE> *tmpBuffer = new dsp::DoubleRingBuffer<dsp::Frame<2>,AUDIOSIZE>();
  dsp::SampleRateConverter<2> conv;
  int inSize;
  int outSize;
  while (data.dc->load()) {
    short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    if ((data.dataToDecodeRingBuffer->size() >= 65536) && (data.dataAudioRingBuffer->capacity() >= 65536)) {
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
        url = url.substr(0, url.find("?", 0));
        break;
      }
    }
  }

  std::cout << "URL" << '\n';
  std::cout << data.url << '\n';
  std::cout << "secURL" << '\n';
  std::cout << data.secUrl << '\n';
  std::cout << "url" << '\n';
  std::cout << url << '\n';


  if ((rack::system::getExtension(url) == ".pls") || (rack::system::getExtension(url) == ".m3u")) {
    CURL *curl;
    curl = curl_easy_init();
    data.url=url;
    data.secUrl="";
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, data.url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteUrlCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    data.free->store(true);
    thread iThread = thread(threadReadTask, data);
    iThread.detach();
  }
  else {
    CURL *curl;
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    data.free->store(true);
  }

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

struct ANTN : BidooModule {
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
    ON_LIGHT,
		NUM_LIGHTS = ON_LIGHT+3
	};

  std::string url;
	dsp::SchmittTrigger trigTrigger;
  bool read = false;
  dsp::DoubleRingBuffer<dsp::Frame<2>,AUDIOSIZE> dataAudioRingBuffer;
  dsp::DoubleRingBuffer<char,DATASIZE> dataToDecodeRingBuffer;
  thread rThread, dThread;
  threadReadData rData;
  threadDecodeData dData;
  std::atomic<bool> tDl;
  std::atomic<bool> tDc;
  std::atomic<bool> trFree;
  std::atomic<bool> tdFree;
  bool dirty = false;

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
    json_t *rootJ = BidooModule::dataToJson();
    json_object_set_new(rootJ, "url", json_string(url.c_str()));
    return rootJ;
  }

  void dataFromJson(json_t *rootJ) override {
    BidooModule::dataFromJson(rootJ);
    json_t *urlJ = json_object_get(rootJ, "url");
  	if (urlJ)
  		url = json_string_value(urlJ);
    dirty = true;
  }

	void process(const ProcessArgs &args) override;

  void onSampleRateChange() override;

  void onReset() override {
		url = "";
		dirty = true;
	}
};

void ANTN::onSampleRateChange() {
  read = false;
  dataAudioRingBuffer.clear();
}

void ANTN::process(const ProcessArgs &args) {
	if (trigTrigger.process(params[TRIG_PARAM].value)) {
    if (read) {
      tDc.store(false);
      while(!tdFree) {
      }

      tDl.store(false);
      while(!trFree) {
      }
      read = false;
      dataToDecodeRingBuffer.clear();
      dataAudioRingBuffer.clear();
    }
    else {
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
	}

  if ((dataAudioRingBuffer.size()>64000) && (args.sampleRate<96000)) {
    read = true;
  }
  if ((dataAudioRingBuffer.size()>128000) && (args.sampleRate>=96000)) {
    read = true;
  }

  lights[ON_LIGHT].setBrightness(read ? 0.0f : 1.0f);
  lights[ON_LIGHT+1].setBrightness(read ? (dataAudioRingBuffer.size()<64000 ? 1.0f : 0.0f) : 0.0f);
  lights[ON_LIGHT+2].setBrightness(read ? 1.0f : 0.0f);

  if (read) {
    dsp::Frame<2> currentFrame = *dataAudioRingBuffer.startData();
    outputs[OUTL_OUTPUT].setVoltage(5.0f*currentFrame.samples[0]*params[GAIN_PARAM].getValue());
    outputs[OUTR_OUTPUT].setVoltage(5.0f*currentFrame.samples[1]*params[GAIN_PARAM].getValue());
    dataAudioRingBuffer.startIncr(1);
  }
}

struct ANTNTextField : LedDisplayTextField {
  ANTN *module;

	void step() override {
		LedDisplayTextField::step();
		if (module && module->dirty) {
			setText(module->url);
			module->dirty = false;
		}
	}

	void onChange(const ChangeEvent& e) override {
		if (module && getText().size()>0)
    {
      std::string tText = getText();
      tText.erase(std::remove_if(tText.begin(), tText.end(), [](unsigned char x){return std::isspace(x);}), tText.end());
      module->url = tText;
    }
	}
};

struct ANTNDisplay : LedDisplay {
	void setModule(ANTN* module) {
		ANTNTextField* textField = createWidget<ANTNTextField>(Vec(0, 0));
		textField->box.size = box.size;
		textField->multiline = true;
		textField->module = module;
		addChild(textField);
	}
};

struct ANTNBufferDisplay : TransparentWidget {
	ANTN *module;

	ANTNBufferDisplay() {

	}

  void drawLayer(const DrawArgs& args, int layer) override {
  	if (layer == 1) {
      if (module) {
        nvgSave(args.vg);
      	nvgStrokeWidth(args.vg, 1.0f);
        nvgStrokeColor(args.vg, BLUE_BIDOO);
        nvgFillColor(args.vg, BLUE_BIDOO);
      	nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg,0,0, box.size.x * std::min((float)module->dataToDecodeRingBuffer.size()/DATASIZE,1.0f),5.f,0.0f);
        nvgRoundedRect(args.vg,0,15.f,box.size.x * std::min((float)module->dataAudioRingBuffer.size()/AUDIOSIZE,1.0f),5.f,0.0f);
      	nvgClosePath(args.vg);
        nvgStroke(args.vg);
      	nvgFill(args.vg);
      	nvgRestore(args.vg);
      }
  	}
  	Widget::drawLayer(args, layer);
  }

};

template <typename BASE>
struct ANTNLight : BASE {
	ANTNLight() {
		this->box.size = mm2px(Vec(6.f, 6.f));
	}
};

struct ANTNWidget : BidooWidget {
	ANTNWidget(ANTN *module) {
		setModule(module);
    prepareThemes(asset::plugin(pluginInstance, "res/ANTN.svg"));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		ANTNBufferDisplay *display = new ANTNBufferDisplay();
		display->module = module;
		display->box.pos = Vec(10.0f, 140.0f);
		display->box.size = Vec(115.0f, 20.0f);
		addChild(display);


    ANTNDisplay* urlDisplay = createWidget<ANTNDisplay>(Vec(5, 25));
		urlDisplay->box.size = Vec(125, 100);
		urlDisplay->setModule(module);
		addChild(urlDisplay);

    addParam(createParam<BidooBlueKnob>(Vec(52.5f, 183), module, ANTN::GAIN_PARAM));

    addParam(createParam<LEDBezel>(Vec(56.5f, 246), module, ANTN::TRIG_PARAM));
		addChild(createLight<ANTNLight<RedGreenBlueLight>>(Vec(58.3f, 247.8f), module, ANTN::ON_LIGHT));

  	static const float portX0[4] = {34, 67, 101};

  	addOutput(createOutput<TinyPJ301MPort>(Vec(portX0[1]-18, 340), module, ANTN::OUTL_OUTPUT));
  	addOutput(createOutput<TinyPJ301MPort>(Vec(portX0[1]+4, 340), module, ANTN::OUTR_OUTPUT));
  }
};

Model *modelANTN = createModel<ANTN, ANTNWidget>("antN");

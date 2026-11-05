// curl pulls in windows.h: include it before any "using namespace std" (C++17 std::byte clash)
#include "curl/curl.h"
#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "dsp/resampler.hpp"
#include "BidooComponents.hpp"
#include "dsp/ringbuffer.hpp"
#include <algorithm>
#include <cctype>
#include <atomic>
#include <chrono>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#define MINIMP3_IMPLEMENTATION
#include "dep/minimp3/minimp3.h"

using namespace std;

// Decoded audio waiting to be played: 2 MB, about 5 s at 48 kHz.
const size_t AUDIOSIZE = 1 << 18;
// Compressed bytes needed before decoding, so minimp3 can sync reliably.
const size_t DECODE_MIN_BYTES = 16384;
// Give up on a stream that delivers this much without one MP3 frame.
const size_t NOT_MP3_BYTES = 512 * 1024;
// Size the panel's top bar is scaled to.
const size_t PENDING_DISPLAY_BYTES = 256 * 1024;
const size_t PLAYLIST_MAX_BYTES = 64 * 1024;
const int PLAYLIST_MAX_DEPTH = 3;
const auto STALL_TIMEOUT = std::chrono::seconds(15);

static std::string lowercase(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
  return s;
}

static bool isPlaylistUrl(const std::string &url) {
  std::string path = lowercase(url.substr(0, url.find_first_of("?#")));
  std::string ext = rack::system::getExtension(path);
  return ext == ".pls" || ext == ".m3u";
}

// First http(s) URL in a .pls or .m3u body, without its query string.
static std::string parsePlaylist(const std::string &body) {
  istringstream iss(body);
  for (std::string line; std::getline(iss, line); ) {
    if (!line.empty() && line[0] == '#') continue;
    std::size_t found = line.find("http");
    if (found == std::string::npos) continue;
    std::string url = line.substr(found);
    url.erase(std::remove_if(url.begin(), url.end(), [](unsigned char x){ return std::isspace(x); }), url.end());
    return url.substr(0, url.find('?'));
  }
  return "";
}

struct ANTN;

// Everything the worker needs for one transfer. Lives on the worker's stack.
struct StreamCtx {
  ANTN *module;
  uint32_t gen;
  std::chrono::steady_clock::time_point lastData;
  bool aborted = false;
  // playlist download
  std::string text;
  // MP3 stream
  std::vector<uint8_t> pending;
  size_t pos = 0;
  mp3dec_t mp3d;
  bool decodedAny = false;
  bool notMp3 = false;
  dsp::SampleRateConverter<2> conv;
  std::vector<dsp::Frame<2>> in;
  std::vector<dsp::Frame<2>> out;
};

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

  // Shared between the UI and the worker, never touched by the audio thread.
  std::mutex urlMutex;
  std::string url;
  std::atomic<bool> dirty{false};

  // Audio thread -> worker
  std::atomic<bool> playing{false};
  std::atomic<uint32_t> startCount{0};
  std::atomic<float> sampleRate{44100.f};
  std::atomic<bool> quit{false};
  // Worker -> audio thread: the stream of that generation has ended or failed.
  std::atomic<uint32_t> doneGen{0};
  std::atomic<uint32_t> errorGen{0};
  // Worker -> panel
  std::atomic<size_t> pendingBytes{0};

  // Single producer (worker), single consumer (audio thread).
  dsp::RingBuffer<dsp::Frame<2>, AUDIOSIZE> audio;

  // Audio thread only
	dsp::SchmittTrigger trigTrigger;
  uint32_t gen = 0;
  bool buffered = false;
  float blinkPhase = 0.f;

  std::thread worker;

	ANTN() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(GAIN_PARAM, 0.0f, 3.f, 1.f, "Gain");
    configButton(TRIG_PARAM, "Play/stop");
    configOutput(OUTL_OUTPUT, "Left");
    configOutput(OUTR_OUTPUT, "Right");
    worker = std::thread(&ANTN::workerLoop, this);
	}

  ~ANTN() {
    quit = true;
    if (worker.joinable())
      worker.join();
  }

  bool shouldAbort(uint32_t g) const {
    return quit || !playing || startCount != g;
  }

  size_t prebufferFrames() const {
    return std::min((size_t)sampleRate.load(), AUDIOSIZE / 2);
  }

  void setUrl(const std::string &u) {
    std::lock_guard<std::mutex> lock(urlMutex);
    url = u;
  }

  std::string getUrl() {
    std::lock_guard<std::mutex> lock(urlMutex);
    return url;
  }

  json_t *dataToJson() override {
    json_t *rootJ = BidooModule::dataToJson();
    json_object_set_new(rootJ, "url", json_string(getUrl().c_str()));
    return rootJ;
  }

  void dataFromJson(json_t *rootJ) override {
    BidooModule::dataFromJson(rootJ);
    json_t *urlJ = json_object_get(rootJ, "url");
  	if (urlJ)
  		setUrl(json_string_value(urlJ));
    dirty = true;
  }

	void process(const ProcessArgs &args) override;

  void onSampleRateChange(const SampleRateChangeEvent &e) override {
    // What is buffered was resampled for the old rate.
    audio.clear();
    buffered = false;
  }

  void onReset() override {
    playing = false;
		setUrl("");
		dirty = true;
	}

  void workerLoop();
  bool runStream(std::string u, uint32_t g);
  CURL *createCurl(StreamCtx &ctx, const std::string &u);
  void decode(StreamCtx &ctx, bool flush);
  void pushFrames(StreamCtx &ctx, const dsp::Frame<2> *frames, int n);
};

static int xferInfoCallback(void *userp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  StreamCtx *ctx = (StreamCtx*) userp;
  if (ctx->module->shouldAbort(ctx->gen) || std::chrono::steady_clock::now() - ctx->lastData > STALL_TIMEOUT) {
    ctx->aborted = true;
    return 1;
  }
  return 0;
}

static size_t writeTextCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  StreamCtx *ctx = (StreamCtx*) userp;
  size_t realsize = size * nmemb;
  if (ctx->text.size() + realsize > PLAYLIST_MAX_BYTES)
    return 0;
  ctx->text.append((const char*) contents, realsize);
  ctx->lastData = std::chrono::steady_clock::now();
  return realsize;
}

static size_t writeStreamCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  StreamCtx *ctx = (StreamCtx*) userp;
  size_t realsize = size * nmemb;
  const uint8_t *bytes = (const uint8_t*) contents;
  ctx->pending.insert(ctx->pending.end(), bytes, bytes + realsize);
  ctx->lastData = std::chrono::steady_clock::now();
  ctx->module->decode(*ctx, false);
  // Returning less than realsize makes curl stop the transfer.
  if (ctx->aborted || ctx->notMp3)
    return 0;
  return realsize;
}

CURL *ANTN::createCurl(StreamCtx &ctx, const std::string &u) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return NULL;
  std::string caPath = asset::system("cacert.pem");
  curl_easy_setopt(curl, CURLOPT_URL, u.c_str());
  curl_easy_setopt(curl, CURLOPT_CAINFO, caPath.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "VCV Rack Bidoo antN");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferInfoCallback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
  ctx.lastData = std::chrono::steady_clock::now();
  return curl;
}

void ANTN::pushFrames(StreamCtx &ctx, const dsp::Frame<2> *frames, int n) {
  // Back-pressure: wait for the audio thread to make room. Not reading from
  // the socket meanwhile lets TCP slow the server down.
  while (audio.capacity() < (size_t)n) {
    if (shouldAbort(ctx.gen)) {
      ctx.aborted = true;
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  audio.pushBuffer(frames, n);
  // Waiting for room is not a stalled connection.
  ctx.lastData = std::chrono::steady_clock::now();
}

void ANTN::decode(StreamCtx &ctx, bool flush) {
  mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  mp3dec_frame_info_t info;

  while (!ctx.aborted && ctx.pending.size() - ctx.pos >= (flush ? 1 : DECODE_MIN_BYTES)) {
    int samples = mp3dec_decode_frame(&ctx.mp3d, ctx.pending.data() + ctx.pos, (int)(ctx.pending.size() - ctx.pos), pcm, &info);
    if (info.frame_bytes == 0)
      break; // needs more data
    ctx.pos += info.frame_bytes;
    if (samples <= 0)
      continue; // skipped ID3 tag or garbage

    ctx.decodedAny = true;
    ctx.in.resize(samples);
    for (int i = 0; i < samples; i++) {
      ctx.in[i].samples[0] = pcm[i * info.channels] * (1.f / 32768.f);
      ctx.in[i].samples[1] = pcm[i * info.channels + info.channels - 1] * (1.f / 32768.f);
    }

    int outRate = (int)sampleRate.load();
    ctx.conv.setRates(info.hz, outRate);
    int inPos = 0;
    while (inPos < samples && !ctx.aborted) {
      int inFrames = samples - inPos;
      int outFrames = inFrames * outRate / info.hz + 64;
      ctx.out.resize(outFrames);
      ctx.conv.process(ctx.in.data() + inPos, &inFrames, ctx.out.data(), &outFrames);
      if (inFrames == 0 && outFrames == 0)
        break;
      inPos += inFrames;
      pushFrames(ctx, ctx.out.data(), outFrames);
    }
  }

  // Drop consumed bytes now and then rather than on every frame.
  if (ctx.pos > 65536) {
    ctx.pending.erase(ctx.pending.begin(), ctx.pending.begin() + ctx.pos);
    ctx.pos = 0;
  }
  if (!ctx.decodedAny && ctx.pending.size() > NOT_MP3_BYTES)
    ctx.notMp3 = true;
  pendingBytes = ctx.pending.size() - ctx.pos;
}

// Returns false if nothing could be played (bad URL, not MP3, network error).
bool ANTN::runStream(std::string u, uint32_t g) {
  for (int depth = 0; isPlaylistUrl(u); depth++) {
    if (depth >= PLAYLIST_MAX_DEPTH)
      return false;
    StreamCtx ctx;
    ctx.module = this;
    ctx.gen = g;
    CURL *curl = createCurl(ctx, u);
    if (!curl)
      return false;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeTextCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (shouldAbort(g))
      return false;
    if (res != CURLE_OK) {
      WARN("antN: cannot fetch playlist %s: %s", u.c_str(), curl_easy_strerror(res));
      return false;
    }
    std::string next = parsePlaylist(ctx.text);
    if (next.empty()) {
      WARN("antN: no stream URL in playlist %s", u.c_str());
      return false;
    }
    u = next;
  }

  StreamCtx ctx;
  ctx.module = this;
  ctx.gen = g;
  mp3dec_init(&ctx.mp3d);
  ctx.conv.setQuality(10);
  CURL *curl = createCurl(ctx, u);
  if (!curl)
    return false;
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStreamCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
  INFO("antN: streaming %s", u.c_str());
  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  if (!shouldAbort(g)) {
    if (ctx.notMp3)
      WARN("antN: no MP3 data in %s", u.c_str());
    else if (res != CURLE_OK)
      WARN("antN: stream %s failed: %s", u.c_str(), curl_easy_strerror(res));
    else
      decode(ctx, true); // a finite file has ended: decode what is left
  }
  pendingBytes = 0;
  return ctx.decodedAny;
}

void ANTN::workerLoop() {
  uint32_t handled = startCount;
  while (!quit) {
    uint32_t g = startCount;
    if (!playing || g == handled) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }
    handled = g;
    bool ok = runStream(getUrl(), g);
    if (startCount != g)
      continue; // restarted meanwhile: the new generation takes over
    if (!ok)
      errorGen = g;
    // The audio thread plays out what is buffered, then stops.
    doneGen = g;
  }
}

void ANTN::process(const ProcessArgs &args) {
  sampleRate.store(args.sampleRate, std::memory_order_relaxed);

	if (trigTrigger.process(params[TRIG_PARAM].getValue())) {
    if (playing) {
      playing = false;
    }
    else {
      gen = ++startCount;
      buffered = false;
      playing = true;
    }
	}

  float l = 0.f;
  float r = 0.f;
  if (playing) {
    bool done = (doneGen == gen);
    if (!buffered) {
      if (audio.size() >= prebufferFrames() || (done && !audio.empty()))
        buffered = true;
      else if (done)
        playing = false;
    }
    if (buffered) {
      if (audio.empty()) {
        // Underrun: wait for the buffer to fill again, or stop if the stream is over.
        buffered = false;
        if (done)
          playing = false;
      }
      else {
        dsp::Frame<2> frame = audio.shift();
        l = frame.samples[0];
        r = frame.samples[1];
      }
    }
  }
  else {
    audio.clear();
    buffered = false;
  }

  float gain = 5.f * params[GAIN_PARAM].getValue();
  outputs[OUTL_OUTPUT].setVoltage(gain * l);
  outputs[OUTR_OUTPUT].setVoltage(gain * r);

  // Red: stopped (blinking after a failed stream). Green: connecting or buffering.
  // Blue: playing, with green added when the buffer runs low.
  blinkPhase += args.sampleTime * 2.f;
  if (blinkPhase >= 1.f)
    blinkPhase -= 1.f;
  bool failed = !playing && gen != 0 && errorGen == gen;
  bool low = buffered && audio.size() < prebufferFrames() / 4;
  lights[ON_LIGHT].setBrightness(playing ? 0.f : (failed && blinkPhase >= 0.5f ? 0.f : 1.f));
  lights[ON_LIGHT+1].setBrightness((playing && !buffered) || low ? 1.f : 0.f);
  lights[ON_LIGHT+2].setBrightness(buffered ? 1.f : 0.f);
}

struct ANTNTextField : LedDisplayTextField {
  ANTN *module;

	void step() override {
		LedDisplayTextField::step();
		if (module && module->dirty) {
			setText(module->getUrl());
			module->dirty = false;
		}
	}

	void onChange(const ChangeEvent& e) override {
		if (module)
    {
      std::string tText = getText();
      tText.erase(std::remove_if(tText.begin(), tText.end(), [](unsigned char x){return std::isspace(x);}), tText.end());
      module->setUrl(tText);
      // An emptied field clears the URL and stops what was playing.
      if (tText.empty())
        module->playing = false;
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
        nvgRoundedRect(args.vg,0,0, box.size.x * std::min((float)module->pendingBytes/PENDING_DISPLAY_BYTES,1.0f),5.f,0.0f);
        nvgRoundedRect(args.vg,0,15.f,box.size.x * std::min((float)module->audio.size()/AUDIOSIZE,1.0f),5.f,0.0f);
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

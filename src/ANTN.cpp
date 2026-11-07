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
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#define MINIMP3_IMPLEMENTATION
#include "dep/minimp3/minimp3.h"
#include "dep/faad2/include/neaacdec.h"
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PULLDATA_API
#include "dep/stb_vorbis/stb_vorbis.c"

using namespace std;

// Decoded audio waiting to be played: 2 MB, about 5 s at 48 kHz.
const size_t AUDIOSIZE = 1 << 18;
// Compressed bytes needed before decoding, so minimp3 and FAAD2 always see whole frames.
const size_t DECODE_MIN_BYTES = 16384;
// Bytes looked at to tell the format (and a playlist or web page from audio).
const size_t DETECT_BYTES = 4096;
// Give up on a stream that delivers this much without one decoded frame.
const size_t NOT_SUPPORTED_BYTES = 512 * 1024;
// An Ogg packet larger than this is not audio we can use.
const size_t OGG_MAX_BUFFER = 1024 * 1024;
// Size the panel's top bar is scaled to.
const size_t PENDING_DISPLAY_BYTES = 256 * 1024;
const size_t PLAYLIST_MAX_BYTES = 64 * 1024;
const int PLAYLIST_MAX_DEPTH = 3;
const auto STALL_TIMEOUT = std::chrono::seconds(15);

static std::string lowercase(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
  return s;
}

static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

static std::string urlExtension(const std::string &url) {
  std::string path = lowercase(url.substr(0, url.find_first_of("?#")));
  return rack::system::getExtension(path);
}

static bool isPlaylistUrl(const std::string &url) {
  std::string ext = urlExtension(url);
  return ext == ".pls" || ext == ".m3u";
}

static bool isHlsUrl(const std::string &url) {
  return urlExtension(url) == ".m3u8";
}

static bool isHlsText(const std::string &body) {
  return body.find("#EXT-X-") != std::string::npos;
}

// First http(s) URL in a .pls or .m3u body.
static std::string parsePlaylist(const std::string &body) {
  istringstream iss(body);
  for (std::string line; std::getline(iss, line); ) {
    if (!line.empty() && line[0] == '#') continue;
    std::size_t found = line.find("http");
    if (found == std::string::npos) continue;
    std::string url = line.substr(found);
    url.erase(std::remove_if(url.begin(), url.end(), [](unsigned char x){ return std::isspace(x); }), url.end());
    return url;
  }
  return "";
}

static std::string formatRate(int hz) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%g kHz", hz / 1000.0);
  return buf;
}

// ADTS header: 12-bit sync and layer 00 (an MP3 header never has layer 00).
static inline bool isAdtsSync(const uint8_t *p) {
  return p[0] == 0xFF && (p[1] & 0xF6) == 0xF0;
}

static inline size_t adtsFrameLength(const uint8_t *p) {
  return ((size_t)(p[3] & 3) << 11) | ((size_t)p[4] << 3) | (p[5] >> 5);
}

// Length of the complete Ogg page at p, 0 if more bytes are needed.
static size_t oggPageLength(const uint8_t *p, size_t n) {
  if (n < 27)
    return 0;
  size_t segs = p[26];
  if (n < 27 + segs)
    return 0;
  size_t len = 27 + segs;
  for (size_t i = 0; i < segs; i++)
    len += p[27 + i];
  return n >= len ? len : 0;
}

enum StreamKind { KIND_UNDECIDED, KIND_AUDIO, KIND_PLAYLIST, KIND_UNSUPPORTED };
enum StreamFormat { FMT_MP3, FMT_AAC, FMT_VORBIS };

struct ANTN;

// Everything the worker needs for one transfer. Lives on the worker's stack.
struct StreamCtx {
  ANTN *module;
  uint32_t gen;
  std::chrono::steady_clock::time_point lastData;
  bool aborted = false;
  // HTTP headers (of the last response when redirected)
  std::string contentType;
  std::string icyName;
  StreamKind kind = KIND_UNDECIDED;
  StreamFormat format = FMT_MP3;
  // playlist body
  std::string text;
  // compressed stream
  std::vector<uint8_t> pending;
  size_t pos = 0;
  size_t received = 0;
  bool decodedAny = false;
  // MP3
  mp3dec_t mp3d;
  // AAC (ADTS)
  NeAACDecHandle aac = NULL;
  bool aacInit = false;
  int aacErrors = 0;
  // Ogg Vorbis: whole pages are fed to stb_vorbis from oggBuf.
  stb_vorbis *vorbis = NULL;
  std::vector<uint8_t> oggBuf;
  size_t oggPos = 0;
  bool oggStarted = false;
  // Stats for the status line
  std::string label;
  int rate = 0;
  double codedBytes = 0.0;
  double seconds = 0.0;
  double statusAt = -1.0;
  dsp::SampleRateConverter<2> conv;
  std::vector<dsp::Frame<2>> in;
  std::vector<dsp::Frame<2>> out;

  ~StreamCtx() {
    if (aac)
      NeAACDecClose(aac);
    if (vorbis)
      stb_vorbis_close(vorbis);
  }
};

// A station from res/stations.json, loaded once at plugin init.
struct AntnStation {
  std::string name, country, continent, category, format, url;
};
static std::vector<AntnStation> antnStations;
static const char *const ANTN_CONTINENTS[] = {"Europe", "North America", "Latin America", "Africa", "Asia", "Oceania"};
static const char *const ANTN_CATEGORIES[] = {"News & talk", "Music"};

void antnLoadStations() {
  antnStations.clear();
  std::string path = asset::plugin(pluginInstance, "res/stations.json");
  std::vector<uint8_t> data;
  try {
    data = rack::system::readFile(path);
  }
  catch (Exception &e) {
    WARN("antN: cannot read %s: %s", path.c_str(), e.what());
    return;
  }
  json_error_t error;
  json_t *rootJ = json_loadb((const char*) data.data(), data.size(), 0, &error);
  if (!rootJ) {
    WARN("antN: %s line %d: %s", path.c_str(), error.line, error.text);
    return;
  }
  auto str = [](json_t *o, const char *key) {
    const char *s = json_string_value(json_object_get(o, key));
    return std::string(s ? s : "");
  };
  size_t i;
  json_t *stationJ;
  json_array_foreach(json_object_get(rootJ, "stations"), i, stationJ) {
    AntnStation s;
    s.name = str(stationJ, "name");
    s.country = str(stationJ, "country");
    s.continent = str(stationJ, "continent");
    s.category = str(stationJ, "category");
    s.format = str(stationJ, "format");
    s.url = str(stationJ, "url");
    if (!s.name.empty() && !s.url.empty())
      antnStations.push_back(s);
  }
  json_decref(rootJ);
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

  // Shared between the UI and the worker, never touched by the audio thread.
  std::mutex urlMutex;
  std::string url;
  std::string stationName;
  std::atomic<bool> dirty{false};
  std::mutex statusMutex;
  std::string statusText;
  std::string icyName;
  std::atomic<bool> failed{false};

  // Audio thread -> worker
  std::atomic<bool> playing{false};
  std::atomic<uint32_t> startCount{0};
  std::atomic<float> sampleRate{44100.f};
  std::atomic<bool> quit{false};
  // UI -> audio thread: (re)start with the current URL (station menu).
  std::atomic<bool> startRequest{false};
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
  // Declicking: a 5 ms fade in when the audio starts or comes back, a 5 ms fade
  // out on stop (on the frames still buffered), at the end of the stream and on
  // an underrun (on the last frame held); the gain knob glides over 5 ms.
  float env = 0.f;
  float lastL = 0.f, lastR = 0.f;
  float smoothGain = -1.f;
  float smoothRate = 0.f;
  float smoothCoef = 1.f;

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

  void setStationName(const std::string &name) {
    std::lock_guard<std::mutex> lock(urlMutex);
    stationName = name;
  }

  std::string getStationName() {
    std::lock_guard<std::mutex> lock(urlMutex);
    return stationName;
  }

  // Station menu: set the URL and play it at once.
  void chooseStation(const AntnStation &s) {
    {
      std::lock_guard<std::mutex> lock(urlMutex);
      url = s.url;
      stationName = s.name;
    }
    dirty = true;
    startRequest = true;
  }

  void setStatus(const std::string &s) {
    std::lock_guard<std::mutex> lock(statusMutex);
    statusText = s;
  }

  void setIcyName(const std::string &s) {
    std::lock_guard<std::mutex> lock(statusMutex);
    icyName = s;
  }

  // The display's two lines: station (chosen, else announced by the server) and status.
  void getDisplayLines(std::string &name, std::string &status) {
    std::string u;
    {
      std::lock_guard<std::mutex> lock(urlMutex);
      name = stationName;
      u = url;
    }
    std::lock_guard<std::mutex> lock(statusMutex);
    if (name.empty())
      name = icyName;
    if (u.empty())
      status = "right-click: Stations";
    else if (playing || failed)
      status = statusText;
    else
      status = "stopped";
  }

  json_t *dataToJson() override {
    json_t *rootJ = BidooModule::dataToJson();
    json_object_set_new(rootJ, "url", json_string(getUrl().c_str()));
    json_object_set_new(rootJ, "station", json_string(getStationName().c_str()));
    return rootJ;
  }

  void dataFromJson(json_t *rootJ) override {
    BidooModule::dataFromJson(rootJ);
    json_t *urlJ = json_object_get(rootJ, "url");
  	if (urlJ)
  		setUrl(json_string_value(urlJ));
    json_t *stationJ = json_object_get(rootJ, "station");
    setStationName(json_string_value(stationJ) ? json_string_value(stationJ) : "");
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
    setStationName("");
		dirty = true;
	}

  void workerLoop();
  bool runStream(std::string u, uint32_t g);
  CURL *createCurl(StreamCtx &ctx, const std::string &u);
  void detect(StreamCtx &ctx, bool flush);
  void unsupported(StreamCtx &ctx, const std::string &why);
  void decode(StreamCtx &ctx, bool flush);
  void decodeMp3(StreamCtx &ctx, bool flush);
  void decodeAac(StreamCtx &ctx, bool flush);
  void decodeVorbis(StreamCtx &ctx);
  void emit(StreamCtx &ctx, int frames, int rate, const char *label, size_t codedBytes);
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

static size_t headerCallback(char *buffer, size_t size, size_t nitems, void *userp) {
  StreamCtx *ctx = (StreamCtx*) userp;
  size_t n = size * nitems;
  std::string line = trim(std::string(buffer, n));
  std::string low = lowercase(line);
  if (low.compare(0, 5, "http/") == 0 || low.compare(0, 4, "icy ") == 0) {
    // a new response (redirect): forget the previous one's headers
    ctx->contentType.clear();
    ctx->icyName.clear();
  }
  else if (low.compare(0, 13, "content-type:") == 0) {
    ctx->contentType = trim(low.substr(13, low.find(';') == std::string::npos ? std::string::npos : low.find(';') - 13));
  }
  else if (low.compare(0, 9, "icy-name:") == 0) {
    ctx->icyName = trim(line.substr(9));
  }
  return n;
}

static size_t writeStreamCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  StreamCtx *ctx = (StreamCtx*) userp;
  size_t realsize = size * nmemb;
  const uint8_t *bytes = (const uint8_t*) contents;
  ctx->lastData = std::chrono::steady_clock::now();
  ctx->received += realsize;
  if (ctx->kind == KIND_PLAYLIST) {
    if (ctx->text.size() + realsize > PLAYLIST_MAX_BYTES)
      return 0;
    ctx->text.append((const char*) bytes, realsize);
    return realsize;
  }
  ctx->pending.insert(ctx->pending.end(), bytes, bytes + realsize);
  ctx->module->decode(*ctx, false);
  if (ctx->kind == KIND_PLAYLIST) {
    ctx->text.assign((const char*) ctx->pending.data() + ctx->pos, ctx->pending.size() - ctx->pos);
    ctx->pending.clear();
    ctx->pos = 0;
  }
  // Returning less than realsize makes curl stop the transfer.
  if (ctx->aborted || ctx->kind == KIND_UNSUPPORTED)
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
  // SHOUTcast v1 servers answer "ICY 200 OK", which curl only passes on as HTTP/0.9:
  // their headers then arrive with the body and detect() reads them.
  curl_easy_setopt(curl, CURLOPT_HTTP09_ALLOWED, 1L);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStreamCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
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

void ANTN::unsupported(StreamCtx &ctx, const std::string &why) {
  ctx.kind = KIND_UNSUPPORTED;
  setStatus(why);
}

// Picks the decoder from the first bytes, then the Content-Type.
void ANTN::detect(StreamCtx &ctx, bool flush) {
  const uint8_t *d = ctx.pending.data() + ctx.pos;
  size_t n = ctx.pending.size() - ctx.pos;

  // SHOUTcast v1 headers in the body: read them, then drop them.
  if (n >= 4 && memcmp(d, "ICY ", 4) == 0) {
    std::string text((const char*) d, std::min(n, DETECT_BYTES));
    size_t end = text.find("\r\n\r\n");
    if (end == std::string::npos) {
      if (n < DETECT_BYTES && !flush)
        return; // wait for the end of the headers
      unsupported(ctx, "not a supported format");
      return;
    }
    istringstream iss(text.substr(0, end));
    for (std::string line; std::getline(iss, line); )
      headerCallback(&line[0], 1, line.size(), &ctx);
    ctx.pos += end + 4;
    d = ctx.pending.data() + ctx.pos;
    n = ctx.pending.size() - ctx.pos;
    if (n < DETECT_BYTES && !flush)
      return; // decide on the audio itself
  }
  const std::string &ct = ctx.contentType;

  std::string head = lowercase(trim(std::string((const char*) d, std::min(n, (size_t)256))));
  if (head.compare(0, 3, "\xef\xbb\xbf") == 0)
    head = head.substr(3);
  if (ct == "audio/x-mpegurl" || ct == "audio/mpegurl" || ct == "application/x-mpegurl" || ct == "application/vnd.apple.mpegurl"
      || ct == "audio/x-scpls" || ct == "audio/scpls" || ct == "application/pls+xml"
      || head.compare(0, 7, "#extm3u") == 0 || head.compare(0, 10, "[playlist]") == 0
      || head.compare(0, 7, "http://") == 0 || head.compare(0, 8, "https://") == 0) {
    ctx.kind = KIND_PLAYLIST;
    return;
  }
  if (ct == "text/html" || head.compare(0, 9, "<!doctype") == 0 || head.compare(0, 5, "<html") == 0) {
    unsupported(ctx, "not a supported format (web page)");
    return;
  }
  if (ct == "audio/flac" || ct == "audio/x-flac" || ct == "audio/opus" || ct == "audio/mp4" || ct == "audio/x-m4a"
      || ct == "audio/wav" || ct == "audio/x-wav" || ct.compare(0, 6, "video/") == 0) {
    unsupported(ctx, "not a supported format (" + ct + ")");
    return;
  }

  // Skip an ID3v2 tag, wait for its end.
  size_t off = 0;
  if (n >= 10 && memcmp(d, "ID3", 3) == 0) {
    off = 10 + (((size_t)(d[6] & 0x7f) << 21) | ((size_t)(d[7] & 0x7f) << 14) | ((size_t)(d[8] & 0x7f) << 7) | (d[9] & 0x7f));
    if (d[5] & 0x10)
      off += 10;
    if (off + DETECT_BYTES > n && !flush)
      return;
  }

  ctx.kind = KIND_AUDIO;
  if (off + 4 <= n && memcmp(d + off, "OggS", 4) == 0) {
    ctx.format = FMT_VORBIS;
    return;
  }
  // Two ADTS headers one frame apart.
  for (size_t i = off; i + 7 <= n && i < off + DETECT_BYTES; i++) {
    if (!isAdtsSync(d + i))
      continue;
    size_t len = adtsFrameLength(d + i);
    if (len >= 7 && i + len + 2 <= n && isAdtsSync(d + i + len)) {
      ctx.format = FMT_AAC;
      return;
    }
  }
  if (ct == "audio/aac" || ct == "audio/aacp" || ct == "audio/x-aac" || ct == "audio/x-aacp")
    ctx.format = FMT_AAC;
  else if (ct == "application/ogg" || ct == "audio/ogg" || ct == "audio/vorbis" || ct == "audio/x-ogg")
    ctx.format = FMT_VORBIS;
  else
    ctx.format = FMT_MP3; // audio/mpeg, or unknown: minimp3 finds its own sync
}

void ANTN::decode(StreamCtx &ctx, bool flush) {
  if (ctx.kind == KIND_UNDECIDED && (ctx.pending.size() - ctx.pos >= DETECT_BYTES || flush)) {
    detect(ctx, flush);
    // the name the server announces (shown when no station was chosen from the menu)
    if (ctx.kind == KIND_AUDIO && !ctx.icyName.empty())
      setIcyName(ctx.icyName);
  }

  if (ctx.kind == KIND_AUDIO) {
    if (ctx.format == FMT_MP3)
      decodeMp3(ctx, flush);
    else if (ctx.format == FMT_AAC)
      decodeAac(ctx, flush);
    else
      decodeVorbis(ctx);
  }

  // Drop consumed bytes now and then rather than on every frame.
  if (ctx.pos > 65536) {
    ctx.pending.erase(ctx.pending.begin(), ctx.pending.begin() + ctx.pos);
    ctx.pos = 0;
  }
  if (!ctx.decodedAny && ctx.kind != KIND_PLAYLIST && ctx.kind != KIND_UNSUPPORTED && ctx.received > NOT_SUPPORTED_BYTES)
    unsupported(ctx, "not a supported format");
  pendingBytes = ctx.pending.size() - ctx.pos;
}

void ANTN::decodeMp3(StreamCtx &ctx, bool flush) {
  mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  mp3dec_frame_info_t info;

  while (!ctx.aborted && ctx.pending.size() - ctx.pos >= (flush ? 1 : DECODE_MIN_BYTES)) {
    int samples = mp3dec_decode_frame(&ctx.mp3d, ctx.pending.data() + ctx.pos, (int)(ctx.pending.size() - ctx.pos), pcm, &info);
    if (info.frame_bytes == 0)
      break; // needs more data
    ctx.pos += info.frame_bytes;
    if (samples <= 0)
      continue; // skipped ID3 tag or garbage

    ctx.in.resize(samples);
    for (int i = 0; i < samples; i++) {
      ctx.in[i].samples[0] = pcm[i * info.channels] * (1.f / 32768.f);
      ctx.in[i].samples[1] = pcm[i * info.channels + info.channels - 1] * (1.f / 32768.f);
    }
    emit(ctx, samples, info.hz, "MP3", info.frame_bytes);
  }
}

void ANTN::decodeAac(StreamCtx &ctx, bool flush) {
  if (!ctx.aac) {
    ctx.aac = NeAACDecOpen();
    if (!ctx.aac) {
      unsupported(ctx, "AAC decoder unavailable");
      return;
    }
    NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(ctx.aac);
    cfg->outputFormat = FAAD_FMT_FLOAT;
    cfg->downMatrix = 1; // 5.1 to stereo
    cfg->defObjectType = LC;
    NeAACDecSetConfiguration(ctx.aac, cfg);
  }

  while (!ctx.aborted) {
    size_t avail = ctx.pending.size() - ctx.pos;
    if (avail < (flush ? 7 : DECODE_MIN_BYTES))
      break;
    uint8_t *p = ctx.pending.data() + ctx.pos;
    if (!isAdtsSync(p)) {
      size_t i = 1;
      while (i + 1 < avail && !isAdtsSync(p + i))
        i++;
      ctx.pos += i;
      continue;
    }
    size_t frameLen = adtsFrameLength(p);
    if (frameLen < 7) {
      ctx.pos++;
      continue;
    }
    if (frameLen > avail)
      break; // the last, incomplete frame of a finite file

    if (!ctx.aacInit) {
      unsigned long rate = 0;
      unsigned char channels = 0;
      long skip = NeAACDecInit(ctx.aac, p, (unsigned long) avail, &rate, &channels);
      if (skip < 0) {
        ctx.pos++;
        continue;
      }
      ctx.pos += skip;
      ctx.aacInit = true;
      continue;
    }

    NeAACDecFrameInfo fi;
    float *pcm = (float*) NeAACDecDecode(ctx.aac, &fi, p, (unsigned long) avail);
    if (fi.error || !pcm) {
      // Corrupt frame: skip it. Many in a row: start the decoder again.
      ctx.pos += frameLen;
      if (++ctx.aacErrors > 32) {
        NeAACDecClose(ctx.aac);
        ctx.aac = NULL;
        ctx.aacInit = false;
        ctx.aacErrors = 0;
        return;
      }
      continue;
    }
    ctx.aacErrors = 0;
    size_t consumed = fi.bytesconsumed ? fi.bytesconsumed : frameLen;
    ctx.pos += consumed;
    if (fi.samples == 0 || fi.channels == 0 || fi.samplerate == 0)
      continue;

    int ch = fi.channels;
    int frames = (int)(fi.samples / ch);
    ctx.in.resize(frames);
    for (int i = 0; i < frames; i++) {
      ctx.in[i].samples[0] = pcm[i * ch];
      ctx.in[i].samples[1] = pcm[i * ch + (ch > 1 ? 1 : 0)];
    }
    bool he = fi.sbr == SBR_UPSAMPLED || fi.sbr == SBR_DOWNSAMPLED || fi.ps;
    emit(ctx, frames, (int) fi.samplerate, he ? "AAC+" : "AAC", consumed);
  }
}

void ANTN::decodeVorbis(StreamCtx &ctx) {
  while (!ctx.aborted) {
    size_t avail = ctx.pending.size() - ctx.pos;
    if (avail < 4)
      break;
    const uint8_t *p = ctx.pending.data() + ctx.pos;
    if (memcmp(p, "OggS", 4) != 0) {
      // lost sync: look for the next page
      size_t i = 1;
      while (i + 4 <= avail && memcmp(p + i, "OggS", 4) != 0)
        i++;
      ctx.pos += i;
      continue;
    }
    size_t len = oggPageLength(p, avail);
    if (len == 0)
      break; // needs more data
    ctx.pos += len;

    bool bos = p[5] & 0x02;
    if (bos) {
      // First page of a logical stream: the start, or a new chained stream
      // (server restart, new song with its own headers). Start over.
      const uint8_t *body = p + 27 + p[26];
      size_t bodyLen = len - 27 - p[26];
      if (bodyLen >= 8 && memcmp(body, "OpusHead", 8) == 0) {
        unsupported(ctx, "Ogg Opus is not supported");
        return;
      }
      if (bodyLen < 7 || body[0] != 1 || memcmp(body + 1, "vorbis", 6) != 0) {
        unsupported(ctx, "Ogg, but not Vorbis: not supported");
        return;
      }
      if (ctx.vorbis) {
        stb_vorbis_close(ctx.vorbis);
        ctx.vorbis = NULL;
      }
      ctx.oggBuf.clear();
      ctx.oggPos = 0;
      ctx.oggStarted = true;
    }
    if (!ctx.oggStarted)
      continue; // joined in the middle of a stream: wait for its headers

    ctx.oggBuf.insert(ctx.oggBuf.end(), p, p + len);

    if (!ctx.vorbis) {
      int used = 0, error = 0;
      ctx.vorbis = stb_vorbis_open_pushdata(ctx.oggBuf.data(), (int) ctx.oggBuf.size(), &used, &error, NULL);
      if (!ctx.vorbis) {
        if (error != VORBIS_need_more_data || ctx.oggBuf.size() > OGG_MAX_BUFFER) {
          // broken headers: wait for the next stream
          ctx.oggBuf.clear();
          ctx.oggStarted = false;
        }
        continue;
      }
      ctx.oggPos = used;
    }

    int rate = stb_vorbis_get_info(ctx.vorbis).sample_rate;
    while (!ctx.aborted) {
      int channels = 0, samples = 0;
      float **outputs = NULL;
      int used = stb_vorbis_decode_frame_pushdata(ctx.vorbis, ctx.oggBuf.data() + ctx.oggPos, (int)(ctx.oggBuf.size() - ctx.oggPos), &channels, &outputs, &samples);
      if (used == 0)
        break; // needs the next page
      ctx.oggPos += used;
      if (samples <= 0 || channels <= 0 || rate <= 0)
        continue;
      ctx.in.resize(samples);
      for (int i = 0; i < samples; i++) {
        ctx.in[i].samples[0] = outputs[0][i];
        ctx.in[i].samples[1] = outputs[channels > 1 ? 1 : 0][i];
      }
      emit(ctx, samples, rate, "Vorbis", used);
    }
    if (ctx.oggPos > 65536) {
      ctx.oggBuf.erase(ctx.oggBuf.begin(), ctx.oggBuf.begin() + ctx.oggPos);
      ctx.oggPos = 0;
    }
    if (ctx.oggBuf.size() - ctx.oggPos > OGG_MAX_BUFFER) {
      // no packet ends: resynchronise on the next pages
      stb_vorbis_flush_pushdata(ctx.vorbis);
      ctx.oggBuf.clear();
      ctx.oggPos = 0;
    }
  }
}

// Resamples the frames in ctx.in to the engine rate and queues them.
void ANTN::emit(StreamCtx &ctx, int frames, int rate, const char *label, size_t codedBytes) {
  ctx.decodedAny = true;
  ctx.codedBytes += codedBytes;
  ctx.seconds += (double) frames / rate;
  if (ctx.label != label || ctx.rate != rate || ctx.seconds - ctx.statusAt >= 2.0) {
    ctx.label = label;
    ctx.rate = rate;
    ctx.statusAt = ctx.seconds;
    std::string s = label;
    if (ctx.seconds >= 1.0) {
      char buf[32];
      snprintf(buf, sizeof(buf), " %d kbps", (int) std::round(ctx.codedBytes * 8.0 / 1000.0 / ctx.seconds));
      s += buf;
    }
    setStatus(s + " " + formatRate(rate));
  }

  int outRate = (int)sampleRate.load();
  ctx.conv.setRates(rate, outRate);
  int inPos = 0;
  while (inPos < frames && !ctx.aborted) {
    int inFrames = frames - inPos;
    int outFrames = (int)((int64_t) inFrames * outRate / rate) + 64;
    ctx.out.resize(outFrames);
    ctx.conv.process(ctx.in.data() + inPos, &inFrames, ctx.out.data(), &outFrames);
    if (inFrames == 0 && outFrames == 0)
      break;
    inPos += inFrames;
    pushFrames(ctx, ctx.out.data(), outFrames);
  }
}

// Returns false if nothing could be played (bad URL, unsupported format, network error).
bool ANTN::runStream(std::string u, uint32_t g) {
  setIcyName("");
  setStatus("connecting...");
  for (int depth = 0; ; depth++) {
    if (isHlsUrl(u)) {
      setStatus("HLS (.m3u8) not supported");
      WARN("antN: %s is an HLS stream, not supported", u.c_str());
      return false;
    }
    StreamCtx ctx;
    ctx.module = this;
    ctx.gen = g;
    if (isPlaylistUrl(u))
      ctx.kind = KIND_PLAYLIST;
    mp3dec_init(&ctx.mp3d);
    ctx.conv.setQuality(10);
    CURL *curl = createCurl(ctx, u);
    if (!curl)
      return false;
    INFO("antN: opening %s", u.c_str());
    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
    if (shouldAbort(g))
      return ctx.decodedAny;

    if (ctx.kind == KIND_UNDECIDED && res == CURLE_OK)
      decode(ctx, true); // a short body: decide on what came

    if (ctx.kind == KIND_PLAYLIST) {
      if (ctx.text.empty()) {
        setStatus(res == CURLE_OK ? "empty playlist" : (httpCode >= 400 ? "HTTP error " + std::to_string(httpCode) : std::string(curl_easy_strerror(res))));
        WARN("antN: cannot fetch playlist %s: %s", u.c_str(), curl_easy_strerror(res));
        return false;
      }
      if (isHlsText(ctx.text)) {
        setStatus("HLS (.m3u8) not supported");
        WARN("antN: %s is an HLS playlist, not supported", u.c_str());
        return false;
      }
      std::string next = parsePlaylist(ctx.text);
      if (next.empty()) {
        setStatus("no stream in the playlist");
        WARN("antN: no stream URL in playlist %s", u.c_str());
        return false;
      }
      if (depth + 1 >= PLAYLIST_MAX_DEPTH) {
        setStatus("too many nested playlists");
        return false;
      }
      setStatus("reading playlist...");
      u = next;
      continue;
    }

    if (ctx.kind == KIND_UNSUPPORTED)
      WARN("antN: %s: not played (%s)", u.c_str(), ctx.contentType.c_str());
    else if (res != CURLE_OK) {
      if (!ctx.decodedAny)
        setStatus(httpCode >= 400 ? "HTTP error " + std::to_string(httpCode) : std::string(curl_easy_strerror(res)));
      else
        setStatus("connection lost");
      WARN("antN: stream %s failed: %s", u.c_str(), curl_easy_strerror(res));
    }
    else {
      decode(ctx, true); // a finite file has ended: decode what is left
      if (!ctx.decodedAny && ctx.kind != KIND_UNSUPPORTED)
        unsupported(ctx, "not a supported format");
    }
    pendingBytes = 0;
    return ctx.decodedAny;
  }
}

void ANTN::workerLoop() {
  // 0, not startCount: a start requested before this thread runs must not be missed
  uint32_t handled = 0;
  while (!quit) {
    uint32_t g = startCount;
    if (!playing || g == handled) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }
    handled = g;
    failed = false;
    bool ok = runStream(getUrl(), g);
    if (startCount != g)
      continue; // restarted meanwhile: the new generation takes over
    if (!ok) {
      failed = true;
      errorGen = g;
    }
    // The audio thread plays out what is buffered, then stops.
    doneGen = g;
  }
}

void ANTN::process(const ProcessArgs &args) {
  sampleRate.store(args.sampleRate, std::memory_order_relaxed);

  bool start = false;
	if (trigTrigger.process(params[TRIG_PARAM].getValue())) {
    if (playing)
      playing = false;
    else
      start = true;
	}
  // A station chosen from the menu (re)starts at once.
  if (startRequest.load(std::memory_order_relaxed) && startRequest.exchange(false))
    start = true;
  if (start) {
    // nothing of the previous stream may play in the new one
    audio.clear();
    gen = ++startCount;
    buffered = false;
    playing = true;
  }

  bool haveFrame = false;
  dsp::Frame<2> frame = {};
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
        frame = audio.shift();
        haveFrame = true;
      }
    }
  }
  else if (env > 0.f && !audio.empty()) {
    // Stopped: the fade out plays on the frames already buffered.
    frame = audio.shift();
    haveFrame = true;
  }
  else {
    audio.clear();
    buffered = false;
  }

  if (haveFrame) {
    lastL = frame.samples[0];
    lastR = frame.samples[1];
  }
  const float fadeStep = args.sampleTime / 0.005f;
  if (playing && haveFrame)
    env = std::min(env + fadeStep, 1.f);
  else
    env = std::max(env - fadeStep, 0.f);
  float l = lastL * env;
  float r = lastR * env;

  if (args.sampleRate != smoothRate) {
    smoothRate = args.sampleRate;
    smoothCoef = 1.f - std::exp(-args.sampleTime / 0.005f);
  }
  float gainTarget = 5.f * params[GAIN_PARAM].getValue();
  if (smoothGain < 0.f)
    smoothGain = gainTarget;
  smoothGain += (gainTarget - smoothGain) * smoothCoef;
  outputs[OUTL_OUTPUT].setVoltage(smoothGain * l);
  outputs[OUTR_OUTPUT].setVoltage(smoothGain * r);

  // Red: stopped (blinking after a failed stream). Green: connecting or buffering.
  // Blue: playing, with green added when the buffer runs low.
  blinkPhase += args.sampleTime * 2.f;
  if (blinkPhase >= 1.f)
    blinkPhase -= 1.f;
  bool failedNow = !playing && gen != 0 && errorGen == gen;
  bool low = buffered && audio.size() < prebufferFrames() / 4;
  lights[ON_LIGHT].setBrightness(playing ? 0.f : (failedNow && blinkPhase >= 0.5f ? 0.f : 1.f));
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
      // A URL typed by hand is no longer the station chosen from the menu.
      if (tText != module->getUrl())
        module->setStationName("");
      module->setUrl(tText);
      // An emptied field clears the URL and stops what was playing.
      if (tText.empty())
        module->playing = false;
    }
	}
};

// Station name and stream status (format, bitrate, rate, or why it does not play).
struct ANTNStatus : Widget {
  ANTN *module = NULL;
  std::string fontPath = asset::system("res/fonts/ShareTechMono-Regular.ttf");

  void drawLayer(const DrawArgs& args, int layer) override {
    if (layer == 1 && module) {
      std::shared_ptr<window::Font> font = APP->window->loadFont(fontPath);
      if (font && font->handle >= 0) {
        std::string name, status;
        module->getDisplayLines(name, status);
        nvgSave(args.vg);
        nvgIntersectScissor(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 11.f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFillColor(args.vg, nvgRGB(0xff, 0xd7, 0x14));
        nvgText(args.vg, 5.f, 2.f, name.c_str(), NULL);
        nvgFillColor(args.vg, nvgRGBA(0xff, 0xd7, 0x14, 0xa0));
        nvgText(args.vg, 5.f, 14.f, status.c_str(), NULL);
        nvgRestore(args.vg);
      }
    }
    Widget::drawLayer(args, layer);
  }
};

struct ANTNDisplay : LedDisplay {
	void setModule(ANTN* module) {
    const float statusHeight = 28.f;
		ANTNTextField* textField = createWidget<ANTNTextField>(Vec(0, 0));
		textField->box.size = Vec(box.size.x, box.size.y - statusHeight);
		textField->multiline = true;
		textField->module = module;
		addChild(textField);

    LedDisplaySeparator *separator = createWidget<LedDisplaySeparator>(Vec(0, box.size.y - statusHeight));
    separator->box.size = Vec(box.size.x, 0);
    addChild(separator);

    ANTNStatus *status = createWidget<ANTNStatus>(Vec(0, box.size.y - statusHeight));
    status->box.size = Vec(box.size.x, statusHeight);
    status->module = module;
    addChild(status);
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

  void appendContextMenu(ui::Menu *menu) override {
    BidooWidget::appendContextMenu(menu);
    ANTN *m = dynamic_cast<ANTN*>(this->module);
    if (!m || antnStations.empty())
      return;
    menu->addChild(new MenuSeparator());
    std::string current = m->getUrl();
    auto has = [current](const std::string &continent, const std::string &category) {
      for (const AntnStation &s : antnStations)
        if (s.url == current && s.continent == continent && (category.empty() || s.category == category))
          return true;
      return false;
    };
    menu->addChild(createSubmenuItem("Stations", "", [=](ui::Menu *menu) {
      for (const char *continent : ANTN_CONTINENTS) {
        std::string cont = continent;
        bool any = false;
        for (const AntnStation &s : antnStations)
          any = any || s.continent == cont;
        if (!any)
          continue;
        menu->addChild(createSubmenuItem(cont, CHECKMARK(has(cont, "")), [=](ui::Menu *menu) {
          for (const char *category : ANTN_CATEGORIES) {
            std::string cat = category;
            menu->addChild(createSubmenuItem(cat, CHECKMARK(has(cont, cat)), [=](ui::Menu *menu) {
              for (const AntnStation &s : antnStations) {
                if (s.continent != cont || s.category != cat)
                  continue;
                AntnStation st = s;
                menu->addChild(createCheckMenuItem(st.name + " (" + st.country + ")", st.format,
                  [=]() { return m->getUrl() == st.url; },
                  [=]() { m->chooseStation(st); }));
              }
            }));
          }
        }));
      }
    }));
  }
};

Model *modelANTN = createModel<ANTN, ANTNWidget>("antN");

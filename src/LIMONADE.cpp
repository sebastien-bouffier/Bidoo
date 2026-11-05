#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "osdialog.h"
#include "dep/dr_wav/dr_wav.h"
#include "dep/osc/wtOsc.h"
#include "dep/lodepng/lodepng.h"
#include "dep/AudioFile/AudioFile.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

using namespace std;

static const char AUDIO_FILTERS[] = "Audio:wav,aif,aiff";
static const char WAV_FILTERS[] = "WAV:wav";
static const char PNG_FILTERS[] = "PNG:png";

// Largest frame size the recorder can hold for a whole table.
static const size_t MAX_FRAME_SIZE = 4 * FS;

// WAV files carry the samples per frame in a "clm " chunk ("<!>2048 ..."),
// as Serum and most wavetable synths read and write it.
static drwav_uint64 readClmChunk(void *user, drwav_read_proc onRead, drwav_seek_proc onSeek, void *readSeekUser, const drwav_chunk_header *header, drwav_container container, const drwav_fmt *fmt) {
	if (memcmp(header->id.fourcc, "clm ", 4) != 0)
		return 0;
	char text[64] = {};
	size_t n = onRead(readSeekUser, text, std::min((size_t)header->sizeInBytes, sizeof(text) - 1));
	int size = 0;
	if (sscanf(text, "<!>%d", &size) == 1 && size > 0)
		*(size_t*)user = size;
	return n;
}

// Mono WAV in memory: channels averaged, cycle set from the "clm " chunk (0 if none).
static bool decodeWav(const std::vector<uint8_t> &bytes, std::vector<float> &mono, size_t &cycle) {
	cycle = 0;
	drwav wav;
	if (bytes.empty() || !drwav_init_memory_ex(&wav, bytes.data(), bytes.size(), readClmChunk, &cycle, 0, NULL))
		return false;
	unsigned channels = std::max(wav.channels, (drwav_uint16)1);
	std::vector<float> data(wav.totalPCMFrameCount * channels);
	size_t frames = drwav_read_pcm_frames_f32(&wav, wav.totalPCMFrameCount, data.data());
	drwav_uninit(&wav);
	mono.resize(frames);
	for (size_t i = 0; i < frames; i++) {
		float sum = 0.f;
		for (unsigned c = 0; c < channels; c++) sum += data[i * channels + c];
		mono[i] = sum / channels;
	}
	return frames > 0;
}

struct WavBuffer {
	std::vector<uint8_t> bytes;
	size_t pos = 0;
};

static size_t wavBufferWrite(void *user, const void *data, size_t size) {
	WavBuffer *b = (WavBuffer*)user;
	if (b->bytes.size() < b->pos + size)
		b->bytes.resize(b->pos + size);
	memcpy(b->bytes.data() + b->pos, data, size);
	b->pos += size;
	return size;
}

static drwav_bool32 wavBufferSeek(void *user, int offset, drwav_seek_origin origin) {
	WavBuffer *b = (WavBuffer*)user;
	long base = origin == DRWAV_SEEK_SET ? 0 : origin == DRWAV_SEEK_CUR ? (long)b->pos : (long)b->bytes.size();
	long pos = base + offset;
	if (pos < 0 || (size_t)pos > b->bytes.size())
		return DRWAV_FALSE;
	b->pos = pos;
	return DRWAV_TRUE;
}

// Mono 32-bit float WAV in memory, `cycle` samples per frame.
static std::vector<uint8_t> encodeWav(const std::vector<float> &samples, int sampleRate, size_t cycle) {
	std::string clm = "<!>" + std::to_string(cycle) + " 00000000 wavetable (Bidoo liMonADe)";
	drwav_metadata meta = {};
	meta.type = drwav_metadata_type_unknown;
	memcpy(meta.data.unknown.id, "clm ", 4);
	meta.data.unknown.chunkLocation = drwav_metadata_location_top_level;
	meta.data.unknown.dataSizeInBytes = clm.size();
	meta.data.unknown.pData = (drwav_uint8*)clm.data();

	drwav_data_format format;
	format.container = drwav_container_riff;
	format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
	format.channels = 1;
	format.sampleRate = sampleRate;
	format.bitsPerSample = 32;
	WavBuffer buffer;
	drwav wav;
	if (!drwav_init_write_with_metadata(&wav, &format, wavBufferWrite, wavBufferSeek, &buffer, NULL, &meta, 1))
		return {};
	size_t written = drwav_write_pcm_frames(&wav, samples.size(), samples.data());
	drwav_uninit(&wav);
	if (written != samples.size())
		return {};
	return buffer.bytes;
}

static std::vector<uint8_t> readBytes(const std::string &path) {
	try {
		return rack::system::readFile(path);
	}
	catch (Exception &e) {
		WARN("liMonADe: cannot read %s: %s", path.c_str(), e.what());
		return {};
	}
}

static bool writeBytes(const std::string &path, const std::vector<uint8_t> &bytes) {
	try {
		rack::system::writeFile(path, bytes);
		return true;
	}
	catch (Exception &e) {
		WARN("liMonADe: cannot write %s: %s", path.c_str(), e.what());
		return false;
	}
}

// Reads a WAV or AIFF file, channels averaged to mono. Empty on failure.
// `cycle` is the frame size the file states, 0 if it states none.
static std::vector<float> readMonoAudio(const std::string &path, size_t &cycle) {
	std::vector<float> mono;
	cycle = 0;
	std::string ext = rack::string::lowercase(rack::system::getExtension(path));
	if (ext == ".wav") {
		if (!decodeWav(readBytes(path), mono, cycle)) {
			WARN("liMonADe: cannot read %s", path.c_str());
			mono.clear();
		}
	}
	else if (ext == ".aif" || ext == ".aiff") {
		AudioFile<float> audioFile;
		if (!audioFile.load(path)) {
			WARN("liMonADe: cannot read %s", path.c_str());
			return mono;
		}
		int frames = audioFile.getNumSamplesPerChannel();
		int channels = audioFile.getNumChannels();
		mono.resize(frames);
		for (int i = 0; i < frames; i++) {
			float sum = 0.f;
			for (int c = 0; c < channels; c++) sum += audioFile.samples[c][i];
			mono[i] = sum / channels;
		}
	}
	else {
		WARN("liMonADe: unsupported file %s", path.c_str());
	}
	return mono;
}

// One frame per image row, bottom row first, luminance mapped to -1..1.
static bool readPng(const std::string &path, std::vector<float> &samples, unsigned &width) {
	std::vector<unsigned char> image;
	unsigned height = 0;
	std::vector<uint8_t> bytes = readBytes(path);
	unsigned error = bytes.empty() ? 78 : lodepng::decode(image, width, height, bytes, LCT_RGB);
	if (error) {
		WARN("liMonADe: cannot read %s: %s", path.c_str(), lodepng_error_text(error));
		return false;
	}
	samples.resize((size_t)width * height);
	for (size_t i = 0; i < height; i++) {
		const unsigned char *row = image.data() + 3 * (height - i - 1) * width;
		for (size_t j = 0; j < width; j++) {
			float lum = (0.299f * row[3 * j] + 0.587f * row[3 * j + 1] + 0.114f * row[3 * j + 2]) / 255.f;
			samples[i * width + j] = 2.f * lum - 1.f;
		}
	}
	return true;
}

// Writers take a copy of the frames, FS samples each, so that no lock is held during disk access.
static void writeWav(const std::vector<float> &samples, int sampleRate, const std::string &path) {
	std::vector<uint8_t> bytes = encodeWav(samples, sampleRate, FS);
	if (bytes.empty())
		WARN("liMonADe: cannot encode %s", path.c_str());
	else
		writeBytes(path, bytes);
}

// Inverse of readPng, 8 bits per sample.
static void writePng(const std::vector<float> &samples, const std::string &path) {
	size_t height = samples.size() / FS;
	std::vector<unsigned char> image(FS * height * 4);
	for (size_t i = 0; i < height; i++) {
		const float *frame = samples.data() + i * FS;
		unsigned char *row = image.data() + 4 * (height - i - 1) * FS;
		for (size_t j = 0; j < FS; j++) {
			unsigned char v = (unsigned char)(clamp(frame[j] * 0.5f + 0.5f, 0.f, 1.f) * 255.f + 0.5f);
			row[4 * j] = row[4 * j + 1] = row[4 * j + 2] = v;
			row[4 * j + 3] = 255;
		}
	}
	std::vector<unsigned char> bytes;
	unsigned error = lodepng::encode(bytes, image, FS, height);
	if (error)
		WARN("liMonADe: cannot write %s: %s", path.c_str(), lodepng_error_text(error));
	else
		writeBytes(path, bytes);
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

	// Table edits requested by the audio thread, run by the worker.
	enum Op : uint32_t {
		OP_MORPH_WT = 1 << 0,
		OP_MORPH_SPECTRUM = 1 << 1,
		OP_MORPH_SPECTRUM_CONSTANT_PHASE = 1 << 2,
		OP_REMOVE_MORPH = 1 << 3,
		OP_NORMALIZE_WT = 1 << 4,
		OP_NORMALIZE_FRAME = 1 << 5,
		OP_NORMALIZE_ALL_FRAMES = 1 << 6,
		OP_REMOVE_DC = 1 << 7,
		OP_WINDOW_WT = 1 << 8,
		OP_WINDOW_FRAME = 1 << 9,
		OP_SMOOTH_WT = 1 << 10,
		OP_SMOOTH_FRAME = 1 << 11,
		OP_ADD_FRAME = 1 << 12,
		OP_REMOVE_FRAME = 1 << 13,
		OP_REC_WT = 1 << 14,
		OP_REC_FRAME = 1 << 15,
	};

	static const int MAX_VOICES = 3;
	static const int MAX_GROUPS = (16 * MAX_VOICES + 3) / 4;

	// Editable table: UI and worker threads only, always under tableMutex.
	std::mutex tableMutex;
	wtTable table;
	int morphType = -1;

	// UI thread
	std::string lastPath;
	std::atomic<bool> dirty{true};
	std::atomic<size_t> frameSize{FS};
	int displayMode = 0;
	int displayEditedFrame = 1;
	int displayPlayedFrame = 1;
	// Played position of channel 1, for the displays.
	std::atomic<float> playedPos{0.f};

	// Worker thread: runs edits and builds the band-limited play table.
	std::thread worker;
	std::mutex jobMutex;
	std::condition_variable jobCv;
	std::deque<std::function<void(wtTable&)>> jobs;
	std::atomic<bool> quit{false};
	std::atomic<bool> rebuildRequested{false};
	std::atomic<uint32_t> pendingOps{0};

	// Play table handoff: the worker puts a new table in `pending`; the audio
	// thread takes it and moves its old one to `retired`, which the worker frees.
	std::atomic<wtPlayTable*> pending{nullptr};
	std::atomic<wtPlayTable*> retired{nullptr};
	wtPlayTable *current = nullptr;

	// Recording: the audio thread fills iRec, then hands it to the worker.
	std::vector<float> iRec;
	size_t recIndex = 0;
	size_t recTarget = 0;
	size_t recCount = 0;
	size_t recFrameSize = FS;
	bool recWt = false;
	bool recFrame = false;
	std::atomic<bool> recBusy{false};

	dsp::SchmittTrigger recWtTrigger, recFrameTrigger, displayModeTrigger, morphWtTrigger, morphSpectrumTrigger, morphSpectrumPhaseConstantTrigger,
	removeMorphingTrigger, normalizeWtTrigger, normalizeFrameTrigger, normalizeAllFramesTrigger, removeDCTrigger, windowWtTrigger, windowFrameTrigger, smoothWtTrigger, smoothFrameTrigger,
	addFrameTrigger, removeFrameTrigger, displayEditedFrameTrigger, displayPlayedFrameTrigger;

	// Audio thread: (channel, unison voice) pairs packed 4 per oscillator.
	wtOscillator<float_4> oscillators[MAX_GROUPS];
	int lastChannels = 0;
	int lastVoices = 0;
	// Channel and unison voice of each lane, rebuilt when either count changes.
	int laneChannel[MAX_GROUPS * 4];
	int laneVoice[MAX_GROUPS * 4];
	float levelBase = 0.f;
	float levelSampleRate = 0.f;

	LIMONADE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(INDEX_PARAM, 0.0f, 1.0f, 0.0f ,"Edited frame");
		configParam(UNISSON_PARAM, 1.0f, 3.0f, 1.0f, "Voices number")->snapEnabled = true;
		configParam(UNISSONRANGE_PARAM, 0.0f, .02f, 0.0f, "Voices range");

		configSwitch(SYNC_PARAM, 0.f, 1.f, 1.f, "Sync mode", {"Soft", "Hard"});
		configParam(FREQ_PARAM, -54.f, 54.f, 0.f, "Frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
		configParam(FINE_PARAM, -1.f, 1.f, 0.f, "Fine frequency");
		configParam(FM_PARAM, 0.f, 1.f, 0.f, "Frequency modulation", "%", 0.f, 100.f);
		configInput(PITCH_INPUT, "1V/oct pitch");
		configInput(FM_INPUT, "Frequency modulation");
		configInput(SYNC_INPUT, "Sync");
		configInput(SYNCMODE_INPUT, "Sync mode (above 0 V: hard)");
		configInput(WTINDEX_INPUT, "Played frame");
		configInput(UNISSONRANGE_INPUT, "Voices range");
		configInput(IN, "Record");
		configOutput(OUT, "Audio");

		configParam(WTINDEX_PARAM, 0.0f, 1.0f, 0.0f, "Played frame");
		configParam(WTINDEXATT_PARAM, -1.0f, 1.0f, 1.0f, "Played frame attenuation");
		configButton(RECWT_PARAM, "Rec. wavetable");
		configButton(RECFRAME_PARAM, "Rec. frame");
		configButton(DISPLAYMODE_PARAM, "3D Wavetable display");
		configButton(DISPLAYEDITEDFRAME_PARAM, "Edited frame display");
		configButton(DISPLAYPLAYEDFRAME_PARAM, "Played frame display");
		configButton(LOADSAMPLE_PARAM, "Load wavetable");
		configButton(LOADFRAME_PARAM, "Load frame");
		configButton(LOADPNG_PARAM, "Load png");
		configButton(MORPHWT_PARAM, "Morph wavetable");
		configButton(MORPHSPECTRUM_PARAM, "Morph spectrum amplitude and phase");
		configButton(MORPHSPECTRUMCONSTANTPHASE_PARAM, "Morph spectrum amplitude constant phase");
		configButton(REMOVEMORPH_PARAM, "Remove morphing");
		configButton(NORMALIZEWT_PARAM, "Normalize wavetable");
		configButton(NORMALIZEFRAME_PARAM, "Normalize frame");
		configButton(NORMALIZEALLFRAMES_PARAM, "Normalize all frames");
		configButton(REMOVEDC_PARAM, "Remove DC offset");
		configButton(WINDOWWT_PARAM, "Fade wavetable frame edges");
		configButton(WINDOWFRAME_PARAM, "Fade frame edges");
		configButton(SMOOTHWT_PARAM, "Smooth wavetable loop points");
		configButton(SMOOTHFRAME_PARAM, "Smooth frame loop point");
		configButton(ADDFRAME_PARAM, "Add frame");
		configButton(REMOVEFRAME_PARAM, "Remove frame");

		iRec.resize(MAX_FRAME_SIZE * NF);
		worker = std::thread(&LIMONADE::workerLoop, this);
	}

	~LIMONADE() {
		quit = true;
		jobCv.notify_one();
		if (worker.joinable())
			worker.join();
		delete current;
		delete pending.load();
		delete retired.load();
	}

	void process(const ProcessArgs &args) override;

	// Runs f on the editable table on the worker thread, then rebuilds the play table.
	void postJob(std::function<void(wtTable&)> f) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			jobs.push_back(std::move(f));
		}
		jobCv.notify_one();
	}

	void requestRebuild() {
		rebuildRequested = true;
		jobCv.notify_one();
	}

	void workerLoop() {
		while (!quit) {
			std::deque<std::function<void(wtTable&)>> todo;
			{
				std::unique_lock<std::mutex> lock(jobMutex);
				// The audio thread cannot notify without locking, so also poll for its ops.
				jobCv.wait_for(lock, std::chrono::milliseconds(5), [&]{ return quit || !jobs.empty() || rebuildRequested; });
				todo.swap(jobs);
			}
			if (quit)
				break;
			uint32_t ops = pendingOps.exchange(0);
			bool changed = rebuildRequested.exchange(false);
			if (ops || !todo.empty()) {
				std::lock_guard<std::mutex> lock(tableMutex);
				for (auto &job : todo) job(table);
				if (ops) runOps(ops);
				changed = true;
			}
			if (changed)
				publish();
			delete retired.exchange(nullptr);
		}
	}

	void publish() {
		std::vector<float> samples;
		size_t n;
		{
			std::lock_guard<std::mutex> lock(tableMutex);
			n = table.nFrames;
			samples.resize(n * FS);
			for (size_t i = 0; i < n; i++)
				std::copy(table.frames[i].sample.begin(), table.frames[i].sample.end(), samples.begin() + i * FS);
		}
		wtPlayTable *t = new wtPlayTable();
		t->build(samples.data(), n);
		delete pending.exchange(t);
	}

	// Worker thread, under tableMutex.
	void runOps(uint32_t ops) {
		size_t i = table.frameAt(params[INDEX_PARAM].getValue());
		bool has = table.nFrames > 0;
		if (ops & OP_MORPH_WT) { table.morph(wtTable::MORPH_WAVE); morphType = 0; }
		if (ops & OP_MORPH_SPECTRUM) { table.morph(wtTable::MORPH_SPECTRUM); morphType = 1; }
		if (ops & OP_MORPH_SPECTRUM_CONSTANT_PHASE) { table.morph(wtTable::MORPH_SPECTRUM_CONSTANT_PHASE); morphType = 2; }
		if (ops & OP_REMOVE_MORPH) { table.deleteMorphing(); morphType = -1; }
		// On a morphed table the whole-table tools process the original frames
		// and the in-betweens are morphed again from them: only the originals
		// are saved, so this is what a reload rebuilds. (Normalize WT is one
		// gain for every frame and morphs the same either way.)
		bool remorph = (ops & (OP_NORMALIZE_ALL_FRAMES | OP_REMOVE_DC | OP_WINDOW_WT | OP_SMOOTH_WT)) && morphType >= 0;
		if (remorph)
			table.deleteMorphing();
		if (ops & OP_NORMALIZE_WT) table.normalize();
		if ((ops & OP_NORMALIZE_FRAME) && has) table.frames[i].normalize();
		if (ops & OP_NORMALIZE_ALL_FRAMES) table.normalizeAllFrames();
		if (ops & OP_REMOVE_DC) table.removeDCOffset();
		if (ops & OP_WINDOW_WT) table.window();
		if ((ops & OP_WINDOW_FRAME) && has) table.frames[i].window();
		if (ops & OP_SMOOTH_WT) table.smooth();
		if ((ops & OP_SMOOTH_FRAME) && has) table.frames[i].smooth();
		if (remorph)
			table.morph(morphType == 0 ? wtTable::MORPH_WAVE : (morphType == 1 ? wtTable::MORPH_SPECTRUM : wtTable::MORPH_SPECTRUM_CONSTANT_PHASE));
		// An edited morphed frame becomes an original, so the edit is saved with the patch.
		if ((ops & (OP_NORMALIZE_FRAME | OP_WINDOW_FRAME | OP_SMOOTH_FRAME)) && has) table.frames[i].morphed = false;
		if (ops & OP_ADD_FRAME) table.addFrame(i);
		if ((ops & OP_REMOVE_FRAME) && has) table.removeFrame(i);
		if (ops & OP_REC_WT) {
			table.loadSample(recCount, recFrameSize, true, iRec.data());
			morphType = -1;
			recBusy = false;
		}
		if (ops & OP_REC_FRAME) {
			if (table.nFrames == 0)
				table.addFrame(0);
			size_t f = table.frameAt(params[INDEX_PARAM].getValue());
			table.frames[f].loadSample(recCount, true, iRec.data());
			table.frames[f].morphed = false;
			recBusy = false;
		}
		table.calcFFT();
		dirty = true;
	}

	// UI thread
	void loadSample(const std::string &path) {
		size_t cycle;
		std::vector<float> data = readMonoAudio(path, cycle);
		if (data.empty())
			return;
		lastPath = path;
		// A frame size stated by the file wins over the one typed on the panel.
		size_t fsz = cycle > 0 ? cycle : (size_t)frameSize;
		postJob([this, data, fsz](wtTable &t) {
			t.loadSample(data.size(), fsz, true, data.data());
			t.calcFFT();
			morphType = -1;
		});
	}

	void loadFrame(const std::string &path) {
		size_t cycle;
		std::vector<float> data = readMonoAudio(path, cycle);
		if (data.empty())
			return;
		lastPath = path;
		float pos = params[INDEX_PARAM].getValue();
		postJob([data, pos](wtTable &t) {
			if (t.nFrames == 0)
				t.addFrame(0);
			wtFrame &f = t.frames[t.frameAt(pos)];
			f.loadSample(data.size(), true, data.data());
			f.morphed = false;
			f.calcFFT();
		});
	}

	void loadPNG(const std::string &path) {
		std::vector<float> data;
		unsigned width = 0;
		if (!readPng(path, data, width) || width == 0)
			return;
		lastPath = path;
		postJob([this, data, width](wtTable &t) {
			t.loadSample(data.size(), width, true, data.data());
			t.calcFFT();
			morphType = -1;
		});
	}

	// Copy of `count` frames from `first`, FS samples each, for the exports.
	std::vector<float> copyFrames(size_t first, size_t count) {
		std::lock_guard<std::mutex> lock(tableMutex);
		std::vector<float> samples;
		for (size_t i = first; i < std::min(first + count, table.nFrames); i++)
			samples.insert(samples.end(), table.frames[i].sample.begin(), table.frames[i].sample.end());
		return samples;
	}

	// Spectrum editor drag on one bin of one frame.
	void editBin(size_t frame, size_t bin, bool isMagnitude, bool zero, float delta) {
		postJob([frame, bin, isMagnitude, zero, delta](wtTable &t) {
			if (frame >= t.nFrames || bin >= FS2)
				return;
			wtFrame &f = t.frames[frame];
			if (isMagnitude)
				f.magnitude[bin] = zero ? 0.f : clamp(f.magnitude[bin] + delta, 0.f, 1.f);
			else
				f.phase[bin] = zero ? 0.f : clamp(f.phase[bin] + delta, -(float)M_PI, (float)M_PI);
			f.morphed = false;
			f.calcIFFT();
		});
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		std::vector<float> wav;
		size_t nFrames = 0;
		{
			std::lock_guard<std::mutex> lock(tableMutex);
			for (size_t i = 0; i < table.nFrames; i++) {
				if (table.frames[i].morphed)
					continue;
				wav.insert(wav.end(), table.frames[i].sample.begin(), table.frames[i].sample.end());
				nFrames++;
			}
			json_object_set_new(rootJ, "morphType", json_integer(morphType));
		}
		json_object_set_new(rootJ, "nFrames", json_integer(nFrames));
		json_object_set_new(rootJ, "wavetable", json_string(rack::string::toBase64((const uint8_t*)wav.data(), wav.size() * sizeof(float)).c_str()));
		json_object_set_new(rootJ, "displayMode", json_integer(displayMode));
		json_object_set_new(rootJ, "displayEditedFrame", json_integer(displayEditedFrame));
		json_object_set_new(rootJ, "displayPlayedFrame", json_integer(displayPlayedFrame));
		json_object_set_new(rootJ, "frameSize", json_integer(frameSize));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *displayModeJ = json_object_get(rootJ, "displayMode");
		if (displayModeJ) displayMode = json_integer_value(displayModeJ);
		json_t *displayEditedFrameJ = json_object_get(rootJ, "displayEditedFrame");
		if (displayEditedFrameJ) displayEditedFrame = json_integer_value(displayEditedFrameJ);
		json_t *displayPlayedFrameJ = json_object_get(rootJ, "displayPlayedFrame");
		if (displayPlayedFrameJ) displayPlayedFrame = json_integer_value(displayPlayedFrameJ);
		json_t *frameSizeJ = json_object_get(rootJ, "frameSize");
		if (frameSizeJ) frameSize = std::max((size_t)1, std::min((size_t)json_integer_value(frameSizeJ), MAX_FRAME_SIZE));

		size_t nFrames = 0;
		json_t *nFramesJ = json_object_get(rootJ, "nFrames");
		if (nFramesJ) nFrames = std::min((size_t)json_integer_value(nFramesJ), (size_t)NF);
		json_t *morphTypeJ = json_object_get(rootJ, "morphType");
		int mt = morphTypeJ ? json_integer_value(morphTypeJ) : -1;
		json_t *wavJ = json_object_get(rootJ, "wavetable");
		std::vector<uint8_t> bytes;
		if (wavJ && json_string_value(wavJ))
			bytes = rack::string::fromBase64(json_string_value(wavJ));
		if (bytes.size() != nFrames * FS * sizeof(float))
			nFrames = 0;

		{
			std::lock_guard<std::mutex> lock(tableMutex);
			table.loadSample(nFrames * FS, FS, false, (const float*)bytes.data());
			morphType = mt;
			if (mt >= 0 && mt <= 2)
				table.morph((wtTable::MorphMode)mt);
			table.calcFFT();
		}
		dirty = true;
		requestRebuild();
	}

	void onReset() override {
		lastPath = "";
		postJob([this](wtTable &t) {
			t.reset();
			morphType = -1;
		});
		dirty = true;
	}
};

void LIMONADE::process(const ProcessArgs &args) {
	// Take the newest play table, once the worker has freed the previous one.
	if (pending.load(std::memory_order_relaxed) && !retired.load(std::memory_order_acquire)) {
		wtPlayTable *p = pending.exchange(nullptr, std::memory_order_acq_rel);
		if (p) {
			retired.store(current, std::memory_order_release);
			current = p;
		}
	}

	if (displayModeTrigger.process(params[DISPLAYMODE_PARAM].getValue()))
		displayMode = (displayMode == 0) ? 1 : 0;
	if (displayEditedFrameTrigger.process(params[DISPLAYEDITEDFRAME_PARAM].getValue()))
		displayEditedFrame = (displayEditedFrame == 0) ? 1 : 0;
	if (displayPlayedFrameTrigger.process(params[DISPLAYPLAYEDFRAME_PARAM].getValue()))
		displayPlayedFrame = (displayPlayedFrame == 0) ? 1 : 0;

	uint32_t ops = 0;
	auto button = [&](dsp::SchmittTrigger &trigger, int param, uint32_t op) {
		if (trigger.process(params[param].getValue()))
			ops |= op;
	};
	button(morphWtTrigger, MORPHWT_PARAM, OP_MORPH_WT);
	button(morphSpectrumTrigger, MORPHSPECTRUM_PARAM, OP_MORPH_SPECTRUM);
	button(morphSpectrumPhaseConstantTrigger, MORPHSPECTRUMCONSTANTPHASE_PARAM, OP_MORPH_SPECTRUM_CONSTANT_PHASE);
	button(removeMorphingTrigger, REMOVEMORPH_PARAM, OP_REMOVE_MORPH);
	button(normalizeWtTrigger, NORMALIZEWT_PARAM, OP_NORMALIZE_WT);
	button(normalizeFrameTrigger, NORMALIZEFRAME_PARAM, OP_NORMALIZE_FRAME);
	button(normalizeAllFramesTrigger, NORMALIZEALLFRAMES_PARAM, OP_NORMALIZE_ALL_FRAMES);
	button(removeDCTrigger, REMOVEDC_PARAM, OP_REMOVE_DC);
	button(windowWtTrigger, WINDOWWT_PARAM, OP_WINDOW_WT);
	button(windowFrameTrigger, WINDOWFRAME_PARAM, OP_WINDOW_FRAME);
	button(smoothWtTrigger, SMOOTHWT_PARAM, OP_SMOOTH_WT);
	button(smoothFrameTrigger, SMOOTHFRAME_PARAM, OP_SMOOTH_FRAME);
	button(addFrameTrigger, ADDFRAME_PARAM, OP_ADD_FRAME);
	button(removeFrameTrigger, REMOVEFRAME_PARAM, OP_REMOVE_FRAME);

	bool recWtPressed = recWtTrigger.process(params[RECWT_PARAM].getValue());
	bool recFramePressed = recFrameTrigger.process(params[RECFRAME_PARAM].getValue());
	if (!recWt && !recFrame && !recBusy) {
		size_t fsz = frameSize;
		if (recWtPressed) {
			recWt = true;
			recFrameSize = fsz;
			recTarget = fsz * NF;
		}
		else if (recFramePressed) {
			recFrame = true;
			recFrameSize = fsz;
			recTarget = fsz;
		}
		recIndex = 0;
	}
	if (recWt || recFrame) {
		iRec[recIndex++] = inputs[IN].getVoltage() * 0.1f;
		if (recIndex >= recTarget) {
			recCount = recIndex;
			recBusy = true;
			ops |= recWt ? OP_REC_WT : OP_REC_FRAME;
			recWt = recFrame = false;
		}
	}
	lights[RECWT_LIGHT].setBrightness(recWt ? 1.f : 0.f);
	lights[RECFRAME_LIGHT].setBrightness(recFrame ? 1.f : 0.f);

	if (ops)
		pendingOps.fetch_or(ops, std::memory_order_release);

	int channels = std::max(inputs[PITCH_INPUT].getChannels(), 1);
	int voices = clamp((int)std::round(params[UNISSON_PARAM].getValue()), 1, MAX_VOICES);
	int lanes = channels * voices;
	int groups = (lanes + 3) / 4;

	// Unison voices start at random phases, so they do not sum as one voice.
	if (channels != lastChannels || voices != lastVoices) {
		for (int g = 0; g < groups; g++)
			for (int j = 0; j < 4; j++)
				oscillators[g].phase[j] = voices > 1 ? rack::random::uniform() : 0.f;
		for (int lane = 0; lane < MAX_GROUPS * 4; lane++) {
			int l = std::min(lane, lanes - 1);
			laneChannel[lane] = l / voices;
			laneVoice[lane] = l % voices;
		}
		lastChannels = channels;
		lastVoices = voices;
	}

	outputs[OUT].setChannels(channels);
	if (!current || current->nFrames == 0) {
		for (int c = 0; c < channels; c++)
			outputs[OUT].setVoltage(0.f, c);
		return;
	}

	float freqParam = params[FREQ_PARAM].getValue() / 12.f + dsp::quadraticBipolar(params[FINE_PARAM].getValue()) * 0.25f;
	float fmParam = dsp::quadraticBipolar(params[FM_PARAM].getValue());
	bool fmConnected = inputs[FM_INPUT].isConnected();
	float wtParam = params[WTINDEX_PARAM].getValue();
	float wtAtt = params[WTINDEXATT_PARAM].getValue() * 0.1f;
	float urParam = params[UNISSONRANGE_PARAM].getValue();
	// Mipmap level k keeps 1024 >> k harmonics: the lowest level that does not
	// alias at frequency f is ceil(log2(f * FS / sampleRate)).
	if (args.sampleRate != levelSampleRate) {
		levelBase = std::log2(dsp::FREQ_C4 * FS / args.sampleRate);
		levelSampleRate = args.sampleRate;
	}

	float pitch[16], pos[16], range[16], sync[16];
	for (int c = 0; c < channels; c++) {
		pitch[c] = freqParam + inputs[PITCH_INPUT].getVoltage(c);
		if (fmConnected)
			pitch[c] += fmParam * inputs[FM_INPUT].getPolyVoltage(c);
		pos[c] = clamp(wtParam + inputs[WTINDEX_INPUT].getPolyVoltage(c) * wtAtt, 0.f, 1.f);
		range[c] = clamp(urParam + inputs[UNISSONRANGE_INPUT].getPolyVoltage(c) * 0.002f, 0.f, 0.02f);
		sync[c] = inputs[SYNC_INPUT].getPolyVoltage(c);
	}
	playedPos.store(pos[0], std::memory_order_relaxed);

	// Detune per voice, in units of the voices range.
	static const float detune[MAX_VOICES + 1][MAX_VOICES] = {{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, {-1.f, 1.f, 0.f}, {0.f, 1.f, -1.f}};
	// the switch, or a positive voltage at its jack, selects hard sync
	bool soft = params[SYNC_PARAM].getValue() + inputs[SYNCMODE_INPUT].getVoltage() <= 0.f;
	bool syncConnected = inputs[SYNC_INPUT].isConnected();
	float sum[16] = {};

	for (int g = 0; g < groups; g++) {
		int n = std::min(4, lanes - 4 * g);
		float_4 p, ps, sy;
		for (int j = 0; j < 4; j++) {
			int c = laneChannel[4 * g + j];
			int v = laneVoice[4 * g + j];
			p[j] = pitch[c] + detune[voices][v] * range[c];
			ps[j] = pos[c];
			sy[j] = sync[c];
		}
		float_4 freq = dsp::FREQ_C4 * dsp::approxExp2_taylor5(p + 30.f) / 1073741824.f;
		float_4 level = simd::clamp(-simd::floor(-(p + levelBase)), 0.f, (float)(WT_LEVELS - 1));

		wtOscillator<float_4> &osc = oscillators[g];
		osc.soft = soft;
		osc.syncEnabled = syncConnected;
		float_4 out = osc.process(current, args.sampleTime, freq, ps, level, sy, n);
		for (int j = 0; j < n; j++)
			sum[laneChannel[4 * g + j]] += out[j];
	}

	float gain = 5.f / voices;
	for (int c = 0; c < channels; c++)
		outputs[OUT].setVoltage(clamp(sum[c] * gain, -10.f, 10.f), c);
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

	// Copied from the table under its lock at each draw.
	size_t nFrames = 0;
	size_t editedFrame = 0;
	std::vector<float> magnitude, phase, editedSample, playedSample;

	LIMONADEBinsDisplay() {
		magnitude.resize(FS2);
		phase.resize(FS2);
		editedSample.resize(FS);
		playedSample.resize(FS);
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
		if ((!scroll) && (nFrames>0) && (refIdx >= 0)) {
			bool zero = (APP->window->getMods() & RACK_MOD_MASK) == GLFW_MOD_CONTROL;
			float delta = -e.mouseDelta.y/(250/APP->scene->rackScroll->zoomWidget->zoom);
			if (refY<=heightMagn)
				module->editBin(editedFrame, refIdx, true, zero, delta);
			else if (refY>=heightMagn+graphGap)
				module->editBin(editedFrame, refIdx, false, zero, delta);
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

	void copyFromTable() {
		std::lock_guard<std::mutex> lock(module->tableMutex);
		const wtTable &table = module->table;
		nFrames = table.nFrames;
		if (nFrames == 0)
			return;
		editedFrame = table.frameAt(module->params[LIMONADE::INDEX_PARAM].getValue());
		const wtFrame &edited = table.frames[editedFrame];
		std::copy(edited.magnitude.begin(), edited.magnitude.end(), magnitude.begin());
		std::copy(edited.phase.begin(), edited.phase.end(), phase.begin());
		std::copy(edited.sample.begin(), edited.sample.end(), editedSample.begin());
		const wtFrame &played = table.frames[table.frameAt(module->playedPos)];
		std::copy(played.sample.begin(), played.sample.end(), playedSample.begin());
	}

	void drawWave(const DrawArgs& args, const std::vector<float> &sample, NVGcolor color) {
		nvgStrokeColor(args.vg, color);
		float invNbSample = 1.f / (float)sample.size();
		nvgBeginPath(args.vg);
		for (size_t i = 0; i < sample.size(); i++) {
			float x = (float)i * invNbSample  * 420.f;
			float y = (-1.f)*sample[i] * 18.f + 35.f;
			if (i == 0)
				nvgMoveTo(args.vg, x, y);
			else
				nvgLineTo(args.vg, x, y);
		}
		nvgLineCap(args.vg, NVG_MITER);
		nvgStrokeWidth(args.vg, 1);
		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		nvgStroke(args.vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module) {
				copyFromTable();
				nvgSave(args.vg);
				size_t tag=1;

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

				if (nFrames>0) {
					nvgText(args.vg, 0.0f, heightMagn + graphGap * 0.5f + 4, ("Frame " + to_string(editedFrame + 1) + " / " + to_string(nFrames)).c_str(), NULL);
					for (size_t i = 0; i < FS2/2; i++) {
						float x, y;
						x = (float)i * IFS2;
						y = magnitude[i];
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
							y = phase[i]*IM_PI;
							p.y = heightPhas * 0.5f * y;
							nvgRect(args.vg, p.x+1, heightMagn + graphGap + heightPhas * 0.5f - p.y, b.size.x * IFS2-2, p.y);
							nvgStroke(args.vg);
							nvgFill(args.vg);
						}
					}
				}

				nvgResetScissor(args.vg);

				if (nFrames > 0) {
					if (module->displayPlayedFrame == 0)
						drawWave(args, playedSample, RED_BIDOO);
					if (module->displayEditedFrame == 0)
						drawWave(args, editedSample, GREEN_BIDOO);
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

	// The 3D view draws at most this many frames and points per frame.
	static const size_t MAX_LINES = 64;
	static const size_t LINE_POINTS = 128;
	static const size_t FRAME_POINTS = 512;

	// Copied from the table under its lock at each draw.
	size_t nFrames = 0;
	size_t editedFrame = 0;
	size_t playedFrame = 0;
	std::vector<size_t> lineFrames;
	std::vector<bool> lineMorphed;
	std::vector<float> lines, edited, played;

	LIMONADEWavDisplay() {
		lines.resize(MAX_LINES * LINE_POINTS);
		edited.resize(FRAME_POINTS);
		played.resize(FRAME_POINTS);
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

	void copyFromTable() {
		std::lock_guard<std::mutex> lock(module->tableMutex);
		const wtTable &table = module->table;
		nFrames = table.nFrames;
		lineFrames.clear();
		lineMorphed.clear();
		if (nFrames == 0)
			return;
		size_t step = (nFrames + MAX_LINES - 1) / MAX_LINES;
		for (size_t f = 0, l = 0; f < nFrames; f += step, l++) {
			lineFrames.push_back(f);
			lineMorphed.push_back(table.frames[f].morphed);
			for (size_t i = 0; i < LINE_POINTS; i++)
				lines[l * LINE_POINTS + i] = table.frames[f].sample[i * (FS / LINE_POINTS)];
		}
		editedFrame = table.frameAt(module->params[LIMONADE::INDEX_PARAM].getValue());
		playedFrame = table.frameAt(module->playedPos);
		for (size_t i = 0; i < FRAME_POINTS; i++) {
			edited[i] = table.frames[editedFrame].sample[i * (FS / FRAME_POINTS)];
			played[i] = table.frames[playedFrame].sample[i * (FS / FRAME_POINTS)];
		}
	}

	void drawLine(const DrawArgs& args, const float *points, size_t count, size_t frame) {
		float y3D = 10.0f * frame / nFrames - 5.0f;
		nvgBeginPath(args.vg);
		for (size_t i = 0; i < count; i++) {
			float x3D = 10.0f * i / count - 5.0f;
			float z3D = -points[i];
			float y2D = z3D*ca1-(ca2*y3D-sa2*x3D)*sa1+5.0f;
			float x2D = ca2*x3D+sa2*y3D+7.5f;
			if (i == 0)
				nvgMoveTo(args.vg, 10.0f * x2D, 10.0f * y2D);
			else
				nvgLineTo(args.vg, 10.0f * x2D, 10.0f * y2D);
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module && (module->displayMode == 0)) {
				copyFromTable();
				nvgSave(args.vg);

				nvgFontSize(args.vg, 8.0f);
				nvgFillColor(args.vg, YELLOW_BIDOO);
				nvgStrokeWidth(args.vg, 1.0f);

				nvgText(args.vg, width+2, height-10, ("V=" + to_string((int)module->params[LIMONADE::UNISSON_PARAM].getValue())).c_str(), NULL);

				// Back to front.
				for (size_t n = lineFrames.size(); n-- > 0; ) {
					drawLine(args, &lines[n * LINE_POINTS], LINE_POINTS, lineFrames[n]);
					nvgStrokeColor(args.vg, nvgRGBA(255, 233, 0, lineMorphed[n] ? 15 : 50));
					nvgStroke(args.vg);
				}

				if (nFrames>0) {
					drawLine(args, edited.data(), FRAME_POINTS, editedFrame);
					nvgStrokeColor(args.vg, GREEN_BIDOO);
					nvgStroke(args.vg);

					drawLine(args, played.data(), FRAME_POINTS, playedFrame);
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
		if (!module)
			return;
		long long val = 0;
		std::istringstream(getText()) >> val;
		if (val >= 1)
			module->frameSize = std::min((size_t)val, MAX_FRAME_SIZE);
	};

	void onDeselect(const DeselectEvent &e) override {
		// Show the value actually used.
		if (module)
			setText(std::to_string(module->frameSize));
		LedDisplayTextField::onDeselect(e);
	}
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

// Asks for a file path; empty if cancelled. Saving adds `ext` when the name has none.
static std::string limonadeFileDialog(LIMONADE *module, osdialog_file_action action, const char *filterSpec, const std::string &defaultName = "", const std::string &ext = "") {
	std::string dir = module->lastPath.empty() ? asset::user("") : rack::system::getDirectory(module->lastPath);
	osdialog_filters* filters = osdialog_filters_parse(filterSpec);
	char *path = osdialog_file(action, dir.c_str(), action == OSDIALOG_SAVE ? defaultName.c_str() : NULL, filters);
	osdialog_filters_free(filters);
	std::string result;
	if (path) {
		result = path;
		free(path);
	}
	if (!result.empty() && !ext.empty() && rack::string::lowercase(rack::system::getExtension(result)) != ext)
		result += ext;
	return result;
}

// Every frame as played (morphed ones included), FS samples each, one after the other.
struct moduleSaveWavetableAsWavItem : MenuItem {
	LIMONADE *module;
	void onAction(const event::Action &e) override {
		std::vector<float> samples = module->copyFrames(0, NF);
		if (samples.empty())
			return;
		std::string path = limonadeFileDialog(module, OSDIALOG_SAVE, WAV_FILTERS, "wavetable.wav", ".wav");
		if (!path.empty())
			writeWav(samples, APP->engine->getSampleRate(), path);
	}
};

// The edited frame: the one the Edited frame knob points at, shown in the editor.
struct moduleSaveFrameAsWavItem : MenuItem {
	LIMONADE *module;
	void onAction(const event::Action &e) override {
		size_t frame;
		{
			std::lock_guard<std::mutex> lock(module->tableMutex);
			frame = module->table.frameAt(module->params[LIMONADE::INDEX_PARAM].getValue());
		}
		std::vector<float> samples = module->copyFrames(frame, 1);
		if (samples.empty())
			return;
		std::string path = limonadeFileDialog(module, OSDIALOG_SAVE, WAV_FILTERS, "frame " + std::to_string(frame + 1) + ".wav", ".wav");
		if (!path.empty())
			writeWav(samples, APP->engine->getSampleRate(), path);
	}
};

struct moduleSaveWavetableAsPngItem : MenuItem {
	LIMONADE *module;
	void onAction(const event::Action &e) override {
		std::vector<float> samples = module->copyFrames(0, NF);
		if (samples.empty())
			return;
		std::string path = limonadeFileDialog(module, OSDIALOG_SAVE, PNG_FILTERS, "wavetable.png", ".png");
		if (!path.empty())
			writePng(samples, path);
	}
};

// Opens the file dialog after the click is over, from step(): a modal dialog
// opened inside the mouse press swallows the release and leaves the button
// held, so the next click does not register.
template <int KIND>
struct LimonadeBlueBtnLoad : BlueBtn {
	bool openDialog = false;

	void onDragEnd(const DragEndEvent &e) override {
		BlueBtn::onDragEnd(e);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT)
			openDialog = true;
	}

	void step() override {
		BlueBtn::step();
		if (!openDialog)
			return;
		openDialog = false;
		LIMONADE *module = getParamQuantity() ? dynamic_cast<LIMONADE*>(getParamQuantity()->module) : NULL;
		if (!module)
			return;
		std::string path = limonadeFileDialog(module, OSDIALOG_OPEN, KIND == 1 ? PNG_FILTERS : AUDIO_FILTERS);
		if (path.empty())
			return;
		if (KIND == 0) module->loadSample(path);
		else if (KIND == 1) module->loadPNG(path);
		else module->loadFrame(path);
	}
};
typedef LimonadeBlueBtnLoad<0> LimonadeBlueBtnLoadSample;
typedef LimonadeBlueBtnLoad<1> LimonadeBlueBtnLoadPNG;
typedef LimonadeBlueBtnLoad<2> LimonadeBlueBtnLoadFrame;

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
		size_t nFrames, editedFrame;
		{
			std::lock_guard<std::mutex> lock(m->tableMutex);
			nFrames = m->table.nFrames;
			editedFrame = m->table.frameAt(m->params[LIMONADE::INDEX_PARAM].getValue());
		}
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Export"));
		moduleSaveWavetableAsWavItem *saveWavetableAsWavItem = new moduleSaveWavetableAsWavItem;
		saveWavetableAsWavItem->text = "Wavetable (all frames) to WAV...";
		saveWavetableAsWavItem->rightText = std::to_string(nFrames) + " x " + std::to_string(FS);
		saveWavetableAsWavItem->disabled = nFrames == 0;
		saveWavetableAsWavItem->module = m;
		menu->addChild(saveWavetableAsWavItem);
		moduleSaveFrameAsWavItem *saveFrameAsWavItem = new moduleSaveFrameAsWavItem;
		saveFrameAsWavItem->text = "Edited frame (single cycle) to WAV...";
		saveFrameAsWavItem->rightText = nFrames ? "frame " + std::to_string(editedFrame + 1) + "/" + std::to_string(nFrames) : "";
		saveFrameAsWavItem->disabled = nFrames == 0;
		saveFrameAsWavItem->module = m;
		menu->addChild(saveFrameAsWavItem);
		moduleSaveWavetableAsPngItem *saveWavetableAsPngItem = new moduleSaveWavetableAsPngItem;
		saveWavetableAsPngItem->text = "Wavetable to PNG image...";
		saveWavetableAsPngItem->rightText = "8-bit";
		saveWavetableAsPngItem->disabled = nFrames == 0;
		saveWavetableAsPngItem->module = m;
		menu->addChild(saveWavetableAsPngItem);
	}

	void onPathDrop(const PathDropEvent& e) override {
		Widget::onPathDrop(e);
		LIMONADE *module = dynamic_cast<LIMONADE*>(this->module);
		if (!module || e.paths.empty())
			return;
		if (rack::string::lowercase(rack::system::getExtension(e.paths[0])) == ".png")
			module->loadPNG(e.paths[0]);
		else
			module->loadSample(e.paths[0]);
	}
};

Model *modelLIMONADE = createModel<LIMONADE, LIMONADEWidget>("liMonADe");

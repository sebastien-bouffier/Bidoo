#pragma once
#include <rack.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include "dep/waves.hpp"
#include "SampleRead.hpp"

using namespace rack;

// The engine shared by POUPRE, MAGMA and OAI: samples played as 16 polyphonic
// slice voices, with a state-variable filter and choke groups (kill) for MAGMA
// and OAI, and a background loader.

// Linear up to 8 V, then bends to stay under 10 V (9.95 V at most).
inline float sliceSoftLimit(float x) {
	float a = std::fabs(x);
	if (a <= 8.0f)
		return x;
	return std::copysign(8.0f + 1.95f * std::tanh((a - 8.0f) * 0.5f), x);
}

struct SliceSample {
	std::vector<float> data;
	sampleread::Mip<1> mip;  // band-limited copies, for playing faster than 1x
	float sampleRate = 0.f;
	int size() const { return (int)data.size(); }
};

// Samples in slots (POUPRE and MAGMA: 1, OAI: one per voice). The worker loads
// them and puts each in `pending`; the audio thread takes it and moves the old
// one to `retired`, which the worker frees.
struct SliceBank {
	static constexpr int MAX_SLOTS = 16;
	int slots = 1;
	SliceSample *current[MAX_SLOTS] = {};          // audio thread
	std::atomic<SliceSample*> pending[MAX_SLOTS];
	std::atomic<SliceSample*> retired[MAX_SLOTS];

	std::thread worker;
	std::mutex jobMutex;
	std::condition_variable jobCv;
	struct Job { int slot; std::string path; float rate; };
	std::deque<Job> jobs;
	std::string paths[MAX_SLOTS];                  // under jobMutex
	std::atomic<bool> quit{false};
	std::atomic<float> reloadRate{0.f};            // set by the audio thread
	std::atomic<int> busy{0};                      // jobs queued or running

	explicit SliceBank(int n) : slots(n) {
		for (int i = 0; i < MAX_SLOTS; i++) {
			pending[i] = nullptr;
			retired[i] = nullptr;
		}
		worker = std::thread(&SliceBank::workerLoop, this);
	}

	~SliceBank() {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			quit = true;
		}
		jobCv.notify_all();
		if (worker.joinable())
			worker.join();
		for (int i = 0; i < MAX_SLOTS; i++) {
			delete pending[i].load();
			delete retired[i].load();
			delete current[i];
		}
	}

	static float engineRate() {
		return (APP && APP->engine) ? APP->engine->getSampleRate() : 48000.f;
	}

	// UI thread: load a file into a slot.
	void load(int slot, const std::string &path) {
		if (slot < 0 || slot >= slots)
			return;
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			paths[slot] = path;
			jobs.push_back({slot, path, engineRate()});
			busy++;
		}
		jobCv.notify_all();
	}

	std::string path(int slot) {
		std::lock_guard<std::mutex> lock(jobMutex);
		return (slot >= 0 && slot < slots) ? paths[slot] : std::string();
	}

	// Audio thread, every few samples (and while bypassed, so the worker
	// never waits on it).
	void poll(float sampleRate) {
		for (int i = 0; i < slots; i++) {
			if (pending[i].load(std::memory_order_relaxed) && !retired[i].load(std::memory_order_acquire)) {
				SliceSample *s = pending[i].exchange(nullptr, std::memory_order_acq_rel);
				if (s) {
					retired[i].store(current[i], std::memory_order_release);
					current[i] = s;
				}
			}
			// The engine rate changed: load the files again at the new rate.
			if (current[i] && current[i]->sampleRate != sampleRate && busy.load() == 0)
				reloadRate = sampleRate;
		}
	}

	void workerLoop() {
		std::unique_lock<std::mutex> lock(jobMutex);
		while (!quit) {
			// The audio thread cannot notify without locking: poll for its requests.
			if (jobs.empty())
				jobCv.wait_for(lock, std::chrono::milliseconds(50));
			for (int i = 0; i < slots; i++)
				delete retired[i].exchange(nullptr, std::memory_order_acq_rel);
			float rr = reloadRate.exchange(0.f);
			if (rr > 0.f) {
				for (int i = 0; i < slots; i++) {
					if (!paths[i].empty()) {
						jobs.push_back({i, paths[i], rr});
						busy++;
					}
				}
			}
			if (jobs.empty() || quit)
				continue;
			Job job = jobs.front();
			jobs.pop_front();
			lock.unlock();

			std::string name, ext;
			int ch = 0, sr = 0, cnt = 0;
			std::vector<dsp::Frame<1>> frames = waves::getMonoWav(job.path, job.rate, name, ext, ch, sr, cnt);
			SliceSample *s = new SliceSample();
			s->sampleRate = job.rate;
			s->data.resize(frames.size());
			for (size_t k = 0; k < frames.size(); k++)
				s->data[k] = frames[k].samples[0];
			s->mip.build(s->data.data(), s->size());
			// Wait until the audio thread has taken the slot's previous sample.
			while (pending[job.slot].load() && !quit)
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			delete pending[job.slot].exchange(s, std::memory_order_acq_rel);

			lock.lock();
			busy--;
		}
	}
};

// Settings of one slice voice, edited on the panel for the selected channel.
struct SliceSettings {
	float start = 0.0f;
	float len = 1.0f;
	bool loop = false;
	float speed = 0.0f;     // octaves
	int gate = 1;           // 1: a trigger plays the slice, 0: plays while the gate is high
	int filterType = 0;     // 0 none, 1 low-pass, 2 band-pass, 3 high-pass
	float q = 0.1f;
	float freq = 1.0f;
	int kill = -1;          // the channel this one chokes, -1 none

	void reset() {
		*this = SliceSettings();
	}

	void randomize() {
		q = random::uniform();
		freq = random::uniform();
		filterType = (int)(random::uniform() * 4.0f) % 4;
		gate = random::uniform() > 0.5f;
		loop = random::uniform() > 0.5f;
		start = random::uniform();
		len = random::uniform();
		speed = random::uniform() * 2.0f - 1.0f;
		kill = (int)(random::uniform() * 17.0f) - 1;
	}

	json_t *toJson() const {
		json_t *j = json_object();
		json_object_set_new(j, "start", json_real(start));
		json_object_set_new(j, "len", json_real(len));
		json_object_set_new(j, "speed", json_real(speed));
		json_object_set_new(j, "loop", json_boolean(loop));
		json_object_set_new(j, "gate", json_integer(gate));
		json_object_set_new(j, "filterType", json_integer(filterType));
		json_object_set_new(j, "q", json_real(q));
		json_object_set_new(j, "freq", json_real(freq));
		json_object_set_new(j, "kill", json_integer(kill));
		return j;
	}

	void fromJson(json_t *j) {
		reset();
		if (!j)
			return;
		if (json_t *v = json_object_get(j, "start")) start = clamp((float)json_number_value(v), 0.f, 1.f);
		if (json_t *v = json_object_get(j, "len")) len = clamp((float)json_number_value(v), 0.f, 1.f);
		if (json_t *v = json_object_get(j, "speed")) speed = clamp((float)json_number_value(v), -4.f, 4.f);
		if (json_t *v = json_object_get(j, "loop")) loop = json_is_true(v);
		if (json_t *v = json_object_get(j, "gate")) gate = json_integer_value(v) ? 1 : 0;
		if (json_t *v = json_object_get(j, "filterType")) filterType = clamp((int)json_integer_value(v), 0, 3);
		if (json_t *v = json_object_get(j, "q")) q = clamp((float)json_number_value(v), 0.1f, 1.f);
		if (json_t *v = json_object_get(j, "freq")) freq = clamp((float)json_number_value(v), 0.f, 1.f);
		if (json_t *v = json_object_get(j, "kill")) kill = clamp((int)json_integer_value(v), -1, 15);
	}
};

// State-variable filter (zero-delay feedback), cutoff kept under Nyquist.
struct SliceFilter {
	float hp = 0.f, bp = 0.f, lp = 0.f, mem1 = 0.f, mem2 = 0.f;
	// The coefficients, computed again only when the cutoff or Q moves.
	float lastFc = -1.f, lastQ = -1.f, g = 0.f, R = 1.f, norm = 1.f;

	void process(float sample, float freq, float q, float sampleTime) {
		float fc = clamp(freq * sampleTime, 1e-5f, 0.45f);
		if (fc != lastFc || q != lastQ) {
			lastFc = fc;
			lastQ = q;
			g = std::tan(float(M_PI) * fc);
			R = 1.0f / (2.0f * q);
			norm = 1.0f / (1.0f + 2.0f * R * g + g * g);
		}
		hp = (sample - (2.0f * R + g) * mem1 - mem2) * norm;
		bp = g * hp + mem1;
		lp = g * bp + mem2;
		mem1 = g * hp + bp;
		mem2 = g * bp + lp;
	}

	void reset() {
		hp = bp = lp = mem1 = mem2 = 0.f;
	}
};

// What one voice plays this sample, from its settings and the polyphonic CVs.
struct SliceControls {
	float start = 0.f, len = 1.f, speed = 1.f;
	bool loop = false;
	int gate = 1;
	int filterType = 0;
	float q = 1.f, freq = 1000.f;
};

struct SliceVoice {
	double head = 0.0;     // double: a float loses the pitch far into long samples
	bool active = false;
	float speed = 1.f;     // of the last sample played
	float lastGain = 0.f;  // declick gain of the last sample played
	dsp::SchmittTrigger trigger;
	SliceFilter filter;
	SliceControls ctl;
	sampleread::Declick<1> dc;
};

// Plays voice v on sample s for one sample; returns the output voltage.
// `gateHigh`/`rise` come from the voice's trigger input.
// `killed`: another voice that chokes this one is playing.
// Every start, restart, stop, choke and loop jump is declicked over
// `fadeLen` samples: the old head fades out while the new one fades in, and a
// slice played to its end fades out just before it.
inline float slicePlay(SliceVoice &v, const SliceSample *s, const SliceControls &c, bool rise, bool gateHigh, bool killed, bool hasFilter, float sampleTime, int fadeLen) {
	int size = s ? s->size() : 0;
	if (size < 2) {
		v.active = false;
		v.dc.reset();
		return 0.f;
	}
	const sampleread::View<1> view{s->data.data(), size, &s->mip};
	float first = clamp(c.start * size, 0.f, size - 2.f);
	float end = std::min((c.start + c.len) * size, size - 1.f);
	if ((!v.active || c.gate == 1) && rise) {
		if (v.active)
			v.dc.tail(v.head, v.speed, v.lastGain, fadeLen);
		else if (!v.dc.tailActive())
			v.filter.reset();
		v.active = true;
		v.head = first;
		v.dc.fadeIn(fadeLen);
	}
	else if (c.gate == 0 && !gateHigh && v.active) {
		v.dc.tail(v.head, v.speed, v.lastGain, fadeLen);
		v.active = false;
	}
	if (killed && v.active) {
		v.dc.tail(v.head, v.speed, v.lastGain, fadeLen);
		v.active = false;
	}
	if (!v.active && !v.dc.tailActive())
		return 0.f;

	float out = 0.f;
	if (v.active) {
		float g = v.dc.nextIn();
		bool loops = c.loop && c.gate == 0;
		if (!loops) {
			// Fades out just before the end of the slice.
			float left = (float)(end - v.head) / c.speed;
			if (left < fadeLen)
				g *= std::max(left, 0.f) / fadeLen;
		}
		float x;
		view.read(v.head, c.speed, &x);
		out = x * g;
		v.lastGain = g;
		v.speed = c.speed;
	}
	v.dc.addTail(view, &out);
	if (v.active) {
		v.head += c.speed;
		if (v.head >= size - 1 || v.head > end) {
			if (c.loop && c.gate == 0) {
				// The old head goes on from the next sample, fading out.
				v.dc.tail(v.head, c.speed, v.lastGain, fadeLen);
				v.head = first;
				v.dc.fadeIn(fadeLen);
			}
			else
				v.active = false;
		}
	}
	if (hasFilter && c.filterType > 0) {
		v.filter.process(out, c.freq, c.q, sampleTime);
		out = (c.filterType == 1) ? v.filter.lp : (c.filterType == 2) ? v.filter.bp : v.filter.hp;
	}
	return sliceSoftLimit(5.0f * out);
}

// The CVs of voice i on top of its settings. M has the shared input ids;
// M::HAS_FILTER adds the filter inputs.
template <class M>
SliceControls sliceControls(M &m, const SliceSettings &st, int i) {
	SliceControls c;
	c.start = clamp(st.start + m.inputs[M::START_INPUT].getPolyVoltage(i) * 0.1f, 0.f, 1.f);
	c.len = clamp(st.len + m.inputs[M::LEN_INPUT].getPolyVoltage(i) * 0.1f, 0.f, 1.f);
	// Speed: octaves from the knob plus 1V/oct.
	c.speed = dsp::exp2_taylor5(clamp(st.speed + m.inputs[M::SPEED_INPUT].getPolyVoltage(i), -4.f, 4.f));
	c.loop = st.loop && (!m.inputs[M::LOOP_INPUT].isConnected() || m.inputs[M::LOOP_INPUT].getPolyVoltage(i) > 0.1f);
	c.gate = m.inputs[M::GATE_INPUT].isConnected() ? (m.inputs[M::GATE_INPUT].getPolyVoltage(i) >= 1.f ? 1 : 0) : st.gate;
	c.filterType = 0;
	c.q = 1.f;
	c.freq = 1000.f;
	if constexpr (M::HAS_FILTER) {
		c.filterType = m.inputs[M::FILTERTYPE_INPUT].isConnected() ? clamp((int)std::round(m.inputs[M::FILTERTYPE_INPUT].getPolyVoltage(i) * 0.3f), 0, 3) : st.filterType;
		c.q = 10.0f * clamp(st.q + m.inputs[M::Q_INPUT].getPolyVoltage(i) * 0.09f, 0.1f, 1.0f);
		// Cutoff: the knob covers 22 Hz to 16 kHz, plus 1V/oct.
		c.freq = dsp::exp2_taylor5(clamp(4.5f + 9.5f * st.freq + m.inputs[M::FREQ_INPUT].getPolyVoltage(i), 3.f, 15.f));
	}
	return c;
}

// Between two full updates: only what an audio-rate CV can move (speed and
// cutoff), and only when its input is patched.
template <class M>
void sliceFastControls(M &m, const SliceSettings &st, int i, SliceControls &c) {
	if (m.inputs[M::SPEED_INPUT].isConnected())
		c.speed = dsp::exp2_taylor5(clamp(st.speed + m.inputs[M::SPEED_INPUT].getPolyVoltage(i), -4.f, 4.f));
	if constexpr (M::HAS_FILTER) {
		if (m.inputs[M::FREQ_INPUT].isConnected())
			c.freq = dsp::exp2_taylor5(clamp(4.5f + 9.5f * st.freq + m.inputs[M::FREQ_INPUT].getPolyVoltage(i), 3.f, 15.f));
	}
}

// The channel voice j chokes, from its setting or the polyphonic kill CV.
template <class M>
int sliceKill(M &m, const SliceSettings &st, int j) {
	if (m.inputs[M::KILL_INPUT].isConnected())
		return clamp((int)std::round(m.inputs[M::KILL_INPUT].getPolyVoltage(j) * 1.6f) - 1, -1, 15);
	return st.kill;
}

// Shared controls (the slice settings of the selected channel).
template <class M>
void sliceConfig(M &m) {
	m.configParam(M::CHANNEL_PARAM, 0.0f, 15.0f, 0.0f, "Edited channel", "", 0.f, 1.f, 1.f);
	m.paramQuantities[M::CHANNEL_PARAM]->snapEnabled = true;
	m.configParam(M::START_PARAM, 0.0f, 1.0f, 0.0f, "Start", "%", 0.f, 100.f);
	m.configParam(M::LEN_PARAM, 0.0f, 1.0f, 1.0f, "Length", "%", 0.f, 100.f);
	m.configSwitch(M::LOOP_PARAM, 0.0f, 1.0f, 0.0f, "Loop (gate mode)", {"Off", "On"});
	m.configSwitch(M::GATE_PARAM, 0.0f, 1.0f, 1.0f, "Mode", {"Gate", "Trigger"});
	m.configParam(M::SPEED_PARAM, -4.0f, 4.0f, 0.0f, "Speed", "x", 2.f);
	m.configInput(M::TRIG_INPUT, "Trigger / gate (polyphonic)");
	m.configInput(M::GATE_INPUT, "Mode (above 1 V: trigger, below: gate)");
	m.configInput(M::START_INPUT, "Start (10 V = 100%)");
	m.configInput(M::LEN_INPUT, "Length (10 V = 100%)");
	m.configInput(M::LOOP_INPUT, "Loop (high: on)");
	m.configInput(M::SPEED_INPUT, "Speed (1V/oct)");
	m.configOutput(M::POLY_OUTPUT, "Audio (polyphonic)");
	if constexpr (M::HAS_FILTER) {
		m.configSwitch(M::FILTERTYPE_PARAM, 0.0f, 3.0f, 0.0f, "Filter", {"Off", "Low-pass", "Band-pass", "High-pass"});
		m.configParam(M::FREQ_PARAM, 0.0f, 1.0f, 1.0f, "Cutoff", " Hz", std::pow(2.f, 9.5f), std::pow(2.f, 4.5f));
		m.configParam(M::Q_PARAM, 0.1f, 1.0f, 0.1f, "Resonance", "", 0.f, 10.f);
		m.configParam(M::KILL_PARAM, -1.0f, 15.0f, -1.0f, "Chokes channel (0: none)", "", 0.f, 1.f, 1.f);
		m.paramQuantities[M::KILL_PARAM]->snapEnabled = true;
		m.configInput(M::FILTERTYPE_INPUT, "Filter type (0, 3.3, 6.7, 10 V)");
		m.configInput(M::FREQ_INPUT, "Cutoff (1V/oct)");
		m.configInput(M::Q_INPUT, "Resonance (10 V = 90%)");
		m.configInput(M::KILL_INPUT, "Chokes channel (polyphonic, 0-10 V = none-16)");
	}
}

// The panel shows the selected channel's settings.
template <class M>
void sliceLoadPanel(M &m, const SliceSettings &st) {
	m.params[M::START_PARAM].setValue(st.start);
	m.params[M::LEN_PARAM].setValue(st.len);
	m.params[M::SPEED_PARAM].setValue(st.speed);
	m.params[M::LOOP_PARAM].setValue(st.loop ? 1.0f : 0.0f);
	m.params[M::GATE_PARAM].setValue(st.gate);
	if constexpr (M::HAS_FILTER) {
		m.params[M::FILTERTYPE_PARAM].setValue(st.filterType);
		m.params[M::Q_PARAM].setValue(st.q);
		m.params[M::FREQ_PARAM].setValue(st.freq);
		m.params[M::KILL_PARAM].setValue(st.kill);
	}
}

template <class M>
void sliceSavePanel(M &m, SliceSettings &st) {
	st.start = m.params[M::START_PARAM].getValue();
	st.len = m.params[M::LEN_PARAM].getValue();
	st.speed = m.params[M::SPEED_PARAM].getValue();
	st.loop = m.params[M::LOOP_PARAM].getValue() > 0.5f;
	st.gate = m.params[M::GATE_PARAM].getValue() > 0.5f ? 1 : 0;
	if constexpr (M::HAS_FILTER) {
		st.filterType = clamp((int)std::round(m.params[M::FILTERTYPE_PARAM].getValue()), 0, 3);
		st.q = m.params[M::Q_PARAM].getValue();
		st.freq = m.params[M::FREQ_PARAM].getValue();
		st.kill = clamp((int)std::round(m.params[M::KILL_PARAM].getValue()), -1, 15);
	}
}

// Slice presets: n equal slices from the start of the sample.
inline void slicePreset(SliceSettings *st, int count, int slices) {
	for (int j = 0; j < count; j++) {
		st[j].start = j * (1.0f / slices);
		st[j].len = 1.0f / slices;
		st[j].speed = 0.0f;
		st[j].loop = false;
		st[j].gate = 1;
	}
}

// The control-rate state of a slice module.
struct SliceRun {
	dsp::ClockDivider control;  // panel and CVs that only move slowly
	bool first = true;
	SliceRun() {
		control.setDivision(16);
	}
};

// One sample of the 16 voices. sampleOf(i) is the sample voice i plays.
// Choking: a voice is choked when another voice that is playing (or starts
// now) names it; a voice starting now is not choked by a voice already
// playing, so the newest note wins. Two voices starting on the same sample
// that choke each other: the lower channel plays, the higher one is cut.
template <class M, class SampleOf>
void sliceProcess(M &m, SliceRun &run, const Module::ProcessArgs &args, SampleOf sampleOf) {
	bool full = run.control.process() || run.first;
	run.first = false;
	if (full) {
		// The panel shows the selected channel.
		int sel = clamp((int)m.params[M::CHANNEL_PARAM].getValue(), 0, 15);
		if (sel != m.currentChannel) {
			m.currentChannel = sel;
			sliceLoadPanel(m, m.settings[m.currentChannel]);
		}
		sliceSavePanel(m, m.settings[m.currentChannel]);
	}

	int c = std::max(m.inputs[M::TRIG_INPUT].getChannels(), 1);
	bool rise[16], high[16];
	for (int i = 0; i < c; i++) {
		SliceVoice &v = m.voices[i];
		rise[i] = v.trigger.process(m.inputs[M::TRIG_INPUT].getVoltage(i), 0.1f, 1.f);
		high[i] = v.trigger.isHigh();
		if (full || rise[i])
			v.ctl = sliceControls(m, m.settings[i], i);
		else
			sliceFastControls(m, m.settings[i], i, v.ctl);
	}
	uint32_t killedMask = 0;
	if constexpr (M::HAS_FILTER) {
		for (int j = 0; j < c; j++) {
			if (m.voices[j].active || rise[j]) {
				int k = sliceKill(m, m.settings[j], j);
				if (k >= 0 && k < c && k != j)
					killedMask |= 1u << k;
			}
		}
		for (int j = 0; j < c; j++)
			if (rise[j])
				killedMask &= ~(1u << j);
		// Two voices starting on the same sample that choke each other (either way):
		// the lower channel has priority, the higher one does not start.
		for (int j = 0; j < c; j++) {
			if (!rise[j])
				continue;
			int k = sliceKill(m, m.settings[j], j);
			if (k >= 0 && k < c && k != j && rise[k])
				killedMask |= 1u << std::max(j, k);
		}
	}
	int fadeLen = sampleread::declickLength(args.sampleRate);
	for (int i = 0; i < c; i++) {
		bool killed = (killedMask >> i) & 1u;
		m.outputs[M::POLY_OUTPUT].setVoltage(slicePlay(m.voices[i], sampleOf(i), m.voices[i].ctl, rise[i], high[i], killed, M::HAS_FILTER, args.sampleTime, fadeLen), i);
	}
	for (int i = c; i < 16; i++) {
		m.voices[i].active = false;
		m.voices[i].dc.reset();
	}
	m.outputs[M::POLY_OUTPUT].setChannels(c);
}

// A preset light flash fades in about a quarter of a second; dt is the time
// since its last update.
inline void slicePresetLight(Light &l, float dt) {
	float b = l.getBrightness();
	if (b > 0.f)
		l.setBrightness(b < 1e-3f ? 0.f : b * std::exp(-4.f * dt));
}

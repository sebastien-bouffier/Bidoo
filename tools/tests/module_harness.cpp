// Every Bidoo module in Rack's own engine, without a window: run with random knobs and random signals on every input
// (mono and polyphonic, audio, gates, CV), the patch data round trip (toJson, fromJson, toJson again: the same), and
// damaged patch data (each key of the module's data missing, null, of the wrong type, huge or negative), each loaded
// into a fresh module that then runs. A crash stops the program; its last line names the module and the step.
// Outputs must stay finite and under 50 V.
//
// Build (MSYS2 MINGW64 or with C:/msys64/mingw64/bin on PATH), from the plugin folder:
//   g++ -std=c++17 -O1 -I G:/Rack-SDK/include -I G:/Rack-SDK/dep/include tools/tests/module_harness.cpp
//       -L G:/Rack-SDK -lRack -o <dir>/module_harness.exe
// Run: python tools/tests/module_harness.py (makes a user folder with the packaged plugin, runs it with Rack Pro's
// libRack.dll), or by hand:
//   module_harness.exe <user folder holding plugins-win-x64/Bidoo-x.y.z-win-x64.vcvplugin> [model slug ...]
// The harness starts Rack itself, so it calls functions rack.hpp forbids plugins (PRIVATE): it includes Rack's
// headers one by one, as Rack's own sources do, where PRIVATE is empty.
#include <common.hpp>
#include <context.hpp>
#include <asset.hpp>
#include <logger.hpp>
#include <settings.hpp>
#include <random.hpp>
#include <system.hpp>
#include <plugin.hpp>
#include <plugin/Plugin.hpp>
#include <plugin/Model.hpp>
#include <engine/Engine.hpp>
#include <engine/Module.hpp>
#include <engine/ParamQuantity.hpp>
#include <patch.hpp>
#include <history.hpp>
#include <jansson.h>
#include <random>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>
#include <set>

using namespace rack;

static int failures = 0;
static int64_t frame = 0;   // the engine frame passed to process()
static std::mt19937 rng(12345);

static float uni(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }
static int pick(int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); }

static void step(const char *model, const char *what) {
	std::printf("%-14s %s\n", model, what);
	std::fflush(stdout);
}

#define FAIL(...) do { failures++; std::printf("FAIL "); std::printf(__VA_ARGS__); std::printf("\n"); std::fflush(stdout); } while (0)

// what each input carries during a run
struct Feed {
	int kind = 0;       // 0 audio noise, 1 gate, 2 slow CV, 3 saw
	int channels = 1;
	float rate = 1.f;   // Hz, for gates, CV and saws
	float phase = 0.f;
};

static void randomFeeds(engine::Module *m, std::vector<Feed> &feeds) {
	feeds.assign(m->inputs.size(), Feed());
	for (Feed &f : feeds) {
		f.kind = pick(4);
		f.channels = pick(4) == 0 ? 1 + pick(16) : 1;
		f.rate = f.kind == 1 ? uni(0.5f, 20.f) : f.kind == 2 ? uni(0.05f, 3.f) : uni(30.f, 600.f);
		if (pick(5) == 0)
			f.channels = 0;   // left unpatched
	}
}

static void feedInputs(engine::Module *m, std::vector<Feed> &feeds, int frames, float sampleRate) {
	for (size_t i = 0; i < m->inputs.size(); i++) {
		Feed &f = feeds[i];
		engine::Input &in = m->inputs[i];
		// the cable's channels, set directly: setChannels() keeps an unpatched port at 0 (unpatched to the module)
		in.channels = f.channels;
		f.phase += f.rate * frames / sampleRate;
		f.phase -= std::floor(f.phase);
		for (int c = 0; c < f.channels; c++) {
			float v = 0.f;
			switch (f.kind) {
				case 0: v = uni(-5.f, 5.f); break;
				case 1: v = std::fmod(f.phase + 0.13f * c, 1.f) < 0.5f ? 10.f : 0.f; break;
				case 2: v = 5.f * std::sin(2.f * M_PI * (f.phase + 0.1f * c)); break;
				case 3: v = 10.f * std::fmod(f.phase + 0.07f * c, 1.f) - 5.f; break;
			}
			in.setVoltage(v, c);
		}
	}
}

// runs `blocks` blocks of 32 frames, checking the outputs after each
static bool run(engine::Module *m, const char *model, int blocks, bool moveKnobs) {
	std::vector<Feed> feeds;
	randomFeeds(m, feeds);
	const float sr = APP->engine->getSampleRate();
	for (int b = 0; b < blocks; b++) {
		if (moveKnobs && b % 64 == 0) {
			for (size_t p = 0; p < m->params.size(); p++) {
				engine::ParamQuantity *q = m->paramQuantities[p];
				if (q && pick(3) == 0) {
					float v = uni(q->getMinValue(), q->getMaxValue());
					if (q->snapEnabled)
						v = std::round(v);
					m->params[p].setValue(v);
				}
			}
			randomFeeds(m, feeds);
		}
		feedInputs(m, feeds, 32, sr);
		// process() frame by frame, as the engine does: Rack Pro's stepBlock checks state only its own launcher sets
		engine::Module::ProcessArgs args;
		args.sampleRate = sr;
		args.sampleTime = 1.f / sr;
		for (int i = 0; i < 32; i++) {
			args.frame = frame++;
			m->process(args);
		}
		for (size_t o = 0; o < m->outputs.size(); o++) {
			engine::Output &out = m->outputs[o];
			for (int c = 0; c < out.getChannels(); c++) {
				const float v = out.getVoltage(c);
				if (!std::isfinite(v) || std::fabs(v) > 50.f) {
					FAIL("%s: output %d channel %d = %g after %d frames", model, (int)o, c, v, (b + 1) * 32);
					return false;
				}
			}
		}
	}
	return true;
}

static engine::Module *fresh(plugin::Model *model) {
	engine::Module *m = model->createModule();
	APP->engine->addModule(m);
	// every output patched, as a cable does (modules skip their work for an unpatched output); the module sets the count
	for (engine::Output &out : m->outputs)
		out.channels = 1;
	return m;
}

static void drop(engine::Module *m) {
	APP->engine->removeModule(m);
	delete m;
}

static json_t *dataOf(engine::Module *m) {
	json_t *j = m->toJson();
	json_t *d = json_object_get(j, "data");
	json_t *copy = d ? json_deep_copy(d) : json_object();
	json_decref(j);
	return copy;
}

static std::string dump(json_t *j) {
	char *s = json_dumps(j, JSON_SORT_KEYS | JSON_COMPACT | JSON_REAL_PRECISION(9));
	std::string out = s ? s : "";
	free(s);
	return out;
}

// Params a patch does not keep on purpose, set by dataFromJson whatever was saved: {model, {param id, value}}
static const std::map<std::string, std::map<int, float>> kNotKept = {
	{"REI", {{2, 0.f}}},        // FREEZE opens off: a held empty reverb would be silence (REI::dataFromJson)
	{"REGISTRE", {{2, 0.f}}},   // HOLD opens off: the patch's edits are committed on load (REGISTRE::dataFromJson)
};

// toJson -> text -> fromJson into a new module -> toJson: params and data the same
static void roundTrip(plugin::Model *model) {
	const char *slug = model->slug.c_str();
	step(slug, "round trip");
	engine::Module *a = fresh(model);
	// knobs and switches at random; not the buttons (Rack doesn't randomize them either): a momentary button left
	// pressed is not a state a patch keeps
	for (size_t p = 0; p < a->params.size(); p++) {
		engine::ParamQuantity *q = a->paramQuantities[p];
		if (q && q->randomizeEnabled) {
			float v = uni(q->getMinValue(), q->getMaxValue());
			a->params[p].setValue(q->snapEnabled ? std::round(v) : v);
		}
	}
	run(a, slug, 64, false);
	auto notKept = kNotKept.find(model->slug);
	if (notKept != kNotKept.end())
		for (auto &p : notKept->second)
			a->params[p.first].setValue(p.second);
	json_t *ja = a->toJson();
	std::string text = dump(ja);
	json_t *loaded = json_loads(text.c_str(), 0, NULL);
	engine::Module *b = fresh(model);
	b->fromJson(loaded);
	json_t *jb = b->toJson();
	for (const char *key : {"params", "data"}) {
		std::string sa = dump(json_object_get(ja, key)), sb = dump(json_object_get(jb, key));
		if (sa != sb) {
			FAIL("%s: %s differ after a round trip", slug, key);
			// the first keys that differ
			json_t *da = json_object_get(ja, key), *db = json_object_get(jb, key);
			if (json_is_object(da) && json_is_object(db)) {
				const char *k;
				json_t *v;
				int shown = 0;
				json_object_foreach(da, k, v) {
					json_t *w = json_object_get(db, k);
					if (!w || dump(v) != dump(w)) {
						std::printf("     %s: %.120s  ->  %.120s\n", k, dump(v).c_str(), w ? dump(w).c_str() : "(missing)");
						if (++shown == 6)
							break;
					}
				}
			}
			else if (json_is_array(da) && json_is_array(db)) {
				// params: the ids that changed, with their names
				int shown = 0;
				for (size_t i = 0; i < std::min(json_array_size(da), json_array_size(db)) && shown < 8; i++) {
					json_t *x = json_array_get(da, i), *y = json_array_get(db, i);
					if (dump(x) != dump(y)) {
						const int id = json_integer_value(json_object_get(x, "id"));
						engine::ParamQuantity *q = id >= 0 && id < (int)b->paramQuantities.size() ? b->paramQuantities[id] : NULL;
						std::printf("     param %d (%s): %g -> %g\n", id, q ? q->getLabel().c_str() : "?",
							json_number_value(json_object_get(x, "value")), json_number_value(json_object_get(y, "value")));
						shown++;
					}
				}
			}
			else
				std::printf("     %.200s\n     %.200s\n", sa.c_str(), sb.c_str());
		}
	}
	json_decref(ja);
	json_decref(jb);
	json_decref(loaded);
	drop(a);
	drop(b);
}

// one damaged copy of the module's data loaded into a fresh module, which then runs
static void loadDamaged(plugin::Model *model, json_t *data, const std::string &what) {
	const char *slug = model->slug.c_str();
	step(slug, what.c_str());
	engine::Module *m = fresh(model);
	json_t *j = m->toJson();
	json_object_set(j, "data", data);
	m->fromJson(j);
	json_decref(j);
	run(m, slug, 64, true);
	drop(m);
}

static void damaged(plugin::Model *model) {
	engine::Module *m = fresh(model);
	run(m, model->slug.c_str(), 16, false);
	json_t *data = dataOf(m);
	drop(m);

	json_t *empty = json_object();
	loadDamaged(model, empty, "data {}");
	json_decref(empty);

	const char *key;
	json_t *value;
	std::vector<std::string> keys;
	json_object_foreach(data, key, value)
		keys.push_back(key);
	for (const std::string &k : keys) {
		std::vector<std::pair<std::string, json_t *>> variants = {
			{"missing", NULL},
			{"null", json_null()},
			{"a string", json_string("garbage")},
			{"an empty string", json_string("")},
			{"2^31-1", json_integer(2147483647)},
			{"-2^31", json_integer(-2147483647 - 1)},
			{"1e30", json_real(1e30)},
			{"-1.5", json_real(-1.5)},
			{"true", json_true()},
			{"[]", json_array()},
			{"{}", json_object()},
			{"[1, 2, 3]", json_pack("[iii]", 1, 2, 3)},
		};
		for (auto &v : variants) {
			json_t *d = json_deep_copy(data);
			if (v.second)
				json_object_set_new(d, k.c_str(), v.second);
			else
				json_object_del(d, k.c_str());
			loadDamaged(model, d, "data." + k + " " + v.first);
			json_decref(d);
		}
		// an array or object: each of its items damaged in turn (the first 3)
		json_t *inner = json_object_get(data, k.c_str());
		if (json_is_array(inner) && json_array_size(inner) > 0) {
			for (size_t i = 0; i < std::min<size_t>(3, json_array_size(inner)); i++)
				for (int t = 0; t < 3; t++) {
					json_t *d = json_deep_copy(data);
					json_t *arr = json_object_get(d, k.c_str());
					json_array_set_new(arr, i, t == 0 ? json_string("garbage") : t == 1 ? json_integer(-2147483647 - 1) : json_null());
					loadDamaged(model, d, "data." + k + "[" + std::to_string(i) + "] damaged");
					json_decref(d);
				}
			json_t *d = json_deep_copy(data);
			json_array_clear(json_object_get(d, k.c_str()));
			loadDamaged(model, d, "data." + k + " emptied");
			json_decref(d);
		}
		if (json_is_object(inner)) {
			const char *ik;
			json_t *iv;
			std::vector<std::string> ikeys;
			json_object_foreach(inner, ik, iv)
				ikeys.push_back(ik);
			for (size_t i = 0; i < std::min<size_t>(4, ikeys.size()); i++) {
				json_t *d = json_deep_copy(data);
				json_object_set_new(json_object_get(d, k.c_str()), ikeys[i].c_str(), json_string("garbage"));
				loadDamaged(model, d, "data." + k + "." + ikeys[i] + " a string");
				json_decref(d);
			}
		}
	}
	// every key a string at once
	json_t *d = json_deep_copy(data);
	for (const std::string &k : keys)
		json_object_set_new(d, k.c_str(), json_string("garbage"));
	loadDamaged(model, d, "every key a string");
	json_decref(d);
	json_decref(data);
}

int main(int argc, char **argv) {
	if (argc < 2) {
		std::printf("usage: module_harness <user folder> [model slug ...]\n");
		return 2;
	}
	// HARNESS_SEED=n changes the knobs and signals tried (the same seed gives the same run)
	if (const char *seed = std::getenv("HARNESS_SEED"))
		rng.seed(std::strtoul(seed, NULL, 10));
	settings::headless = true;
	system::init();   // the clock stepBlock reads
	asset::systemDir = "C:/Program Files/VCV/Rack2Pro";
	asset::userDir = argv[1];
	logger::logPath = asset::user("log.txt");
	logger::init();
	random::init();
	settings::init();
	plugin::init();
	contextSet(new Context);
	APP->engine = new engine::Engine;
	APP->history = new history::State;
	APP->patch = new patch::Manager;
	APP->engine->setSampleRate(48000.f);

	plugin::Plugin *bidoo = plugin::getPlugin("Bidoo");
	if (!bidoo) {
		std::printf("Bidoo not loaded from %s\n", argv[1]);
		return 2;
	}
	std::set<std::string> only;
	for (int i = 2; i < argc; i++)
		only.insert(argv[i]);
	int models = 0;
	for (plugin::Model *model : bidoo->models) {
		if (!only.empty() && !only.count(model->slug))
			continue;
		models++;
		const char *slug = model->slug.c_str();
		step(slug, "run");
		engine::Module *m = fresh(model);
		run(m, slug, 1500, true);   // 1 s at 48 kHz, the knobs moved every 64 blocks
		// the engine's rate changed while it runs (onSampleRateChange), then back
		for (float rate : {96000.f, 44100.f, 48000.f}) {
			step(slug, rate == 96000.f ? "run at 96 kHz" : rate == 44100.f ? "run at 44.1 kHz" : "run at 48 kHz again");
			APP->engine->setSampleRate(rate);
			run(m, slug, 400, true);
		}
		drop(m);
		roundTrip(model);
		damaged(model);
	}
	std::printf("%d modules, %d failures\n", models, failures);
	std::fflush(stdout);
	// left to the OS: the modules' worker threads and Rack's globals
	std::_Exit(failures ? 1 : 0);
}

#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "osdialog.h"
#include <vector>
#include "cmath"
#include <iomanip>
#include <sstream>
#include <algorithm>
#include "window.hpp"
#include <mutex>
#include "dep/dr_wav/dr_wav.h"

using namespace std;

struct CANARD : Module {
	enum ParamIds {
		RECORD_PARAM,
		SAMPLE_START_PARAM,
		LOOP_LENGTH_PARAM,
		READ_MODE_PARAM,
		SPEED_PARAM,
		FADE_PARAM,
		MODE_PARAM,
		SLICE_PARAM,
		CLEAR_PARAM,
		THRESHOLD_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		INL_INPUT,
		INR_INPUT,
		TRIG_INPUT,
		GATE_INPUT,
		SAMPLE_START_INPUT,
		LOOP_LENGTH_INPUT,
		READ_MODE_INPUT,
		SPEED_INPUT,
		RECORD_INPUT,
		FADE_INPUT,
		SLICE_INPUT,
		CLEAR_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTL_OUTPUT,
		OUTR_OUTPUT,
		EOC_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		REC_LIGHT,
		NUM_LIGHTS
	};

	bool play = false;
	bool record = false;
	bool save = false;
	unsigned int channels = 2;
  unsigned int sampleRate = 0;
  drwav_uint64 totalSampleCount = 0;
	vector<vector<float>> playBuffer, recordBuffer;
	float samplePos = 0.0f, sampleStart = 0.0f, loopLength = 0.0f, fadeLenght = 0.0f, fadeCoeff = 1.0f, speedFactor = 1.0f;
	size_t prevPlayedSlice = 0;
	size_t playedSlice = 0;
	bool changedSlice = false;
	int readMode = 0; // 0 formward, 1 backward, 2 repeat
	float speed;
	std::vector<int> slices;
	int selected = -1;
	bool deleteFlag = false;
	int addSliceMarker = -1;
	bool addSliceMarkerFlag = false;
	int deleteSliceMarker = -1;
	bool deleteSliceMarkerFlag = false;
	size_t index = 0;
	float prevGateState = 0.0f;
	float prevTrigState = 0.0f;
	std::string lastPath;
	std::string waveFileName;
	std::string waveExtension;
	bool loading = false;
	dsp::SchmittTrigger trigTrigger;
	dsp::SchmittTrigger recordTrigger;
	dsp::SchmittTrigger clearTrigger;
	dsp::PulseGenerator eocPulse;
	std::mutex mylock;
	bool newStop = false;

	CANARD() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(RECORD_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(SAMPLE_START_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(LOOP_LENGTH_PARAM, 0.0f, 10.0f, 10.0f);
		configParam(READ_MODE_PARAM, 0.0f, 2.0f, 0.0f);
		configParam(SPEED_PARAM, -4.0f, 4.0f, 1.0f);
		configParam(FADE_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(SLICE_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(CLEAR_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(THRESHOLD_PARAM, 0.01f, 10.0f, 1.0f);
		configParam(MODE_PARAM, 0.0f, 1.0f, 0.0f);

		playBuffer.resize(2);
		playBuffer[0].resize(0);
		playBuffer[1].resize(0);
		recordBuffer.resize(2);
		recordBuffer[0].resize(0);
		recordBuffer[1].resize(0);
	}

	void process(const ProcessArgs &args) override;

	void calcLoop();
	void initPos();
	void loadSample(std::string path);
	void saveSample();

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		// lastPath
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
		json_t *slicesJ = json_array();
		for (size_t i = 0; i<slices.size() ; i++) {
			json_t *sliceJ = json_integer(slices[i]);
			json_array_append_new(slicesJ, sliceJ);
		}
		json_object_set_new(rootJ, "slices", slicesJ);

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (lastPathJ) {
			lastPath = json_string_value(lastPathJ);
			waveFileName = rack::string::filename(lastPath);
			waveExtension = rack::string::filenameBase(lastPath);
			loadSample(lastPath);
			if (totalSampleCount>0) {
				json_t *slicesJ = json_object_get(rootJ, "slices");
				if (slicesJ) {
					size_t i;
					json_t *sliceJ;
					json_array_foreach(slicesJ, i, sliceJ) {
							if (i != 0)
								slices.push_back(json_integer_value(sliceJ));
					}
				}
			}
		}
	}
};

void CANARD::loadSample(std::string path) {
	loading = true;
	unsigned int c;
  unsigned int sr;
  drwav_uint64 sc;
	float* pSampleData;
  pSampleData = drwav_open_and_read_file_f32(path.c_str(), &c, &sr, &sc);
  if (pSampleData != NULL)  {
		lastPath = path;
		waveFileName = rack::string::filename(path);
		waveExtension = rack::string::filenameBase(path);
		channels = c;
		sampleRate = sr;
		slices.clear();
		slices.push_back(0);
		playBuffer[0].clear();
		playBuffer[1].clear();
		for (unsigned int i=0; i < sc; i = i + c) {
			playBuffer[0].push_back(pSampleData[i]);
			if (channels == 2)
				playBuffer[1].push_back((float)pSampleData[i+1]);
			else
				playBuffer[1].push_back((float)pSampleData[i]);
		}
		totalSampleCount = playBuffer[0].size();
		drwav_free(pSampleData);
	}
	loading = false;
}

void CANARD::saveSample() {
	drwav_data_format format;
	format.container = drwav_container_riff;
	format.format = DR_WAVE_FORMAT_PCM;
	format.channels = 2;
	format.sampleRate = sampleRate;
	format.bitsPerSample = 32;

	int *pSamples = (int*)calloc(2*totalSampleCount,sizeof(int));
	memset(pSamples, 0, 2*totalSampleCount*sizeof(int));
	for (unsigned int i = 0; i < totalSampleCount; i++) {
		*(pSamples+2*i)= floor(playBuffer[0][i]*2147483647);
		*(pSamples+2*i+1)= floor(playBuffer[1][i]*2147483647);
	}

	drwav* pWav = drwav_open_file_write(lastPath.c_str(), &format);
	drwav_write(pWav, 2*totalSampleCount, pSamples);
	drwav_close(pWav);
	free(pSamples);
}

void CANARD::calcLoop() {
	prevPlayedSlice = index;
	index = 0;
	int sliceStart = 0;;
	int sliceEnd = totalSampleCount > 0 ? totalSampleCount - 1 : 0;
	if ((params[MODE_PARAM].value == 1) && (slices.size()>0))
	{
		index = round(clamp(params[SLICE_PARAM].value + inputs[SLICE_INPUT].value, 0.0f,10.0f)*(slices.size()-1)/10);
		sliceStart = slices[index];
		sliceEnd = (index < (slices.size() - 1)) ? (slices[index+1] - 1) : (totalSampleCount - 1);
	}

	if (totalSampleCount > 0) {
		sampleStart = rescale(clamp(inputs[SAMPLE_START_INPUT].value + params[SAMPLE_START_PARAM].value, 0.0f, 10.0f), 0.0f, 10.0f, sliceStart, sliceEnd);
		loopLength = clamp(rescale(clamp(inputs[LOOP_LENGTH_INPUT].value + params[LOOP_LENGTH_PARAM].value, 0.0f, 10.0f), 0.0f, 10.0f, 0.0f, sliceEnd - sliceStart + 1),1.0f,sliceEnd-sampleStart+1);
		fadeLenght = rescale(clamp(inputs[FADE_INPUT].value + params[FADE_PARAM].value, 0.0f, 10.0f), 0.0f, 10.0f,0.0f, floor(loopLength/2));
	}
	else {
		loopLength = 0;
		sampleStart = 0;
		fadeLenght = 0;
	}
	playedSlice = index;
}

void CANARD::initPos() {
	if ((inputs[SPEED_INPUT].value + params[SPEED_PARAM].value)>=0)
	{
		samplePos = sampleStart;
	}
	else
	{
		samplePos = sampleStart + loopLength;
	}
	speedFactor = 1.0f;
}

void CANARD::process(const ProcessArgs &args) {
	sampleRate = args.sampleRate;
	if (save) {
		saveSample();
		save = false;
	}
	if (!loading) {
		if (clearTrigger.process(inputs[CLEAR_INPUT].value + params[CLEAR_PARAM].value))
		{
			mylock.lock();
			playBuffer[0].clear();
			playBuffer[1].clear();
			totalSampleCount = 0;
			slices.clear();
			mylock.unlock();
			lastPath = "";
			waveFileName = "";
			waveExtension = "";
		}

		if ((selected>=0) && (deleteFlag)) {
			int nbSample=0;
			if ((size_t)selected<(slices.size()-1)) {
				nbSample = slices[selected + 1] - slices[selected] - 1;
				mylock.lock();
				playBuffer[0].erase(playBuffer[0].begin() + slices[selected], playBuffer[0].begin() + slices[selected + 1]-1);
				playBuffer[1].erase(playBuffer[1].begin() + slices[selected], playBuffer[1].begin() + slices[selected + 1]-1);
				mylock.unlock();
			}
			else {
				nbSample = totalSampleCount - slices[selected];
				mylock.lock();
				playBuffer[0].erase(playBuffer[0].begin() + slices[selected], playBuffer[0].end());
				playBuffer[1].erase(playBuffer[1].begin() + slices[selected], playBuffer[1].end());
				mylock.unlock();
			}
			slices.erase(slices.begin()+selected);
			totalSampleCount = playBuffer[0].size();
			for (size_t i = selected; i < slices.size(); i++)
			{
				slices[i] = slices[i]-nbSample;
			}
			selected = -1;
			deleteFlag = false;
			calcLoop();
		}

		if ((addSliceMarker>=0) && (addSliceMarkerFlag)) {
			if (std::find(slices.begin(), slices.end(), addSliceMarker) != slices.end()) {
				addSliceMarker = -1;
				addSliceMarkerFlag = false;
			}
			else {
				auto it = std::upper_bound(slices.begin(), slices.end(), addSliceMarker);
				mylock.lock();
				slices.insert(it, addSliceMarker);
				mylock.unlock();
				addSliceMarker = -1;
				addSliceMarkerFlag = false;
				calcLoop();
			}
		}

		if ((deleteSliceMarker>=0) && (deleteSliceMarkerFlag)) {
			if (std::find(slices.begin(), slices.end(), deleteSliceMarker) != slices.end()) {
				mylock.lock();
				slices.erase(std::find(slices.begin(), slices.end(), deleteSliceMarker));
				mylock.unlock();
				deleteSliceMarker = -1;
				deleteSliceMarkerFlag = false;
				calcLoop();
			}
		}

		if (recordTrigger.process(inputs[RECORD_INPUT].value + params[RECORD_PARAM].value))
		{
			if(record) {
				if (floor(params[MODE_PARAM].value) == 0) {
					mylock.lock();
					slices.clear();
					slices.push_back(0);
					playBuffer.resize(2);
					playBuffer[0].resize((int)recordBuffer[0].size());
					playBuffer[1].resize((int)recordBuffer[0].size());
					for (int i = 0; i < (int)recordBuffer[0].size(); i++) {
						playBuffer[0][i] = recordBuffer[0][i];
						playBuffer[1][i] = recordBuffer[1][i];
					}
					totalSampleCount = playBuffer[0].size();
					mylock.unlock();
					lastPath = "";
					waveFileName = "";
					waveExtension = "";
				}
				else {
					mylock.lock();
					slices.push_back(totalSampleCount > 0 ? (totalSampleCount-1) : 0);
					playBuffer[0].insert(playBuffer[0].end(), recordBuffer[0].begin(), recordBuffer[0].end());
					playBuffer[1].insert(playBuffer[1].end(), recordBuffer[1].begin(), recordBuffer[1].end());
					totalSampleCount = playBuffer[0].size();
					mylock.unlock();
				}
				mylock.lock();
				recordBuffer[0].resize(0);
				recordBuffer[1].resize(0);
				mylock.unlock();
				lights[REC_LIGHT].value = 0.0f;
			}
			record = !record;
		}

		if (record) {
			lights[REC_LIGHT].value = 10.0f;
			mylock.lock();
			recordBuffer[0].push_back(inputs[INL_INPUT].value/10);
			recordBuffer[1].push_back(inputs[INR_INPUT].value/10);
			mylock.unlock();
		}

		int trigMode = inputs[TRIG_INPUT].active ? 1 : (inputs[GATE_INPUT].active ? 2 : 0);
		int readMode = round(clamp(inputs[READ_MODE_INPUT].value + params[READ_MODE_PARAM].value,0.0f,2.0f));
		speed = inputs[SPEED_INPUT].value + params[SPEED_PARAM].value;
		calcLoop();

		if (trigMode == 1) {
			if (trigTrigger.process(inputs[TRIG_INPUT].value) && (prevTrigState == 0.0f))
			{
				initPos();
				play = true;
			}
			else {
				if ((readMode == 0) && (speed>=0) && (samplePos == (sampleStart+loopLength))) {
					play = false;
					if (newStop) {
						eocPulse.trigger(10 / args.sampleRate);
						newStop = false;
					}
				}
				else if ((readMode == 0) && (speed<0) && (samplePos == sampleStart)) {
					play = false;
					if (newStop) {
						eocPulse.trigger(10 / args.sampleRate);
						newStop = false;
					}
				}
				else if ((readMode == 1) && (speed>=0) && (samplePos == (sampleStart+loopLength))) {
					initPos();
				}
				else if ((readMode == 1) && (speed<0) && (samplePos == sampleStart)) {
					initPos();
				}
				else if ((readMode == 2) && ((samplePos == (sampleStart)) || (samplePos == (sampleStart+loopLength)))) {
					speedFactor = -1 * speedFactor;
					samplePos = samplePos + speedFactor * speed;
				}
				else {
					samplePos = samplePos + speedFactor * speed;
				}
			}
			samplePos = clamp(samplePos,sampleStart,sampleStart+loopLength);
		}
		else if (trigMode == 2)
		{
			if (inputs[GATE_INPUT].value>0)
			{
				if (prevGateState == 0.0f) {
					initPos();
					play = true;
				}
				else {
					if ((readMode == 0) && (speed>=0) && (samplePos == (sampleStart+loopLength))) {
						play = false;
						if (newStop) {
							eocPulse.trigger(10 / args.sampleRate);
							newStop = false;
						}
					}
					else if ((readMode == 0) && (speed<0) && (samplePos == sampleStart)) {
						play = false;
						if (newStop) {
							eocPulse.trigger(10 / args.sampleRate);
							newStop = false;
						}
					}
					else if ((readMode == 1) && (speed>=0) && (samplePos == (sampleStart+loopLength))) {
						initPos();
					}
					else if ((readMode == 1) && (speed<0) && (samplePos == sampleStart)) {
						initPos();
					}
					else if ((readMode == 2) && ((samplePos == (sampleStart)) || (samplePos == (sampleStart+loopLength)))) {
						speedFactor = -1 * speedFactor;
						samplePos = samplePos + speedFactor * speed;
					}
					else {
						samplePos = samplePos + speedFactor * speed;
					}
				}
				samplePos = clamp(samplePos,sampleStart,sampleStart+loopLength);
			}
			else {
				play = false;
			}
		}
		prevGateState = inputs[GATE_INPUT].value;
		prevTrigState = inputs[TRIG_INPUT].value;

		if (play) {
			newStop = true;
			if (samplePos<totalSampleCount) {
				if (fadeLenght>1000) {
					if ((samplePos-sampleStart)<fadeLenght)
						fadeCoeff = rescale(samplePos-sampleStart,0.0f,fadeLenght,0.0f,1.0f);
					else if ((sampleStart+loopLength-samplePos)<fadeLenght)
						fadeCoeff = rescale(sampleStart+loopLength-samplePos,fadeLenght,0.0f,1.0f,0.0f);
					else
						fadeCoeff = 1.0f;
				}
				else
					fadeCoeff = 1.0f;

				outputs[OUTL_OUTPUT].value = playBuffer[0][floor(samplePos)]*fadeCoeff*5;
				outputs[OUTR_OUTPUT].value = playBuffer[1][floor(samplePos)]*fadeCoeff*5;
			}
		}
		else {
			outputs[OUTL_OUTPUT].value = 0.0f;
			outputs[OUTR_OUTPUT].value = 0.0f;
		}
	}
	else {
		outputs[OUTL_OUTPUT].value = 0.0f;
		outputs[OUTR_OUTPUT].value = 0.0f;
	}
	outputs[EOC_OUTPUT].value = eocPulse.process(1 / args.sampleRate) ? 10.0f : 0.0f;
}

struct CANARDDisplay : OpaqueWidget {
	CANARD *module;
	shared_ptr<Font> font;
	const float width = 175.0f;
	const float height = 50.0f;
	float zoomWidth = 175.0f;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refX = 0.0f;

	CANARDDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void onButton(const event::Button &e) override {
		if (module->slices.size()>0) {
			refX = e.pos.x;
			refIdx = ((e.pos.x - zoomLeftAnchor)/zoomWidth)*(float)module->totalSampleCount;
			module->addSliceMarker = refIdx;
			auto lower = std::lower_bound(module->slices.begin(), module->slices.end(), refIdx);
			module->selected = distance(module->slices.begin(),lower-1);
			module->deleteSliceMarker = *(lower-1);
		}
		if (e.button == 0)
			OpaqueWidget::onButton(e);
		else {
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) {
				ModuleWidget *pWidget = dynamic_cast<ModuleWidget*>(this->parent);
				pWidget->createContextMenu();
				e.consume(this);
			}
		}
	}

	void onDragStart(const event::DragStart &e) override {
		appGet()->window->cursorLock();
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(const event::DragMove &e) override {
		float zoom = 1.0f;
		if (e.mouseDelta.y > 0.0f) {
			zoom = 1.0f/(((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 2.0f : 1.1f);
		}
		else if (e.mouseDelta.y < 0.0f) {
			zoom = ((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 2.0f : 1.1f;
		}
		zoomWidth = clamp(zoomWidth*zoom,width,zoomWidth*((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT) ? 2.0f : 1.1f));
		zoomLeftAnchor = clamp(refX - (refX - zoomLeftAnchor)*zoom + e.mouseDelta.x, width - zoomWidth,0.0f);
		OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const event::DragEnd &e) override {
		appGet()->window->cursorUnlock();
		OpaqueWidget::onDragEnd(e);
	}

	void draw(const DrawArgs &args) override {
		if (module) {
			module->mylock.lock();
			std::vector<float> vL(module->playBuffer[0]);
			std::vector<float> vR(module->playBuffer[1]);
			std::vector<int> s(module->slices);
			module->mylock.unlock();
			size_t nbSample = vL.size();

			// Draw play line
			if (!module->loading) {
				nvgStrokeColor(args.vg, LIGHTBLUE_BIDOO);
				{
					nvgBeginPath(args.vg);
					nvgStrokeWidth(args.vg, 2);
					if (nbSample>0) {
						nvgMoveTo(args.vg, module->samplePos * zoomWidth / nbSample + zoomLeftAnchor, 0);
						nvgLineTo(args.vg, module->samplePos * zoomWidth / nbSample + zoomLeftAnchor, 2*height+10);
					}
					else {
						nvgMoveTo(args.vg, 0, 0);
						nvgLineTo(args.vg, 0, 2*height+10);
					}
					nvgClosePath(args.vg);
				}
				nvgStroke(args.vg);
			}

			// Draw ref line
			nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
			nvgStrokeWidth(args.vg, 1);
			{
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0, height/2);
				nvgLineTo(args.vg, width, height/2);
				nvgClosePath(args.vg);
			}
			nvgStroke(args.vg);

			nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
			nvgStrokeWidth(args.vg, 1);
			{
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0, 3*height*0.5f+10);
				nvgLineTo(args.vg, width, 3*height*0.5f+10);
				nvgClosePath(args.vg);
			}
			nvgStroke(args.vg);

			if ((!module->loading) && (vL.size()>0)) {
				// Draw loop
				nvgFillColor(args.vg, nvgRGBA(255, 255, 255, 60));
				nvgStrokeWidth(args.vg, 1);
				{
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, (module->sampleStart + module->fadeLenght) * zoomWidth / nbSample + zoomLeftAnchor, 0);
					nvgLineTo(args.vg, module->sampleStart * zoomWidth / vL.size() + zoomLeftAnchor, 2*height+10);
					nvgLineTo(args.vg, (module->sampleStart + module->loopLength) * zoomWidth / nbSample + zoomLeftAnchor, 2*height+10);
					nvgLineTo(args.vg, (module->sampleStart + module->loopLength - module->fadeLenght) * zoomWidth / nbSample + zoomLeftAnchor, 0);
					nvgLineTo(args.vg, (module->sampleStart + module->fadeLenght) * zoomWidth / nbSample + zoomLeftAnchor, 0);
					nvgClosePath(args.vg);
				}
				nvgFill(args.vg);

				//draw selected
				if ((module->selected >= 0) && ((size_t)module->selected < s.size())) {
					nvgStrokeColor(args.vg, RED_BIDOO);
					{
						nvgScissor(args.vg, 0, 0, width, 2*height+10);
						nvgBeginPath(args.vg);
						nvgStrokeWidth(args.vg, 4);
						nvgMoveTo(args.vg, (s[module->selected] * zoomWidth / nbSample) + zoomLeftAnchor , 2*height+9);
						if ((size_t)module->selected < (s.size()-1))
							nvgLineTo(args.vg, (s[module->selected+1] * zoomWidth / nbSample) + zoomLeftAnchor , 2*height+9);
						else
							nvgLineTo(args.vg, zoomWidth + zoomLeftAnchor, 2*height+9);
						nvgClosePath(args.vg);
					}
					nvgStroke(args.vg);
					nvgResetScissor(args.vg);
				}

				// Draw waveform

				if (nbSample>0) {
				nvgStrokeColor(args.vg, PINK_BIDOO);
				nvgSave(args.vg);
				Rect b = Rect(Vec(zoomLeftAnchor, 0), Vec(zoomWidth, height));
				nvgScissor(args.vg, 0, b.pos.y, width, height);
				float invNbSample = 1.0f / nbSample;
				nvgBeginPath(args.vg);
				for (size_t i = 0; i < vL.size(); i++) {
					float x, y;
					x = (float)i * invNbSample ;
					y = vL[i] * 0.5f + 0.5f;
					Vec p;
					p.x = b.pos.x + b.size.x * x;
					p.y = b.pos.y + b.size.y * (1.0f - y);
					if (i == 0) {
						nvgMoveTo(args.vg, p.x, p.y);
					}
					else {
						nvgLineTo(args.vg, p.x, p.y);
					}
				}
				//nvgClosePath(vg);
				nvgLineCap(args.vg, NVG_MITER);
				nvgStrokeWidth(args.vg, 1);
				nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
				nvgStroke(args.vg);

				b = Rect(Vec(zoomLeftAnchor, height+10), Vec(zoomWidth, height));
				nvgScissor(args.vg, 0, b.pos.y, width, height);
				nvgBeginPath(args.vg);
				for (size_t i = 0; i < vR.size(); i++) {
					float x, y;
					x = (float)i * invNbSample;
					y = vR[i] * 0.5f + 0.5f;
					Vec p;
					p.x = b.pos.x + b.size.x * x;
					p.y = b.pos.y + b.size.y * (1.0f - y);
					if (i == 0)
						nvgMoveTo(args.vg, p.x, p.y);
					else {
						nvgLineTo(args.vg, p.x, p.y);
					}
				}
				//nvgClosePath(args.vg);
				nvgLineCap(args.vg, NVG_MITER);
				nvgStrokeWidth(args.vg, 1);
				nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
				nvgStroke(args.vg);
				nvgResetScissor(args.vg);
			}

				//draw slices

				if (floor(module->params[CANARD::MODE_PARAM].value) == 1) {
					nvgScissor(args.vg, 0, 0, width, 2*height+10);
					for (size_t i = 0; i < s.size(); i++) {
						if (s[i] != module->deleteSliceMarker) {
							nvgStrokeColor(args.vg, YELLOW_BIDOO);
						}
						else {
							nvgStrokeColor(args.vg, RED_BIDOO);
						}
						nvgStrokeWidth(args.vg, 1);
						{
							nvgBeginPath(args.vg);
							nvgMoveTo(args.vg, s[i] * zoomWidth / nbSample + zoomLeftAnchor , 0);
							nvgLineTo(args.vg, s[i] * zoomWidth / nbSample + zoomLeftAnchor , 2*height+10);
							nvgClosePath(args.vg);
						}
						nvgStroke(args.vg);
					}
					nvgResetScissor(args.vg);
				}

				nvgRestore(args.vg);
			}
		}
	}
};

struct CANARDWidget : ModuleWidget {

	CANARDWidget(CANARD *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CANARD.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		{
			CANARDDisplay *display = new CANARDDisplay();
			display->module = module;
			display->box.pos = Vec(10, 35);
			display->box.size = Vec(175, 110);
			addChild(display);
		}

		static const float portX0[5] = {16, 53, 90, 126, 161};

		addChild(createLight<SmallLight<RedLight>>(Vec(portX0[0]-10, 167), module, CANARD::REC_LIGHT));
		addParam(createParam<BlueCKD6>(Vec(portX0[0]-6, 170), module, CANARD::RECORD_PARAM));

		addParam(createParam<BidooBlueKnob>(Vec(portX0[2]-7, 170), module, CANARD::SAMPLE_START_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[3]-7, 170), module, CANARD::LOOP_LENGTH_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[4]-7, 170), module, CANARD::READ_MODE_PARAM));

		addInput(createInput<PJ301MPort>(Vec(portX0[0]-4, 202), module, CANARD::RECORD_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1]-4, 202), module, CANARD::TRIG_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1]-4, 172), module, CANARD::GATE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[2]-4, 202), module, CANARD::SAMPLE_START_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[3]-4, 202), module, CANARD::LOOP_LENGTH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[4]-4, 202), module, CANARD::READ_MODE_INPUT));

		addParam(createParam<BidooBlueKnob>(Vec(portX0[0]-7, 245), module, CANARD::SPEED_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[1]-7, 245), module, CANARD::FADE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[2]-7, 245), module, CANARD::SLICE_PARAM));
		addParam(createParam<BlueCKD6>(Vec(portX0[3]-6, 245), module, CANARD::CLEAR_PARAM));
		addOutput(createOutput<PJ301MPort>(Vec(portX0[4]-4, 247), module, CANARD::EOC_OUTPUT));

		addInput(createInput<PJ301MPort>(Vec(portX0[0]-4, 277), module, CANARD::SPEED_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1]-4, 277), module, CANARD::FADE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[2]-4, 277), module, CANARD::SLICE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[3]-4, 277), module, CANARD::CLEAR_INPUT));
		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[4]-1, 280), module, CANARD::THRESHOLD_PARAM));

		addParam(createParam<CKSS>(Vec(90, 325), module, CANARD::MODE_PARAM));

		addInput(createInput<TinyPJ301MPort>(Vec(19, 331), module, CANARD::INL_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(19+24, 331), module, CANARD::INR_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(138, 331), module, CANARD::OUTL_OUTPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(138+24, 331), module, CANARD::OUTR_OUTPUT));
	}

	struct CANARDDeleteSlice : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			module->deleteFlag = true;
		}
	};

	struct CANARDDeleteSliceMarker : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			module->deleteSliceMarkerFlag = true;
		}
	};

	struct CANARDAddSliceMarker : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			module->addSliceMarkerFlag = true;
		}
	};

	struct CANARDTransientDetect : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			module->slices.clear();
			module->slices.push_back(0);
			unsigned int i = 0;
			unsigned int size = 256;
			vector<float>::const_iterator first;
			vector<float>::const_iterator last;
			float prevNrgy = 0.0f;
			while (i+size<module->totalSampleCount) {
				first = module->playBuffer[0].begin() + i;
				last = module->playBuffer[0].begin() + i + size;
				vector<float> newVec(first, last);
				float nrgy = 0.0f;
				float zcRate = 0.0f;
				unsigned int zcIdx = 0;
				bool first = true;
				for (unsigned int k = 0; k < size; k++) {
					nrgy += 100*newVec[k]*newVec[k]/size;
					if (newVec[k]==0.0f) {
						zcRate += 1;
						if (first) {
							zcIdx = k;
							first = false;
						}
					}
				}
				if ((nrgy > module->params[CANARD::THRESHOLD_PARAM].value) && (nrgy > 10*prevNrgy))
					module->slices.push_back(i+zcIdx);
				i+=size;
				prevNrgy = nrgy;
			}
		}
	};

	struct CANARDLoadSample : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			std::string dir = module->lastPath.empty() ? asset::user("") : rack::string::directory(module->lastPath);
			char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
			if (path) {
				module->loadSample(path);
				free(path);
			}
		}
	};

	struct CANARDSaveSample : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			std::string dir = module->lastPath.empty() ? asset::user("") : rack::string::directory(module->lastPath);
			std::string fileName = module->waveFileName.empty() ? "temp.wav" : module->waveFileName;
			char *path = osdialog_file(OSDIALOG_SAVE, dir.c_str(), fileName.c_str(), NULL);
			if (path) {
				module->lastPath = path;
				module->waveFileName = rack::string::filenameBase(path);
				module->waveExtension = rack::string::filenameExtension(path);
				if (!module->save) module->save = true;
				free(path);
			}
		}
	};


	void appendContextMenu(ui::Menu *menu) override {
		CANARD *module = dynamic_cast<CANARD*>(this->module);
		assert(module);

		menu->addChild(construct<MenuLabel>());
		menu->addChild(construct<CANARDDeleteSlice>(&MenuItem::text, "Delete slice", &CANARDDeleteSlice::module, module));
		menu->addChild(construct<CANARDDeleteSliceMarker>(&MenuItem::text, "Delete slice marker", &CANARDDeleteSliceMarker::module, module));
		menu->addChild(construct<CANARDAddSliceMarker>(&MenuItem::text, "Add slice marker", &CANARDAddSliceMarker::module, module));
		menu->addChild(construct<CANARDTransientDetect>(&MenuItem::text, "Detect transients", &CANARDTransientDetect::module, module));
		menu->addChild(construct<CANARDLoadSample>(&MenuItem::text, "Load sample", &CANARDLoadSample::module, module));
		menu->addChild(construct<CANARDSaveSample>(&MenuItem::text, "Save sample", &CANARDSaveSample::module, module));
	}
};

Model *modelCANARD = createModel<CANARD, CANARDWidget>("cANARd");

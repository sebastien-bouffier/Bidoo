// The Riso look of the Bidoo panels (sOUNAiRe, cOPIA): paper, ink and one colour per module (blue for a
// sound source, orange for an effect). Knobs, buttons and jacks that draw themselves; the panel SVG holds
// the labels.
#pragma once

namespace riso {

static const NVGcolor PAPER = nvgRGB(0xF2, 0xEE, 0xE4);
static const NVGcolor INK = nvgRGB(0x1B, 0x1A, 0x17);
static const NVGcolor TRACK = nvgRGB(0xD3, 0xCC, 0xBC);
static const NVGcolor BLUE = nvgRGB(0x2F, 0x55, 0xD4);
static const NVGcolor ORANGE = nvgRGB(0xF0, 0x62, 0x2F);
static const NVGcolor GRAPHITE = nvgRGB(0x5E, 0x5A, 0x52);

// a knob drawn in ink with a value arc in the module's colour
struct RisoKnob : app::Knob {
	NVGcolor arc = BLUE;
	RisoKnob() {
		box.size = Vec(26, 26);
		minAngle = -0.75f * M_PI;
		maxAngle = 0.75f * M_PI;
	}
	void draw(const DrawArgs &args) override {
		engine::ParamQuantity *pq = getParamQuantity();
		float v = pq ? pq->getScaledValue() : 0.f;
		float cx = box.size.x / 2, cy = box.size.y / 2, r = box.size.x / 2 - 1.5f;
		float a0 = 0.75f * M_PI, a1 = a0 + 1.5f * M_PI;
		float av = a0 + v * 1.5f * M_PI;
		nvgLineCap(args.vg, NVG_ROUND);
		nvgStrokeWidth(args.vg, 2.2f);
		nvgBeginPath(args.vg);
		nvgArc(args.vg, cx, cy, r, a0, a1, NVG_CW);
		nvgStrokeColor(args.vg, TRACK);
		nvgStroke(args.vg);
		if (v > 0.005f) {
			nvgBeginPath(args.vg);
			nvgArc(args.vg, cx, cy, r, a0, av, NVG_CW);
			nvgStrokeColor(args.vg, arc);
			nvgStroke(args.vg);
		}
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, cx, cy, r * 0.74f);
		nvgFillColor(args.vg, INK);
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, cx, cy);
		nvgLineTo(args.vg, cx + std::cos(av) * r * 0.55f, cy + std::sin(av) * r * 0.55f);
		nvgStrokeColor(args.vg, PAPER);
		nvgStrokeWidth(args.vg, 2.f);
		nvgStroke(args.vg);
	}
};

struct RisoSnapKnob : RisoKnob {
	RisoSnapKnob() {
		snap = true;
	}
};

// latching button: ink, lit in the module's colour when on; faded while
// `dimmed` says it has no effect
struct RisoButton : app::Switch {
	std::function<bool()> dimmed;
	NVGcolor lit = BLUE;
	RisoButton() {
		box.size = Vec(20, 20);
	}
	void draw(const DrawArgs &args) override {
		engine::ParamQuantity *pq = getParamQuantity();
		bool on = pq && pq->getValue() > 0.5f;
		if (dimmed && dimmed())
			nvgGlobalAlpha(args.vg, 0.3f);
		float c = box.size.x / 2;
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, c - 0.5f);
		nvgFillColor(args.vg, INK);
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, c * 0.42f);
		nvgFillColor(args.vg, on ? lit : PAPER);
		nvgFill(args.vg);
	}
};

// round jack: on paper for inputs, on the ink block for outputs
struct RisoJack : app::PortWidget {
	bool onInk = false;
	RisoJack() {
		box.size = Vec(24, 24);
	}
	void draw(const DrawArgs &args) override {
		float c = box.size.x / 2, r = c - 1.f;
		// a round nut
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, r);
		nvgFillColor(args.vg, onInk ? INK : PAPER);
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, onInk ? PAPER : INK);
		nvgStrokeWidth(args.vg, 1.2f);
		nvgStroke(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, r * 0.55f);
		nvgFillColor(args.vg, onInk ? nvgRGB(0x57, 0x53, 0x4A) : nvgRGB(0xCF, 0xC8, 0xB8));
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, r * 0.3f);
		nvgFillColor(args.vg, INK);
		nvgFill(args.vg);
	}
};

}  // namespace riso

// The samplers' screens drawn one way (sOUNAiRe, cANARd, OUAIve, eDsaroS, cOPIA): the waveform in hot pink where it
// plays (inside START..END and the loop), dimmer pink elsewhere; the loop shaded in pink; START, END, loop points and
// cuts as yellow bars; play heads as electric blue bars added onto what is under them, so they light the pink up.
// Include after BidooTheme.hpp (BidooComponents.hpp does).
#pragma once

namespace wavestyle {

const NVGcolor kActive = nvgRGB(0xFF, 0x2D, 0x95);           // the waveform where it plays
const NVGcolor kIdle = nvgRGBA(0xA4, 0x03, 0x6F, 0x8C);       // PINK_BIDOO, dimmed: where it does not play
const NVGcolor kLoop = nvgRGBA(0xFF, 0x2D, 0x95, 0x38);       // behind the loop
const NVGcolor kBar = theme::kScreenYellow;                   // START, END, loop points, cuts
const NVGcolor kHead = nvgRGB(0x29, 0x8C, 0xFF);              // play heads

// One column of the waveform: a rectangle from its highest to its lowest value (-1..1), `amp` px either side of
// `mid`. Call between nvgBeginPath and fillWave.
inline void column(NVGcontext *vg, float x, float lo, float hi, float mid, float amp, float w = 1.f) {
	lo = clamp(lo, -1.f, 1.f);
	hi = clamp(hi, -1.f, 1.f);
	nvgRect(vg, x, mid - hi * amp, w, std::max(1.f, (hi - lo) * amp));
}

inline void fill(NVGcontext *vg, bool active) {
	nvgFillColor(vg, active ? kActive : kIdle);
	nvgFill(vg);
}

// The loop (or the part that plays, where a module shows it as a band), behind the waveform
inline void band(NVGcontext *vg, float x0, float x1, float top, float bottom, NVGcolor c = kLoop) {
	if (x1 < x0)
		std::swap(x0, x1);
	nvgBeginPath(vg);
	nvgRect(vg, x0, top, x1 - x0, bottom - top);
	nvgFillColor(vg, c);
	nvgFill(vg);
}

// A yellow bar; flag 1 or -1 adds a small flag at the top, pointing right (a start) or left (an end)
inline void bar(NVGcontext *vg, float x, float top, float bottom, int flag = 0, NVGcolor c = kBar) {
	nvgBeginPath(vg);
	nvgRect(vg, x - 0.5f, top, 1.f, bottom - top);
	nvgFillColor(vg, c);
	nvgFill(vg);
	if (flag) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, x, top);
		nvgLineTo(vg, x + 5.f * flag, top);
		nvgLineTo(vg, x, top + 6.f);
		nvgClosePath(vg);
		nvgFillColor(vg, c);
		nvgFill(vg);
	}
}

// A play head: a 2 px blue bar with a faint glow, added onto what is under it (NVG_LIGHTER), so the pink under it
// turns white-violet. `strength` (0..1) dims it, for a voice that fades. Draw it after the waveform and the bars.
// `c`: another colour (cOPIA's write head is orange), its glow a quarter of it.
inline void head(NVGcontext *vg, float x, float top, float bottom, float strength = 1.f, NVGcolor c = kHead) {
	strength = clamp(strength, 0.f, 1.f);
	auto scaled = [&](NVGcolor c) { return nvgRGBf(c.r * strength, c.g * strength, c.b * strength); };
	nvgSave(vg);
	nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
	nvgBeginPath(vg);
	nvgRect(vg, x - 3.f, top, 2.f, bottom - top);
	nvgRect(vg, x + 1.f, top, 2.f, bottom - top);
	nvgFillColor(vg, scaled(nvgRGBf(c.r * 0.25f, c.g * 0.25f, c.b * 0.25f)));
	nvgFill(vg);
	nvgBeginPath(vg);
	nvgRect(vg, x - 1.f, top, 2.f, bottom - top);
	nvgFillColor(vg, scaled(c));
	nvgFill(vg);
	nvgRestore(vg);
}

}  // namespace wavestyle

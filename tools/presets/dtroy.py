# Factory presets for dTrOY (src/DTROY.cpp, engine in src/MetroSeq.hpp): python tools/presets/dtroy.py writes presets/dTrOY/.
# A preset is a whole sequence: dataToJson holds 16 patterns (the panel edits "selectedPattern"), and every control
# but CLOCK, RUN and RESET lives in the pattern. At each control tick the module writes the panel knobs into the
# selected pattern (savePanel), so the knobs of a preset must be the selected pattern's values: params() reads them
# from pattern 1, which every preset selects (PATTERN 1, selectedPattern 0).
# Pitch: stage knob in volts, 0 V = C4, quantized to KEY/SCALE; notes are given by name. CLOCK: the internal clock's
# pulse rate, 2 * 2^knob Hz (0 = 120 pulses a minute, 2 = sixteenths at 120 BPM); an EXT CLK overrides it.
# The momentary buttons (run, reset, play/count mode, slide, skip, copy, shifts) stay 0: the modes, slides and skips
# are in the pattern data, "running" in the data starts it.
# Gate modes: 0 rest, 1 single (first pulse), 2 multiple (every pulse: ratchets), 3 hold (the whole stage), 4/5 G1/G2.
# Play modes: 0 forward, 1 backward, 2 ping-pong, 3 random, 4 brownian. Count: 0 steps, 1 pulses (STEPS = pulses).
# Scales (quantizer.hpp): 1 Ionian, 2 Dorian, 3 Phrygian, 4 Lydian, 6 Aeolian, 24 Pentatonic, 25 Pentatonic m7, 45 Chromatic.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preset_kit

REST, SINGLE, MULTIPLE, HOLD = 0, 1, 2, 3
NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

def note(name):
	# "Eb2" -> volts from C4
	base = NAMES[name[0]]
	rest = name[1:]
	while rest and rest[0] in "#b":
		base += 1 if rest[0] == "#" else -1
		rest = rest[1:]
	return round((base + 12 * (int(rest) - 4)) / 12.0, 6)

def stage(n, pulses=1, gate=SINGLE, slide=False, skip=False, prob=1.0, rnd=0.0, acc=0.0, accrnd=0.0):
	return {"pitch": note(n), "pulses": pulses, "type": gate, "slide": slide, "skip": skip,
		"gateProb": prob, "pitchRnd": rnd, "accent": acc, "accentRnd": accrnd}

FRESH = [stage("C4", gate=MULTIPLE) for _ in range(8)]   # a fresh module's stages: 0 V, 1 pulse, multiple

def pattern(stages=FRESH, play=0, count=0, steps=8, root=0, scale=0, gate=0.5, slide=0.2, sens=1.0):
	assert len(stages) == 8
	return {"playMode": play, "countMode": count, "numSteps": steps, "rootNote": root, "scale": scale,
		"gateTime": gate, "slideTime": slide, "sensitivity": sens, "stages": [dict(s) for s in stages]}

def data(patterns, extended):
	# the module's dataToJson: 16 patterns, the rest fresh ones
	pats = list(patterns) + [pattern() for _ in range(16 - len(patterns))]
	keep = ("pitch", "pulses", "type", "slide", "skip") + (("gateProb", "pitchRnd", "accent", "accentRnd") if extended else ())
	for p in pats:
		p["stages"] = [{k: s[k] for k in keep} for s in p["stages"]]
	return {"running": True, "stepOutputsMode": False, "selectedPattern": 0, "playedPattern": 0, "patterns": pats}

def params(p, clock, extended):
	# the panel as loadPanel would set it from pattern p
	v = {"CLOCK_PARAM": clock, "RUN_PARAM": 0, "RESET_PARAM": 0, "PLAY_MODE_PARAM": 0, "COUNT_MODE_PARAM": 0,
		"PATTERN_PARAM": 1, "LEFT_PARAM": 0, "RIGHT_PARAM": 0, "UP_PARAM": 0, "DOWN_PARAM": 0, "COPY_PARAM": 0,
		"STEPS_PARAM": p["numSteps"], "SLIDE_TIME_PARAM": p["slideTime"], "GATE_TIME_PARAM": p["gateTime"],
		"ROOT_NOTE_PARAM": p["rootNote"], "SCALE_PARAM": p["scale"], "SENSITIVITY_PARAM": p["sensitivity"]}
	for i in range(1, 8):
		v["SENSITIVITY_PARAM+%d" % i] = 0   # ids the enum leaves unused (TRIG_COUNT_PARAM = SENSITIVITY_PARAM + 8)
	arrays = [("TRIG_PITCH_PARAM", "pitch"), ("TRIG_COUNT_PARAM", "pulses"), ("TRIG_TYPE_PARAM", "type"),
		("TRIG_SLIDE_PARAM", None), ("TRIG_SKIP_PARAM", None)]
	if extended:
		arrays += [("TRIG_GATEPROB_PARAM", "gateProb"), ("TRIG_PITCHRND_PARAM", "pitchRnd"),
			("TRIG_ACCENT_PARAM", "accent"), ("TRIG_RNDACCENT_PARAM", "accentRnd")]
	for name, key in arrays:
		for i in range(8):
			v[name if i == 0 else "%s+%d" % (name, i)] = p["stages"][i][key] if key else 0
	return v

def preset(title, patterns, clock=2.0, extended=False):
	return (title, params(patterns[0], clock, extended), data(patterns, extended))

S = stage
PRESETS = [
	# Acid bass in C minor (Aeolian): pulses count, 16 pulses = one bar of sixteenths. Octave jumps, slides into the
	# high C and the Bb, a two-pulse ratchet on G, short gates (40 %) so the slides stand out.
	preset("Acid bassline", [pattern([
		S("C2", 2), S("C3", 2, slide=True), S("Eb2"), S("C2"), S("G2", 2, MULTIPLE), S("Bb2", 2, slide=True),
		S("C3", 3, HOLD), S("F2", 3)], count=1, steps=16, root=0, scale=6, gate=0.4, slide=0.3)]),
	# Arpeggio up and down A minor pentatonic over two octaves: ping-pong over 8 one-pulse stages, sixteenths.
	preset("Ping-pong arp", [pattern([
		S("A3"), S("C4"), S("D4"), S("E4"), S("G4"), S("A4"), S("C5"), S("E5")], play=2, root=9, scale=25, gate=0.6)]),
	# Metropolis bass in D Dorian: pulse counts make the stages 1 to 4 sixteenths long (16 in all), with ratchets
	# (multiple), held notes (hold), a rest and a slide.
	preset("Metropolis ratchets", [pattern([
		S("D2"), S("D2", 3, MULTIPLE), S("A2"), S("C3", 2, HOLD, slide=True), S("F2", 4, MULTIPLE), S("D2", 1, REST),
		S("E2", 2), S("A1", 2, HOLD)], root=2, scale=2, gate=0.5)]),
	# Slow lead in F Lydian: 4 to 8 eighth notes per stage, held gates (90 %), long linear-ish slides between most notes.
	preset("Gliding lead", [pattern([
		S("F3", 8, HOLD), S("C4", 4, HOLD, slide=True), S("A3", 4, HOLD, slide=True), S("E4", 8, HOLD, slide=True),
		S("D4", 8, HOLD), S("G3", 4, HOLD, slide=True), S("B3", 4, HOLD), S("C4", 8, HOLD, slide=True)],
		root=5, scale=4, gate=0.9, slide=0.8)], clock=1.0),
	# Brownian melody in C major pentatonic: each sixteenth moves one stage forward or back at random, so the line
	# wanders up and down the scale (it leaps only when it wraps from stage 8 to 1 or back); a rest and two slides.
	preset("Brownian wander", [pattern([
		S("C4"), S("D4"), S("E4", slide=True), S("G4"), S("A4", 1, REST), S("C5"), S("D5", slide=True), S("E5")],
		play=4, root=0, scale=24, gate=0.5)]),
	# Polymeter in E Phrygian: pulses count with STEPS 12, so the line loops every 12 sixteenths over a 16 bar. Stages
	# 1-8 (6 skipped) make 9 pulses, the loop runs on into steps 9-11 (stages 1-3 again) until 12 are counted.
	preset("12 over 16", [pattern([
		S("E2"), S("E3"), S("F2"), S("E2", 2, MULTIPLE), S("G2"), S("B2", skip=True), S("D3", 2, slide=True), S("C3")],
		count=1, steps=12, root=4, scale=3, gate=0.45)]),
	# Song mode: patterns 1-4 hold the same arpeggio shape over Am, F, C, G (A minor, Aeolian). Patch a stepped CV to
	# PATTERN (0, 0.7, 1.3, 1.9 V pick patterns 1-4) to play the progression; the panel shows pattern 1.
	preset("Four chord patterns", [
		pattern([S(n) for n in ("A2", "E3", "A3", "C4", "E4", "C4", "A3", "E3")], root=9, scale=6, gate=0.5),
		pattern([S(n) for n in ("F2", "C3", "F3", "A3", "C4", "A3", "F3", "C3")], root=9, scale=6, gate=0.5),
		pattern([S(n) for n in ("C3", "G3", "C4", "E4", "G4", "E4", "C4", "G3")], root=9, scale=6, gate=0.5),
		pattern([S(n) for n in ("G2", "D3", "G3", "B3", "D4", "B3", "G3", "D3")], root=9, scale=6, gate=0.5)]),
]

if __name__ == "__main__":
	preset_kit.write("dTrOY", "DTROY.cpp", PRESETS)

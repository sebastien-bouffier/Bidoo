# Factory presets for rEgIStRE (src/REGISTRE.cpp, engine in src/RegistreEngine.hpp): python tools/presets/registre.py
# writes presets/REGISTRE/.
# The whole song lives in data["song"]: the engine's binary form (serialize(), format 2, no uids), base64. This script
# writes that form itself: header 'RGS' 2, transition, resetAt, resetTrack, RecSettings (15 bytes); per track: number
# of patterns, their lengths, their flags, track flags, div, mult, MATH (5 x 4 int8), steps (CV A, CV B, DUR, GATE,
# flags, groups as uint16), tables A and B (100 float32 each); then the parts that are set (part, then per track the
# reset, loop start and loop end step indices as int16, -1 = none); then the groups that are not default (group,
# MATH, HIGH[3], LOW[3] as transforms, slopes 3 x 4 float32). Little endian.
# A step holds indices into its track's tables; DUR and GATE are clock pulses (the presets assume 4 pulses a beat:
# a step of 1 is a 16th). GATE 0 is a rest, GATE >= DUR ties. Part 1 plays when loaded; a part without loops plays
# the tracks whole. RUN is on in every preset so it plays as soon as a clock is patched.
import base64, math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preset_kit

PARAMS = {
	"RUN_PARAM": 1,       # running
	"GO_PARAM": 0,        # momentary
	"HOLD_PARAM": 0,      # edits play at once
	"COMMIT_PARAM": 0,    # momentary
	"PUNCH_PARAM": 0,     # not recording
}

# ---- The engine's constants
CV_A, CV_B, DUR, GATE = range(4)
SMOOTH_A, SMOOTH_B, RATCHET = 1, 2, 4
TRACK_TRIGGERS, NOTES_A, NOTES_B = 8, 16, 32
FIRST, LAST, NOW = range(3)
X, Y, Z = range(3)

# ---- Reference tables (referenceTable in RegistreEngine.hpp)
def scale_volts(i, cents, zero):
	n = len(cents)
	k = i - zero
	octave = k // n
	return octave + cents[k - octave * n] / 1200.0

MAJOR = [0, 200, 400, 500, 700, 900, 1100]
MINOR = [0, 200, 300, 500, 700, 800, 1000]
PENTA_MAJ = [0, 200, 400, 700, 900]
PENTA_MIN = [0, 300, 500, 700, 1000]

def table(name):
	out = []
	for i in range(100):
		v = {
			"12TET": lambda: (i - 48) / 12.0,
			"MAJOR": lambda: scale_volts(i, MAJOR, 28),
			"MINOR": lambda: scale_volts(i, MINOR, 28),
			"PENTA": lambda: scale_volts(i, PENTA_MAJ, 20) if i < 50 else scale_volts(i - 50, PENTA_MIN, 20),
			"LIN10": lambda: i * 0.1,
			"LIN5": lambda: (i - 50) * 0.1,
		}[name]()
		out.append(min(10.0, max(-5.0, v)))
	return out

# 12-TET: the index of a note name (C4 = 0 V = index 48)
NOTE = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5, "F#": 6, "Gb": 6, "G": 7, "G#": 8, "Ab": 8,
        "A": 9, "A#": 10, "Bb": 10, "B": 11}
def n(name):
	return 48 + NOTE[name[:-1]] + 12 * (int(name[-1]) - 4)

# ---- Song building
def transform(**kw):
	# MATH settings per field: rd, g, jt, add, qt, each a dict {field: value}; identity by default
	t = {"rd": [0] * 4, "g": [1] * 4, "jt": [0] * 4, "add": [0] * 4, "qt": [1] * 4}
	for k, d in kw.items():
		for f, v in d.items():
			t[k][f] = v
	return t

def step(a=48, b=0, dur=4, gate=2, flags=0, groups=0):
	return [a, b, dur, gate, flags, groups]

def track(patterns, ta="12TET", tb="LIN10", flags=NOTES_A, div=1, mult=1, pattern_flags=None):
	# patterns: lists of steps
	return {"patterns": patterns, "ta": table(ta), "tb": table(tb), "flags": flags, "div": div, "mult": mult,
	        "pflags": pattern_flags or [0] * len(patterns)}

def empty_track():
	# what a new track holds: 16 rests a beat long
	return track([[step(48, 0, 4, 0) for _ in range(16)]])

def song(tracks, parts=None, groups=None, transition=LAST, shown_part=1):
	# parts: {part: [(reset, loopStart, loopEnd) per track, step indices or -1]}; groups: {group: dict}
	tracks = tracks + [empty_track() for _ in range(4 - len(tracks))]
	o = bytearray(b"RGS" + bytes([2, transition, 0, 0]))
	o += bytes([0, 0, 1] + [1] * 4 + [1] * 4 + [1] * 4)   # RecSettings: real time, track end, wait, NEW ON A, grids 1
	identity = transform()
	def put_transform(t):
		for k in ("rd", "g", "jt", "add", "qt"):
			o.extend(struct.pack("<4b", *t[k]))
	for tr in tracks:
		pats = tr["patterns"]
		assert 1 <= len(pats) <= 100 and sum(len(p) for p in pats) <= 1000
		o.append(len(pats))
		o.extend(len(p) for p in pats)
		o.extend(tr["pflags"])
		o.extend([tr["flags"], tr["div"], tr["mult"]])
		put_transform(identity)
		for p in pats:
			assert 1 <= len(p) <= 100
			for s in p:
				assert all(0 <= v <= 99 for v in s[:4]), s
				o.extend(bytes(s[:5]))
				o.extend(struct.pack("<H", s[5]))
		for tab in (tr["ta"], tr["tb"]):
			o.extend(struct.pack("<100f", *tab))
	parts = parts or {}
	o.append(len(parts))
	for p in sorted(parts):
		o.append(p)
		for t in range(4):
			r, ls, le = parts[p][t] if t < len(parts[p]) else (-1, -1, -1)
			o.extend(struct.pack("<3h", r, ls, le))
	groups = groups or {}
	o.append(len(groups))
	for g in sorted(groups):
		gr = groups[g]
		o.append(g)
		put_transform(gr.get("math", identity))
		for c in range(3):
			put_transform(gr.get("high", {}).get(c, identity))
		for c in range(3):
			put_transform(gr.get("low", {}).get(c, identity))
		for c in range(3):
			o.extend(struct.pack("<4f", *gr.get("slope", {}).get(c, [0.0] * 4)))
	return data(bytes(o), shown_part)

def data(blob, part=1):
	# the module's dataToJson, without panelTheme (a preset keeps the module's theme)
	return {
		"song": base64.b64encode(blob).decode("ascii"),
		"snapshots": [None] * 16,
		"snapshotNames": [""] * 16,
		"part": part,
		"playingPart": 1,
		"commitAt": 2,          # COMMIT_PATTERN
		"commitTrack": 0,
		"armed": [False] * 4,
		"midiPulsesPerBeat": 4,
		"midiBeatsPerBar": 4,
	}

# ---- 1. Chord progression: Am - F - C - G, a bar each, four voices on the four tracks (POLY A + POLY G to a
# polyphonic VCO, or A/GATE 1-4 to four voices). Each chord lasts 16 pulses with the gate down for the last one so
# every chord is struck again. CV B (LIN 0-10) gives a velocity: the first chord of the cycle a little louder.
def chords():
	prog = [("A2", "C4", "E4", "A4"), ("F2", "C4", "F4", "A4"), ("C3", "C4", "E4", "G4"), ("G2", "B3", "D4", "G4")]
	vel = [80, 65, 70, 65]
	tracks = []
	for v in range(4):
		tracks.append(track([[step(n(c[v]), vel[i], 16, 15) for i, c in enumerate(prog)]]))
	return song(tracks)

# ---- 2. Bass and lead in C minor: tables A are MINOR (index 28 = C4, one index a scale degree), so both lines stay
# in the key and MATH + on CV A transposes within it. Track 1, an eighth-note bass around C2 with octave jumps;
# track 2, a lead in C4..C5 with held notes, glides (SM A) into two of them and a rest; CV B is velocity.
def bass_lead():
	deg = lambda d: 28 + d   # degree d above C4
	bass_notes = [-14, -14, -7, -14, -9, -9, -2, -9, -12, -12, -5, -12, -10, -10, -3, -11]   # C2 C2 C3 C2 Ab2 Ab2 Ab3...
	bass = [step(deg(d), 90 if i % 4 == 0 else 60, 2, 1) for i, d in enumerate(bass_notes)]
	lead = [
		step(deg(4), 80, 6, 5), step(deg(3), 60, 2, 2), step(deg(2), 70, 4, 3, SMOOTH_A), step(deg(4), 80, 4, 3),
		step(deg(7), 90, 8, 7), step(deg(6), 60, 2, 1), step(deg(4), 60, 2, 1), step(deg(5), 70, 4, 3, SMOOTH_A),
		step(deg(2), 70, 4, 3), step(deg(0), 80, 8, 6), step(deg(0), 0, 4, 0),
	]
	lead.append(step(deg(-1), 70, 16, 12, SMOOTH_A))   # a bar on Bb3, gliding back to the G4 that starts it again
	assert sum(s[DUR] for s in bass) == 32 and sum(s[DUR] for s in lead) == 64   # bass twice per lead
	return song([track([bass, bass], ta="MINOR"), track([lead], ta="MINOR")])

# ---- 3. Pentatonic melody: table A is PENTA (0..49 C major pentatonic, 0 V at 20), so any index is in the scale.
# A 4-bar phrase over two patterns, with uneven durations, a ratcheted note and a smoothed slide; CV B (LIN 0-10)
# accents the long notes. Track 2 plays its first and third bars in the minor pentatonic half of the table (indices
# + 47: three degrees lower, from F3), at half speed (/2 in its lane): a slower, darker counter-line.
def penta():
	p1 = [(20, 3, 2), (21, 1, 1), (22, 2, 1), (24, 2, 2), (23, 4, 3), (22, 2, 1), (21, 2, 1)]
	p2 = [(25, 2, 1), (24, 1, 1), (23, 1, 1), (22, 4, 2), (24, 2, 2), (23, 2, 1), (21, 2, 1), (20, 2, 2)]
	p3 = [(22, 3, 2), (23, 1, 1), (24, 2, 1), (26, 2, 2), (25, 4, 3), (24, 2, 1), (23, 2, 1)]
	p4 = [(24, 2, 1), (23, 2, 1), (22, 2, 1), (21, 2, 1), (20, 8, 6)]
	def mk(notes, offset=0, accent=True):
		out = []
		for a, d, g in notes:
			out.append(step(a + offset, (80 if d >= 4 else 50) if accent else 60, d, g))
		return out
	A = mk(p1) + mk(p2)
	B = mk(p3) + mk(p4)
	A[4][4] = RATCHET; A[4][3] = 1          # the long 5th note rolls: 4 pulses, gate 1 = two hits
	B[4][4] = SMOOTH_A                      # slide from the high note down
	assert sum(s[DUR] for s in A) == 32 and sum(s[DUR] for s in B) == 32
	low = [step(a + 50 - 3, 60, d, g) for a, d, g in p1 + p3]
	return song([track([A, B], ta="PENTA"), track([low], ta="PENTA", div=2)])

# ---- 4. Polyrhythm: four short loops of different lengths phasing against each other, all in C minor pentatonic
# (PENTA's second half: 70 = C4, then Eb F G Bb C...). Track 1: 3 steps of a 16th around C5; track 2: 5 steps of a
# 16th around C4, one a rest (both at x2 in their lanes, so a 16th is 2 pulses with a gate of 1); track 3: 7 steps of an 8th, an octave down; track 4: a bass on dotted quarters (6
# pulses) against the bar. Loops of 3, 5, 14 and 24 pulses meet again every 840.
def poly():
	t1 = [step(75, 70, 2, 1), step(77, 50, 2, 1), step(76, 50, 2, 1)]
	t2 = [step(73, 70, 2, 1), step(72, 50, 2, 1), step(71, 50, 2, 1), step(70, 50, 2, 0), step(72, 50, 2, 1)]
	t3 = [step(65, 60, 2, 1), step(67, 60, 2, 1), step(68, 60, 2, 1), step(69, 60, 2, 1), step(68, 60, 2, 0),
	      step(67, 60, 2, 1), step(66, 60, 2, 1)]
	t4 = [step(60, 80, 6, 4), step(60, 60, 6, 4), step(63, 70, 6, 4), step(62, 60, 6, 4)]
	return song([track([t1], ta="PENTA", mult=2), track([t2], ta="PENTA", mult=2), track([t3], ta="PENTA"), track([t4], ta="PENTA")])

# ---- 5. Modulation: four CV sources, no notes. Track 1 (A in LIN +-5): a triangle LFO, -5 to +4.9 V and back over
# two bars (two smoothed steps of 16, gate 0); B the same triangle upside down. Track 2 (LIN 0-10): a slewed random-
# looking line, 8 smoothed steps of a beat, B stepped (not smoothed) for a sample and hold. Track 3 (LIN 0-10): a
# ramp up over 15 pulses that falls back on the 16th, with a gate on each fall (a saw to sync to). Track 4: gates
# only, a clock divided by 3 (triggers, /3 in its lane) for things that should drift against the bar.
def modulation():
	t1 = [step(0, 99, 16, 0, SMOOTH_A | SMOOTH_B), step(99, 0, 16, 0, SMOOTH_A | SMOOTH_B)]
	rnd_a = [62, 18, 85, 40, 71, 9, 55, 30]
	rnd_b = [33, 77, 12, 90, 48, 66, 21, 58]
	t2 = [step(a, b, 4, 0, SMOOTH_A) for a, b in zip(rnd_a, rnd_b)]
	t3 = [step(0, 0, 15, 0, SMOOTH_A), step(99, 99, 1, 1)]
	t4 = [step(0, 99, 1, 1)]
	return song([
		track([t1], ta="LIN5", tb="LIN5", flags=0),
		track([t2], ta="LIN10", tb="LIN10", flags=0),
		track([t3], ta="LIN10", tb="LIN10", flags=0),
		track([t4], ta="LIN10", tb="LIN10", flags=TRACK_TRIGGERS, div=3),
	])

# ---- 6. Drums with rolls: four gate tracks for a kit (GATE 1 kick, 2 snare, 3 closed hat, 4 open hat), CV B of
# each a velocity (LIN 0-10: index 90 = 9 V). Every track runs at x2 (32 pulses a bar), so a gate of 1 is a 32nd. The snare's last hit and two hats are ratchets (a roll: GATE pulses
# on, GATE off, through the step); ghost snares at low velocity. Change entry 90 of a track's table B to change
# every accent of that track at once.
def drums():
	k = [step(48, 90, 8, 1), step(48, 90, 8, 1), step(48, 90, 6, 1), step(48, 60, 2, 1), step(48, 90, 8, 1)]
	s = [step(48, 0, 8, 0), step(48, 90, 6, 1), step(48, 30, 2, 1), step(48, 0, 4, 0), step(48, 30, 2, 1),
	     step(48, 0, 2, 0), step(48, 90, 8, 1, RATCHET)]   # the last snare: a roll of four 16ths
	h = [step(48, 90 if i % 2 == 0 else 50, 4, 1) for i in range(6)]
	h[3] = step(48, 70, 4, 1, RATCHET)      # two 16ths inside an 8th
	h.append(step(48, 70, 8, 1, RATCHET))   # the last quarter: four 16ths
	o = [step(48, 0, 28, 0), step(48, 80, 4, 3)]
	for t in (k, s, h, o):
		assert sum(x[DUR] for x in t) == 32, sum(x[DUR] for x in t)
	return song([track([p], flags=0, mult=2) for p in (k, s, h, o)])

# ---- 7. Verse and chorus: two parts, each looping one pattern of every track. Part 1 (playing when loaded) loops the
# verse (Am - G, bass + arpeggio). The screen shows part 2, so GO (or a gate into GO) cues the chorus (F - C - G - Am);
# with a CV into PART, 0.1 V picks the verse and 0.2 V the chorus. The switch comes once every track has ended its
# loop (Part transition: Last). 12-TET tables.
def parts():
	def arp(notes):
		return [step(n(x), 60 + 20 * (i % 4 == 0), 2, 1) for i, x in enumerate(notes)]
	verse_arp = arp(["A3", "C4", "E4", "A4", "E4", "C4", "A3", "C4"] + ["G3", "B3", "D4", "G4", "D4", "B3", "G3", "B3"])
	chorus_arp = arp(["F3", "A3", "C4", "F4", "C4", "A3"] + ["C4", "E4", "G4", "C5", "G4", "E4"] +
	                 ["G3", "B3", "D4", "G4", "D4", "B3"] + ["A3", "C4", "E4", "A4", "E4", "C4"])
	# the chorus moves faster: 12 pulses (6 eighths) a chord, 48 pulses in all
	verse_bass = [step(n("A2"), 80, 6, 5), step(n("A2"), 60, 2, 1), step(n("E2"), 60, 8, 7),
	              step(n("G2"), 80, 6, 5), step(n("G2"), 60, 2, 1), step(n("D2"), 60, 8, 7)]
	chorus_bass = [step(n("F2"), 80, 12, 11), step(n("C3"), 80, 12, 11), step(n("G2"), 80, 12, 11), step(n("A2"), 80, 12, 11)]
	for p in (verse_arp, verse_bass):
		assert sum(x[DUR] for x in p) == 32
	for p in (chorus_arp, chorus_bass):
		assert sum(x[DUR] for x in p) == 48
	tr_bass = track([verse_bass, chorus_bass])
	tr_arp = track([verse_arp, chorus_arp])
	vb, va = len(verse_bass), len(verse_arp)
	cb, ca = len(chorus_bass), len(chorus_arp)
	prt = {
		1: [(0, 0, vb - 1), (0, 0, va - 1)],
		2: [(vb, vb, vb + cb - 1), (va, va, va + ca - 1)],
	}
	return song([tr_bass, tr_arp], parts=prt, shown_part=2)   # part 2 shown: GO cues the chorus

# ---- 8. Octaves on X: a 16th-note bass line in 12-TET (track 1, x2: 2 pulses a 16th) whose every other step is in group 1; while X GATE
# is high, group 1's HIGH transform for X adds 12 to CV A (an octave up) and 40 to CV B (a brighter filter on B).
# Patch a slow square LFO or a gate to X GATE. And a slope of 1 V/V on CV A for Y: patch a CV to Y CV to transpose
# the grouped steps alone, live. Nothing patched: the plain line.
def octaves():
	line = ["C2", "C2", "Eb2", "C2", "G2", "C2", "Bb2", "C2", "F2", "F2", "Ab2", "F2", "G2", "G2", "Bb2", "D2"]
	steps_ = [step(n(x), 30, 2, 1, 0, 1 if i % 2 == 1 else 0) for i, x in enumerate(line)]
	g1 = {"high": {X: transform(add={CV_A: 12, CV_B: 40})}, "slope": {Y: [1.0, 0.0, 0.0, 0.0]}}
	return song([track([steps_], mult=2)], groups={0: g1})

PRESETS = [
	("Chord progression", PARAMS, chords()),
	("Bass and lead", PARAMS, bass_lead()),
	("Pentatonic melody", PARAMS, penta()),
	("Polyrhythm", PARAMS, poly()),
	("Modulation", PARAMS, modulation()),
	("Drums with rolls", PARAMS, drums()),
	("Verse and chorus", PARAMS, parts()),
	("Octaves on X", PARAMS, octaves()),
]

if __name__ == "__main__":
	preset_kit.write("REGISTRE", "REGISTRE.cpp", PRESETS)

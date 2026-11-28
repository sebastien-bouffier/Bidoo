# Factory presets for zOu MAi (src/ZOUMAI.cpp, engine in ZouSeq.hpp): python tools/presets/zoumai.py writes presets/ZOUMAI/.
# A preset is a whole pattern (pattern 1, PTRN knob at 1): 8 tracks of up to 64 trigs, in the data as dataToJson writes
# it (only what differs from a fresh trig or track; slideMode always). Times (trig length, trim, pulse distance) are in
# steps here; zOu MAi saves them as reals in steps, enCORE (tools/presets/encore.py, same builder) as ticks, 96 a step.
# Notes are names, C4 = 0 V (the panel's OCT 4); octaves C1..B7. CV1 is used as velocity / accent, CV2 as a tone CV.
# Clock: Step mode (the default), one clock a step. Run is not saved by the module: patch the clock, then press RUN.
# The selected track and trig knobs (Len., Puls., Trim...) are written for track 1, trig 1 (dataFromJson sets them too).
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preset_kit

NOTES = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5, "F#": 6, "Gb": 6, "G": 7, "G#": 8,
	"Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11}
ROOTS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
# quantizer::scales (src/dep/quantizer.hpp), the ones used here
SCALES = {"No": 0, "Ionian": 1, "Dorian": 2, "Phrygian": 3, "Aeolian": 6, "Pentatonic m7": 25, "Chromatic": 45}
CONDITIONS = ["DICE", "COUNT", "FILL", "!FILL", "PRE", "!PRE", "NEI", "!NEI", "FIRST", "LAST"]
# zouSpeedLabels: the factor saved in the patch
SPEEDS = {"x1/8": 0.125, "x1/4": 0.25, "x1/2": 0.5, "x3/4": 0.75, "x1": 1.0, "x3/2": 1.5, "x2": 2.0, "x3": 3.0, "x4": 4.0,
	"x5": 5.0, "x16/3": 16.0 / 3.0, "x6": 6.0, "x7": 7.0, "x8": 8.0}
SPEED_INDEX = {k: i for i, k in enumerate(SPEEDS)}
FRAC = [(1, 32), (1, 24), (1, 16), (1, 12), (3, 32), (1, 8), (1, 6), (3, 16), (1, 4), (1, 3), (3, 8), (1, 2), (2, 3), (3, 4),
	(7, 8), (1, 1), (4, 3), (3, 2), (2, 1), (3, 1), (4, 1), (6, 1), (8, 1), (12, 1), (16, 1), (24, 1), (32, 1), (48, 1), (64, 1)]

class Traits:
	# ZouMaiTraits / EncoreTraits (src/ZouTraits.hpp)
	def __init__(self, model, src, ticks, knob_per_step, interpolate, min_length, max_length, default_length,
			max_distance, default_distance, max_trim):
		self.__dict__.update(locals())
		del self.__dict__["self"]

ZOUMAI = Traits("ZOUMAI", "ZOUMAI.cpp", 1008000, 1, True, 0, 64 * 1008000, 1008000 // 10 * 9, 64 * 1008000, 1008000 // 2,
	1008000)

def note(name):
	# "A1" -> (octave relative to C4, semitone)
	m = name[:-1] if name[-1].isdigit() else name
	o = int(name[len(m):])
	assert 1 <= o <= 7, name
	return o - 4, NOTES[m]

class Song:
	# One pattern (index 0) of 8 tracks; track(i, ...) then trig(i, step, ...), steps 1-based like the panel.
	def __init__(self, traits):
		self.t = traits
		self.tracks = [{"slideMode": False} for _ in range(8)]
		self.trigs = [dict() for _ in range(8)]
		self.labels = ["Track %d" % (i + 1) for i in range(8)]

	def time(self, steps):
		# steps (a float or a fraction) -> ticks, exact
		ticks = steps * self.t.ticks
		assert abs(ticks - round(ticks)) < 1e-6, (steps, "not on the grid")
		return int(round(ticks))

	def jtime(self, ticks):
		return ticks / self.t.ticks if self.t.interpolate else ticks

	def track(self, i, label=None, length=16, speed="x1", read=0, root="C", scale="No", swing=50, slide_mode=False,
			cv_hold=False, active=True):
		tr = {}
		if not active: tr["isActive"] = False
		if speed != "x1": tr["speed"] = SPEEDS[speed]
		if read: tr["readMode"] = read
		if length != 16: tr["length"] = length
		if ROOTS.index(root): tr["rootNote"] = ROOTS.index(root)
		if SCALES[scale]: tr["scale"] = SCALES[scale]
		tr["slideMode"] = slide_mode
		if cv_hold: tr["cvHold"] = True
		if swing != 50: tr["swing"] = float(swing)
		self.tracks[i - 1] = tr
		if label: self.labels[i - 1] = label

	def trig(self, i, step, n=None, length=None, pulses=1, dist=None, trim=0, slide=0.0, slide_type=False, cv1=0.0, cv2=0.0,
			cv1_end=None, cv2_end=None, pitch_ramp=0, dist_ramp=100.0, cond=None, active=True, trig_type=0):
		# cond: None (always), ("DICE", %), ("COUNT", val, base), "FILL", "!FILL", "PRE", "!PRE", "NEI", "!NEI", "FIRST", "LAST"
		t = self.t
		g = {}
		if active: g["isActive"] = True
		if slide: g["slide"] = float(slide)
		if trig_type: g["trigType"] = trig_type
		if trim: g["trim"] = self.jtime(self.time(trim))
		if length is not None and self.time(length) != t.default_length:
			assert t.min_length <= self.time(length) <= t.max_length, length
			g["length"] = self.jtime(self.time(length))
		if pulses != 1: g["pulseCount"] = pulses
		if dist is not None and self.time(dist) != t.default_distance:
			assert 0 <= self.time(dist) <= t.max_distance, dist
			g["pulseDistance"] = self.jtime(self.time(dist))
		if cond is not None:
			c = cond if isinstance(cond, tuple) else (cond,)
			p = CONDITIONS.index(c[0])
			if p: g["proba"] = p
			if c[0] == "DICE" and c[1] != 100: g["count"] = c[1]
			if c[0] == "COUNT":
				if c[1] != 100: g["count"] = c[1]
				if c[2] != 1: g["countReset"] = c[2]
		if n is not None:
			o, s = note(n)
			if o: g["octave"] = o
			if s: g["semitones"] = s
		if cv1: g["CV1"] = float(cv1)
		if cv2: g["CV2"] = float(cv2)
		if cv1_end is not None and cv1_end != cv1: g["CV1End"] = float(cv1_end)
		if cv2_end is not None and cv2_end != cv2: g["CV2End"] = float(cv2_end)
		if pitch_ramp: g["pitchRamp"] = pitch_ramp
		if dist_ramp != 100.0: g["distRamp"] = float(dist_ramp)
		if slide_type: g["trigSlideType"] = True
		assert 1 <= step <= 64 and not (step - 1) in self.trigs[i - 1], (i, step)
		self.trigs[i - 1][step - 1] = g

	def hits(self, i, steps, **kw):
		for s in steps:
			self.trig(i, s, **kw)

	def data(self):
		d = {"currentPattern": 0, "currentTrack": 0, "currentTrig": 0, "trigPage": 0, "ticksPerStep": self.t.ticks}
		for i, l in enumerate(self.labels):
			d["label%d" % i] = l
		pat = {}
		for j in range(8):
			tr = dict(self.tracks[j])
			for k in sorted(self.trigs[j]):
				tr["trig%d" % k] = self.trigs[j][k]
			pat["track%d" % j] = tr
		d["pattern0"] = pat
		return d

	# The knobs as updateTrackToParams / updateTrigToParams set them for track 1, trig 1.
	def table(self, distance):
		t = self.t
		tab = [0 if distance else t.min_length]
		top = t.max_distance if distance else t.max_length
		for n, dd in FRAC:
			v = t.ticks * n
			if v % dd: continue
			v //= dd
			if v > tab[-1] and v <= top: tab.append(v)
		return tab

	def time_knob(self, ticks, distance):
		tab = self.table(distance)
		best = min(range(len(tab)), key=lambda i: (abs(tab[i] - ticks), i))
		return best / (len(tab) - 1)

	def params(self, defaults):
		t = self.t
		p = dict(defaults)
		tr = self.tracks[0]
		g = self.trigs[0].get(0, {})
		def ticks(key, default):
			if key not in g: return default
			return int(round(g[key] * t.ticks)) if t.interpolate else g[key]
		speed = [k for k, v in SPEEDS.items() if abs(v - tr.get("speed", 1.0)) < 1e-9][0]
		p.update({
			"TRACKLENGTH_PARAM": tr.get("length", 16), "TRACKSPEED_PARAM": SPEED_INDEX[speed],
			"TRACKREADMODE_PARAM": tr.get("readMode", 0), "TRACKROOTNOTE_PARAM": tr.get("rootNote", 0),
			"TRACKSCALE_PARAM": tr.get("scale", 0), "TRACKQUANTIZECV1_PARAM": tr.get("quantizeCV1", 0),
			"TRACKSWING_PARAM": tr.get("swing", 50.0),
			"TRIGLENGTH_PARAM": self.time_knob(ticks("length", t.default_length), False),
			"TRIGSLIDE_PARAM": g.get("slide", 0.0), "TRIGTYPE_PARAM": g.get("trigType", 0),
			"TRIGTRIM_PARAM": ticks("trim", 0) * t.knob_per_step / t.ticks,
			"TRIGPULSECOUNT_PARAM": g.get("pulseCount", 1),
			"TRIGPULSEDISTANCE_PARAM": self.time_knob(ticks("pulseDistance", t.default_distance), True),
			"TRIGCV1_PARAM": g.get("CV1", 0.0), "TRIGCV2_PARAM": g.get("CV2", 0.0),
			"TRIGCV1END_PARAM": g.get("CV1End", g.get("CV1", 0.0)), "TRIGCV2END_PARAM": g.get("CV2End", g.get("CV2", 0.0)),
			"TRIGPITCHRAMP_PARAM": g.get("pitchRamp", 0),
			"TRIGDISTRAMP_PARAM": math.log(g.get("distRamp", 100.0) * 100 / 2500.0) / math.log(16.0),
			"TRIGPROBA_PARAM": g.get("proba", 0), "TRIGPROBACOUNT_PARAM": g.get("count", 100),
			"TRIGPROBACOUNTRESET_PARAM": g.get("countReset", 1),
			"TRIGSLIDETYPE_PARAM": 1 if g.get("trigSlideType") else 0,
		})
		for k in range(7):
			p["OCTAVE_PARAMS" + ("+%d" % k if k else "")] = 1.0 if k == g.get("octave", 0) + 3 else 0.0
		for k in range(8):
			p["TRACKSONOFF_PARAMS" + ("+%d" % k if k else "")] = 0.0 if self.tracks[k].get("isActive", True) is False else 1.0
		return p

def defaults(traits):
	# configParam defaults (ZouSeq constructor), by name
	d = {}
	def arr(name, n, v):
		for k in range(n):
			d[name + ("+%d" % k if k else "")] = v
	arr("STEPS_PARAMS", 16, 0.0)
	arr("TRACKSONOFF_PARAMS", 8, 1.0)
	arr("TRACKSELECT_PARAMS", 8, 0.0)
	d["TRACKSELECT_PARAMS"] = 1.0      # track 1 selected
	arr("TRIGPAGE_PARAM", 4, 0.0)
	arr("OCTAVE_PARAMS", 7, 0.0)
	arr("NOTE_PARAMS", 12, 0.0)
	d.update({"FILL_PARAM": 0.0, "RUN_PARAM": 0.0, "PATTERN_PARAM": 0.0, "RECORD_PARAM": 0.0, "QUANTIZE_PARAM": 1.0,
		"TRIGPAGE2_PARAM": 0.0, "TRACKPAGE2_PARAM": 0.0})
	return d

def build(song):
	return song.params(defaults(song.t)), song.data()

def euclid(k, n, rot=0):
	# k hits spread over n steps (1-based), turned right by rot
	return [((i + rot) % n) + 1 for i in range(n) if (i * k) % n < k]

# ---------------------------------------------------------------- the presets

def house(T):
	# Four on the floor: kick on the beats, clap on 2 and 4, open hat on the off-beats, closed hats in between with the
	# 16ths on DICE 65 % (CV1 lower: ghosts), a rim with a COUNT 1-of-2 hit, an off-beat A minor bass. FILL swaps the
	# clap on 4 for a roll on the last steps (2, 4, 4 hits a step). Swing 56 % on the hats.
	s = Song(T)
	s.track(1, "Kick")
	s.hits(1, [1, 5, 9, 13], length=1 / 4, cv1=10)
	s.track(2, "Clap")
	s.trig(2, 5, length=1 / 4, cv1=9)
	s.trig(2, 13, length=1 / 4, cv1=9, cond="!FILL")
	s.trig(2, 14, length=1 / 2, pulses=2, dist=1 / 2, cv1=5, cond="FILL")
	s.trig(2, 15, length=1, pulses=4, dist=0, cv1=4, cv1_end=10, cond="FILL")
	s.trig(2, 16, length=1, pulses=4, dist=0, cv1=6, cv1_end=10, cond="FILL")
	s.track(3, "Closed hat", swing=56)
	for st in range(1, 17):
		if st in (3, 7, 11, 15): continue
		if st % 2: s.trig(3, st, length=1 / 8, cv1=7)
		else: s.trig(3, st, length=1 / 8, cv1=3.5, cond=("DICE", 65))
	s.track(4, "Open hat", swing=56)
	s.hits(4, [3, 7, 11, 15], length=1 / 2, cv1=8)
	s.track(5, "Rim")
	s.trig(5, 4, length=1 / 8, cv1=5, cond=("DICE", 40))
	s.trig(5, 11, length=1 / 8, cv1=7)
	s.trig(5, 16, length=1 / 8, cv1=8, cond=("COUNT", 1, 2))
	s.track(6, "Bass", root="A", scale="Aeolian")
	for st, n in [(3, "A1"), (7, "A1"), (11, "A1"), (15, "C2")]:
		s.trig(6, st, n, length=1 / 2, cv1=6)
	return build(s)

def breakbeat(T):
	# A two-bar break (32-step kick and snare): the kick and snare of the classic breaks, ghost snares (DICE 80 %,
	# CV1 3.5 V), a 32nd-note snare flam at the end of bar 2, 8th hats (accents on the beats) swung 54 %, a crash on
	# the first pass only (FIRST).
	s = Song(T)
	s.track(1, "Kick", length=32)
	s.hits(1, [1, 3, 11, 12, 17, 19, 27], length=1 / 4, cv1=10)
	s.track(2, "Snare", length=32)
	s.hits(2, [5, 13, 21, 29], length=1 / 4, cv1=10)
	s.hits(2, [8, 10, 16, 24, 26], length=1 / 8, cv1=3.5, cond=("DICE", 80))
	s.trig(2, 31, length=1 / 4, pulses=2, dist=1 / 2, cv1=5, cv1_end=9)
	s.track(3, "Hat", swing=54)
	for st in range(1, 17, 2):
		s.trig(3, st, length=1 / 8, cv1=8 if st % 4 == 1 else 5)
	s.hits(3, [8, 14], length=1 / 8, cv1=3, cond=("DICE", 50))
	s.track(4, "Crash", length=32)
	s.trig(4, 1, length=1, cv1=10, cond="FIRST")
	return build(s)

def trap(T):
	# Half-time trap at 140-150 BPM: kick and snare on 3, 8th hats with ratchets (a 32nd pair, a 16th-note triplet,
	# a six-hit roll that speeds up, 80 % each gap, and fades, CV1 7 -> 2 V), an 808 line in C minor gliding into the
	# last note, a tom fill falling 2 semitones a hit every 4th bar (COUNT 1 of 4).
	s = Song(T)
	s.track(1, "Kick")
	s.hits(1, [1, 8, 11], length=1 / 4, cv1=10)
	s.track(2, "Snare")
	s.trig(2, 9, length=1 / 4, cv1=10)
	s.track(3, "Hat")
	for st in range(1, 17, 2):
		if st in (7, 11, 15): continue
		s.trig(3, st, length=1 / 8, cv1=7)
	s.trig(3, 7, length=1 / 8, pulses=2, dist=1 / 2, cv1=6)
	s.trig(3, 11, length=1, pulses=3, dist=0, cv1=6)
	s.trig(3, 15, length=1 / 16, pulses=6, dist=1 / 4, dist_ramp=80.0, cv1=7, cv1_end=2)
	s.track(4, "808", root="C", scale="Aeolian")
	s.trig(4, 1, "C2", length=6, cv1=10)
	s.trig(4, 8, "C2", length=3, cv1=8)
	s.trig(4, 11, "G2", length=3, slide=0.4, cv1=9)
	s.trig(4, 14, "Eb2", length=2.5, slide=0.3, cv1=7)
	s.track(5, "Tom")
	s.trig(5, 13, "G3", length=1 / 4, pulses=4, dist=1, pitch_ramp=-2, cv1=8, cond=("COUNT", 1, 4))
	return build(s)

def euclidean(T):
	# Euclidean polymeter: each track a different length, so they drift: kick E(4,16), rim E(3,8) (tresillo), clave
	# E(5,12), hat E(7,16), tom E(2,5) on two notes, shaker E(5,9). The pattern only repeats after 720 steps.
	s = Song(T)
	parts = [("Kick", 4, 16, 0, "C4"), ("Rim", 3, 8, 0, "C4"), ("Clave", 5, 12, 2, "C4"), ("Hat", 7, 16, 1, "C4"),
		("Tom", 2, 5, 0, None), ("Shaker", 5, 9, 0, "C4")]
	for i, (lab, k, n, rot, nt) in enumerate(parts, 1):
		s.track(i, lab, length=n)
		for j, st in enumerate(euclid(k, n, rot)):
			if lab == "Tom":
				s.trig(i, st, "G3" if j == 0 else "D3", length=1 / 4, cv1=8)
			else:
				s.trig(i, st, nt, length=1 / 8 if lab in ("Hat", "Shaker") else 1 / 4, cv1=10 if j == 0 else 7)
	return build(s)

def acid(T):
	# An acid line in A minor (A1..A2): rests, octave jumps, slides on the notes that glide in (Slide 0.25, the note
	# before them held a whole step so the gate ties), CV1 10 V on the accents (patch it to the VCA or accent input),
	# CV2 a cutoff contour (2..6 V). Track 2: a kick on the beats to play against.
	s = Song(T)
	s.track(1, "Acid", root="A", scale="Aeolian")
	line = [  # step, note, accent, slide, length (steps)
		(1, "A1", 1, 0, 1 / 2), (2, "A1", 0, 0, 1), (3, "A2", 0, 1, 1 / 2), (5, "G1", 0, 0, 1 / 2), (6, "A1", 1, 0, 1),
		(7, "C2", 0, 1, 1 / 2), (9, "A1", 0, 0, 1), (10, "E2", 1, 1, 1 / 2), (11, "D2", 0, 0, 1 / 2), (13, "A1", 1, 0, 1),
		(14, "G2", 0, 1, 1 / 2), (15, "F2", 0, 0, 1), (16, "E2", 0, 1, 1 / 2)]
	for k, (st, n, acc, sl, ln) in enumerate(line):
		s.trig(1, st, n, length=ln, slide=0.25 if sl else 0.0, cv1=10 if acc else 0.0, cv2=round(2 + 4 * ((k * 5) % 7) / 6, 2))
	s.track(2, "Kick")
	s.hits(2, [1, 5, 9, 13], length=1 / 4, cv1=10)
	return build(s)

def arpeggio(T):
	# An arpeggio in C minor (Dorian), 12 steps up and down C3..C5 over a 16-step bass, so the arp starts on another
	# note of the bar each time (3 against 4); the bass C2, C2, Ab1, Bb1 (the arp turns into Abmaj7 and Bb6 over it);
	# a slow counter-melody at x1/2 over 12 steps, gliding, some notes on DICE.
	s = Song(T)
	s.track(1, "Arp", length=12, root="C", scale="Dorian")
	arp = [("C3", 9), ("G3", 6), ("C4", 7), ("Eb4", 6), ("G4", 7), ("Bb4", 8), ("C5", 10), ("Bb4", 7), ("G4", 6), ("Eb4", 7),
		("C4", 6), ("G3", 7)]
	for st, (n, c) in enumerate(arp, 1):
		s.trig(1, st, n, length=1 / 2, cv1=c)
	s.track(2, "Bass", root="C", scale="Dorian")
	s.trig(2, 1, "C2", length=3.5, cv1=8)
	s.trig(2, 5, "C2", length=3.5, cv1=6)
	s.trig(2, 9, "Ab1", length=3.5, cv1=8)
	s.trig(2, 13, "Bb1", length=3.5, cv1=6)
	s.track(3, "Counter", length=12, speed="x1/2", root="C", scale="Dorian")
	for st, n, c in [(1, "G4", None), (3, "F4", ("DICE", 70)), (4, "Eb4", None), (7, "D4", ("DICE", 60)), (8, "C4", None),
			(10, "Bb3", ("DICE", 50)), (11, "D4", None)]:
		s.trig(3, st, n, length=1.5, slide=0.15, cv1=6, cond=c)
	return build(s)

def generative(T):
	# A melody that is never the same: a D minor pentatonic lead read in random walk, every note on its own DICE
	# chance, a few slides; a bass that chooses between two notes (DICE 50 then !PRE: one or the other, never both)
	# and changes every other bar (COUNT 1 of 2, !PRE for the other bar); track 3 answers the bass (NEI: plays when
	# the track above's last condition played).
	s = Song(T)
	s.track(1, "Lead", read=4, root="D", scale="Pentatonic m7")
	lead = [("D4", 95), ("F4", 70), ("G4", 60), ("A4", 80), ("C5", 50), ("D5", 75), ("A4", 40), ("G4", 65),
		("F4", 85), ("D4", 55), ("C4", 45), ("D4", 90), ("F4", 50), ("A4", 70), ("G4", 35), ("D5", 60)]
	for st, (n, p) in enumerate(lead, 1):
		s.trig(1, st, n, length=3 / 4, slide=0.2 if st % 5 == 3 else 0.0, cv1=4 + (st * 7) % 6, cond=("DICE", p))
	s.track(2, "Bass", root="D", scale="Pentatonic m7")
	s.trig(2, 1, "D2", length=2, cv1=9)
	s.trig(2, 7, "A1", length=1 / 2, cv1=7, cond=("DICE", 50))
	s.trig(2, 8, "C2", length=1 / 2, cv1=7, cond="!PRE")
	s.trig(2, 11, "F2", length=2, cv1=8, cond=("COUNT", 1, 2))
	s.trig(2, 12, "G2", length=2, cv1=8, cond="!PRE")
	s.track(3, "Answer", root="D", scale="Pentatonic m7")
	s.trig(3, 9, "C5", length=1, cv1=6, cond="NEI")
	return build(s)

def presets(T):
	return [
		("House groove", *house(T)),
		("Breakbeat", *breakbeat(T)),
		("Trap hats and ratchets", *trap(T)),
		("Euclidean polymeter", *euclidean(T)),
		("Acid line", *acid(T)),
		("Arpeggio 3 against 4", *arpeggio(T)),
		("Generative melody", *generative(T)),
	]

if __name__ == "__main__":
	preset_kit.write("ZOUMAI", "ZOUMAI.cpp", presets(ZOUMAI))

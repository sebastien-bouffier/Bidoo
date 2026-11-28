# Factory presets for ForK (src/FORK.cpp): python tools/presets/fork.py writes presets/ForK/.
# ForK sings a vowel: four formants, each a frequency (F_PARAM+k) and a level (A_PARAM+k), over a pulse train at the
# pitch (PITCH: semitones from C4, -54..54, added to V/O). PRESET_PARAM is the "next vowel" button (left at 0); the
# data "preset" is the index (0..8) of the module's own vowel table (FORK::PF, PA) the button last loaded: the next
# press loads index + 1. A preset built on one of those vowels stores its index, so Presets goes on from there; a
# custom vowel stores the index of the table vowel it was made from.
# The table, as the module has it (F1..F4 in Hz, G1..G4):
VOWELS = [
	("A (father)",  (730, 1090, 2440, 3400), (1.0, 2.0, 0.3, 0.2)),
	("I (see)",     (200, 2100, 3100, 4700), (0.5, 0.5, 0.15, 0.1)),
	("O (more)",    (400, 900, 2300, 3000),  (1.0, 0.7, 0.2, 0.2)),
	("U (French)",  (250, 1700, 2100, 3300), (1.0, 0.7, 0.4, 0.3)),
	("OU (moon)",   (190, 800, 2000, 3400),  (0.7, 0.35, 0.1, 0.1)),
	("E (day)",     (350, 1900, 2500, 3700), (1.0, 0.3, 0.3, 0.1)),
	("E (bed)",     (550, 1600, 2250, 3200), (1.0, 0.5, 0.7, 0.3)),
	("AW (law)",    (550, 850, 1900, 3000),  (0.3, 1.0, 0.2, 0.2)),
	("UH (but)",    (450, 1100, 1500, 3000), (1.0, 0.7, 0.2, 0.3)),
]
# Knob ranges (FORK::F_MIN, F_MAX, A_MAX): F1 190-730, F2 800-2100, F3 1500-3100, F4 3000-4700 Hz; G 1, 2, 0.7, 0.3.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preset_kit

def p(pitch, vowel, f=None, a=None):
	# pitch in semitones from C4; the formants of table vowel `vowel`, or given ones (f, a)
	vf, va = VOWELS[vowel][1], VOWELS[vowel][2]
	f, a = f or vf, a or va
	d = {"PITCH_PARAM": pitch, "PRESET_PARAM": 0}
	for k in range(4):
		suffix = "" if k == 0 else "+%d" % k
		d["F_PARAM" + suffix] = f[k]
		d["A_PARAM" + suffix] = a[k]
	return d, {"preset": vowel}

def preset(name, pitch, vowel, f=None, a=None):
	values, data = p(pitch, vowel, f, a)
	return (name, values, data)

PRESETS = [
	# The module's first vowel, an open "ah" at C4: the choir pad sound, polyphonic V/O through a VCA and a slow ADSR.
	preset("Ah choir", 0, 0),
	# A round "oh" an octave down (C3): a tenor voice for slow melodies.
	preset("Oh tenor", -12, 2),
	# A dark "oo" two octaves down (C2): the talking bass; sweep F1 and F2 with an envelope to make it speak.
	preset("Oo bass", -24, 4),
	# A bright, thin "ee" at C4, F2 high: cuts through as a lead.
	preset("Ee lead", 0, 1),
	# The closed "ay" (French e) at C4: between "ee" and "ah", a soft, open lead.
	preset("Ay voice", 0, 5),
	# "aw" an octave down: F2 stronger than F1, a hollow, horn-like bass voice.
	preset("Aw horn", -12, 7),
	# A custom vowel from "eh": F1 and F2 low and level, F3 and F4 at the top of their range and full: a nasal, reedy
	# buzz like an oboe or a muted brass, an octave up (C5).
	preset("Nasal reed", 12, 6, f=(500, 1400, 2900, 4200), a=(0.6, 1.0, 0.7, 0.3)),
	# A custom vowel from "uh": F1 at its bottom, F2 full and low, F3 and F4 quiet: a deep, throaty growl at C1 for
	# drones (the pulses of a low note give it a buzz).
	preset("Throat drone", -36, 8, f=(250, 900, 1600, 3000), a=(1.0, 1.6, 0.15, 0.05)),
]

if __name__ == "__main__":
	preset_kit.write("ForK", "FORK.cpp", PRESETS)

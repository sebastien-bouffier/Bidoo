# Factory presets for bordL (src/BORDL.cpp, engine in src/MetroSeq.hpp): python tools/presets/bordl.py writes presets/bordL/.
# bordL is dTrOY plus, per stage, a gate probability (0..1), a random pitch (0..1 = up to +-5 V, before the quantizer),
# an accent (0..10 V on the V output) and a random accent (0..1 = up to +-5 V, kept in 0..10 V); all drawn once when a
# stage starts. The pattern and panel rules are dTrOY's (see tools/presets/dtroy.py, whose helpers this script uses):
# the panel knobs mirror pattern 1, "running" starts it, CLOCK 2 = sixteenths at 120 BPM on the internal clock.
# Random pitch is strong: 0.05 is +-0.25 V, a fourth either way, so the generative presets keep it at 0.02..0.1.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preset_kit
from dtroy import pattern, preset, stage as S, REST, SINGLE, MULTIPLE, HOLD

def P(title, patterns, clock=2.0):
	return preset(title, patterns, clock, extended=True)

PRESETS = [
	# Acid bass in A minor (Aeolian), one bar of sixteenths (pulses count, 16): V at 8 V on the accented notes and 0
	# elsewhere (patch V to a filter's cutoff or aCiDe's accent), slides into the octave jumps; every gate plays.
	P("Acid accents", [pattern([
		S("A1", 2, acc=8), S("A2", 1, slide=True), S("C2", 1), S("A1", 2, MULTIPLE, acc=8), S("E2", 2, slide=True, acc=4),
		S("G2", 2), S("A2", 3, HOLD, slide=True, acc=8), S("D2", 3)], count=1, steps=16, root=9, scale=6, gate=0.4, slide=0.3)]),
	# Generative lead in C minor pentatonic over 16 steps (stages 1-8 twice, each pass drawing again): a few stages
	# with a little random pitch (the quantizer keeps it in the scale), probabilities 50 to 100 %, a mid V with some
	# random on it.
	P("Generative pentatonic", [pattern([
		S("C4", prob=1.0, acc=6), S("Eb4", prob=0.7, rnd=0.05, acc=4, accrnd=0.2), S("G4", prob=0.8, acc=4, accrnd=0.2),
		S("Bb4", prob=0.5, rnd=0.08, acc=3, accrnd=0.3), S("C5", prob=0.9, slide=True, acc=6),
		S("G4", prob=0.6, rnd=0.05, acc=4, accrnd=0.2), S("F4", prob=0.7, acc=4, accrnd=0.2), S("Eb4", prob=0.5, rnd=0.1, acc=3, accrnd=0.3)],
		steps=16, root=0, scale=25, gate=0.5)]),
	# Bass in E Dorian with ratchets that come and go: the 3- and 4-pulse multiple stages play 60 % and 50 % of the
	# time (a whole ratchet or nothing), the downbeats always play, accented.
	P("Ghost ratchets", [pattern([
		S("E2", acc=9), S("E2", 3, MULTIPLE, prob=0.6, acc=2), S("B2", acc=5), S("D3", 2, prob=0.8, slide=True, acc=3),
		S("E2", acc=9), S("G2", 4, MULTIPLE, prob=0.5, acc=2), S("A2", 2, prob=0.8, acc=4), S("B1", 2, HOLD, acc=6)],
		root=4, scale=2, gate=0.45)]),
	# Steady arpeggio in D Dorian, every note playing, but V draws a new value per note (5 V +- 2 V): patch V to a
	# filter cutoff for a line that breathes.
	P("Breathing arp", [pattern([
		S(n, acc=5, accrnd=0.4) for n in ("D3", "F3", "A3", "C4", "E4", "C4", "A3", "F3")], root=2, scale=2, gate=0.6)]),
	# Random order in G Mixolydian (scale 5): each step jumps to any stage; held one- and two-pulse notes at 80 %
	# probability, slight random pitch on two stages, random V.
	P("Random steps", [pattern([
		S("G3", 2, HOLD, prob=0.8, acc=5, accrnd=0.3), S("B3", 1, SINGLE, prob=0.8, acc=3, accrnd=0.3),
		S("D4", 2, HOLD, prob=0.8, rnd=0.03, acc=6, accrnd=0.3), S("F4", 1, SINGLE, prob=0.8, acc=3, accrnd=0.3),
		S("G4", 2, HOLD, prob=0.8, slide=True, acc=7, accrnd=0.3), S("A4", 1, SINGLE, prob=0.8, rnd=0.03, acc=3, accrnd=0.3),
		S("C5", 2, HOLD, prob=0.8, acc=5, accrnd=0.3), S("E4", 1, SINGLE, prob=0.8, acc=3, accrnd=0.3)],
		play=3, root=7, scale=5, gate=0.7, slide=0.4)]),
	# Sparse bells in G (Scottish pentatonic, scale 43), high register: 2- to 4-pulse stages with short single gates
	# that play 30 to 60 % of the time, a touch of random pitch; for a plucky voice with a long release.
	P("Sparse bells", [pattern([
		S("G4", 2, prob=0.6, acc=7), S("D5", 3, prob=0.4, rnd=0.03, acc=4), S("B4", 2, prob=0.5, acc=5),
		S("E5", 4, prob=0.3, rnd=0.04, acc=3), S("A4", 2, prob=0.5, acc=5), S("G5", 3, prob=0.35, rnd=0.03, acc=6),
		S("D5", 2, prob=0.5, acc=4), S("C5", 4, prob=0.4, rnd=0.04, acc=3)], root=7, scale=43, gate=0.3)]),
]

if __name__ == "__main__":
	preset_kit.write("bordL", "BORDL.cpp", PRESETS)

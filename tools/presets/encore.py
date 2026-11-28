# Factory presets for enCORE (src/ENCORE.cpp, the ZouSeq.hpp engine with EncoreTraits): python tools/presets/encore.py
# writes presets/ENCORE/. Same builder as tools/presets/zoumai.py (see it for the data); enCORE saves times as integer
# ticks, 96 a step, so triplets and 32nds are exact: these presets lean on that (trims of a few ticks, swing 66.67 %
# = a triplet shuffle, triplet ratchets). Pulse distances go up to one step here.
# Clock: 32 clocks a step at x1 (tOCAnTe's /128 for 16th-note steps). Run is not saved: patch the clock, press RUN.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preset_kit
from zoumai import Traits, Song, build, euclid

ENCORE = Traits("ENCORE", "ENCORE.cpp", 96, 32, False, 3, 6144, 45, 96, 3, 95)

def techno(T):
	# Techno 4x4 with micro-timing: kick on the beats, clap laid back 4 ticks (1/24 step), off-beat open hats, 16th
	# closed hats with the even ones pushed 3 ticks early and on DICE 70 %, a ride on DICE 35 %, and a triplet tom
	# ratchet (3 hits in a step) every other bar (COUNT 1 of 2).
	s = Song(T)
	s.track(1, "Kick")
	s.hits(1, [1, 5, 9, 13], length=1 / 4, cv1=10)
	s.track(2, "Clap")
	s.hits(2, [5, 13], length=1 / 4, trim=4 / 96, cv1=9)
	s.track(3, "Closed hat")
	for st in range(1, 17):
		if st in (3, 7, 11, 15): continue
		if st % 2: s.trig(3, st, length=1 / 8, cv1=7)
		else: s.trig(3, st, length=1 / 8, trim=-3 / 96, cv1=4, cond=("DICE", 70))
	s.track(4, "Open hat")
	s.hits(4, [3, 7, 11, 15], length=1 / 2, cv1=8)
	s.track(5, "Ride")
	for st in range(1, 17):
		s.trig(5, st, length=1 / 8, cv1=6 if st % 4 == 3 else 3, cond=("DICE", 35))
	s.track(6, "Tom")
	s.trig(6, 15, "G3", length=1, pulses=3, dist=0, pitch_ramp=-3, cv1=8, cond=("COUNT", 1, 2))
	return build(s)

def garage(T):
	# A 2-step garage shuffle: swing 66.67 % on the hats and snare (exactly a triplet shuffle on the 96-tick grid),
	# kick on 1 and the "and" of 3 with a DICE pickup, snare on 2 and 4, 16th hats with DICE ghosts, a rim that
	# drags 6 ticks.
	s = Song(T)
	s.track(1, "Kick")
	s.trig(1, 1, length=1 / 4, cv1=10)
	s.trig(1, 11, length=1 / 4, cv1=9)
	s.trig(1, 16, length=1 / 4, cv1=6, cond=("DICE", 50))
	s.track(2, "Snare", swing=66.67)
	s.hits(2, [5, 13], length=1 / 4, cv1=10)
	s.trig(2, 8, length=1 / 8, cv1=3, cond=("DICE", 60))
	s.track(3, "Hat", swing=66.67)
	for st in range(1, 17):
		if st % 2: s.trig(3, st, length=1 / 8, cv1=8 if st % 4 == 3 else 6)
		else: s.trig(3, st, length=1 / 8, cv1=3, cond=("DICE", 75))
	s.track(4, "Rim", swing=66.67)
	s.hits(4, [4, 10, 14], length=1 / 8, trim=6 / 96, cv1=6)
	return build(s)

def trap(T):
	# Triplet trap, half time: kick, snare on 3, 8th hats with exact triplet ratchets (3 hits a step, 32 ticks apart),
	# a 32nd-triplet roll of 6 that slows down (125 % each gap) and fades, an 808 line in F minor with glides.
	s = Song(T)
	s.track(1, "Kick")
	s.hits(1, [1, 4, 11], length=1 / 4, cv1=10)
	s.track(2, "Snare")
	s.trig(2, 9, length=1 / 4, cv1=10)
	s.track(3, "Hat")
	for st in [1, 3, 5, 9, 13]:
		s.trig(3, st, length=1 / 8, cv1=7)
	s.trig(3, 7, length=1 / 12, pulses=3, dist=1 / 3, cv1=6)
	s.trig(3, 11, length=1 / 12, pulses=3, dist=1 / 3, cv1=5, cv1_end=8)
	s.trig(3, 15, length=1 / 24, pulses=6, dist=1 / 6, dist_ramp=125.0, cv1=8, cv1_end=3)
	s.track(4, "808", root="F", scale="Aeolian")
	s.trig(4, 1, "F1", length=3, cv1=10)
	s.trig(4, 4, "F1", length=6, cv1=8)
	s.trig(4, 11, "C2", length=2, slide=0.4, cv1=9)
	s.trig(4, 14, "Ab1", length=2.5, slide=0.3, cv1=8)
	return build(s)

def speeds(T):
	# Tempo ratios: every track at another speed: kick x1, conga x3/2 (3 steps in 2 clocks' time), low tom x1/2, shaker
	# x2 on 32nds, cowbell x3/4; euclidean hits on each, lengths 16, 12, 8, 32, 9.
	s = Song(T)
	parts = [("Kick", "x1", 4, 16, "C4"), ("Conga", "x3/2", 5, 12, "G4"), ("Low tom", "x1/2", 3, 8, "C3"),
		("Shaker", "x2", 13, 32, "C4"), ("Cowbell", "x3/4", 4, 9, "E4")]
	for i, (lab, sp, k, n, nt) in enumerate(parts, 1):
		s.track(i, lab, length=n, speed=sp)
		for j, st in enumerate(euclid(k, n)):
			s.trig(i, st, nt, length=1 / 4, cv1=10 if j == 0 else 6)
	return build(s)

def fills(T):
	# Conditions at work: a crash on the first pass only (FIRST), snare on 2, and on 4 except in fill (!FILL), a
	# fill (FILL: press FILL or patch a gate into it) of snare rolls, 2, 4 and 6 hits a step, rising 1 semitone a hit,
	# an open hat every 4th bar (COUNT 1 of 4), closed hats on DICE 60 % with a PRE hat right after each (plays when
	# the one before did).
	s = Song(T)
	s.track(1, "Kick")
	s.hits(1, [1, 5, 9, 13], length=1 / 4, cv1=10)
	s.track(2, "Snare")
	s.trig(2, 5, length=1 / 4, cv1=10)
	s.trig(2, 13, length=1 / 4, cv1=10, cond="!FILL")
	s.trig(2, 12, "C4", length=1, pulses=2, dist=0, pitch_ramp=1, cv1=6, cond="FILL")
	s.trig(2, 14, "D4", length=1, pulses=4, dist=0, pitch_ramp=1, cv1=6, cv1_end=9, cond="FILL")
	s.trig(2, 15, "F#4", length=1, pulses=6, dist=0, pitch_ramp=1, cv1=7, cv1_end=10, cond="FILL")
	s.track(3, "Closed hat")
	for st in range(1, 17, 4):
		s.trig(3, st + 1, length=1 / 8, cv1=7)
		s.trig(3, st + 2, length=1 / 8, cv1=5, cond=("DICE", 60))
		s.trig(3, st + 3, length=1 / 8, cv1=4, cond="PRE")
	s.track(4, "Open hat")
	s.trig(4, 15, length=1, cv1=9, cond=("COUNT", 1, 4))
	s.track(5, "Crash")
	s.trig(5, 1, length=2, cv1=10, cond="FIRST")
	return build(s)

def jungle(T):
	# A jungle break over two bars (32 steps, for 160-175 BPM): syncopated kicks, snares on 2 and 4, ghost snares on
	# DICE dragged 4 ticks, a snare triplet ratchet at the end of bar 2, 8th rides swung 58 %.
	s = Song(T)
	s.track(1, "Kick", length=32)
	s.hits(1, [1, 11, 17, 19, 26], length=1 / 4, cv1=10)
	s.track(2, "Snare", length=32)
	s.hits(2, [5, 13, 21, 29], length=1 / 4, cv1=10)
	s.hits(2, [8, 15, 24, 27], length=1 / 8, trim=4 / 96, cv1=3.5, cond=("DICE", 75))
	s.trig(2, 31, length=1 / 6, pulses=3, dist=1 / 3, cv1=5, cv1_end=9)
	s.track(3, "Ride", swing=58)
	for st in range(1, 17, 2):
		s.trig(3, st, length=1 / 8, cv1=7 if st % 4 == 1 else 4)
	return build(s)

def presets(T):
	return [
		("Techno micro timing", *techno(T)),
		("Garage shuffle", *garage(T)),
		("Triplet trap", *trap(T)),
		("Speed ratios", *speeds(T)),
		("Fills and conditions", *fills(T)),
		("Jungle break", *jungle(T)),
	]

if __name__ == "__main__":
	preset_kit.write("ENCORE", "ENCORE.cpp", presets(ENCORE))

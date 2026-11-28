# Factory presets for BanCoù (src/BANCAU.cpp): python tools/presets/bancau.py writes presets/BanCau/.
# Two Serge-style slopes, each rising 0 to 10 V then falling back. Time knobs (BANCAU::knobTime): 0.2 ms * 10^(5 k)
# for a full 0-10 V segment, k = 0..1 (0.2 ms to 20 s); t() below gives the knob for a time. The CV amounts are
# -1..1 (octaves per volt), the EXP and CYC switches 0/1. In cycle mode the period is rise + fall.
# Nothing is saved beyond the params: data is {}.
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preset_kit

def t(seconds):
	# the time knob for a full segment of `seconds`
	return math.log10(seconds / 0.0002) / 5.0

def side(n, rise, fall, rexp=0, fexp=0, cycle=0, risecv=0.0, fallcv=0.0):
	# one side's params, n = 1 or 2; rise and fall in seconds
	return {
		"RISE%d_PARAM" % n: t(rise), "FALL%d_PARAM" % n: t(fall),
		"RISECV%d_PARAM" % n: risecv, "FALLCV%d_PARAM" % n: fallcv,
		"RISEEXP%d_PARAM" % n: rexp, "FALLEXP%d_PARAM" % n: fexp, "CYCLE%d_PARAM" % n: cycle,
	}

def p(a, b):
	d = dict(a)
	d.update(b)
	return d

C4 = 261.626

PRESETS = [
	# Two percussive AD envelopes for TRIG: side 1 a 1 ms attack and a 400 ms curved decay (kick, tom, pluck VCA);
	# side 2 snappier, 1 ms and 80 ms (hats, clicks, a filter blip).
	("Percussive AD", p(side(1, 0.001, 0.4, fexp=1), side(2, 0.001, 0.08, fexp=1))),
	# Two slow swells for pads: side 1 a 2 s curved attack and a 4 s curved release, side 2 0.8 s and 1.5 s. A
	# trigger plays the whole swell (no sustain).
	("Pad swells", p(side(1, 2.0, 4.0, rexp=1, fexp=1), side(2, 0.8, 1.5, rexp=1, fexp=1))),
	# Cycling, linear, equal times: triangle LFOs; side 1 at 0.25 Hz (2 s + 2 s), side 2 at 0.1 Hz (5 s + 5 s).
	# GATE gives side 1's square, NORM side 2's triangle centred on 0 V.
	("Slow triangles", p(side(1, 2.0, 2.0, cycle=1), side(2, 5.0, 5.0, cycle=1))),
	# Cycling ramps at 0.5 Hz: side 1 rises for 2 s and drops (0.2 ms), a ramp up; side 2 jumps up and falls for
	# 2 s, a ramp down (NORM: -5 to 5 V).
	("Ramps up and down", p(side(1, 2.0, 0.0002, cycle=1), side(2, 0.0002, 2.0, cycle=1))),
	# A curved LFO with shape CV: 0.5 s + 0.5 s, curved rise and fall (rounder than a triangle), at about 1 Hz;
	# the rise CV amount is +100 % and the fall's -100 %, so a voltage into the middle (both) input skews it, +1 V
	# halving the rise and doubling the fall. Side 2 the same, unskewed, at half the speed.
	("Skewed LFO", p(side(1, 0.5, 0.5, rexp=1, fexp=1, cycle=1, risecv=1.0, fallcv=-1.0),
	                 side(2, 1.0, 1.0, rexp=1, fexp=1, cycle=1))),
	# Two oscillators at C4 for VO (1V/oct): side 1 a triangle (rise = fall, a period of 1 / 261.6 Hz), side 2 a
	# downward saw (a 0.2 ms rise). GATE is side 1's square.
	("C4 oscillators", p(side(1, 0.5 / C4, 0.5 / C4, cycle=1), side(2, 0.0002, 1.0 / C4 - 0.0002, cycle=1))),
	# Slew limiters for IN, cycle off: side 1 a linear portamento of 0.5 s per 10 V (50 ms per octave of V/oct);
	# side 2 a curved lag, 1 s up and 3 s down, to smooth a stepped CV or follow an envelope.
	("Glide and lag", p(side(1, 0.5, 0.5), side(2, 1.0, 3.0, rexp=1, fexp=1))),
	# A trigger delay into an envelope: side 1 rises for 250 ms and drops at once, its EOC pulsing at the end (and
	# ignoring triggers on the way up, a clock divider at fast clocks); patch EOC 1 into TRIG 2 for side 2's AD
	# (5 ms, 300 ms curved) a quarter second after each trigger.
	("Delayed AD", p(side(1, 0.25, 0.0002), side(2, 0.005, 0.3, fexp=1))),
]

if __name__ == "__main__":
	preset_kit.write("BanCau", "BANCAU.cpp", [(n, v, None) for n, v in PRESETS], data={})

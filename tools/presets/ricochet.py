# Factory presets for rICOcHEt (src/RICOCHET.cpp): python tools/presets/ricochet.py writes presets/RICOCHET/
# TIME: unclocked 2 ms * 4000^knob (2 ms .. 8 s); with a clock, note value number floor(knob * 18) of
# 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/2T 1/4. 1/2 1/2. 1/1 1/1. 2/1 3/1 4/1 (of a beat = clockPpqn pulses).
# FEEDBACK: loop gain = knob * 1.15, unity at 0.87 (above it every type self-oscillates, bounded).
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from preset_kit import write

DIGITAL, TAPE, BBD, LOFI, DIFFUSE, REVERSE = range(6)

def time_knob(seconds):
	# the free time's knob for a delay in seconds
	return round(math.log(seconds / 0.002) / math.log(4000.0), 4)

def note_knob(index):
	# the middle of a note value's slice of the knob (index in the list above)
	return round((index + 0.5) / 18.0, 4)

DEFAULTS = {
	"TYPE_PARAM": DIGITAL,
	"TIME_PARAM": 0.62,
	"FEEDBACK_PARAM": 0.45,
	"MIX_PARAM": 0.35,
	"TONE_PARAM": 0.0,
	"CHARACTER_PARAM": 0.0,
	"SPREAD_PARAM": 0.0,
	"MODE_PARAM": 0,      # 0 stereo, 1 ping-pong
	"FREEZE_PARAM": 0,
}

def p(**kw):
	d = dict(DEFAULTS)
	d.update({k + "_PARAM": v for k, v in kw.items()})
	return d

# a missing clockPpqn loads as 1 (patches from before 2.7.9), so every preset sets the default 4 (a 16th-note clock)
DATA = {"clockPpqn": 4}

PRESETS = [
	# Tape slapback: one quick, slightly saturated repeat behind a voice or guitar. 110 ms free (a quarter note
	# with a clock), low feedback for one or two repeats, a little wear and a darker tone like an old echo unit.
	("Tape slapback", p(TYPE=TAPE, TIME=time_knob(0.110), FEEDBACK=0.15, MIX=0.3, TONE=-0.2, CHARACTER=0.3), DATA),
	# Dotted eighth, for a clock (16ths, 4 pulses per beat): the rhythmic digital echo that fills the gaps of
	# eighth-note lines. Clean repeats, a slight roll-off, spread to widen. Unclocked it is a 63 ms doubler.
	("Dotted eighth clock", p(TYPE=DIGITAL, TIME=note_knob(7), FEEDBACK=0.4, MIX=0.3, TONE=-0.15, SPREAD=0.15), DATA),
	# BBD ping-pong: a warm analog bucket brigade bouncing left / right, 290 ms free (a dotted quarter clocked);
	# CHARACTER for chorus-like wobble and grit, spread so the sides never line up, tone a bit dark.
	("BBD ping-pong", p(TYPE=BBD, TIME=0.6, FEEDBACK=0.5, MIX=0.35, TONE=-0.3, CHARACTER=0.4, SPREAD=0.3, MODE=1), DATA),
	# Lo-fi sampler echo: each repeat comes back crunchier through the old sampler's bits and rate, 160 ms
	# (a half-note triplet clocked); a thin tone keeps the grit from muddying the mix.
	("Lo-fi crunch", p(TYPE=LOFI, TIME=time_knob(0.160), FEEDBACK=0.45, MIX=0.4, TONE=0.3, CHARACTER=0.6), DATA),
	# Dub tape feedback: long, dark, worn repeats that ring for many bars (feedback 0.82, just under unity 0.87;
	# turn it past 0.87 for the classic runaway, bounded by the tape's saturation). 477 ms free (a half note
	# clocked); ride TIME for pitch swoops and FREEZE for a held loop.
	("Dub feedback", p(TYPE=TAPE, TIME=0.66, FEEDBACK=0.82, MIX=0.4, TONE=-0.45, CHARACTER=0.5, SPREAD=-0.1), DATA),
]

write("RICOCHET", "RICOCHET.cpp", PRESETS)

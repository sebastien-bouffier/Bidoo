# Factory presets for lATe (src/LATE.cpp): python tools/presets/late.py writes presets/lATe/
# Swing: every even clock is delayed by SWING/10 of the interval before it. The knob reads 0..9 and shows
# 50 + 5 * value %: 50 % straight, 66.7 % (3.333) a triplet shuffle, 75 % (5) a dotted swing. CVCOEFF scales the swing
# CV: 1 V at 100 % moves the knob by 1 (5 %). Nothing is in data.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from preset_kit import write

DEFAULTS = {
	"SWING_PARAM": 0.0,      # 0 .. 9 (50 .. 95 %)
	"CVCOEFF_PARAM": 0.0,    # -1 .. 1
}

def p(**kw):
	d = dict(DEFAULTS)
	d.update({k + "_PARAM": v for k, v in kw.items()})
	return d

def swing(percent):
	return round((percent - 50.0) / 5.0, 6)

PRESETS = [
	# Light swing, 55 %: the off-beat 16ths are a tenth of a 16th late, a loose human feel for hats and basslines.
	("Light swing 55", p(SWING=swing(55)), None),
	# Triplet shuffle, 66.7 %: the even clock lands on the last third of the pair, exactly a triplet shuffle (MPC
	# swing 66), hard to hit on the knob by hand.
	("Triplet shuffle 66", p(SWING=swing(200.0 / 3.0)), None),
	# Hard swing, 75 %: the even clock lands on the last quarter of the pair, a dotted 16th-32nd skip.
	("Hard swing 75", p(SWING=swing(75)), None),
	# Drifting swing: 60 % with the swing CV at 40 %: a +-5 V slow LFO into CV moves it between 50 and 70 %, so
	# the groove tightens and loosens along the song. Without CV it is a medium 60 % swing.
	("Drifting swing", p(SWING=swing(60), CVCOEFF=0.4), None),
]

write("lATe", "LATE.cpp", PRESETS)

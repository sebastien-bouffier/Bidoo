# Factory presets for ChUTE (src/CHUTE.cpp): python tools/presets/chute.py writes presets/ChUTE/
# A ball dropped from ALTITUDE (m) under GRAVITY (m/s², Moon 1.62 .. Neptune 11.15), keeping COR of its speed at each
# impact. A drop from h takes sqrt(2h/g) s; the whole bounce lasts that times (1 + 2 COR / (1 - COR)). FALL opens a
# gate on each impact. Nothing is in data; the ball only moves when TRIG or the drop button drops it.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from preset_kit import write

DEFAULTS = {
	"ALTITUDE_PARAM": 1.0,   # 0.01 .. 3 m
	"GRAVITY_PARAM": 9.798,  # 1.622 .. 11.15 m/s²
	"COR_PARAM": 0.69,       # 0 .. 1
	"RUN_PARAM": 0,          # momentary drop button
}

def p(**kw):
	d = dict(DEFAULTS)
	d.update({k + "_PARAM": v for k, v in kw.items()})
	return d

PRESETS = [
	# Bouncing ball: the default, a 1 m drop on Earth with a rubber ball. First impact after 0.45 s, about 20
	# accelerating hits over 2.5 s: a ball rolling to rest, on a drum or an envelope.
	("Bouncing ball", p(), None),
	# Ping-pong roll: a low drop (25 cm) of a very elastic ball (0.88). Hits start 0.4 s apart and tighten into a
	# buzz over 3.5 s (about 30 hits): a snare roll or a ratchet that speeds up, one per trigger.
	("Ping-pong roll", p(ALTITUDE=0.25, COR=0.88), None),
	# Moon drop: 1.5 m in lunar gravity, a dull ball (0.6). First impact after 1.4 s, then a few slow, spaced hits
	# over 5 s: sparse triggers for ambient patches; ALT and TOP give long, slow decaying CVs.
	("Moon drop", p(ALTITUDE=1.5, GRAVITY=1.622, COR=0.6), None),
	# Falling delay: no bounce at all (COR 0), a single impact 375 ms after the drop, a dotted 8th at 120 BPM
	# (0.689 m on Earth): a trigger delay; ALT is a 375 ms falling curve for a pitch drop or a fade.
	("Falling delay", p(ALTITUDE=0.689, COR=0.0), None),
]

write("ChUTE", "CHUTE.cpp", PRESETS)

# Factory presets for baR (src/BAR.cpp, src/BarCompressor.hpp): python tools/presets/bar.py writes presets/baR/
# Levels: 5 V = 0 dBFS, peak detection (stereo linked). RATIO 1..20, the top (20) is the limit (infinite ratio),
# with lookahead it catches peaks ahead of the delayed audio. LOOKAHEAD is in % of the attack time.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from preset_kit import write

DEFAULTS = {
	"THRESHOLD_PARAM": 0.0,    # -93.6 .. 0 dB
	"RATIO_PARAM": 1.0,        # 1 .. 20 (20: limit)
	"ATTACK_PARAM": 10.0,      # 1 .. 100 ms
	"RELEASE_PARAM": 10.0,     # 1 .. 300 ms
	"KNEE_PARAM": 6.0,         # 0 .. 24 dB
	"MAKEUP_PARAM": 0.0,       # 0 .. 60 dB
	"MIX_PARAM": 1.0,          # 0 .. 1
	"LOOKAHEAD_PARAM": 0.0,    # 0 .. 200 % of the attack
	"BYPASS_PARAM": 0,         # momentary button; the bypass state is in data
}

def p(**kw):
	d = dict(DEFAULTS)
	d.update({k + "_PARAM": v for k, v in kw.items()})
	return d

# the bypass toggle lives in data and a missing key reads as off: say so explicitly, a preset never loads bypassed
DATA = {"bypass": False}

PRESETS = [
	# Drum bus glue: gentle 4:1 on a full drum mix peaking near 5 V, a few dB of reduction on the hits; 30 ms
	# attack lets the transients through, 120 ms release breathes with the groove, soft knee, +3 dB back.
	("Drum bus glue", p(THRESHOLD=-10.0, RATIO=4.0, ATTACK=30.0, RELEASE=120.0, KNEE=6.0, MAKEUP=3.0), DATA),
	# Vocal leveller: low threshold, gentle 3:1 over a wide 12 dB knee so the reduction grows smoothly with the
	# level; fast 5 ms attack and slow 200 ms release ride the phrase rather than each syllable; +6 dB back.
	("Vocal leveller", p(THRESHOLD=-20.0, RATIO=3.0, ATTACK=5.0, RELEASE=200.0, KNEE=12.0, MAKEUP=6.0), DATA),
	# Sidechain pump: patch a kick into SC; the pad or bass in IN ducks hard on every kick (8:1, hard knee,
	# 1 ms attack) and swells back over 250 ms. Without SC it just squashes its own input.
	("Sidechain pump", p(THRESHOLD=-24.0, RATIO=8.0, ATTACK=1.0, RELEASE=250.0, KNEE=0.0), DATA),
	# Brickwall-ish limiter: RATIO at the top (the limit) with 100 % lookahead (2 ms) so the catcher holds peaks
	# at -6 dBFS before they arrive; +5 dB make-up puts the ceiling at -1 dBFS (about 4.5 V) and the whole mix
	# louder. Adds 2 ms of latency.
	("Brickwall limiter", p(THRESHOLD=-6.0, RATIO=20.0, ATTACK=2.0, RELEASE=60.0, KNEE=0.0, MAKEUP=5.0, LOOKAHEAD=100.0), DATA),
	# Parallel smash: New York compression in one module. The wet side is crushed (12:1 from -30 dB, 1 ms attack)
	# and pushed +15 dB, then MIX 40 % blends it under the dry: quiet details and room come up, the dry keeps
	# the punch. Great on drums; raise MIX for more smash.
	("Parallel smash", p(THRESHOLD=-30.0, RATIO=12.0, ATTACK=1.0, RELEASE=80.0, KNEE=3.0, MAKEUP=15.0, MIX=0.4), DATA),
]

write("baR", "BAR.cpp", PRESETS)

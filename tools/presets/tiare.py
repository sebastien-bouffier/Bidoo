# Factory presets for TiARE (src/TIARE.cpp): python tools/presets/tiare.py writes presets/TiARE/.
# FREQ is in semitones from C4 (-54..54), divided by 100 in LFO mode (data "freqFactor": 100, else 1). Phase distortion:
# the phase runs through a line to (X, Y); X = Y = 0.5 is no distortion. Every output is +-5 V.
# dataFromJson keeps freqFactor when the key is missing, so every preset sets it: a preset loaded on a module in LFO
# mode would otherwise stay 100 times too slow.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preset_kit

DEFAULTS = {
	"FREQ_PARAM": 0.0,
	"FINE_PARAM": 0.0,
	"MODE_PARAM": 1,      # 0 Digital, 1 Analog
	"SYNC_PARAM": 1,      # 0 Soft, 1 Hard
	"FM_PARAM": 0.0,
	"PW_PARAM": 0.5,      # 0.01..0.99
	"PWM_PARAM": 0.0,
	"DIST_X_PARAM": 0.5,  # 0.01..0.99
	"DIST_Y_PARAM": 0.5,
}

AUDIO = {"freqFactor": 1}
LFO = {"freqFactor": 100}

def p(**kw):
	d = dict(DEFAULTS)
	d.update({k + "_PARAM": v for k, v in kw.items()})
	return d

PRESETS = [
	# The plain VCO: Analog engine (curved saw and triangle, AC-coupled square), no distortion, square at 50 %.
	("Analog VCO", p(), AUDIO),
	# Casio-style phase distortion: Digital, the first half of the cycle squeezed into 10 % (X 0.1, Y 0.5), so the
	# sine output becomes a bright, resonant saw-like tone; move X back to 0.5 for a pure sine.
	("Phase distortion saw", p(MODE=0, DIST_X=0.1, DIST_Y=0.5), AUDIO),
	# Hard sync lead: patch a master VCO into SYNC; this one sits an octave above it (FREQ +12), digital edges, and FM
	# depth ready for an envelope on FM to sweep the classic sync scream.
	("Hard sync lead", p(FREQ=12, MODE=0, SYNC=1, FM=0.3), AUDIO),
	# Pads and strings: Analog, square at 50 %, PWM 50 % so an LFO of +-5 V on PW sweeps the width 25 to 75 %; soft
	# sync for gentle locking if a master is patched.
	("PWM strings", p(SYNC=0, PWM=0.5), AUDIO),
	# A slow modulation source: LFO mode at 1.3 Hz (C3 / 100), Digital for exact shapes, the phase bent (X 0.85) so
	# the triangle and sine rise slowly and fall fast.
	("Skewed LFO", p(FREQ=-12, MODE=0, DIST_X=0.85, DIST_Y=0.5), LFO),
]

if __name__ == "__main__":
	preset_kit.write("TiARE", "TIARE.cpp", PRESETS)

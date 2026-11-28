# Factory presets for lIMbO (src/LIMBO.cpp): python tools/presets/limbo.py writes presets/lIMbO/.
# Mappings (LIMBO::runGroup): cutoff = 2^(4.5 + 9.5 knob) Hz (23 Hz to 16 kHz); feedback k = 3.5 * RES, under the
# ladder's 4, so it never self-oscillates; the passband falls by 1 / (1 + k) as RES rises. Gain boost = 2^(3 knob) (x1
# to x8): in Linear it multiplies the output (up to +18 dB: can reach the soft limit at 8 V with a 5 V input), in
# Saturated it drives the stages, the output ceiling staying about 5 V / tanh(boost / 3), so levels hold.
# CMOD 100 % = cutoff CV at 1 V/oct. No dataToJson beyond the theme: data is {}.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preset_kit

DEFAULTS = {
	"CUTOFF_PARAM": 1.0,
	"Q_PARAM": 0.0,
	"CMOD_PARAM": 0.0,   # -1..1
	"MUG_PARAM": 0.0,
	"MODE_PARAM": 0,     # 0 Linear, 1 Saturated
}

def p(**kw):
	d = dict(DEFAULTS)
	d.update({k + "_PARAM": v for k, v in kw.items()})
	return d

PRESETS = [
	# A clean, transparent low-pass at 1.2 kHz: Linear, a touch of RES (k 0.7, passband x0.59), boost 0.25 (x1.68)
	# gives the level back. CV tracks 1 V/oct for keyboard or envelope sweeps.
	("Clean lowpass", p(CUTOFF=0.6, Q=0.2, CMOD=1.0, MUG=0.25), None),
	# Warm ladder colour on a synth or drum bus: Saturated at 1.6 kHz, moderate RES, boost 0.5 (stages driven x2.8)
	# for round harmonics without losing the top.
	("Warm drive", p(CUTOFF=0.65, Q=0.35, MUG=0.5, MODE=1), None),
	# A resonant sweep for a sequenced envelope or LFO on the cutoff CV: Linear at 610 Hz, RES 85 % (k 3: the peak
	# meets the input level), boost 0.5 (x2.8) brings the passband to x0.7 and the peak to x2.8; 1 V/oct.
	("Resonant sweep", p(CUTOFF=0.5, Q=0.85, CMOD=1.0, MUG=0.5), None),
	# Fat, crushed lows: Saturated at 220 Hz with mid RES and heavy boost (0.8, x5.3): a kick or bass turns thick and
	# dark, the saturation keeping the level near 5 V.
	("Crushed lows", p(CUTOFF=0.35, Q=0.5, MUG=0.8, MODE=1), None),
	# Acid filter for a saw: Saturated, full RES (k 3.5, the most it gives, still no self-oscillation), 470 Hz so an
	# envelope on the 1 V/oct CV does the squelch, boost 0.6 for bite.
	("Acid squelch", p(CUTOFF=0.45, Q=1.0, CMOD=1.0, MUG=0.6, MODE=1), None),
]

if __name__ == "__main__":
	preset_kit.write("lIMbO", "LIMBO.cpp", PRESETS, data={})

# Factory presets for bAFIs (src/BAFIS.cpp): python tools/presets/bafis.py writes presets/BAFIS/
# Four bands at three Linkwitz-Riley crossovers (FREQ_PARAM..+2, knob = (log2 Hz - 4.5) / 9.5, 22.6 Hz .. 16 kHz).
# Per band (Q, GAIN, TYPE, PREPOST, VOLUME +0..+3): resonance 0.1 (flat sum) .. 1 (Q 10 at the band's edges);
# drive 1 (clean, d = 0.1) .. 10 (d = 10), each curve normalised so a 5 V peak stays at 5 V, so drive raises
# quiet parts (about d / tanh d for tanh); type 0 tanh, 1 sin (folds past drive ~2.3), 2 digital hard clip;
# PREPOST 0 distorts the full input then filters the band (harmonics stay in band), 1 filters then distorts
# (harmonics spill above the band). Volume 1 = unity per band; the default 0.5 on all bands is -6 dB overall.
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from preset_kit import write

TANH, SIN, DIGITAL = 0, 1, 2
PRE, POST = 0, 1

def freq_knob(hz):
	return round((math.log2(hz) - 4.5) / 9.5, 4)

def bands(freqs, q, gain, typ, post, vol):
	# freqs: the 3 crossovers in Hz; the others: 4 values, band 1 (lows) .. band 4 (highs)
	d = {}
	for i, f in enumerate(freqs):
		d["FREQ_PARAM" if i == 0 else "FREQ_PARAM+%d" % i] = freq_knob(f)
	for name, vals in (("Q", q), ("GAIN", gain), ("TYPE", typ), ("PREPOST", post), ("VOLUME", vol)):
		for i, v in enumerate(vals):
			d["%s_PARAM" % name if i == 0 else "%s_PARAM+%d" % (name, i)] = v
	return d

# the module's defaults: 200 / 1000 / 5000 Hz, flat, clean, tanh, before the filter, volume 0.5
DEFAULTS = bands([200, 1000, 5000], [0.1] * 4, [1.0] * 4, [TANH] * 4, [PRE] * 4, [0.5] * 4)
assert len(DEFAULTS) == 23

def p(**kw):
	d = dict(DEFAULTS)
	d.update(bands(kw.get("freqs", [200, 1000, 5000]), kw.get("q", [0.1] * 4), kw.get("gain", [1.0] * 4),
		kw.get("typ", [TANH] * 4), kw.get("post", [PRE] * 4), kw.get("vol", [0.5] * 4)))
	return d

PRESETS = [
	# Warm saturation: gentle tanh on every band (drive 2-2.5, d 1.2-1.8), distorted before the filters so the
	# harmonics stay in each band and the top stays smooth; the highs barely touched. Volumes 0.75 offset the
	# few dB the drive adds to quiet parts: about unity on a mix.
	("Warm saturation", p(freqs=[120, 1000, 6000], gain=[2.0, 2.5, 2.0, 1.5], vol=[0.75, 0.75, 0.75, 0.75]), None),
	# Crunchy mids: lows and highs clean, 250-800 Hz pushed through tanh (drive 5) and 800-3500 Hz through the
	# hard clip (drive 6, a slight resonance at its edges) after their filters, so the grit spreads upwards.
	# For guitars, synth leads, snares. Driven bands are louder on quiet parts: their volumes sit lower.
	("Crunchy mids", p(freqs=[250, 800, 3500], q=[0.1, 0.1, 0.35, 0.1], gain=[1.0, 5.0, 6.0, 1.0],
		typ=[TANH, TANH, DIGITAL, TANH], post=[PRE, POST, POST, PRE], vol=[0.85, 0.55, 0.5, 0.8]), None),
	# Fuzz bass, clean lows: the sub under 120 Hz stays clean and full (volume 0.9) so the low end keeps its
	# weight, while 120-600 Hz is hard-clipped (drive 8) and 600-3000 Hz fuzzed with tanh (drive 7), both after
	# their filters; the top band is turned down to tame the fizz.
	("Fuzz bass", p(freqs=[120, 600, 3000], gain=[1.0, 8.0, 7.0, 1.0], typ=[TANH, DIGITAL, TANH, TANH],
		post=[PRE, POST, POST, PRE], vol=[0.9, 0.45, 0.4, 0.3]), None),
	# High exciter: everything under 5 kHz passes clean at unity (volume 1, flat crossovers); the air band is
	# folded by sin (drive 3, d 2.3) after its filter, adding bright harmonics on top, with a small resonance at
	# 5 kHz. Adds presence and sparkle to dull pads, vocals, hats.
	("High exciter", p(freqs=[150, 1500, 5000], q=[0.1, 0.1, 0.1, 0.25], gain=[1.0, 1.0, 1.0, 3.0],
		typ=[TANH, TANH, TANH, SIN], post=[PRE, PRE, PRE, POST], vol=[1.0, 1.0, 1.0, 0.6]), None),
	# Telephone: lows under 300 Hz and highs over 3.4 kHz muted, the voice band hard-clipped (drive 6) with
	# resonant edges: a narrow, honky, broken-speaker sound for vocals, drums or a radio effect.
	("Telephone", p(freqs=[300, 1200, 3400], q=[0.1, 0.45, 0.45, 0.1], gain=[1.0, 6.0, 6.0, 1.0],
		typ=[TANH, DIGITAL, DIGITAL, TANH], post=[PRE, POST, POST, PRE], vol=[0.0, 0.65, 0.65, 0.0]), None),
]

write("BAFIS", "BAFIS.cpp", PRESETS)

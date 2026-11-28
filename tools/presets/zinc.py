# ziNC factory presets: python tools/presets/zinc.py (writes presets/ziNC/)
# Band centres (BG_PARAM+i): 100 139 193 268 373 518 720 1k 1.39k 1.93k 2.68k 3.73k 5.18k 7.2k 10k, air 13.9k.
# Band gain 0..2 (1 = 0 dB, 2 = +6 dB). Envelope knob 0..0.25 -> 1.5 ms x 400^(v/0.25):
# 0.03 ~3 ms, 0.1 ~16 ms, 0.125 ~30 ms, 0.15 ~55 ms, 0.2 ~180 ms. Q knob 1..10, 5 = bands meet (Q 3).
# The default gains (1, 1, 1) leave a ±5 V speech / saw pair well under 1 V out: the presets put the product
# MOD x CAR x OUT near 10 (about ±3-5 V), trimmed where bands are cut or boosted so the level stays even.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from preset_kit import write

BANDS = 16

def base():
	p = {"BG_PARAM" if i == 0 else "BG_PARAM+%d" % i: 1.0 for i in range(BANDS)}
	p.update({
		"ATTACK_PARAM": 0.0, "DECAY_PARAM": 0.0,
		"Q1_PARAM": 5.0, "Q2_PARAM": 5.0, "Q3_PARAM": 5.0, "Q4_PARAM": 5.0,
		"GMOD_PARAM": 1.0, "GCARR_PARAM": 1.0, "G_PARAM": 1.0,
		"SHAPE_PARAM": 0.0,   # in the enum but never configured nor read: left at 0
	})
	return p

def preset(bands=None, **kw):
	p = base()
	if bands:
		for i, g in enumerate(bands):
			p["BG_PARAM" if i == 0 else "BG_PARAM+%d" % i] = g
	p.update(kw)
	return p

# Output levels measured on a simulation of the module's chain (filters, envelopes, gains): a speech-like modulator
# (pink noise in 4 Hz syllables) peaking at 5 V and a 110 Hz saw at 5 V give about 4 V peak out with these MOD/CAR/OUT
# gains. The band gains, Qs and times change the level a lot, so each preset's OUT is set from its own measurement.
presets = [
	# The classic robot: flat bands meeting at Q 3, a quick 3 ms attack and 16 ms decay so the carrier
	# chops with the syllables without buzzing on the envelope ripple.
	("Robot voice", preset(
		ATTACK_PARAM=0.03, DECAY_PARAM=0.1,
		GMOD_PARAM=2.0, GCARR_PARAM=2.0, G_PARAM=7.5), None),
	# Intelligible speech: lows thinned, presence (1.9-5.2 kHz) and air lifted, narrower analysis
	# filters to separate the formants, wider carrier filters for a smoother tone. OUT trimmed for the boosts.
	("Bright speech", preset(
		[0.7, 0.8, 0.9, 1.0, 1.0, 1.0, 1.0, 1.1, 1.2, 1.5, 1.7, 1.8, 1.7, 1.6, 1.5, 1.6],
		ATTACK_PARAM=0.02, DECAY_PARAM=0.09,
		Q1_PARAM=6.0, Q2_PARAM=6.0, Q3_PARAM=4.0, Q4_PARAM=4.0,
		GMOD_PARAM=2.0, GCARR_PARAM=2.0, G_PARAM=7.3), None),
	# Telephone: nothing under 300 Hz or above 4 kHz, a hump around 1-2 kHz. Half the bands are off,
	# so OUT is raised to keep the level of the others.
	("Telephone", preset(
		[0.0, 0.0, 0.0, 0.3, 0.8, 1.2, 1.5, 1.7, 1.7, 1.5, 1.2, 0.5, 0.0, 0.0, 0.0, 0.0],
		ATTACK_PARAM=0.02, DECAY_PARAM=0.08,
		GMOD_PARAM=2.0, GCARR_PARAM=2.0, G_PARAM=7.6), None),
	# Dark whisper pad: 55 ms attack, 180 ms decay smear the words into swells, the top rolled off,
	# wide carrier filters (Q 1.2) for a breathy wash; good on noise or a chord pad. The slow decay holds
	# the envelopes higher, so OUT is a little lower.
	("Whisper pad", preset(
		[1.1, 1.1, 1.1, 1.0, 1.0, 1.0, 1.0, 1.0, 0.9, 0.9, 0.8, 0.7, 0.5, 0.35, 0.25, 0.15],
		ATTACK_PARAM=0.15, DECAY_PARAM=0.2,
		Q1_PARAM=5.0, Q2_PARAM=5.0, Q3_PARAM=2.0, Q4_PARAM=2.0,
		GMOD_PARAM=2.5, GCARR_PARAM=2.0, G_PARAM=7.9), None),
	# Talk box: narrow filters on both sides (Q 5.5) ring at the band centres, the formant region
	# (270 Hz-2.7 kHz) pushed and the extremes cut, for a vowel-heavy, mouth-like sound on a lead synth.
	("Talk box", preset(
		[0.4, 0.6, 0.9, 1.3, 1.5, 1.6, 1.6, 1.5, 1.4, 1.4, 1.2, 0.9, 0.6, 0.4, 0.3, 0.2],
		ATTACK_PARAM=0.025, DECAY_PARAM=0.11,
		Q1_PARAM=9.0, Q2_PARAM=9.0, Q3_PARAM=9.0, Q4_PARAM=9.0,
		GMOD_PARAM=2.0, GCARR_PARAM=2.0, G_PARAM=5.8), None),
]

write("ziNC", "ZINC.cpp", presets, data={}, constants={"BANDS": BANDS})

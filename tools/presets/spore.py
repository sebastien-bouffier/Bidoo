# SPORE factory presets: python tools/presets/spore.py (writes presets/SPORE/)
# A grain of GRAINSIZE ms is taken every analysis hop and one is played every synthesis hop, read at the
# pitch's speed (so a grain pitched down lasts longer). Equal hops keep the timing; a hop of about a quarter
# of the grain gives a smooth overlap. WINDOW: 0 Welch, 1 Tukey, 2 Hann, 3 Blackman, 4 B-Nuttall, 5 B-Harris;
# ATTACKRELEASE shapes only Tukey (its alpha) and Blackman.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from preset_kit import write

def preset(**kw):
	p = {
		"PITCH_PARAM": 0.0, "GRAINSIZE_PARAM": 10.0,
		"HOPSIZEANALYSIS_PARAM": 4.0, "HOPSIZESYNTHESIS_PARAM": 4.0,
		"WINDOW_PARAM": 0, "ATTACKRELEASE_PARAM": 0.5,
	}
	p.update(kw)
	return p

presets = [
	# Octave down: long 60 ms grains (120 ms once slowed) every 15 ms, Hann, for a smooth sub voice.
	("Octave down", preset(PITCH_PARAM=-12.0, GRAINSIZE_PARAM=60.0,
		HOPSIZEANALYSIS_PARAM=15.0, HOPSIZESYNTHESIS_PARAM=15.0, WINDOW_PARAM=2), None),
	# Octave up shimmer: 30 ms grains played in 15 ms, a new one every 5 ms; mix with a reverb for shimmer.
	("Octave up shimmer", preset(PITCH_PARAM=12.0, GRAINSIZE_PARAM=30.0,
		HOPSIZEANALYSIS_PARAM=5.0, HOPSIZESYNTHESIS_PARAM=5.0, WINDOW_PARAM=2), None),
	# Fifth up: 40 ms grains every 10 ms, classic Blackman (alpha 0.16) for clean edges; a harmoniser voice.
	("Fifth up", preset(PITCH_PARAM=7.0, GRAINSIZE_PARAM=40.0,
		HOPSIZEANALYSIS_PARAM=10.0, HOPSIZESYNTHESIS_PARAM=10.0, WINDOW_PARAM=3, ATTACKRELEASE_PARAM=0.16), None),
	# Detune: +15 cents with long soft Welch grains; mixed with the dry signal it thickens like a chorus.
	("Detune chorus", preset(PITCH_PARAM=0.15, GRAINSIZE_PARAM=50.0,
		HOPSIZEANALYSIS_PARAM=12.0, HOPSIZESYNTHESIS_PARAM=12.0, WINDOW_PARAM=0), None),
	# Glitch: 4 ms grains every 30 ms, each repeated to fill its 30 ms hop, almost square Tukey edges
	# (alpha 0.1): a buzzy, stuttering texture.
	("Glitch grains", preset(PITCH_PARAM=0.0, GRAINSIZE_PARAM=4.0,
		HOPSIZEANALYSIS_PARAM=30.0, HOPSIZESYNTHESIS_PARAM=30.0, WINDOW_PARAM=1, ATTACKRELEASE_PARAM=0.1), None),
]

write("SPORE", "SPORE.cpp", presets, data={})

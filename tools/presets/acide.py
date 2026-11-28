# Factory presets for aCiDe (src/ACIDE.cpp): python tools/presets/acide.py writes presets/ACIDE/.
# Knob mappings (ACIDE::control): cutoff = 200 Hz * 12.5^knob (200 Hz to 2.5 kHz), decay = 0.2 s * 10^knob (0.2 to 2 s),
# RES is linear up to 85 %, past it the loop gain bends up and the ladder can self-oscillate at high cutoffs.
# The voice makes up its level against RES, and the output is +-5 V into a soft limit, so VOLUME stays at 0.8.
# No dataToJson beyond the theme: data is {}.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preset_kit

DEFAULTS = {
	"TUNE_PARAM": 0.0,      # semitones, -12..12
	"WAVE_PARAM": 0,        # 0 saw, 1 square
	"VOLUME_PARAM": 0.8,
	"CUTOFF_PARAM": 0.4,    # 551 Hz
	"RES_PARAM": 0.5,
	"ENVMOD_PARAM": 0.5,
	"DECAY_PARAM": 0.5,     # 0.63 s
	"ACCENT_PARAM": 0.5,
}

def p(**kw):
	d = dict(DEFAULTS)
	d.update({k + "_PARAM": v for k, v in kw.items()})
	return d

PRESETS = [
	# The 303 line: saw, low cutoff (428 Hz) so the envelope does the opening, high RES (80 %, under the knee: no
	# self-oscillation), strong env mod and short decay (0.4 s) for the squelch, accent up so accented steps bark.
	("Classic squelch", p(CUTOFF=0.3, RES=0.8, ENVMOD=0.7, DECAY=0.3, ACCENT=0.7), None),
	# Sub bass under a mix: an octave down, cutoff near the bottom (258 Hz), little RES and env mod so the notes stay
	# round, a longer decay (0.8 s) so held notes keep their body; low accent.
	("Deep sub bass", p(TUNE=-12, CUTOFF=0.1, RES=0.15, ENVMOD=0.2, DECAY=0.6, ACCENT=0.3), None),
	# Square for a hollow, rubbery tone: mid cutoff (484 Hz), medium RES and env mod, medium decay (0.56 s).
	("Rubbery mid bass", p(WAVE=1, CUTOFF=0.35, RES=0.55, ENVMOD=0.45, DECAY=0.45, ACCENT=0.5), None),
	# Lead an octave up: RES 92 % (past the knee, the ladder may sing on its own at the top of the sweep: meant), cutoff
	# 802 Hz, deep env mod, long decay (1 s) and full-ish accent for screaming accented notes. The soft limit holds it.
	("Screaming lead", p(TUNE=12, CUTOFF=0.55, RES=0.92, ENVMOD=0.85, DECAY=0.7, ACCENT=0.9), None),
	# Short, soft pluck for arpeggios: square, low cutoff (330 Hz), light RES, the env mod opens it briefly and the
	# shortest decays (0.22 s) close it fast; little accent.
	("Soft pluck", p(WAVE=1, CUTOFF=0.2, RES=0.25, ENVMOD=0.6, DECAY=0.05, ACCENT=0.2), None),
]

if __name__ == "__main__":
	preset_kit.write("ACIDE", "ACIDE.cpp", PRESETS, data={})

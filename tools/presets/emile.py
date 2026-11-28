# Factory presets for EMILE (src/EMILE.cpp): python tools/presets/emile.py writes the images res/emile/*.png and the
# presets presets/EMILE/.
# A preset can't hold an image (a drawing lives in the patch's storage), so each one names an image shipped with the
# plugin, "plugin:res/emile/<name>.png", which EMILE finds in the plugin's folder on any computer. Drawing on it starts
# a drawing from it, as with any loaded file.
# Harmonics mode (mode 1): each row of the image is one cycle's spectrum. CURVE sets the number of bands the row is cut
# into, one harmonic each: bands = round(16 * 16^t), t = (CURVE - 0.01) / 0.09 (16 at 0.01, 32 at 0.0325, 64 at 0.055).
# A band's level is the mean brightness of its columns (linear amplitude), and the row's brightest harmonic sets the
# output level, so the images use the full brightness for their loudest harmonic. The images are 256 x 256 grey
# (R = G = B, opaque); the presets play R, G and B, not A, so the level is the grey itself.
# POS 0 is the top row: the images that morph go from the top (POS 0) to the bottom (POS 1), for an LFO or an envelope
# on POS (bipolar: +-5 V around the knob covers the image).
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preset_kit
from PIL import Image

ROOT = preset_kit.ROOT
W = H = 256

def curve_for(bands):
	return round(0.01 + 0.09 * math.log(bands / 16.0, 16), 6)

def image(name, bands, level):
	# level(k, v): harmonic k's amplitude (0..1) on the row at v (0 top, 1 bottom); written in each band's columns
	im = Image.new("RGBA", (W, H))
	px = im.load()
	for y in range(H):
		v = y / (H - 1)
		row = [max(0.0, min(1.0, level(k, v))) for k in range(1, bands + 1)]
		peak = max(row) or 1.0
		for k in range(1, bands + 1):
			g = int(round(255 * row[k - 1] / peak))
			x0, x1 = (k - 1) * W // bands, k * W // bands
			for x in range(x0, x1):
				px[x, y] = (g, g, g, 255)
	d = os.path.join(ROOT, "res", "emile")
	os.makedirs(d, exist_ok=True)
	im.save(os.path.join(d, name + ".png"), optimize=True)
	return "plugin:res/emile/%s.png" % name

def saw(k, v):
	return 1.0 / k

def square(k, v):
	return 1.0 / k if k % 2 else 0.0

def organ(k, v):
	# drawbars 16' 8' 5 1/3' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1' as harmonics 1 2 3 4 6 8 10 12 16 (here on 8', the fundamental
	# is the 16' sub an octave below: TUNE -1): a full gospel registration, 888 000 000 + a little 4' and 2'
	bars = {1: 1.0, 2: 1.0, 3: 1.0, 4: 0.35, 6: 0.0, 8: 0.25, 10: 0.0, 12: 0.0, 16: 0.0}
	return bars.get(k, 0.0)

def filter_sweep(k, v):
	# a saw through a closing low-pass: open at the top, nearly a sine at the bottom
	cutoff = 32.0 * (1.0 - v) + 1.0
	return (1.0 / k) / (1.0 + (k / cutoff) ** 4)

def pwm(k, v):
	# a pulse whose width goes from 50 % (top, a square) to 5 % (bottom, thin and nasal)
	w = 0.5 - 0.45 * v
	return abs(math.sin(math.pi * k * w)) / k

def vowels(k, v):
	# a saw-like source through two formants gliding a -> e -> i -> o -> u down the image, for a note near C3 (130 Hz):
	# formant frequencies over the fundamental (F1, F2 in Hz / 130)
	table = [(800, 1150), (400, 1600), (280, 2250), (450, 800), (325, 700)]
	x = v * (len(table) - 1)
	i = min(int(x), len(table) - 2)
	t = x - i
	f1 = (table[i][0] * (1 - t) + table[i + 1][0] * t) / 130.0
	f2 = (table[i][1] * (1 - t) + table[i + 1][1] * t) / 130.0
	def peak(f, width):
		return math.exp(-((k - f) / width) ** 2)
	return (1.0 / math.sqrt(k)) * (0.08 + peak(f1, 1.6) + 0.7 * peak(f2, 2.5))

def glass(k, v):
	# odd high partials over a weak fundamental: a hollow, glassy tone (harmonics 1, 3, 5, 9, 13 and 17 ringing)
	ring = {1: 0.5, 3: 0.3, 5: 0.8, 9: 1.0, 13: 0.7, 17: 0.45, 21: 0.25}
	return ring.get(k, 0.0)

def stairs(k, v):
	# harmonics entering one by one down the image: 1 at the top, all 16 at the bottom, each at 1/sqrt(k)
	n = 1 + int(v * 15.999)
	return 1.0 / math.sqrt(k) if k <= n else 0.0

DEFAULTS = {
	"CURVE_PARAM": 0.05,
	"GAIN_PARAM": 1.0,
	"TUNE_PARAM": 0.0,     # octaves, added to V/O (C4 at 0 V)
	"POS_PARAM": 0.0,
	"R_PARAM": 0.0, "G_PARAM": 0.0, "B_PARAM": 0.0, "A_PARAM": 0.0,   # momentary buttons: the state is in the data
}

def preset(name, path, bands, **kw):
	d = dict(DEFAULTS)
	d["CURVE_PARAM"] = curve_for(bands)
	d.update({k + "_PARAM": v for k, v in kw.items()})
	data = {"lastPath": path, "r": True, "g": True, "b": True, "a": False, "mode": 1, "drawn": False}
	return (name, d, data)

def build():
	return [
		# The textbook saw: 32 harmonics at 1/k. Bright, buzzy, the start of any subtractive patch (put a filter after it).
		preset("Saw", image("saw", 32, saw), 32),
		# Odd harmonics at 1/k: a square, hollow and woody; an octave down for a bass.
		preset("Square bass", image("square", 32, square), 32, TUNE=-1),
		# Drawbar organ: the 16' sub (the fundamental, TUNE -1), 8', 5 1/3' full, a little 4' and 2'.
		preset("Drawbar organ", image("organ", 16, organ), 16, TUNE=-1),
		# A saw closing to a sine from the top to the bottom: POS is the cutoff. An envelope or an LFO on POS gives a
		# filter sweep without a filter.
		preset("Filter sweep on POS", image("filter_sweep", 64, filter_sweep), 64, POS=0.2),
		# A pulse from 50 % (top) to 5 % (bottom): an LFO on POS is pulse width modulation.
		preset("PWM on POS", image("pwm", 64, pwm), 64, POS=0.3),
		# Two formants gliding a, e, i, o, u from the top to the bottom, tuned for notes around C3: a talking pad
		# (sweep POS slowly).
		preset("Vowels on POS", image("vowels", 32, vowels), 32, TUNE=-1, POS=0.0),
		# Odd high partials over a quiet fundamental: glassy, bell-like, an octave up.
		preset("Glass", image("glass", 32, glass), 32, TUNE=1),
		# The harmonics come in one by one down the image (1 at the top, 16 at the bottom): a ramp on POS builds the
		# tone up, a sequencer on POS picks a brightness per step.
		preset("Harmonic stairs", image("stairs", 16, stairs), 16, POS=0.5),
	]

if __name__ == "__main__":
	preset_kit.write("EMILE", "EMILE.cpp", build())

# Factory presets for PRiSME (src/PRISME.cpp): python tools/presets/prisme.py writes presets/PRISME/
# Bars are 0..1 in dB (1 = 0 dB, 0 = off, 60 dB of range: src/PrismeEngine.hpp barToAmp); the T page multiplies DECAY
# and RELEASE by 8^(2 t - 1) (0.5 = x1). Knob laws: ATTACK 1 ms * 10000^v, DECAY and RELEASE 5 ms * 4000^v (time to
# -60 dB), SWEEP 5 ms * 4000^v (0 = off), FORMANT 100 Hz * 80^v.
import math, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from preset_kit import write

N = 64

def db_bar(db):
	return min(max(1.0 + db / 60.0, 0.0), 1.0)

def power(k, p):
	return db_bar(-20.0 * p * math.log10(k))

def saw():
	return [power(k, 1) for k in range(1, N + 1)]

def square():
	return [power(k, 1) if k % 2 else 0.0 for k in range(1, N + 1)]

def flat_times(t=0.5):
	return [t] * N

def r4(xs):
	return [round(x, 4) for x in xs]

def knob(seconds, base_s, ratio):
	return round(math.log(seconds / base_s) / math.log(ratio), 4)

def attack(s): return knob(s, 0.001, 10000.0)
def decay(s): return knob(s, 0.005, 4000.0)
def sweep(s): return knob(s, 0.005, 4000.0)
def formant(hz): return round(math.log(hz / 100.0) / math.log(80.0), 4)

DEFAULTS = {
	"MORPH_PARAM": 0.0, "SWEEP_PARAM": 0.0, "BRIGHT_PARAM": 0.0, "ODDEVEN_PARAM": 0.0, "STRETCH_PARAM": 0.0,
	"FORMANT_PARAM": 0.5, "ATTACK_PARAM": 0.15, "DECAY_PARAM": 0.6, "SUSTAIN_PARAM": 0.7, "RELEASE_PARAM": 0.45,
	"DAMP_PARAM": 0.3, "DEPTH_PARAM": 0.0, "TUNE_PARAM": 0.0, "SPREAD_PARAM": 0.0,
}

def p(**kw):
	d = dict(DEFAULTS)
	d.update({k + "_PARAM": v for k, v in kw.items()})
	return d

def data(a, b, t, spread_phases=False):
	return {"bars": [r4(a), r4(b), r4(t)], "page": 0, "tool": 0, "brushWidth": 1.0, "spreadPhases": spread_phases}

# Harp pluck: the sawtooth with no sustain, high harmonics dying first (DAMP 0.8), a short attack and a ring of 1.5 s;
# B a duller saw (-12 dB per octave) for MORPH to darken it.
def pluck():
	return data(saw(), [power(k, 2) for k in range(1, N + 1)], flat_times())

# Drawbar organ: the classic stops (16', 8', 5 1/3', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'), full sustain, an instant
# attack and a short release; B adds the upper stops for a brighter registration, the T page leaves the times alone.
def organ():
	a = [0.0] * N
	b = [0.0] * N
	for k, db in ((1, 0), (2, -3), (3, -6), (4, -6), (6, -10)):
		a[k - 1] = db_bar(db)
	for k, db in ((1, 0), (2, -2), (3, -4), (4, -4), (5, -8), (6, -6), (8, -6), (10, -10), (12, -12), (16, -14)):
		b[k - 1] = db_bar(db)
	return data(a, b, flat_times())

# Glass bells: stretched partials (STRETCH 0.55) from a sparse random spectrum, long decays shimmering at different
# rates (a random T page), no sustain; the spread phases keep the strike's peak down.
def bells():
	rnd = random.Random(19)
	a = [0.0] * N
	for k in (1, 2, 3, 5, 7, 9, 12, 15, 19, 24):
		a[k - 1] = db_bar(-rnd.uniform(0, 18) - 6 * math.log2(k) * 0.6)
	t = [0.3 + 0.5 * rnd.random() for _ in range(N)]
	return data(a, square(), t, True)

# Vowel pad: a saw glided over two seconds into a brighter spectrum while a formant at 700 Hz (an open "a") shapes
# it; slow attack, full sustain, long release, harmonics spread across the stereo field.
def pad():
	b = [db_bar(-3.0 * math.log2(k)) for k in range(1, N + 1)]
	return data(saw(), b, flat_times(0.6))

# Hollow sweep: each note starts as a sawtooth and turns hollow (B the square) in 400 ms, then decays to a held
# level; ODD/EVEN a little to the odd side, a bit of brightness.
def hollow():
	return data(saw(), square(), [max(0.25, 0.75 - 0.008 * i) for i in range(N)])

def spectrum(levels, n=N):
	# {harmonic: dB} as bars, the rest off
	a = [0.0] * n
	for k, db in levels.items():
		a[k - 1] = db_bar(db)
	return a

def times(levels, default=0.5):
	# {harmonic: T bar} over a default
	t = [default] * N
	for k, v in levels.items():
		t[k - 1] = v
	return t

# Clarinet: the odd harmonics strong and falling, the even ones 25 dB down (a cylindrical bore); a soft attack, a held
# tone, a short release. B is the overblown register, brighter, for MORPH or the MORPH CV (a breath controller).
def clarinet():
	a = {k: (-4.5 * math.log2(k) if k % 2 else -25 - 3 * math.log2(k)) for k in range(1, 25)}
	b = {k: (-3.0 * math.log2(k) if k % 2 else -18 - 3 * math.log2(k)) for k in range(1, 33)}
	return data(spectrum(a), spectrum(b), flat_times())

# Brass swell: a dull sawtooth (-12 dB per octave) opening into a bright one in 250 ms on every note, as a horn's
# lip buzz builds up; the high harmonics take longer to come in than to fade (T falls slowly with the harmonic).
def brass():
	return data([power(k, 2) for k in range(1, N + 1)], [power(k, 0.75) for k in range(1, N + 1)], flat_times())

# Electric piano: a tine and its bar. The fundamental and the 2nd hold for seconds, a cluster around the 7th and the
# 14th gives the bell of the attack and dies in a fraction of that (T low there); DAMP rounds the rest.
def epiano():
	a = spectrum({1: 0, 2: -10, 3: -20, 4: -22, 5: -26, 6: -20, 7: -10, 8: -16, 13: -24, 14: -18, 15: -26})
	t = times({1: 0.75, 2: 0.65, 3: 0.5, 6: 0.25, 7: 0.15, 8: 0.15, 13: 0.05, 14: 0.05, 15: 0.05})
	return data(a, spectrum({1: 0, 2: -6, 3: -14, 4: -14, 7: -6, 14: -12}), t)

# Marimba: a wooden bar's partials near 1, 4 and 10 times the fundamental (tuned bars keep them harmonic), the upper
# two dying far sooner; no sustain.
def marimba():
	a = spectrum({1: 0, 4: -9, 10: -16, 2: -30})
	t = times({1: 0.62, 2: 0.4, 4: 0.3, 10: 0.12})
	return data(a, spectrum({1: 0, 3: -14, 4: -8, 10: -12}), t)

# Tubular bell: the strike tone sits on the 2nd harmonic with the 3rd, 4th, 5th and 6th close behind and a hum below;
# STRETCH 0.3 pulls them into a chime; each rings for its own time, the hum the longest.
def tubular():
	a = spectrum({1: -8, 2: 0, 3: -3, 4: -6, 5: -5, 6: -10, 8: -12, 10: -16, 12: -18})
	t = times({1: 0.9, 2: 0.7, 3: 0.6, 4: 0.55, 5: 0.5, 6: 0.42, 8: 0.35, 10: 0.3, 12: 0.25})
	return data(a, a, t, True)

# Choir "aah": a soft sawtooth (-8 dB per octave) through a formant at 800 Hz (an open vowel), a slow attack and
# release, spread wide. B a little brighter: turn FORMANT for other vowels (400 Hz "oo", 2 kHz "ee").
def choir():
	return data([power(k, 1.35) for k in range(1, N + 1)], [power(k, 1.0) for k in range(1, N + 1)], flat_times(0.6))

# Sub bass: the fundamental with three harmonics fading fast, one octave down (TUNE -12), a short decay to a firm
# sustain and a quick release; DAMP keeps the attack's edge short.
def sub():
	return data(spectrum({1: 0, 2: -9, 3: -16, 4: -22, 5: -28}), spectrum({1: 0, 2: -4, 3: -9, 4: -12, 5: -16, 6: -20}), flat_times())

# Flute: almost a sine, the 2nd and 3rd harmonics low, the upper ones a whisper; a slow breathy attack. B is the
# overblown tone with a stronger 2nd and 3rd, for MORPH.
def flute():
	a = spectrum({1: 0, 2: -14, 3: -20, 4: -30, 5: -34, 6: -40})
	b = spectrum({1: 0, 2: -7, 3: -12, 4: -20, 5: -24, 6: -30, 7: -34})
	return data(a, b, flat_times())

# Gong: thirty partials from a random draw, stretched far apart (STRETCH 0.85), a long ring with every partial
# fading at its own rate, wide stereo; the spread phases keep the hit from peaking.
def gong():
	rnd = random.Random(7)
	a = [0.0] * N
	for k in sorted(rnd.sample(range(1, 48), 30)):
		a[k - 1] = db_bar(-rnd.uniform(0, 14) - 7 * math.log2(k) * 0.5)
	t = [0.35 + 0.5 * rnd.random() for _ in range(N)]
	return data(a, a, t, True)

# Strings: a sawtooth for the bow, a gentle formant near 1.2 kHz for the body, slow attack and release, wide spread;
# B leans odd (a softer, sul tasto colour) for MORPH.
def strings():
	b = [power(k, 1.2) if k % 2 else power(k, 1.2) * 0.75 for k in range(1, N + 1)]
	return data(saw(), b, flat_times(0.55))

PRESETS = [
	("Harp pluck", p(ATTACK=attack(0.002), DECAY=decay(1.5), SUSTAIN=0.0, RELEASE=decay(0.6), DAMP=0.8, MORPH=0.2), pluck()),
	("Drawbar organ", p(ATTACK=attack(0.003), DECAY=decay(2.0), SUSTAIN=1.0, RELEASE=decay(0.08), DAMP=0.0, MORPH=0.0), organ()),
	("Glass bells", p(ATTACK=attack(0.001), DECAY=decay(4.0), SUSTAIN=0.0, RELEASE=decay(2.5), DAMP=0.45, STRETCH=0.55,
		BRIGHT=0.15, SPREAD=0.4), bells()),
	("Vowel pad", p(ATTACK=attack(0.6), DECAY=decay(3.0), SUSTAIN=0.85, RELEASE=decay(2.0), DAMP=0.2, MORPH=1.0,
		SWEEP=sweep(2.0), FORMANT=formant(700), DEPTH=0.7, SPREAD=0.6), pad()),
	("Hollow sweep", p(ATTACK=attack(0.004), DECAY=decay(1.2), SUSTAIN=0.5, RELEASE=decay(0.4), DAMP=0.35, MORPH=1.0,
		SWEEP=sweep(0.4), ODDEVEN=-0.3, BRIGHT=0.2), hollow()),
	("Clarinet", p(ATTACK=attack(0.035), DECAY=decay(2.0), SUSTAIN=1.0, RELEASE=decay(0.15), DAMP=0.1), clarinet()),
	("Brass swell", p(ATTACK=attack(0.04), DECAY=decay(1.0), SUSTAIN=0.8, RELEASE=decay(0.25), DAMP=0.15, MORPH=1.0,
		SWEEP=sweep(0.25), BRIGHT=0.1), brass()),
	("Electric piano", p(ATTACK=attack(0.001), DECAY=decay(5.0), SUSTAIN=0.0, RELEASE=decay(0.5), DAMP=0.35, SPREAD=0.25), epiano()),
	("Marimba", p(ATTACK=attack(0.001), DECAY=decay(1.4), SUSTAIN=0.0, RELEASE=decay(0.8), DAMP=0.0), marimba()),
	("Tubular bell", p(ATTACK=attack(0.001), DECAY=decay(9.0), SUSTAIN=0.0, RELEASE=decay(5.0), DAMP=0.2, STRETCH=0.3,
		SPREAD=0.35), tubular()),
	("Choir aah", p(ATTACK=attack(0.45), DECAY=decay(3.0), SUSTAIN=0.9, RELEASE=decay(1.5), DAMP=0.15, MORPH=0.3,
		FORMANT=formant(800), DEPTH=0.8, SPREAD=0.55), choir()),
	("Sub bass", p(ATTACK=attack(0.002), DECAY=decay(0.5), SUSTAIN=0.65, RELEASE=decay(0.12), DAMP=0.6, TUNE=-1.0), sub()),
	("Flute", p(ATTACK=attack(0.09), DECAY=decay(2.0), SUSTAIN=1.0, RELEASE=decay(0.25), DAMP=0.0, BRIGHT=0.05), flute()),
	("Gong", p(ATTACK=attack(0.006), DECAY=decay(14.0), SUSTAIN=0.0, RELEASE=decay(8.0), DAMP=0.25, STRETCH=0.85,
		SPREAD=0.7), gong()),
	("Strings", p(ATTACK=attack(0.3), DECAY=decay(2.0), SUSTAIN=0.95, RELEASE=decay(0.9), DAMP=0.1, FORMANT=formant(1200),
		DEPTH=0.35, SPREAD=0.6), strings()),
]

write("PRISME", "PRISME.cpp", PRESETS)

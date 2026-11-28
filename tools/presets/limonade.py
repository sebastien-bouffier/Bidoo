# Factory presets for liMonADe (src/LIMONADE.cpp): python tools/presets/limonade.py writes presets/liMonADe/.
# A preset holds the wavetable itself, as dataToJson writes it: the original (non-morphed) frames as base64 float32,
# 2048 samples per frame (FS, fixed: "frameSize" is only the slice size for loading and recording), "nFrames" of them,
# and "morphType" (0 sample crossfade, 1 spectrum magnitude and phase, 2 spectrum magnitude with the first frame's
# phases, -1 none): dataFromJson spreads the originals over 256 frames and morphs the gaps again on load.
# A frame is 8 KB (11 KB of base64), so each table keeps 4 to 8 originals and lets the morph do the rest.
# The tables are made here, deterministically: additive spectra through an inverse FFT (sine phase), or waveshapes
# computed 16x oversampled and cut to their first 512 harmonics so the frames hold no folded-over partials. The
# module band-limits each note again (mipmaps), so a frame may hold up to 1023 harmonics. Every frame is normalized
# to a 0.95 peak (OUT is 5 V for a full-scale frame, divided by the unison voices).
# FREQ is in semitones from C4 (-54..54); some tables have a fundamental below the pitch heard (organ, bell), and their
# preset lowers FREQ so V/O 0 V still plays C4. The panel display flags keep a fresh module's values.
import base64, math, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preset_kit

FS = 2048
OS = 16  # oversampling of the waveshaped frames
H = FS // 2 - 1

DEFAULTS = {
	"RESET_PARAM": 0.0,
	"SYNC_PARAM": 1,             # 0 Soft, 1 Hard
	"FREQ_PARAM": 0.0,           # semitones from C4
	"FINE_PARAM": 0.0,
	"FM_PARAM": 0.0,
	"INDEX_PARAM": 0.0,          # edited frame
	"WTINDEX_PARAM": 0.0,        # SCAN, played frame 0..1
	"WTINDEXATT_PARAM": 1.0,     # SCAN input attenuverter
	"UNISSON_PARAM": 1,          # voices 1..3
	"UNISSONRANGE_PARAM": 0.0,   # 0..0.02 V
}
# the editing and display buttons: momentary, released
for b in ["RECWT", "RECFRAME", "DISPLAYMODE", "LOADSAMPLE", "LOADFRAME", "LOADPNG", "MORPHWT", "MORPHSPECTRUM",
          "MORPHSPECTRUMCONSTANTPHASE", "REMOVEMORPH", "NORMALIZEWT", "NORMALIZEFRAME", "NORMALIZEALLFRAMES", "REMOVEDC",
          "WINDOWWT", "WINDOWFRAME", "SMOOTHWT", "SMOOTHFRAME", "ADDFRAME", "REMOVEFRAME", "DISPLAYEDITEDFRAME",
          "DISPLAYPLAYEDFRAME"]:
	DEFAULTS[b + "_PARAM"] = 0.0

def p(**kw):
	d = dict(DEFAULTS)
	d.update({k + "_PARAM": v for k, v in kw.items()})
	return d

# --- frame makers ---------------------------------------------------------------------------------------------------

def norm(x):
	x = np.asarray(x, dtype=np.float64)
	x = x - x.mean()
	return 0.95 * x / np.max(np.abs(x))

def additive(amps, phases=None):
	# amps[k-1]: amplitude of harmonic k, sine phase (plus phases[k-1] radians)
	X = np.zeros(FS // 2 + 1, dtype=np.complex128)
	a = np.asarray(amps, dtype=np.float64)[:H]
	ph = np.zeros(len(a)) if phases is None else np.asarray(phases, dtype=np.float64)[:len(a)]
	X[1:len(a) + 1] = -1j * a * np.exp(1j * ph) * FS / 2
	return norm(np.fft.irfft(X, FS))

def shaped(f, harmonics=512):
	# f(t), t in [0, 1) over one cycle, sampled 16x and cut to its first `harmonics` harmonics
	t = np.arange(FS * OS) / (FS * OS)
	S = np.fft.rfft(f(t))
	X = np.zeros(FS // 2 + 1, dtype=np.complex128)
	X[1:harmonics + 1] = S[1:harmonics + 1] / OS
	return norm(np.fft.irfft(X, FS))

def ks(n=H):
	return np.arange(1, n + 1, dtype=np.float64)

def sine():
	return additive([1.0])

def triangle():
	k = ks()
	return additive(np.where(k % 2 == 1, np.where((k - 1) % 4 == 0, 1.0, -1.0) / k ** 2, 0.0))

def saw():
	k = ks()
	return additive((-1.0) ** (k + 1) / k)

def square():
	k = ks()
	return additive(np.where(k % 2 == 1, 1.0 / k, 0.0))

def scatter(n):
	# fixed spread phases: keeps dense additive frames from peaking in one spike
	return [(k * k * 0.618034 % 1.0) * 2 * math.pi for k in range(1, n + 1)]

# --- tables ---------------------------------------------------------------------------------------------------------

def basic_shapes():
	return [sine(), triangle(), saw(), square()]

def pwm():
	# pulse widths 50 % down to 6 %
	return [shaped(lambda t, d=d: np.where(t < d, 1.0, -1.0)) for d in np.linspace(0.5, 0.06, 8)]

def sync_sweep():
	# a hard-synced saw: the slave runs 1x to 6.5x the master, reset each cycle (a whole ratio at the end would only
	# be a plain saw, higher)
	return [shaped(lambda t, r=r: 2.0 * ((t * r) % 1.0) - 1.0) for r in np.geomspace(1.0, 6.5, 8)]

C3 = 130.81
VOWELS = [  # bass-voice formants (Hz, bandwidth Hz, gain)
	("A", [(600, 80, 1.0), (1040, 90, 0.45), (2250, 120, 0.25), (2450, 130, 0.2)]),
	("E", [(400, 70, 1.0), (1620, 90, 0.35), (2400, 120, 0.3), (2800, 130, 0.15)]),
	("I", [(250, 60, 1.0), (1750, 90, 0.2), (2600, 120, 0.25), (3050, 130, 0.15)]),
	("O", [(400, 70, 1.0), (750, 80, 0.5), (2400, 120, 0.1), (2600, 130, 0.08)]),
	("U", [(350, 60, 1.0), (600, 70, 0.3), (2400, 120, 0.05), (2675, 130, 0.04)]),
]

def vowel(formants, f0=C3, n=48):
	k = ks(n)
	f = k * f0
	env = sum(g / np.sqrt(1.0 + ((f - F) / (B / 2.0)) ** 2) for F, B, g in formants)
	return additive(env / np.sqrt(k), scatter(n))

def vowels():
	# A E I O U on C3 (FREQ -12): the formants sit right at that pitch, and move with it away from it
	return [vowel(f) for _, f in VOWELS]

DRAWBARS = [1, 3, 2, 4, 6, 8, 10, 12, 16]  # 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1' in harmonics of 16'

def organ(reg):
	a = np.zeros(16)
	for h, lvl in zip(DRAWBARS, reg):
		if lvl != "0":
			a[h - 1] = 10 ** ((int(lvl) - 8) * 3 / 20)  # 3 dB a step
	return additive(a)

def drawbar_organ():
	# flute 00 8400 000, jazz 88 8000 000, gospel 88 8800 008, full 88 8888 888: the table's fundamental is the 16'
	return [organ(r) for r in ["008400000", "888000000", "888800008", "888888888"]]

BELL = [(8, "hum"), (16, "prime"), (19, "tierce"), (24, "quint"), (32, "nominal"), (40, ""), (43, ""), (48, ""),
        (67, ""), (86, ""), (109, ""), (133, "")]

def bell_frame(levels):
	a = np.zeros(140)
	for (h, _), lvl in zip(BELL, levels):
		a[h - 1] = lvl
	return additive(a, scatter(140))

def bell():
	# church-bell partials on harmonics of a fundamental 4 octaves under the strike note (FREQ -48: the prime is C4):
	# hum 1/2, prime, minor third 1.19, fifth 1.5, nominal 2, then the upper partials. Strike to hum, as a bell dies.
	return [
		bell_frame([0.5, 0.8, 0.6, 0.35, 1.0, 0.5, 0.45, 0.6, 0.45, 0.35, 0.25, 0.2]),
		bell_frame([0.6, 1.0, 0.7, 0.3, 0.8, 0.25, 0.2, 0.25, 0.12, 0.06, 0.03, 0.0]),
		bell_frame([0.8, 1.0, 0.5, 0.1, 0.35, 0.05, 0.0, 0.04, 0.0, 0.0, 0.0, 0.0]),
		bell_frame([1.0, 0.45, 0.12, 0.0, 0.05, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]),
	]

def resonant(cut, q=5.0):
	k = ks()
	r = k / cut
	lp = 1.0 / np.sqrt((1 - r ** 2) ** 2 + (r / q) ** 2)
	return additive((-1.0) ** (k + 1) / k * lp)

def resonant_sweep():
	# a saw through a resonant low-pass whose peak climbs from the 2nd to the 40th harmonic
	return [resonant(c) for c in np.geomspace(2.0, 40.0, 8)]

def wavefold():
	# a sine folded back on itself harder and harder (sin of a sine, drive x1 to x7)
	return [shaped(lambda t, g=g: np.sin(g * math.pi / 2 * np.sin(2 * math.pi * t))) for g in np.geomspace(1.0, 7.0, 6)]

def crush(f, levels, steps):
	# f sampled-and-held over `steps` per cycle, quantized to `levels`: the stairs are the sound, so no band limit
	n = np.arange(FS)
	t = np.floor(n * steps / FS) / steps
	v = f(t)
	return norm(np.round((v + 1) / 2 * (levels - 1)) / (levels - 1) * 2 - 1)

def bytebeat(steps, fn):
	n = np.floor(np.arange(FS) * steps / FS).astype(np.int64)
	return norm((fn(n) & 255) / 127.5 - 1.0)

def bitcrush():
	# a sine losing bits and sample rate, then two byte patterns: a clean start for a scan into harsh digital grit
	s = lambda t: np.sin(2 * math.pi * t)
	return [
		crush(s, 64, 256),
		crush(s, 8, 32),
		crush(s, 4, 12),
		crush(lambda t: 2 * t - 1, 3, 8),
		bytebeat(64, lambda n: (n * 5) ^ (n >> 1) * 3),
		bytebeat(128, lambda n: (n * 9) ^ (n * 2 >> 3) | (n << 2)),
	]

def pad():
	# square, a reed (odd strong, even soft), saw, an octave-rich even spectrum, sparse glassy octaves
	k = ks()
	return [
		square(),
		additive(np.where(k % 2 == 1, 1.0 / k, 0.3 / k) * np.exp(-k / 40)),
		saw(),
		additive(np.where(k % 2 == 0, 1.0 / k, 0.25 / k) * np.exp(-k / 30)),
		additive(np.where(np.isin(k, [1, 2, 4, 8, 16]), 1.0 / np.sqrt(k), 0.0)),
	]

def data(frames, morph):
	wav = np.concatenate(frames).astype("<f4")
	assert wav.size == len(frames) * FS
	return {
		"morphType": morph,
		"nFrames": len(frames),
		"wavetable": base64.b64encode(wav.tobytes()).decode("ascii"),
		"displayMode": 0,
		"displayEditedFrame": 1,
		"displayPlayedFrame": 1,
		"frameSize": FS,
		"lastPath": "",
	}

PRESETS = [
	# Sine, triangle, saw, square, crossfaded (morph 0). SCAN at the middle sits between triangle and saw; sweep it by
	# hand or with an envelope for the classic analog shapes in one oscillator.
	("Basic shapes", p(WTINDEX=0.5), data(basic_shapes(), 0)),
	# Pulse width from 50 % to 6 % over the table: an LFO on SCAN is PWM. Three detuned voices for a string ensemble.
	("PWM strings", p(WTINDEX=0.25, UNISSON=3, UNISSONRANGE=0.006), data(pwm(), 0)),
	# A hard-synced saw, slave from 1x to 6.5x: an envelope on SCAN gives the tearing sync lead.
	("Sync sweep", p(WTINDEX=0.3), data(sync_sweep(), 0)),
	# The vowels A E I O U on C3, formants interpolated by magnitude (morph 2): a choir-ish voice that talks as SCAN
	# moves. FREQ -12 so V/O 0 V is C3, where the formants are right.
	("Vowels", p(FREQ=-12, WTINDEX=0.0), data(vowels(), 2)),
	# Four drawbar registrations (flute, jazz, gospel, full), morphed by spectrum (morph 1): SCAN pulls the drawbars.
	# The table's fundamental is the 16': FREQ -12 puts the 8' on the played note. SCAN at 1/3: the jazz 888.
	("Drawbar organ", p(FREQ=-12, WTINDEX=0.33), data(drawbar_organ(), 1)),
	# Bell partials from the strike to the hum (morph 2). FREQ -48: the table is 4 octaves under the strike note. Sweep
	# SCAN from 0 to 1 with a decaying envelope (and a VCA) for a bell; held at 0 it is a bright metallic tone.
	("Bell", p(FREQ=-48, WTINDEX=0.0), data(bell(), 2)),
	# A saw through a resonant low-pass from the 2nd to the 40th harmonic: a filter sweep on SCAN, an octave down for
	# a bass. Hard sync stays on: patch another oscillator in SYNC for more bite.
	("Resonant bass", p(FREQ=-12, WTINDEX=0.3), data(resonant_sweep(), 2)),
	# A sine folded harder and harder (morph 0): west-coast timbres, from round to buzzy.
	("Wavefolder", p(WTINDEX=0.2), data(wavefold(), 0)),
	# A sine losing bits and sample rate, ending in byte patterns: digital, harsh, aliased on purpose (morph 0).
	("Bitcrush", p(WTINDEX=0.45), data(bitcrush(), 0)),
	# Square, reed, saw, even and glassy spectra morphed by spectrum (morph 1), three detuned voices: a slow LFO on
	# SCAN for an evolving pad.
	("Evolving pad", p(WTINDEX=0.5, UNISSON=3, UNISSONRANGE=0.012), data(pad(), 1)),
]

if __name__ == "__main__":
	preset_kit.write("liMonADe", "LIMONADE.cpp", PRESETS)

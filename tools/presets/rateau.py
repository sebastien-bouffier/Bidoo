# Factory presets for ratEaU (src/RATEAU.cpp): python tools/presets/rateau.py writes presets/RATEAU/
# A Trigger Riot: 16 knobs, each a division of a 16th-note clock (CLK, or the internal clock at the pattern's BPM).
# The counter starts at the pattern's cycle start; a division d with clock shift s fires on counts k*d + s (k >= 1),
# so every preset puts the cycle start on count 16 and picks the shifts with at() below: the first clock after
# loading (or a reset) is the downbeat, and "pos" is the 16th of the bar a hit lands on.
# Outputs: A1-A4 (right) = rows (knobs 1-4, 5-8, 9-12, 13-16), B1-B4 (bottom) = columns (knobs 1/5/9/13, ...).
# Matrix mode: one set feeds both; independent mode: rows read set A, columns set B (8 separate outputs).
# The whole state is in data (16 patterns x 3 sets x 16 cells); only pattern 1 is written, the others are empty.
# The 16 knob params show the current page of the current set; dataFromJson sets them again, they are given anyway.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from preset_kit import write

INDEP_A, INDEP_B, MATRIX = 0, 1, 2
START = 16   # cycle start: count 16 is the downbeat (divisions of 16 all fire on it)

def cell(div=0, step=0, prob=1.0, speed=24, cs=0, ts=0.0, gate=0.5):
	# [div, step, prob, speed (ticks per clock, 24 = 16th), clock shift, time shift (degrees), pulse width (0..1)]
	# PW is a share of the division plus its shift (one clock for a lone step), so a long or shifted division with the
	# default 25 % would hold its gate over the next hits of its row: `gate` gives the length in 16ths instead.
	base = (div + cs + ts / 360.0 * (div - cs)) * speed / 24.0 if div > 0 else speed / 24.0
	return [div, step, prob, speed, cs, ts, round(min(1.0, gate / base), 6)]

def empty_cell():
	return [0, 0, 1.0, 24, 0, 0.0, 0.25]   # RiotCell()

def at(div, pos=0, **kw):
	# a division of `div` 16ths whose hits include the 16th `pos` of the bar (0 = downbeat)
	return cell(div, cs=(START + pos) % div, **kw)

def once(pos, **kw):
	# a single hit per cycle, on the 16th `pos` of the bar (the STEP page)
	return cell(0, step=START + pos, **kw)

def empty_set():
	return [empty_cell() for _ in range(16)]

def grid(cells):
	# cells: {knob number 1..16: cell}
	s = empty_set()
	for k, c in cells.items():
		s[k - 1] = c
	return s

def data(sets, mode, cycle_start=START, cycle_end=START + 15, bpm=120.0):
	# sets: {set index: 16 cells}; one bar loop (counts 16..31) unless cycle_end says otherwise (0 = endless)
	banks = []
	for b in range(16):
		cells = [empty_set() for _ in range(3)]
		bank = {"cycleStart": 1, "cycleEnd": 0, "bpm": 120.0, "cells": cells}
		if b == 0:
			for s, c in sets.items():
				cells[s] = c
			bank.update(cycleStart=cycle_start, cycleEnd=cycle_end, bpm=bpm)
		banks.append(bank)
	return {"riotBanks": banks, "bank": 0, "mode": mode, "previousMode": MATRIX, "page": 0,
	        "mutes": [False] * 8, "running": True, "clock24ppq": False, "beatSync": False}

def params(d):
	# the defaults (configParam), DIV page on, the 16 knobs showing the DIV page of the current set
	p = {"DIV_PARAM": 1, "STEP_PARAM": 0, "PROB_PARAM": 0, "SPEED_PARAM": 0, "CLOCKSHIFT_PARAM": 0,
	     "TIMESHIFT_PARAM": 0, "PULSEWIDTH_PARAM": 0, "BANK_PARAM": 0, "INDEPEDITA_PARAM": 0, "INDEPEDITB_PARAM": 0,
	     "MATRIX_PARAM": 0, "STARTSTOP_PARAM": 0}
	for i in range(8):
		p["MUTE_PARAMS" if i == 0 else "MUTE_PARAMS+%d" % i] = 0
	cells = d["riotBanks"][0]["cells"][d["mode"]]
	for i in range(16):
		p["CONTROLS_PARAMS" if i == 0 else "CONTROLS_PARAMS+%d" % i] = round(cells[i][0] * 10.0 / 255.0, 6)
	return p

def preset(name, sets, mode, **kw):
	d = data(sets, mode, **kw)
	return (name, params(d), d)

GHOST = 0.25   # gate of ghost notes and shakers, in 16ths: short ticks

PRESETS = [
	# Four on the floor (independent mode, one bar): A1 kick on every beat, A2 clap on 2 and 4, A3 closed hat on the
	# 8ths, A4 open hat on the off-beats; B1 a crash on the downbeat of each bar, B2 a tom fill on the last three 16ths,
	# B3 a dotted-8th percussion line (3 over 16), B4 a shaker on the 16ths that skips about a third of its hits.
	preset("Four on the floor", {
		INDEP_A: grid({1: at(4), 5: at(8, 4), 9: at(2), 13: at(4, 2)}),
		INDEP_B: grid({1: at(16), 2: once(13), 6: once(14), 10: once(15), 3: at(3), 4: at(1, prob=0.7, gate=GHOST)}),
	}, INDEP_A),
	# Breakbeat (independent mode, one bar), after the Amen break: A1 kick on 16ths 0, 2, 10 and 11, A2 snare on 4 and
	# 12 with ghost notes on 7, 9 and 15 that come and go (probability 50-60 %, short), A3 hat on the 8ths, A4 open hat
	# on 14; B1 a ride on the beats, B2 a bar pulse to reset other sequencers.
	preset("Breakbeat", {
		INDEP_A: grid({1: at(16, 0), 2: at(16, 2), 3: at(16, 10), 4: at(16, 11),
		               5: at(8, 4), 6: at(16, 7, prob=0.6, gate=GHOST), 7: at(16, 9, prob=0.6, gate=GHOST), 8: at(16, 15, prob=0.5, gate=GHOST),
		               9: at(2), 13: at(16, 14)}),
		INDEP_B: grid({1: at(4), 2: at(16)}),
	}, INDEP_A),
	# Polyrhythm 4:3:5:7 (matrix mode, endless cycle): the diagonal knobs 1, 6, 11 and 16 divide by 4, 3, 5 and 7, all
	# starting on the first clock; knob 4 (16) and knob 13 (6) and knob 7 (8, on beat 2) mix in, so the columns are other
	# blends of the same lines: A1 quarters, A2 3 + 8, A3 5, A4 7 + 6, B1 4 + 6, B2 3, B3 5 + 8, B4 7 + 16. They line up
	# again every 1680 16ths; patch a bar pulse into RST to bring them together on each bar.
	preset("Polyrhythm 4-3-5-7", {
		MATRIX: grid({1: at(4), 6: at(3), 11: at(5), 16: at(7), 4: at(16), 13: at(6, 3), 7: at(8, 4)}),
	}, MATRIX, cycle_end=0),
	# Triplets and ratchets (independent mode, one bar), with the SPD page: A1 kick on the beats, A2 8th-note triplets
	# (speed 32, 8t) against the straight 16ths, A3 32nd-note hats (speed 12) at 55 % for ratchets and rolls, A4 a
	# quarter-note triplet (speed 64, 4t) for a slow cross-beat; B1 16th-note triplets (speed 16) at 35 %, short, for
	# hat flams and rolls; B2 a snare on 2 and 4.
	preset("Triplets and ratchets", {
		INDEP_A: grid({1: at(4), 5: cell(1, speed=32), 9: cell(1, speed=12, prob=0.55, gate=GHOST), 13: cell(1, speed=64)}),
		INDEP_B: grid({1: cell(1, speed=16, prob=0.35, gate=GHOST), 2: at(8, 4)}),
	}, INDEP_A),
	# Shuffle (independent mode, one bar), with the TMS page: the off-beat 16ths of the hat (A3, knob 10) are pushed a
	# third of a 16th late (time shift 120 deg), the on-beat ones (knob 9) stay put: a triplet shuffle. A1 kick on 0, 6
	# and 10, A2 snare on 4 and 12, A4 a ghost snare on the late off-beats at 40 %; B1 cowbell on 3 and 6, the 16ths
	# 2, 6, 10 and 14 at 50 % and the last 16th of the bar; B2 straight 16ths, to compare with A3.
	preset("Shuffle", {
		INDEP_A: grid({1: at(16, 0), 2: at(16, 6), 3: at(16, 10), 5: at(8, 4),
		               9: at(2, 0), 10: at(2, 1, ts=120.0), 13: at(2, 1, ts=120.0, prob=0.4, gate=GHOST)}),
		INDEP_B: grid({1: at(16, 3), 5: at(16, 6), 9: at(4, 2, prob=0.5), 13: once(15), 2: at(1, gate=GHOST)}),
	}, INDEP_A),
	# Generative percussion (independent mode, two bars): a steady core on A (kick 0 and 10, snare 4 and 12, 8th hats)
	# and four probability lines on B for congas, rim, shaker and clave: every bar comes out different. B1 16ths at
	# 30 %, B2 off-beat 8ths at 50 %, B3 dotted 8ths at 60 %, B4 a 5-step line at 70 %.
	preset("Generative percussion", {
		INDEP_A: grid({1: at(16, 0), 2: at(16, 10), 5: at(8, 4), 9: at(2)}),
		INDEP_B: grid({1: at(1, prob=0.3, gate=GHOST), 2: at(2, 1, prob=0.5), 3: at(3, prob=0.6), 4: at(5, prob=0.7)}),
	}, INDEP_A, cycle_end=START + 31),
	# Clock divider (independent mode, endless): square clocks at 50 % width from a 16th-note clock. A1-A4: /2, /4
	# (quarters), /8, /16 (bars); B1-B4: /3, /6, /32 (2 bars), /64 (4 bars). The cycle starts on count 64 so every
	# output starts high on the first clock; patch a reset into RST to bring them back in line.
	preset("Clock divider", {
		INDEP_A: grid({1: cell(2, gate=1), 5: cell(4, gate=2), 9: cell(8, gate=4), 13: cell(16, gate=8)}),
		INDEP_B: grid({1: cell(3, cs=64 % 3, gate=1.5), 2: cell(6, cs=64 % 6, gate=3), 3: cell(32, gate=16), 4: cell(64, gate=32)}),
	}, INDEP_A, cycle_start=64, cycle_end=0),
]

write("RATEAU", "RATEAU.cpp", PRESETS)

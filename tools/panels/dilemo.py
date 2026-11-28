# DilEMO's panel (5 HP), migrated from the hand-drawn one: the same arrangement (the threshold knob under the title, then
# the AND, OR and XOR pairs and NOT: each pair's two inputs in one tall black box beside its two outputs in a tall red one,
# NOT alone at the bottom), now in the theme's boxes, type and colours, with the old picture kept (dilemo_parts.svg). No
# screws, as before. The positions are the same in src/DILEMO.cpp.
import os, re
import panel_kit as k

PW = 75
W = PW * k.K
# The boxes: 26 px wide and 6 px from the panel's sides as póUPrè's (the old ones were 28.3 px, 5 px from the sides), so
# the inputs sit at 19 and the outputs at 56 (both were 0.15 px left). The same in src/DILEMO.cpp (kBoxSide, kBoxWidth)
BOX_SIDE, BOX_W = 6.0, 26.0
IN_X = BOX_SIDE + BOX_W / 2
OUT_X = PW - IN_X
# The rows: NOT on the bottom row, the XOR pair's lower jacks on row 1, the pairs 46 px apart (a box, 4 px, the next
# box), the two jacks of a pair 37 px apart (they were 43 px, all rows: seven boxed rows at 43 px don't fit between the
# knob and the bottom row once the boxes are 25 above and 17 below their jacks). src/DILEMO.cpp kPairPitch
PAIR_PITCH = 37.0
LOWER_Y = [k.row(1) - (2 - j) * (k.ROW_PITCH + PAIR_PITCH) for j in range(3)]   # 130, 213, 296
JACK_Y = [y for low in LOWER_Y for y in (low - PAIR_PITCH, low)] + [k.row(0)]   # 93 130 176 213 259 296 342
OUT_CAPTIONS = ('&&', '!&&', '||', '!||', '^', '!^', '!')
# The threshold knob on the panel's centre, 4.24 px lower than before (old corner 22.5, 30) so that its label clears the
# title and the knob clears the first box (4.2 px). src/DILEMO.cpp kKnobY
KNOB_Y = 49.0

def parts():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dilemo_parts.svg'), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def build():
    out = [k.header(PW, W), k.background(W), parts(), k.title('DilEMO', PW)]   # 57.7 px wide at 16: fits, no screws
    out.append(k.knob_label('THRESHOLD', PW / 2, KNOB_Y))
    # one tall box per pair of jacks, as the old panel; NOT in its own box
    for r in (0, 2, 4, 6):
        top = JACK_Y[r] - k.BOX_ABOVE
        bottom = JACK_Y[r + 1 if r < 6 else r] + k.BOX_BELOW
        out.append(k.box(BOX_SIDE, top, BOX_W, bottom - top, k.INPUT))
        out.append(k.box(PW - BOX_SIDE - BOX_W, top, BOX_W, bottom - top, k.OUTPUT))
    for r, y in enumerate(JACK_Y):
        above = JACK_Y[r - 1] if r % 2 == 1 else None   # the lower jack of a pair: its caption under the upper jack
        out.append(k.jack_label('IN', IN_X, y, jack_above=above))
        out.append(k.jack_label(OUT_CAPTIONS[r], OUT_X, y, jack_above=above))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('DILEMO.svg', build())

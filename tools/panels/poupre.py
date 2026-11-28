# póUPrè's panel (5 HP), migrated from the hand-drawn one: the same arrangement (the sample light and the channel knob
# under the title with its dial ticks, the four preset buttons on the right with their numbers, five rows of a boxed jack
# joined by a line to its switch or knob, TRIG and OUT), now in the theme's boxes, type and colours, with the old picture
# and dial ticks kept (poupre_parts.svg). No screws, as before. The positions are the same in src/POUPRE.cpp.
import os, re
import panel_kit as k

PW = 75
W = PW * k.K
# The boxes: one per jack, 26 px wide and 6 px from the panel's sides as oAï's (the old ones were 5 px), so the two
# columns are symmetric about the centre: the jacks at 19 (they were 0.15 px left), the controls and OUT at 56 (the knobs
# were 0.24 px left, OUT 0.16 px). The same in src/POUPRE.cpp (kBoxSide, kBoxWidth)
BOX_SIDE, BOX_W = 6.0, 26.0
JACK_X = BOX_SIDE + BOX_W / 2
CONTROL_X = PW - JACK_X
ROW_PITCH = 47.0                       # the old rows, kept (src/POUPRE.cpp kRowPitch): TRIG and OUT on the bottom row
JACK_Y = [k.row(0) - ROW_PITCH * (5 - i) for i in range(6)]   # GATE LOOP STRT LEN V/O TRIG; were 0.15 px higher
CKSS_HALF = 20.64111 / 2
KNOB_HALF = 29.5275591 / 2
# the controls keep their old rows, 4 px above their jacks, the line to their box at their centre as before
GATE_Y = 92.5 + CKSS_HALF              # CKSS, old corner 92.5; LOOP one row below
START_Y = 182 + KNOB_HALF              # BidooBlueKnob, old corner 182; LEN and TUNE below
CONTROL_Y = [GATE_Y, GATE_Y + ROW_PITCH, START_Y, START_Y + ROW_PITCH, START_Y + 2 * ROW_PITCH]
CAPTIONS = ('GATE', 'LOOP', 'STRT', 'LEN', 'V/O', 'TRIG')
# the preset buttons' numbers, at 6.5 as maGma's (7.5 would touch the buttons), their capitals centred on each light;
# 1.4 px left of the buttons (old corner x 66, src/POUPRE.cpp kPresetPairs)
PRESET_X = 60.5
LIGHT_HALF = 2 * 75 / 25.4 / 2         # SmallLight, mm2px(2) / 2
PRESET_TOPS = (20, 30, 62, 72)         # the lights' old tops (src/POUPRE.cpp)
PRESET_SIZE = 6.5

def parts():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'poupre_parts.svg'), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def line(x0, x1, y):
    # the old rule from a jack's box to its control
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:0.26458" d="M{x0 * k.K:.4f},{y * k.K:.4f}H{x1 * k.K:.4f}"/>'

def build():
    out = [k.header(PW, W), k.background(W), parts(), k.title('póUPrè', PW)]   # 57.7 px wide at 16: fits, no screws
    for n, top in zip(('8', '16', '32', '64'), PRESET_TOPS):
        out.append(k.text(n, PRESET_X, top + LIGHT_HALF + k.CAP_HEIGHT * PRESET_SIZE / 2, PRESET_SIZE, k.INK))
    # the switches: their two positions above and below them, as lIMbO's
    for y, up, down in ((CONTROL_Y[0], 'TRIG', 'GATE'), (CONTROL_Y[1], 'ON', 'OFF')):
        out.append(k.text(up, CONTROL_X, y - CKSS_HALF - 4, k.BOX_LABEL_SIZE, k.INK))
        out.append(k.text(down, CONTROL_X, y + CKSS_HALF + 4 + k.CAP_HEIGHT * k.BOX_LABEL_SIZE, k.BOX_LABEL_SIZE, k.INK))
    for y in CONTROL_Y:
        out.append(line(BOX_SIDE + BOX_W, CONTROL_X, y))
    for y, lab in zip(JACK_Y, CAPTIONS):
        out.append(k.box(BOX_SIDE, y - k.BOX_ABOVE, BOX_W, k.BOX_ABOVE + k.BOX_BELOW, k.INPUT))
        out.append(k.jack_label(lab, JACK_X, y))
    out.append(k.box(PW - BOX_SIDE - BOX_W, k.box_top(0), BOX_W, k.BOX_ABOVE + k.BOX_BELOW, k.OUTPUT))
    out.append(k.jack_label('OUT', CONTROL_X, k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('POUPRE.svg', build())

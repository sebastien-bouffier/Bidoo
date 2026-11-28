# dTrOY's panel (34 HP), migrated from the hand-drawn one: the same arrangement (left, the section it shares with bordL,
# metroseq_panel.py, over G1, G2, GATE and V/O; right, the eight stages under their bracketed names: pitch knobs with the
# sensitivity jack and trimpot on the PITCH line, pulse count sliders by their 1 to 8, gate mode sliders by their icons,
# slide and skip buttons, the stage outputs), now in the theme's boxes, type and colours, with the old picture kept
# (dtroy_parts.svg). The positions are the same in src/DTROY.cpp and src/MetroSeq.hpp.
import panel_kit as k
import metroseq_panel as m

PW = 510
W = PW * k.K
# the stages' columns, as the old panel spaces them (38 or 39 px): the sliders', buttons' and jacks' centres; the pitch
# knobs were 0.76 px right of them, the tiny outputs 0.5. src/DTROY.cpp kStageLeft, kStageCentre
STAGE_X = tuple(x + 11 for x in (200, 238, 276, 315, 353, 392, 430, 469))
HANDLE = 12.0                          # bidooSlidePotHandle's height: a slider's value v sits 12 px a step
PULSE_TOP, TYPE_TOP = 87.0, 204.0      # the sliders' old corners (src/DTROY.cpp)
def pulse_y(v):                        # 1 to 8, 8 at the top
    return PULSE_TOP + HANDLE / 2 + (8 - v) * HANDLE
def type_y(t):                         # rest, single, multiple, hold, G1, G2 from the bottom
    return TYPE_TOP + HANDLE / 2 + (5 - t) * HANDLE
SLIDE_Y, SKIP_Y = 297 + 9, 321 + 9     # the old LEDButtons' centres, now BidooRoundButtons (src/DTROY.cpp)
OUT_Y = 344 + 7.5                      # the tiny stage outputs, old corner 344: full jacks in boxes on the bottom row would
                                       # sit under the skip buttons, so they stay tiny, each on its own red square
OUT_SQUARE = 18.0
LEGEND_RIGHT = 197.8                   # the digits, gate icons and SL / SK end here, as before
ICON_W, ICON_H, ICON_SQUARE, ICON_STEP = 28.0, 8.0, 8.0, 10.0   # the old gate icons, each now on its handle's row
                                       # (they were 0.3 to 1.5 px lower)
ICON_LINE = 0.29104                    # their outline, the old one
# the stages' names on their brackets, which reach from the first stage's column to the last; the lines at their old
# heights (PITCH's 0.34 px higher, so that its name shares CLOCK's baseline), the names centred on them at the theme's
# size (they were 11)
BRACKET_X = (STAGE_X[0], STAGE_X[-1])
PITCH_LINE = 25.66                     # src/DTROY.cpp pitchLine
BRACKETS = (('PITCH', PITCH_LINE), ('PULSE COUNT', 77.5), ('GATE MODE', 196.0), ('SLIDE/SKIP', 287.4))
TICK = 5.0
NAME_GAP = 4.0                         # between a bracket's line and its name
# the pitch sensitivity: its tiny jack and trimpot on the PITCH line (they were 0.16 px higher and 1.94 px lower), S over
# the bracket's end as before. src/DTROY.cpp
S_LABEL = (476.2, 23.0)

def bracket(lab, y):
    out = [m.vline(BRACKET_X[0], y, y + TICK), m.vline(BRACKET_X[1], y, y + TICK)]
    mid = (BRACKET_X[0] + BRACKET_X[1]) / 2
    half = m.width(lab, m.SECTION_SIZE) / 2 + NAME_GAP
    out.append(m.hline(BRACKET_X[0], mid - half, y))
    out.append(m.hline(mid + half, BRACKET_X[1], y))   # under the sensitivity jack and trimpot on PITCH's
    out.append(k.text(lab, mid, m.centred_on(y, m.SECTION_SIZE), m.SECTION_SIZE, k.INK))
    return out

def gate_icons():
    x = LEGEND_RIGHT - ICON_W
    out = []
    for t in range(4):
        y = type_y(t) - ICON_H / 2
        if t == 2:
            for j in range(3):
                out.append(m.rect(x + j * ICON_STEP, y, ICON_SQUARE, ICON_H, True, ICON_LINE))
        else:
            out.append(m.rect(x, y, ICON_W, ICON_H, t == 3, ICON_LINE))
            if t == 1:
                out.append(m.rect(x, y, ICON_SQUARE, ICON_H, True, ICON_LINE))
    return out

def build():
    out = [k.header(PW, W), k.background(W), m.parts('dtroy_parts.svg'), k.title('dTrOY', PW)]
    out += m.left_section()
    out += m.bottom_boxes(m.COLS, m.BOX_W, ('G1', 'G2', 'GATE', 'V/O'), (k.INPUT, k.INPUT, k.OUTPUT, k.OUTPUT))
    for lab, y in BRACKETS:
        out += bracket(lab, y)
    out.append(k.text('S', S_LABEL[0], S_LABEL[1], k.BOX_LABEL_SIZE, k.INK))
    # the sliders' positions: 1 to 8, then G2, G1 and the gate icons, at the size of a switch's positions
    for v in range(1, 9):
        out.append(m.text_right(str(v), LEGEND_RIGHT, m.centred_on(pulse_y(v), k.BOX_LABEL_SIZE), k.BOX_LABEL_SIZE))
    for t, lab in ((4, 'G1'), (5, 'G2')):
        out.append(m.text_right(lab, LEGEND_RIGHT, m.centred_on(type_y(t), k.BOX_LABEL_SIZE), k.BOX_LABEL_SIZE))
    out += gate_icons()
    for lab, y in (('SL', SLIDE_Y), ('SK', SKIP_Y)):
        out.append(m.text_right(lab, LEGEND_RIGHT, m.centred_on(y, k.BOX_LABEL_SIZE), k.BOX_LABEL_SIZE))
    for x in STAGE_X:
        out.append(m.output_square(x, OUT_Y, OUT_SQUARE))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('DTROY.svg', build())

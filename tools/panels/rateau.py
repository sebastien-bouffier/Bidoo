# rATEAU's panel (15 HP), migrated from the hand-drawn one: the same arrangement (the CLK box with RUN and its trigger
# jack, RST, the INDEP A / B and MATRIX buttons with the mode jack, the pattern knob joined to its PTRN jack, the screen
# and its seven page buttons, the 4 x 4 knobs numbered 1 to 16 with a mute for each row and each column, the A outputs
# in a column on the right and the B outputs in a row at the bottom), now in the theme's boxes, type and colours, with
# the old picture kept (rateau_parts.svg). The outputs keep the old red L with its white A and B, as the user drew it
# (rateau_outputs.svg): the A column on the knobs' rows, the B row at its old height. The positions are the same in
# src/RATEAU.cpp.
import os, re
import panel_kit as k

PW = 225
W = PW * k.K
SIZE = k.BOX_LABEL_SIZE
CAP = k.CAP_HEIGHT * SIZE
LINE = 0.26458

TOP_Y = 48.5 + 23.7 / 2              # the top row's jacks, old corner 48.5 (src/RATEAU.cpp kTopY); the knob on it too
TOP_BOX = (TOP_Y - k.BOX_ABOVE, TOP_Y + k.BOX_BELOW)   # its boxes (they were 33.4 to 76.1)
CLK_X, RST_X = 6 + 23.7 / 2, 52 + 23.7 / 2             # old corners
CLK_BOX = (3.0, 47.0)                # as before from 3 (the CLK jack and RUN don't fit from the margin), 4 px to RST's
RST_BOX = (51.0, 2 * RST_X - 51.0)   # centred on its jack
RUN_X = 38.0                         # RUN and its trigger jack, 1.5 px left to leave the box's edge room (kRunX)
RUN_Y, RUN_JACK_Y = 52.0, 66.5       # 3 and 2 px lower: RUN's caption sits on the row's baseline (kRunY, kRunJackY)
RUN_SIZE = 6.5                       # the caption of a 10 px button, in a narrow box
MODE_X = (89.0, 111.5, 134.0)        # INDEP A, INDEP B, MATRIX: the old LEDButtons' centres (kModeX)
MODE_Y = 54.0
MODE_JACK_Y = 75.5                   # the mode jack under MATRIX, 1 px lower to give its box room (kModeJackY)
MODE_BOX = 9.5                       # its box around the tiny jack: 2 px from MATRIX and from the screen
LABEL_Y = MODE_Y - k.SMALL_LABEL_ABOVE   # INDEP and MATRIX, above their buttons
AB_Y = 74.0                          # A and B under the INDEP buttons, as before
BANK_X = 149 + 29.5275591 / 2        # the pattern knob, old corner (on the top row, 0.04 px lower)
RIGHT_BOX = (184.0, PW - k.MARGIN)   # PTRN's box and the A boxes: one column, to the margin (kRightX: its centre)
RIGHT_X = sum(RIGHT_BOX) / 2
SCREEN = (k.MARGIN, 88.0, PW - 2 * k.MARGIN, 23.0)     # the display's text is at 17, 103 (src/RATEAU.cpp); it was 11..212.7, 86.1..110.7
KNOB0 = (15 + 29.5275591 / 2, 150 + 29.5275591 / 2)    # the first knob, old corner (kKnobPitch 43 across and down)
PITCH = 43.0
NUMBER = (14.8, 16.6)                # a knob's number at its lower right, as before

def outputs():
    # the red L and its A and B, from the old panel (mm units, scaled like the picture)
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rateau_outputs.svg'), encoding='utf-8') as f:
        s = f.read()
    return f'<g transform="scale({W / 76.2:.6f},{k.H / 128.5:.6f})">' + re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1) + '</g>'

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rateau_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    # the old panel is in mm (76.2 x 128.5): scale it to the panel's units
    return f'<g transform="scale({W / 76.2:.6f},{k.H / 128.5:.6f})">' + re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1) + '</g>'

def poly(*pts):
    return (f'<path style="fill:none;stroke:{k.INK};stroke-width:{LINE}" d="M'
            + 'L'.join(f'{x * k.K:.4f},{y * k.K:.4f}' for x, y in pts) + '"/>')

def knob(c, r):
    return KNOB0[0] + PITCH * c, KNOB0[1] + PITCH * r

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('ratEaU', PW)]
    # the top row: CLK (with RUN and its trigger), RST, PTRN, in black boxes
    for (x0, x1) in (CLK_BOX, RST_BOX, RIGHT_BOX):
        out.append(k.box(x0, TOP_BOX[0], x1 - x0, TOP_BOX[1] - TOP_BOX[0], k.INPUT))
    out.append(k.jack_label('CLK', CLK_X, TOP_Y))
    out.append(k.jack_label('RUN', RUN_X, TOP_Y, size=RUN_SIZE))
    out.append(k.jack_label('RST', RST_X, TOP_Y))
    out.append(k.jack_label('PTRN', RIGHT_X, TOP_Y))
    # the modes: INDEP over A and B with its bracket down to their names, MATRIX over its button and the mode jack
    a, b = MODE_X[0], MODE_X[1]
    bracket = LABEL_Y + 3
    out.append(k.text('INDEP', (a + b) / 2, LABEL_Y, SIZE, k.INK))
    out.append(poly((a, AB_Y - CAP - 2), (a, bracket), (b, bracket), (b, AB_Y - CAP - 2)))
    out.append(k.text('A', a, AB_Y, SIZE, k.INK))
    out.append(k.text('B', b, AB_Y, SIZE, k.INK))
    out.append(k.text('MATRIX', MODE_X[2], LABEL_Y, SIZE, k.INK))
    out.append(poly((MODE_X[2], bracket), (MODE_X[2], MODE_Y)))
    out.append(k.box(MODE_X[2] - MODE_BOX, MODE_JACK_Y - MODE_BOX, 2 * MODE_BOX, 2 * MODE_BOX, k.INPUT))
    # the pattern knob, joined to its jack
    out.append(poly((BANK_X, TOP_Y), (RIGHT_BOX[0], TOP_Y)))
    out.append(k.screen(*SCREEN))
    # the knobs' numbers
    for i in range(16):
        x, y = knob(i % 4, i // 4)
        out.append(k.text(str(i + 1), x + NUMBER[0], y + NUMBER[1], SIZE, k.INK))
    # the outputs: the old red L, A down the right on the knobs' rows, B along the bottom under the knobs' columns
    out.append(outputs())
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('RATEAU.svg', build())

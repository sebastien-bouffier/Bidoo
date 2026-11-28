# ACnE's panel (34 HP), migrated from the hand-drawn one: the same arrangement (the 16 x 8 matrix of coloured trimpots,
# the snapshot buttons under their bracket with the scene CV jack at its left end and RAMP on its right, AUTOSAVE, SAVE,
# COPY/PASTE and VOLUME joined to their labels by lines, the output mutes left of the matrix, the input mutes and solos
# under it with the track links between them, the M / M / S clear buttons, the strips of tiny jacks: outputs on the red
# ones at the right, inputs on the black one at the bottom), now in the theme's type and colours, with the old picture
# kept (acne_parts.svg). The positions are the same in src/ACNE.cpp.
import os, re
import panel_kit as k

PW = 510
W = PW * k.K
LINE = 0.26458                      # the lines joining labels to controls, as the old panel's
SIZE = k.BOX_LABEL_SIZE             # every label is a button's, a trimpot's or a side label (VOLUME on its line)
CAP = k.CAP_HEIGHT * SIZE           # a label's capitals: centred on a line, the baseline is CAP / 2 under it

# The matrix: the trimpots keep their old corners (43 + 27 j, 77 + 27 i, 6.3 mm), every column and row of the module
# shares them (src/ACNE.cpp firstTrim, kPitch)
TRIM_HALF = 6.3 * 75 / 25.4 / 2
PITCH = 27.0
def X(j): return 43 + TRIM_HALF + PITCH * j
def Y(i): return 77 + TRIM_HALF + PITCH * i

SNAP_Y = 58.0                       # the snapshot buttons, the old LEDButtons' centre (src/ACNE.cpp kSnapY)
MUTE_Y, SOLO_Y = 301.0, 323.0       # the input mutes and solos (kMuteY, kSoloY)
LINK_Y = (MUTE_Y + SOLO_Y) / 2      # the track links, between them and between two columns
BUTTON_HALF = 10.0                  # BidooRoundButton
OUT_X = 489.5                       # the outputs' column, old corner 482 (kOutX); VOLUME over it
VOLUME = (OUT_X, 39 + 29.5275591 / 2)
RAMP = (432 + 19.1929131 / 2, 28 + 19.1929131 / 2)   # BidooBlueTrimpot at its old corner
BRACKET_Y = RAMP[1]                 # the SNAPSHOTS bracket, through RAMP (0.4 px higher than the old line)
SCENE_CV = (65.5, BRACKET_Y)        # the scene CV tiny jack at the bracket's left end (old corner 58, 30: 0.1 px lower)
CV_BOX = 10.5                       # its box, around the tiny jack: 1.3 px from the first snapshot button's edge
COPY = (21.0, 53.0)                 # the copy / paste button, the old CKD6's centre (kCopy)
AUTOSAVE, SAVE = (43.0, 12.0), (467.0, 12.0)   # the old LEDButtons' centres, 2 px further from the screws (kAutosave, kSave)
TOP_LINE = 24.0                     # COPY/PASTE and VOLUME on their lines (2 px higher, to clear the scene CV's box)
IN_Y = 347.5                        # the input jacks, old corner 340 (kInY)
STRIP_TOP, STRIP_BOTTOM = 336.0, k.BOTTOM_BOTTOM       # the inputs' strip, between the solos and the panel's bottom row
STRIP_LEFT, STRIP_RIGHT = X(0) - 12, PW - k.MARGIN     # 12 px left of the first jack, to the margin (it was at 501)
OUT_LEFT = 473.0                    # the red strips' left edge, as before
OUT_ROOM = 11.5                     # a strip's edge from its first and last jack
OUT_SPLIT = (Y(1) + Y(2)) / 2       # outputs 1-2 (VOLUME) above, 3-8 below, the theme's gap between
OUT_BOTTOM = 326.0                  # room for OUT under the last jack, as before (326.1)
MS = ((10.0, MUTE_Y), (29.0, MUTE_Y), (19.0, SOLO_Y))   # the clear buttons M, M, S: the old corners (S 1 px lower)

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'acne_parts.svg'), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def width(s, size=SIZE):
    return sum(k._glyphs[k._cmap[ord(c)]].width for c in s) * size / k._upm

def path(d, col=None):
    return f'<path style="fill:none;stroke:{col or k.INK};stroke-width:{LINE}" d="{d}"/>'

def poly(*pts, col=None):
    return path('M' + 'L'.join(f'{x * k.K:.4f},{y * k.K:.4f}' for x, y in pts), col)

def towards(a, b, r):
    # the point r px from a towards b: a line starting just outside a control
    dx, dy = b[0] - a[0], b[1] - a[1]
    n = (dx * dx + dy * dy) ** 0.5
    return (a[0] + dx * r / n, a[1] + dy * r / n)

def on_line(s, x, y, col=k.INK):
    # a label whose capitals are centred on y
    return k.text(s, x, y + CAP / 2, SIZE, col)

def rotated(s, x, y, col):
    # reading upwards, its capitals centred on (x, y)
    return (f'<g transform="translate({x * k.K:.4f},{y * k.K:.4f}) rotate(-90)">'
            f'{k._text(s, 0, CAP / 2, SIZE, col)}</g>')

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('ACnE', PW)]
    gap = 3.0                       # between a label and its line
    # AUTOSAVE and SAVE beside their buttons, under the screws' row
    a = AUTOSAVE[0] + BUTTON_HALF + 4 + width('AUTOSAVE') / 2
    out.append(on_line('AUTOSAVE', a, AUTOSAVE[1]))
    out.append(on_line('SAVE', SAVE[0] - BUTTON_HALF - 4 - width('SAVE') / 2, SAVE[1]))
    # COPY/PASTE: a line from the button up to its label, as before
    corner = (52.0, TOP_LINE)
    out.append(poly(towards(COPY, corner, BUTTON_HALF + 2), corner, (86.0, TOP_LINE)))
    out.append(on_line('COPY/PASTE', 86 + gap + width('COPY/PASTE') / 2, TOP_LINE))
    # VOLUME: the same from the knob, mirrored; a red tick joins it to outputs 1-2
    corner = (455.0, TOP_LINE)
    vx = corner[0] - gap - width('VOLUME') / 2
    out.append(poly(towards(VOLUME, corner, 29.5275591 / 2 + 2), corner, (vx + width('VOLUME') / 2 + gap, TOP_LINE)))
    out.append(on_line('VOLUME', vx, TOP_LINE))
    out.append(poly((OUT_X, VOLUME[1] + 29.5275591 / 2), (OUT_X, Y(0) - OUT_ROOM + 1), col=k.OUTPUT))
    # the SNAPSHOTS bracket over the buttons, from the scene CV jack to the last one, broken by its name and by RAMP
    sx, sw = PW / 2, width('SNAPSHOTS')             # under the title (the buttons' middle is 0.2 px left)
    rw = width('RAMP')
    rx = RAMP[0] - 19.1929131 / 2 - gap - 6 - gap - rw / 2      # RAMP, a short line to its trimpot
    tick = SNAP_Y - BUTTON_HALF
    out.append(poly((X(0), tick), (X(0), BRACKET_Y), (sx - sw / 2 - gap, BRACKET_Y)))
    out.append(on_line('SNAPSHOTS', sx, BRACKET_Y))
    out.append(poly((sx + sw / 2 + gap, BRACKET_Y), (rx - rw / 2 - gap, BRACKET_Y)))
    out.append(on_line('RAMP', rx, BRACKET_Y))
    out.append(poly((rx + rw / 2 + gap, BRACKET_Y), (RAMP[0] - 19.1929131 / 2, BRACKET_Y)))
    out.append(poly((RAMP[0] + 19.1929131 / 2, BRACKET_Y), (X(15), BRACKET_Y), (X(15), tick)))
    # the scene CV jack in its own small box (tiny: a full jack would touch the COPY/PASTE line and the first button)
    out.append(k.box(SCENE_CV[0] - CV_BOX, SCENE_CV[1] - CV_BOX, 2 * CV_BOX, 2 * CV_BOX, k.INPUT))
    # the clear buttons: the right M (inputs) and S joined to their rows, the left M (outputs) to the output mutes
    out.append(poly((MS[1][0], MUTE_Y), (X(0) - BUTTON_HALF - 1, MUTE_Y)))
    out.append(poly((MS[2][0], SOLO_Y), (X(0) - BUTTON_HALF - 1, SOLO_Y)))
    out.append(poly((4.6, 303.9), (12.0, 288.1)))                 # the old slash, as it was
    # the outputs: two red strips of tiny jacks, OUT reading upwards under the last one
    out.append(k.box(OUT_LEFT, Y(0) - OUT_ROOM, STRIP_RIGHT - OUT_LEFT, OUT_SPLIT - k.BOX_GAP / 2 - Y(0) + OUT_ROOM, k.OUTPUT))
    out.append(k.box(OUT_LEFT, OUT_SPLIT + k.BOX_GAP / 2, STRIP_RIGHT - OUT_LEFT, OUT_BOTTOM - OUT_SPLIT - k.BOX_GAP / 2, k.OUTPUT))
    out.append(rotated('OUT', OUT_X, (Y(7) + 7.5 + OUT_BOTTOM) / 2, k.WHITE))
    # the inputs: a black strip of sixteen tiny jacks with their meters, IN at its right end under OUT
    out.append(k.box(STRIP_LEFT, STRIP_TOP, STRIP_RIGHT - STRIP_LEFT, STRIP_BOTTOM - STRIP_TOP, k.INPUT))
    out.append(on_line('IN', OUT_X, IN_Y, k.WHITE))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('ACNE.svg', build())

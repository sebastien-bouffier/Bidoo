# SpOre's panel (3 HP), migrated from the hand-drawn one: the same arrangement (four knobs down the middle, each joined
# by a line to its tiny CV jack and named down the left side; the window's two trimpots side by side; IN and OUT), now
# in the theme's boxes, type and colours, with the old picture kept (spore_parts.svg). No screws, as before. The
# positions are the same in src/SPORE.cpp.
import panel_kit as k
import frein as f                     # the 3 HP title, logo, line and boxes (frein.py)

PW = 45
W = PW * k.K
COL = PW / 2                           # the knobs, the CV jacks and the boxes, on the panel's centre as before
KNOB_HALF = 29.5275591 / 2             # BidooBlueKnob
# the knobs: the first 5 px lower than its old corner (15) so it clears the title, the next ones 52 px apart (were 54)
# so that the trimpots' name fits under the last CV jack. src/SPORE.cpp kKnobTop, kGroupPitch
KNOB_TOP = 20.0
GROUP_PITCH = 52.0
CV_BELOW = 34.5 + 15 / 2 - KNOB_HALF   # a CV jack under its knob, as before (corners 34.5 apart): src/SPORE.cpp kCvBelow
NAMES = ('PITCH', 'SIZE', 'ANALYSIS', 'SYNTHESIS')
SIDE_SIZE = 7.5
SIDE_X = (COL - KNOB_HALF) / 2         # the names' capitals centred between the panel's edge and the knobs (3.9)
# the trimpots: their old x (corners 2 and 24), 255 their centre (old 251.4): 6 px under the last CV jack's name above,
# 6.4 px over the IN box. src/SPORE.cpp kTrimpotY
TRIMPOT_X = (2 + 19.1929131 / 2, 24 + 19.1929131 / 2)
TRIMPOT_Y = 255.0

def side_text(s, y):
    # rotated a quarter turn, read from below, its capitals' middle at (SIDE_X, y)
    c = k.CAP_HEIGHT * SIDE_SIZE / 2
    return (f'<g transform="rotate(-90,{SIDE_X * k.K:.4f},{y * k.K:.4f})">'
            f'{k._text(s, SIDE_X, y + c, SIDE_SIZE, k.INK)}</g>')

def build():
    out = [k.header(PW, W), k.background(W), f.parts('spore_parts.svg'), f.title('SpOre')]   # its old title: 13, the largest that leaves 3 px each side
    for i, name in enumerate(NAMES):
        y = KNOB_TOP + KNOB_HALF + GROUP_PITCH * i
        out.append(f.line(y, y + CV_BELOW))
        out.append(side_text(name, y))
    # one name over both trimpots, centred: WINDOW (34 px at 7.5) can't be centred over the left one (11.6) on 45 px,
    # nor SHAPE (24.8) over the right one; the old names were 2.7 px tall
    out.append(k.small_label('WINDOW', COL, TRIMPOT_Y))
    out += f.jack('IN', 1, k.INPUT)
    out += f.jack('OUT', 0, k.OUTPUT)
    out.append(f.logo())   # scaled to leave 3 px each side, as the other 3 HP panels
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('SPORE.svg', build())

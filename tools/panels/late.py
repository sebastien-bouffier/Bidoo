# lATe's panel (3 HP), migrated from the hand-drawn one: the same arrangement (OFFSET, its knob joined by a line through
# its CV amount to its CV jack, RST, IN and OUT at the bottom, down the middle), now in the theme's boxes, type and
# colours, with the old picture kept (late_parts.svg). No screws, as before. The positions are the same in src/LATE.cpp.
# The 3 HP title, logo, line and boxes are fREIN's (frein.py).
import panel_kit as k
import frein as f

PW = f.PW
W = PW * k.K
KNOB_Y = 70 + 29.5275591 / 2       # the swing knob, old corner 70 (src/LATE.cpp swing)
CV_Y = 130 + 23.7 / 2              # its CV jack, old corner 130 (src/LATE.cpp swingCv); the trimpot sits on the line
LINE = 0.37041664                  # this panel's old line, a little heavier than fREIN's

def build():
    out = [k.header(PW, W), k.background(W), f.parts('late_parts.svg'), f.title('lATe')]   # 16
    out.append(f.line(KNOB_Y, CV_Y, width=LINE))
    out.append(k.knob_label('OFFSET', f.COL, KNOB_Y))
    out += f.jack('RST', 2, k.INPUT)
    out += f.jack('IN', 1, k.INPUT)
    out += f.jack('OUT', 0, k.OUTPUT)
    out.append(f.logo())
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('LATE.svg', build())

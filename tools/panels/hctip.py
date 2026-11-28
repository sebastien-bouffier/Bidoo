# HCTIP's panel (3 HP), migrated from the hand-drawn one: the same arrangement (the pitch knob joined by a line to its
# CV jack, IN and OUT at the bottom, down the middle), now in the theme's boxes, type and colours, with the old picture
# kept (hctip_parts.svg). The knob is named PITCH (it had no name). No screws, as before. The positions are the same in
# src/HCTIP.cpp. The 3 HP title, logo, line and boxes are fREIN's (frein.py).
import panel_kit as k
import frein as f

PW = f.PW
W = PW * k.K
KNOB_Y = 70 + 29.5275591 / 2       # the pitch knob, old corner 70 (src/HCTIP.cpp pitch)
CV_Y = 130 + 23.7 / 2              # its CV jack, old corner 130 (src/HCTIP.cpp pitchCv)

def build():
    out = [k.header(PW, W), k.background(W), f.parts('hctip_parts.svg'), f.title('HCTIP')]   # 13
    out.append(k.knob_label('PITCH', k.jack_col(0, 1, PW), KNOB_Y))
    out.append(f.line(KNOB_Y, CV_Y))
    out += f.jack('IN', 1, k.INPUT)
    out += f.jack('OUT', 0, k.OUTPUT)
    out.append(f.logo())
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('HCTIP.svg', build())

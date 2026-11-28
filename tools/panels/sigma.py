# Σ's panel (5 HP), migrated from the hand-drawn one: the same arrangement (two columns of three groups, three inputs in
# a black box joined by a short line to their sum's output in a red box below), now in the theme's boxes, type and
# colours, with the old picture kept (sigma_parts.svg). No screws, as before. The positions are the same in src/SIGMA.cpp.
import os, re
import panel_kit as k

PW = 75
W = PW * k.K
N = 2                                  # the two columns on the grid: boxes from the side margins, 4 px apart (the old
                                       # ones were 24 px wide, 6.5 px apart: these are 25.5); the jacks 0.25 px nearer the centre
# Tiny jacks: twelve full jacks in their boxes don't fit one above the other in 380 px. The outputs are boxed as any
# jack, 25 above and 17 below, the last on the bottom row; each group of inputs sits in its box 4 px above its output's,
# 12 px between the box's edges and the outer jacks (the old 12.5), the jacks 19 px apart (they were 20). The groups are
# 112 px apart (they were 100) to fill the height. The same in src/SIGMA.cpp (kGroupPitch, kInputPitch, kLastInputAbove)
GROUP_PITCH = 112.0
IN_PITCH = 19.0
IN_PAD = 12.0                          # from an input box's edge to its outer jack
LAST_IN_ABOVE = k.BOX_ABOVE + k.BOX_GAP + IN_PAD   # 41: from the last input to its output
OUT_Y = [k.row(0) - GROUP_PITCH * (2 - g) for g in range(3)]   # 118 230 342; they were 127.5 227.5 327.5

def parts():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sigma_parts.svg'), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def joint(x, y0, y1):
    # the old line from the inputs' box to the output's
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:0.26458" d="M{x * k.K:.4f},{y0 * k.K:.4f}V{y1 * k.K:.4f}"/>'

def build():
    out = [k.header(PW, W), k.background(W), parts(), k.title('Σ', PW)]   # 10.2 px wide at 16: fits, no screws
    for i in range(N):
        x0, x1 = k.box_left(i, N, PW), k.box_right(i, N, PW)
        x = k.jack_col(i, N, PW)
        for y in OUT_Y:
            in_top = y - LAST_IN_ABOVE - 2 * IN_PITCH - IN_PAD
            in_bottom = y - LAST_IN_ABOVE + IN_PAD
            out.append(joint(x, in_bottom, y - k.BOX_ABOVE))
            out.append(k.box(x0, in_top, x1 - x0, in_bottom - in_top, k.INPUT))
            out.append(k.box(x0, y - k.BOX_ABOVE, x1 - x0, k.BOX_ABOVE + k.BOX_BELOW, k.OUTPUT))
            out.append(k.jack_label('OUT', x, y))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('SIGMA.svg', build())

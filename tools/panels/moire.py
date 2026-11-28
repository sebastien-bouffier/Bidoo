# MOiRE's panel (15 HP), migrated from the hand-drawn one: the same arrangement (left, the target and current scenes,
# each a CV jack, a trimpot beside its screen and an arrow button, the morph knob between the two screens in the
# hourglass, its CV jack, the range switch and POLY; right, SAVE and RND, the 4 x 4 knobs in their white field, each with
# its type button, and the 16 CV outputs under them), now in the theme's screens, boxes, type and colours, with the old
# picture kept (moire_parts.svg). The positions are the same in src/MOIRE.cpp.
import os, re
import panel_kit as k

PW = 225
W = PW * k.K
# The left side is symmetric about the morph knob's row, as the old drawing was (its arrows and hourglass about 140.0)
AXIS = 140.0                     # the morph knob (1.24 px lower than its old corner gave), src/MOIRE.cpp kAxis
SCENE_X = 29.0                   # the scene trimpots, arrow buttons, arrows and CV jacks: the old lines' column
MORPH_X = 61.0                   # the screens, the hourglass, the morph knob and its CV jack (the knob was 0.24 px left)
SCENE_FROM_AXIS = 64.95          # the screens' and trimpots' centres from the axis (the trimpots were 0.55 and 0.35 px
                                 # lower), src/MOIRE.cpp kSceneFromAxis
BUTTON_FROM_AXIS = 19.0          # the two arrow buttons (old centres 121 and 159), src/MOIRE.cpp kButtonFromAxis
SCREEN_W, SCREEN_H = 30.0, 20.0  # the scene screens, the old ones (src/MOIRE.cpp kScreen...)
ARROW_BAR, ARROW_TIP, ARROW_BASE, ARROW_HALF = 51.5, 51.04, 44.04, 5.0   # the old arrows, from the axis
ARROW_LINE_HALF = 10.0
HOURGLASS_BASE, HOURGLASS_FADE, HOURGLASS_HALF = 52.0, 10.0, 16.0   # its bases from the axis, faded out 10 px before it
STRIPE = 2.0                     # the hourglass's fade, drawn in stripes of the theme's black (no gradient: Dark and
                                 # Black only convert exact colours)
# the jacks on the left: one box each, 26 px wide as LoURdE's; the target CV jack 3.5 px higher than before, so its box
# clears the trimpot; the current and morph CV jacks on grid row 2 (they were 18.5 px higher, on the old red disc for the
# morph jack), full jacks in boxes where tiny ones were (src/MOIRE.cpp)
BOX_W = 26.0                     # the scene CV boxes; the morph CV box as wide as the screens above it, 4 px from the
                                 # current scene's box
TARGET_CV_Y = 45.0
SWITCH = (47.75, 292.0)          # CKSS between its labels; 2.7 px lower than before, centred between the boxes
CKSS_HALF = 20.64111 / 2
POLY_X = SWITCH[0]               # POLY under the switch (it was 0.75 px left), on the bottom row (10.15 px lower)
# right: SAVE and RND over the first two knob columns (they were 0.15 px right); the knob grid at its old corners, the
# outputs in the knobs' columns, their rows spread from grid row 2 to the bottom row (they were 8.15 to 10.15 px higher)
SAVE_Y = 38 + 28 / 2             # the old CKD6 corners' centre, src/MOIRE.cpp save
COL0, PITCH = 92 + 23.7 / 2, 30.0    # src/MOIRE.cpp firstCol, kGridPitch
KNOB_Y0 = 82 + 23.622 / 2        # BidooColoredKnob, old corner 82
OUT_PITCH = (k.row(0) - k.row(2)) / 3
OUT_BOX = 26.0                   # a box per output, 4 px apart across (4.7 down): no caption, the 30 px columns leave
                                 # no room for one
FIELD_HALF = 65.5                # the white field around the knobs and their type buttons, centred on them; its top and
                                 # bottom on the screens' middles, as before (its sides were 0.2 and 1.4 px nearer)
BRACKET_Y, BRACKET_DOWN = 221.0, 8.0

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'moire_parts.svg'), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def path(d, fill='none', stroke=k.INK, width=0.26458):
    return f'<path style="fill:{fill};stroke:{stroke};stroke-width:{width}" d="{d}"/>'

def P(x, y):
    return f'{x * k.K:.4f},{y * k.K:.4f}'

def arrow(side):
    # a bar and a triangle pointing to it: up to the target scene, down to the current one
    bar, tip, base = AXIS + side * ARROW_BAR, AXIS + side * ARROW_TIP, AXIS + side * ARROW_BASE
    out = path(f'M{P(SCENE_X - ARROW_LINE_HALF, bar)}H{(SCENE_X + ARROW_LINE_HALF) * k.K:.4f}')
    out += path(f'M{P(SCENE_X, tip)}L{P(SCENE_X - ARROW_HALF, base)}H{(SCENE_X + ARROW_HALF) * k.K:.4f}Z', k.INK)
    return out

def hourglass(side):
    # a triangle from under a screen to the morph knob, black at its base, fading out before the knob: stripes as
    # thick as the old gradient was dark
    base = AXIS + side * HOURGLASS_BASE
    fade = HOURGLASS_BASE - HOURGLASS_FADE
    d = ''
    n = int(round(fade / STRIPE))
    for i in range(n):
        a = 1 - (i + 0.5) / n                      # the gradient's darkness in this stripe
        y0 = base - side * STRIPE * i
        y1 = y0 - side * STRIPE * a
        def half(y):                               # the triangle's half width at y
            return HOURGLASS_HALF * abs(y - AXIS) / HOURGLASS_BASE
        d += (f'M{P(MORPH_X - half(y0), y0)}L{P(MORPH_X + half(y0), y0)}L{P(MORPH_X + half(y1), y1)}'
              f'L{P(MORPH_X - half(y1), y1)}Z')
    return path(d, k.INK, 'none')

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('MOiRE', PW)]   # 52 px wide at 16: fits
    # left: the scenes
    for side in (-1, 1):
        y = AXIS + side * SCENE_FROM_AXIS
        out.append(k.screen(MORPH_X - SCREEN_W / 2, y - SCREEN_H / 2, SCREEN_W, SCREEN_H))
        out.append(arrow(side))
        out.append(hourglass(side))
    # the line from SAVE to the target screen, as before
    save_x = COL0
    top = AXIS - SCENE_FROM_AXIS - SCREEN_H / 2
    out.append(path(f'M{P(save_x - 10 - 2, SAVE_Y)}H{MORPH_X * k.K:.4f}V{top * k.K:.4f}'))
    # the CV jacks
    for x, y, bw in ((SCENE_X, TARGET_CV_Y, BOX_W), (SCENE_X, k.row(2), BOX_W), (MORPH_X, k.row(2), SCREEN_W)):
        out.append(k.box(x - bw / 2, y - k.BOX_ABOVE, bw, k.BOX_ABOVE + k.BOX_BELOW, k.INPUT))
        out.append(k.jack_label('CV', x, y))
    # the range switch: its two positions above and below it, as lIMbO's
    out.append(k.text('-5V/+5V', SWITCH[0], SWITCH[1] - CKSS_HALF - 4, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.text('0V/+10V', SWITCH[0], SWITCH[1] + CKSS_HALF + 4 + k.CAP_HEIGHT * k.BOX_LABEL_SIZE, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.box(POLY_X - BOX_W / 2, k.box_top(0), BOX_W, k.box_bottom(0) - k.box_top(0), k.OUTPUT))
    out.append(k.jack_label('POLY', POLY_X, k.row(0)))
    # right: SAVE and RND, the white field of knobs, the outputs
    out.append(k.small_label('SAVE', COL0, SAVE_Y))
    out.append(k.small_label('RND', COL0 + PITCH, SAVE_Y))
    mid = COL0 + 1.5 * PITCH
    f_top, f_bottom = AXIS - SCENE_FROM_AXIS, AXIS + SCENE_FROM_AXIS
    out.append(f'<path style="fill:{k.WHITE};stroke:{k.FRAME};stroke-width:0.5" '
               f'd="M{P(mid - FIELD_HALF, f_top)}H{(mid + FIELD_HALF) * k.K:.4f}V{f_bottom * k.K:.4f}H{(mid - FIELD_HALF) * k.K:.4f}Z"/>')
    label = 'CV OUTPUTS'
    out.append(k.text(label, mid, BRACKET_Y + k.CAP_HEIGHT * k.KNOB_LABEL_SIZE / 2, k.KNOB_LABEL_SIZE, k.INK))
    half = sum(k._glyphs[k._cmap[ord(c)]].width for c in label) * k.KNOB_LABEL_SIZE / k._upm / 2 + 3
    for x0, x1 in ((COL0, mid - half), (COL0 + 3 * PITCH, mid + half)):
        out.append(path(f'M{P(x1, BRACKET_Y)}H{x0 * k.K:.4f}V{(BRACKET_Y + BRACKET_DOWN) * k.K:.4f}'))
    for j in range(4):
        for i in range(4):
            x, y = COL0 + PITCH * i, k.row(2) + OUT_PITCH * j
            out.append(k.box(x - OUT_BOX / 2, y - OUT_BOX / 2, OUT_BOX, OUT_BOX, k.OUTPUT))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('MOIRE.svg', build())

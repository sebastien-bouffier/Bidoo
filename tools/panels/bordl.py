# bordL's panel (36 HP), migrated from the hand-drawn one: the same arrangement (left, the section it shares with dTrOY,
# metroseq_panel.py, over G1, G2, GATE, V/O and V; right, the eight stages by their rotated, bracketed names: the note
# screen over the pitch knobs and their random trimpots, the accent knobs and theirs, the pulse screen over the pulse
# knobs and their probability trimpots, the gate screen over the gate trimpots, slide and skip buttons, the stage
# outputs; the sensitivity trimpot and jack under S), now in the theme's screens, boxes, type and colours, with the old
# picture kept (bordl_parts.svg). The positions are the same in src/BORDL.cpp and src/MetroSeq.hpp.
import panel_kit as k
import metroseq_panel as m

PW = 540
W = PW * k.K
# the stages: 41 px apart, their knobs' centres (the gate trimpots were 0.34 px right of them, the slide and skip
# buttons 0.04, the tiny outputs 0.74). src/BORDL.cpp kStageLeft, kStageCentre
STAGE_X = tuple(200 + 41 * i + 1 + m.KNOB_HALF for i in range(8))
# the stages' screens where the widget writes: the old strips, their sides lined up (198 to 521)
SCREENS = ((198.0, 26.1, 323.0, 24.3), (198.0, 164.7, 323.0, 19.1), (198.0, 243.3, 323.0, 20.05))
# slide and skip: BidooRoundButtons, 23 px apart (the old 10.6 px buttons were 21 px apart, at 303.8 and 324.8: the
# 20 px ones need the room), src/BORDL.cpp kSlideY, kSkipBelow
SLIDE_Y, SKIP_Y = 300.0, 323.0
OUT_Y = 344 + 7.5                      # the tiny stage outputs, old corner 344: full jacks in boxes on the bottom row would
                                       # sit under the skip buttons, so they stay tiny, each on its own red square
OUT_SQUARE = 18.0
# the bottom row: five boxes as before (28.3 px wide, 4.9 apart), the jacks within 0.1 px of their old places
BOX_W = 28.4
BOX_X = tuple(21.9 + 33.2 * i for i in range(5))   # src/BORDL.cpp kBoxFirst, kBoxPitch
# the rows' names, rotated, by their brackets at their old places (SL and SK follow the buttons); 8.5 as dTrOY's
# names (they were 8.3)
NAME_X, BRACKET_X, TICK = 183.0, 190.0, 5.0
BRACKET_LINE = 0.39687                 # the old brackets' width
HALF_BUTTON_BRACKET = 9.0              # SL's and SK's brackets, 18 px as before
BRACKETS = (('S', 25.07, 26.86, 29.1, 'top'), ('PITCH', 84.4, 69.77, 99.0, 'bottom'), ('V', 131.5, 109.0, 154.0, 'both'),
            ('PULSE', 198.5, 165.0, 232.0, 'both'), ('GATE', 265.55, 244.0, 287.1, 'both'),
            ('SL', SLIDE_Y, SLIDE_Y - HALF_BUTTON_BRACKET, SLIDE_Y + HALF_BUTTON_BRACKET, 'both'),
            ('SK', SKIP_Y, SKIP_Y - HALF_BUTTON_BRACKET, SKIP_Y + HALF_BUTTON_BRACKET, 'both'))
# the trimpots' names at the foot of their brackets, as before
TRIM_NAMES = (('RND', 94.75), ('RND', 149.32), ('PROB', 227.37))
TRIM_NAME_LEFT = 193.4

def bracket(y0, y1, ticks):
    out = [m.vline(BRACKET_X, y0, y1, BRACKET_LINE)]
    if ticks in ('top', 'both'):
        out.append(m.hline(BRACKET_X, BRACKET_X + TICK, y0, BRACKET_LINE))
    if ticks in ('bottom', 'both'):
        out.append(m.hline(BRACKET_X, BRACKET_X + TICK, y1, BRACKET_LINE))
    return out

def build():
    out = [k.header(PW, W), k.background(W), m.parts('bordl_parts.svg'), k.title('bordL', PW)]
    out += m.left_section()
    out += m.bottom_boxes(BOX_X, BOX_W, ('G1', 'G2', 'GATE', 'V/O', 'V'),
                          (k.INPUT, k.INPUT, k.OUTPUT, k.OUTPUT, k.OUTPUT))
    for s in SCREENS:
        out.append(k.screen(*s))
    for lab, y, y0, y1, ticks in BRACKETS:
        out.append(m.rotated(lab, NAME_X, y, m.SECTION_SIZE))
        out += bracket(y0, y1, ticks)
    for lab, y in TRIM_NAMES:
        out.append(m.text_left(lab, TRIM_NAME_LEFT, y, k.BOX_LABEL_SIZE))
    for x in STAGE_X:
        out.append(m.output_square(x, OUT_Y, OUT_SQUARE))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('BORDL.svg', build())

# antN's panel (9 HP), migrated from the hand-drawn one: the same arrangement (the URL and status screen under the title,
# the data and audio buffer bars, GAIN, the LISTEN button, the stereo out at the bottom), now in the theme's screen, boxes,
# type and colours, with the old picture kept (antn_parts.svg). The positions are the same in src/ANTN.cpp.
import os, re
import panel_kit as k

PW = 135
W = PW * k.K
N = 3                              # the bottom row's columns: OUT L and OUT R between them (0.5 and 1.5), the same in src/ANTN.cpp
# The screen (ANTNDisplay): the theme's top and margins, down to its old bottom (125); the same in src/ANTN.cpp
SCREEN_BOTTOM = 125.0
# The buffer bars (ANTNBufferDisplay, drawn by the widget across the screen's width), each under its caption: the data
# bar at its old height (140), the audio bar 16.5 below it (it was 15) so that its caption clears the data bar by 3 px.
# The same in src/ANTN.cpp (kBufferTop, kAudioBarBelow)
BAR_TOP, BAR_H, AUDIO_BELOW = 140.0, 5.0, 16.5
CAPTION_GAP = 3.0                  # from a caption's baseline to its bar
# GAIN and LISTEN on the panel's centre (the old corners put them 0.24 and 0.37 px left), at their old heights: the knob's
# corner 183, the old LEDBezel's centre (corner 246, 21.26 px). The same in src/ANTN.cpp
GAIN = (PW / 2, 183 + 29.5275591 / 2)
LISTEN = (PW / 2, 246 + 21.26028 / 2)

def width(s, size):
    return sum(k._glyphs[k._cmap[ord(c)]].width for c in s) * size / k._upm

def left_text(s, x, y, size, col):
    # the buffer captions start at the bars' left end, as before
    return k.text(s, x + width(s, size) / 2, y, size, col)

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'antn_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('antN', PW)]
    out.append(k.screen(k.MARGIN, k.SCREEN_TOP, PW - 2 * k.MARGIN, SCREEN_BOTTOM - k.SCREEN_TOP))
    for name, top in (('DATA BUFFER', BAR_TOP), ('AUDIO BUFFER', BAR_TOP + AUDIO_BELOW)):
        out.append(left_text(name, k.MARGIN, top - CAPTION_GAP, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.knob_label('GAIN', *GAIN))
    out.append(k.small_label('LISTEN', *LISTEN))
    # the stereo out: full jacks, one box each, half a column in from the outer columns of three (the four columns'
    # boxes would leave their captions 1 px from the edges)
    for i, name in ((0.5, 'OUT L'), (1.5, 'OUT R')):
        out.append(k.grid_box(i, i, N, 0, PW, k.OUTPUT))
        out.append(k.jack_label(name, k.jack_col(i, N, PW), k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('ANTN.svg', build())

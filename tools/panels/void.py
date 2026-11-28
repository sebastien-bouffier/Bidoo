# vOId's panel (12 HP), migrated from the hand-drawn one: a blank panel whose drawing (three headless men walking with
# chainsaws) is the module. The drawing is kept as it was, where it was, at full opacity (void_parts.svg: seven layers
# of grey, lightest first), with the theme's title and logo. The screws are in src/VOID_.cpp.
import os, re
import panel_kit as k

PW = 180
W = PW * k.K
# The drawing's darkest layer was #0b0b0b: drawn in the theme's black, so its lines turn light grey in Dark and Black
# like the panel's text (a near black stays near black there, invisible on the dark panel). The other greys are not near
# a theme colour and stay as they are in every theme.
NEAR = {'#0b0b0b': k.INK}

def drawing():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'void_parts.svg'), encoding='utf-8') as f:
        s = re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)
    for old, new in NEAR.items():
        s = s.replace('fill:' + old + ';', 'fill:' + new + ';')
    return s

def build():
    out = [k.header(PW, W), k.background(W), drawing(), k.title('vOId', PW)]
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('VOID.svg', build())

# maGma's panel (8 HP): oAï's (oai.py), the same arrangement, with its own picture (magma_parts.svg) and the four
# preset buttons right of the channel knob, each with its number. The buttons are in src/MAGMA.cpp, the rest in
# src/SliceSampler.hpp (sliceSamplerControls).
import os
import panel_kit as k
import oai

PRESET_X = 104.0              # the numbers' centre, left of the buttons (old corner x 109, src/MAGMA.cpp); 5.5 px right of
                              # the SPEED column, as before: the buttons leave them no room further right
FIRST_PRESET_Y = 23.562       # the first preset light's centre, on the old 8's middle (src/MAGMA.cpp)
PRESET_BELOW = 10.0           # likewise kPresetBelow
PRESET_SIZE = 6.5             # four numbers in a 10 px column: 7.5 would touch the buttons

def build():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'magma_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    # each number's capitals centred on its light
    numbers = [k.text(n, PRESET_X, FIRST_PRESET_Y + PRESET_BELOW * i + k.CAP_HEIGHT * PRESET_SIZE / 2, PRESET_SIZE, k.INK)
               for i, n in enumerate(('8', '16', '32', '64'))]
    return oai.build('maGma', s, numbers)

if __name__ == '__main__':
    k.write('MAGMA.svg', build())

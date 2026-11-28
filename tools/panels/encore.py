# enCORE's panel (31 HP): the ZouSeq sequencer's panel of zouseq_panel.py with its title and its old picture
# (encore_parts.svg). zOù MAï's (zoumai.py) is the same with its own. The widget is src/ZouSeq.hpp (ZouSeqWidget).
import panel_kit as k
import zouseq_panel

def build():
    return zouseq_panel.sequencer('enCORE', 'encore_parts.svg')

if __name__ == '__main__':
    k.write('ENCORE.svg', build())

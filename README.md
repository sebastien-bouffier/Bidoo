# Bidoo's plugins for [VCVRack](https://vcvrack.com)

<!-- Version and License Badges -->
[![Version](https://img.shields.io/github/v/release/sebastien-bouffier/Bidoo?label=version&color=green&style=flat-square)](https://github.com/sebastien-bouffier/Bidoo/releases/latest)
![License](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg?style=flat-square)
![Language](https://img.shields.io/badge/language-C++-yellow.svg?style=flat-square)

## How to

Install the plugin from the [VCV Library](https://library.vcvrack.com/Bidoo), or download the newest version from the [latest release](https://github.com/sebastien-bouffier/Bidoo/releases/latest): put the .vcvplugin for your system in the plugins folder of your Rack user folder (Help > Open user folder in Rack), then restart Rack.

You can find information on that plugins pack in the [wiki](https://github.com/sebastien-bouffier/Bidoo/wiki). When doing tests it happens that I record a video so you may find some ideas on how to use those modules [here](https://www.youtube.com/bidoo).

## License

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. See [LICENSE.txt](LICENSE.txt).

All **graphics** in the `res` directory are copyright © 2019 Sébastien Bouffier (sebastien.bouffier@gmail.com) and licensed under [CC BY-NC-ND 4.0](https://creativecommons.org/licenses/by-nc-nd/4.0/).

### Third-party code

The plugin includes the following code, each under its own license, in `src/dep`:

| Code | Author | License | Used by |
|---|---|---|---|
| [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) and Signalsmith Linear | Geraint Luff / Signalsmith Audio | MIT (`signalsmith/*/LICENSE.txt`) | sOUNAiRe |
| [dr_flac and dr_wav](https://github.com/mackron/dr_libs) | David Reid | Public domain or MIT-0 (in the headers) | sOUNAiRe, cOPIA, liMonADe, the samplers |
| [AudioFile](https://github.com/adamstark/AudioFile) | Adam Stark | MIT (`AudioFile/LICENSE`) | liMonADe, the samplers |
| [minimp3](https://github.com/lieff/minimp3) | lieff | CC0 1.0 (`minimp3/LICENSE`) | antN |
| [FAAD2](https://github.com/knik0/faad2) 2.11.4 | M. Bakker, Nero AG and contributors | GPL-2.0-or-later (`faad2/COPYING`) | antN (AAC) |
| [stb_vorbis](https://github.com/nothings/stb) 1.22 | Sean Barrett | Public domain or MIT (end of `stb_vorbis/stb_vorbis.c`) | antN (Ogg Vorbis) |
| [GVerb](https://github.com/swh/ladspa) | Juhana Sadeharju | GPL-2.0-or-later (in the sources) | dFUZE |
| [PFFFT](https://bitbucket.org/jpommier/pffft) | Julien Pommier | FFTPACK license, BSD-like (in `pffft.h`) | sOUNAiRe, fLAME, FiLTRe, HCTIP, ziNC, liMonADe |
| [LodePNG](https://github.com/lvandeve/lodepng) | Lode Vandevenne | zlib (in `lodepng.h`) | EMILE, liMonADe |
| Resampler | Laurent de Soras | LGPL-2.1 (`resampler/license.txt`) | antN, eDsaroS, fLAME, the samplers |
| [Biquad](https://www.earlevel.com/main/2012/11/26/biquad-c-source-code/) | Nigel Redmon | free to use and modify (in `filters/biquad.h`) | the filters in `filters` |
| bidoo-dsp (excerpt) | Sébastien Bouffier | GPL-3.0-or-later (`bidoo-dsp/README.md`) | REI, cOPIA, FiLTRe, bAFIs, lIMbO, aCiDe |
| Potion voice filter (excerpt) | Sébastien Bouffier | GPL-3.0-or-later (`potion/README.md`) | FiLTRe |

## Donate

If you enjoy those modules you can support the development by making a donation. Here's the link: [DONATE](https://paypal.me/sebastienbouffier)

# Potion's voice filter (excerpt)

`Filter.h` is copied unchanged from the author's Potion synth, commit a34b6a2 (0.66.4), `src/dsp/Filter.h`:
the class `potion::VoiceFilter` that plays FiLTRe's 71 types. `Params.h` here is not Potion's: it is a
stand-in with only what `Filter.h` reads (the `FilterType` enum, `kNumFilterTypes`, `usesMorph`).

`Filter.h` needs C++20 and is compiled only in `src/FiltreEngine.cpp` (see the Makefile). The bidoo-dsp headers
it includes are in `../bidoo-dsp`.

Copyright (c) 2026 Sebastien Bouffier (Bidoo). Distributed here under the GPL-3.0-or-later, as the rest of
this plugin.

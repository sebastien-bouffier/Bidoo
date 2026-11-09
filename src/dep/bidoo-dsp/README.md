# bidoo-dsp (excerpt)

Headers copied unchanged from the author's bidoo-dsp library:

- for REI: `bidoo/dsp/reverb/Prism.h` (PrismReverb, the reverb and shimmer of FadaBox's Prism) from the library
  commit that follows 2799eca (the 2026-11-09 Prism merge: the changes first made in this copy, taken upstream;
  write its hash here once committed), with `bidoo/dsp/core/Random.h`, `bidoo/dsp/core/Inline.h` and
  `bidoo/dsp/shaping/TanhPade.h` from commit 702faf7 (also bAFIs, lIMbO)
- library commit 2799eca: `bidoo/dsp/core/Math.h`, `bidoo/dsp/reverb/FdnEcoReverb.h` and
  `bidoo/dsp/reverb/FdnReverb.h` (cOPIA's reverb: the LFO kept on the unit circle, NaN guards, Size glide)
- commit 2f039ad, for cOPIA: `bidoo/dsp/effects/glitch/` (the glitch engine of the Mangle plugin: effects,
  pattern, slots, scenes' engine; built with BIDOO_DSP_GLITCH_LITE), `bidoo/dsp/time/PulseClock.h`, and what
  they include (core, delay, filters, oversampling, reverb, shaping, synth, time)
- commit 2f039ad, for FiLTRe: `bidoo/dsp/filters/VoiceFilters.h` (the voice filter families of Potion's filter,
  `../potion/Filter.h`), with the filters and shaping headers above
- commit 2f039ad, for zOù MAï and enCORE: `bidoo/dsp/time/Euclid.h` (the euclidean fill)
- commit 8646ec3, for aCiDe:
  `bidoo/dsp/filters/DiodeLadder.h` (the diode ladder model) and `bidoo/dsp/synth/AcidVoice.h` (the voice), which also
  use `bidoo/dsp/synth/KaiserBlep.h`, `bidoo/dsp/oversampling/IirHalfBand.h` and `bidoo/dsp/shaping/TanhPade.h` above
- for rICOcHEt: `bidoo/dsp/delay/CharacterDelay.h` (the delay) and `bidoo/dsp/delay/WowFlutter.h`, with the core, delay
  and shaping headers above

Every header here is identical to the library's file at the commit named: no local edits.

Copyright (c) 2026 Sebastien Bouffier (Bidoo). Distributed here under the GPL-3.0-or-later, as the
rest of this plugin.

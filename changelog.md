### v2.6.0 (not released yet)
Improvements
- zOù MAï, enCORE: new per-track, per-pattern "Hold CV1 and CV2 between notes" option in the Track menu (undoable). CV1 and CV2 keep the last trig's value (a ramp's last pulse) while the gate is off, like V/O, instead of dropping to 0 V; the poly outputs follow. Off by default, and patches without it play exactly as before
- zOù MAï, enCORE: both screens are tables, one column per knob with its label over its value, centred on the knob; the P2 buttons clear the header rule, and long values such as scale names shrink to fit their cell instead of running into the next one
Fixes
- baR: the top of RATIO is now ∞:1, a true limiter: with lookahead, no sample passes above threshold + make-up gain, single-sample clicks included (a peak catcher ramps the gain down within the lookahead). Before, about 2-3 dB of sharp attacks got through at 20:1 and 100 % lookahead. Lower ratios sound exactly as before

### v2.5.0 (10/11/2026)
New module
- FiLTRe: all 71 filter types of the Potion synth (28 HP), the same code as Potion: low, high, band, notch, shelves, morphing, dual, combs, phasers, formants, ring mod, downsample, diffuser, reverb. TYPE, CUTOFF, RES, DRIVE, MORPH and MIX with attenuverters and CV; stereo, up to 16 voices. The screen shows the spectrum of the output, the input behind it, and the measured curve of the selected filter with the current settings
- aCiDe: a mono acid bass voice (10 HP): saw or square, a diode ladder modelled from published analyses of the circuit, filter and volume envelopes, accent with its build-up, slide and VCA. V/OCT, GATE, ACCENT, SLIDE, CV on CUTOFF, RES, ENV MOD and DECAY. Plays directly from zOù MAï: GATE and V/O, CV1 for accent, CV2 for slide
- sAiSOn: a pattern launcher with 16 slots (20 HP), HUITre in two columns; slot n selects pattern n on zOù MAï, enCORE, dTrOY, bordL and ratEaU
- rICOcHEt: a stereo delay with six characters (14 HP): Digital, Tape, BBD, Lo-fi, Diffuse and Reverse, each with its own way of reacting to TIME (crossfade or pitch swoop). TIME up to 8 s or a ratio of a clock, FEEDBACK up to controlled self-oscillation, TONE, MIX, CHARACTER, SPREAD, ping-pong, freeze (button and gate), CV on TIME, FEEDBACK, TONE, MIX, CHARACTER and TYPE. Digital repeats are sample-exact
- dOSE: a polyphonic VCA (6 HP) whose envelope goes through a curve: CURVE at 0 % uses ENV as it is, turned left it is exponential and envelopes sound shorter, turned right it is logarithmic and they sound longer. Mono or polyphonic ENV (unplugged: gain 1), polyphonic CURVE CV with an attenuverter, LEVEL up to +6 dB, OUT, a MIX of all channels and ENV OUT with the shaped envelope. The screen shows the curve and each channel's envelope on it
Improvements
- cOPIA: the engine guards its inputs and its recorded history itself, and a slot whose output overflows restarts from silence
- aCiDe and FiLTRe: the voice and the filters recover by themselves from a NaN or an infinity
- FiLTRe: the same filter code as Potion 0.66.4 (its setup is skipped when nothing changed)
- rICOcHEt: costs almost nothing once silent and fully decayed (about 1.5 ns a sample instead of 30 to 84); on waking the wow, flutter and noise start again from their beginning
- The shared DSP code is the same as the Bidoo VST plugins' again (bidoo-dsp 4e6f6e2)
- fLAME: stereo. IN L and IN R (R follows L when unplugged), new BAND L / BAND R outputs with the audio inside the box's band (48 dB/oct high- and low-pass at its edges, no latency, edges glide so dragging does not click), and AMP L / AMP R (AMP L is the old OUT, so older cables stay). The screen is in dB, with a live spectrum redrawn every frame, a peak hold and a fading history, and scales for time (left), frequency (bottom) and level (right); new panel with the jacks in three boxes
- REI: changing the algorithm fades out and back in instead of clearing the buffers under the tail, the pre-delay crossfades when moved, the shimmer keeps its timing at every sample rate, and a NaN or infinity in the input is heard as silence; at 48 kHz with the settings held the sound is the same
- cOPIA: its reverb's modulation stays exact over long runs, SIZE glides, and a NaN or infinity in the input is heard as silence
- All modules: controls, jacks and lights placed through one shared system; small misalignments (mostly under 1 px, at most 1.6 px) corrected onto the panels' lines, labels and boxes
- aCiDe, FiLTRe, rICOcHEt, cOPIA, sOUNAiRe, dOSE: each jack's caption centred between the top of its box (or the jack above) and the jack; jacks centred in their boxes
- sOUNAiRe: new polyphonic CV inputs END, LP IN and LP OUT, added to their knobs (10 V = the whole sample) and followed while notes play, so an LFO can sweep a loop without clicks; the display follows them
- aCiDe, FiLTRe, cOPIA, sOUNAiRe, rICOcHEt: the jacks on the common rows, at the height of the older modules' bottom row, with larger screens and more room for the knobs
- sOUNAiRe, cOPIA: new panels in the look of the other modules, with the Light, Dark and Black themes; cOPIA draws its loop in pink and its lanes in yellow
- sOUNAiRe: the waveform is pink, blue is a choice in the menu (Waveform colour); grey keys and pads, the root, the played keys, the selected pad and the loop lines in yellow
- OUAIve, cANARd: the waveform is filled, no longer only its outline
- zOù MAï, enCORE: a row of 4 polyphonic outputs under the track outputs (GATE, V/O, CV1, CV2): 8 channels each, channel n = track n
- zOù MAï, enCORE: undo (Ctrl+Z) for erase, paste, random, up/down/left/right, notes, octaves, step and track on/off, solo, slide mode, labels, at trig, page, track and pattern level, each named in the Undo menu; undoing a knob puts the value back on the trig or track it was moved on, whatever is selected by then
- dTrOY, bordL: undo for paste, the slide, skip, play mode and count mode buttons, transpose, rotate and the Randomize menu lines; undoing a knob puts the value back on its pattern
- zOù MAï, enCORE: new trig conditions FIRST (first pass of the track since the pattern started or a reset) and LAST (each step's last turn before a requested pattern change)
- zOù MAï, enCORE: new track speeds x1/8, x1/4, x1/2, x3/4 and x3/2 besides the whole ones, exact over long runs
- zOù MAï, enCORE: much smaller patches: settings at their default are no longer saved
- zOù MAï, enCORE: euclidean fill of a track, Hits and Rotation in the Track menu, B over SEL or ON/OFF; only turns trigs on and off
- zOù MAï, enCORE: per-track swing, 50-75 %, on page 2 of the upper screen or in the Track menu: even-numbered steps start later, added to their trim
- zOù MAï, enCORE: ratchet ramps per trig, on page 2 of the lower screen (P2 button in the corner of each screen): CV1 End, CV2 End, Pitch (semitones per pulse) and Distance (each gap a percentage of the previous one)
- zOù MAï, enCORE (and expanders): 16 patterns; PTRN CV 0.625 V per pattern, 0-10 V adds 0-16 to the knob (was 1.25 V); patterns at their default are not saved
- dTrOY, bordL: PATTERN CV exactly 0.625 V per pattern (was about 0.66 V), as the other sequencers
- zOù MAï and enCORE (and expanders), dTrOY and bordL: one shared code each
- zOù MAï, enCORE: exact timing: positions, trims, lengths and pulse distances are integers, so the same musical position falls on the same sample at any speed, clock or platform, with no drift. Existing patches load, and can shift by a sample; zOù MAï's trigs right at a step or trim boundary and enCORE's behaviour fixes below can change how they play
Fixes
- sOUNAiRe: closing Rack or removing the module no longer crashes when it holds a sample it has not started to play yet, for instance while bypassed
- All modules, Dark and Black themes: labels on red output boxes are white, no longer dark grey on dark red; the same for a few labels on black shapes (RATEAU's CLK and RUN, the MU's icons)
- enCORE now behaves as zOù MAï: PRE is cleared on reset and on each loop and follows every trig, the next trig's condition is drawn ahead, a reset redraws the condition of step 0 even when the track is already on it, the pendulum plays 0..n-1..1 with no repeated last step and keeps what passes the turn; the clock is also read while stopped
- zOù MAï, enCORE: COUNT starts at 1 (the first cycle played one pass too many); a pulse lasts its length exactly; a trig that does not play no longer holds back the next trig trimmed early, and a trim on an exact sample plays on that sample; pendulum on a 1-step track plays every step
- zOù MAï: RUN or a reset between two clocks: the next steps stay on the clock's subdivisions (at fast speeds they ran early until the next clock, or were lost)
- dTrOY, bordL: the internal clock keeps exact time (up to one sample off over minutes); gate lengths and stage output pulses no longer depend on float rounding; existing patches can shift by a sample
- ratEaU: exact timing: position, events, time shift and pulse width are integers, so triggers fall on the same sample at any tempo and clock, with no drift (before, up to 3 samples off at the end of 10 minutes on the internal clock and more within it). Existing patches can shift by some samples, pulse lengths too. RESET between two clocks now waits for the next clock instead of firing the steps in between; a RESET up to 1 ms after a clock belongs to that clock
- zOù MAï, enCORE: random notes reach all 12 semitones (B never came up) and stay in the track's root and scale when it has one; random Val. and Base 1-100 (were 0-99); random track length up to 64 (was 63); full random speed over the whole range (was x1-x3); enCORE: random length and pulse distance over their whole range (length 0 no longer drawn)

### v2.4.0 (07/11/2026)
New module
- cOPIA: a live resampler (34 HP) on the glitch engine of the Bidoo Mangle plugin. CAPTURE records 1 to 8 bars of the input from the next bar of the clock, then replays them through eight glitch slots (Repeat, Reverser, Slicer, Gate, Tape, Grain, Crush, Drive, Filter, Delay, Ring, Flanger, Phaser, Filter Pattern, Reactor) with lanes drawn on the display; eight scenes with pads, SCENE CV and a morph; PUNCH knobs; a new capture takes over on its bar without a gap; mix, remix, BAR, REC and LANES outputs; the loop is kept in the patch as FLAC
Fixes
- PILOT: the first move after loading a patch (or adding the module) no longer skips a scene: it goes to the top scene (or back to the bottom one when MORPH sits at the top)
- Library tags: duplicate aliases removed (lambda, BanCau); BanCau also tagged Function generator

### v2.3.0 (07/11/2026)
New in antN
- AAC (AAC-LC, HE-AAC v1/v2) and Ogg Vorbis streams besides MP3, format told from the stream itself; a status line shows the station, format, bitrate and rate, or why a stream does not play (HLS, Opus...); right-click > Stations: about 120 checked stations by continent, news & talk or music (list in res/stations.json)
Audio quality
- Sample players (OUAIve, cANARd, póUPrè, maGma, oAï, eDsaroS, SpORE, fREIN, antN): no more clicks on start, retrigger, stop, loop, choke and state changes; pitching up is anti-aliased; play positions in double precision (no detuning far into long samples); loaded files no longer delayed or cut at the end
- sOUNAiRe: files resampled to the patch rate, anti-aliased pitching up, constant-level loop crossfade (menu: linear or equal-power), smoother filter under audio-rate CV
- dFUZE: full-length diffusers (smoother, stereo tail); REI: no click on algorithm or pre-delay change, shimmer right at 96/192 kHz
- bAFIS: flat Linkwitz-Riley bands, oversampled distortion, near-linear at drive 1; lIMbO: boost no longer raises the resonance, Saturated mode oversampled; pErCO opens up to 21.6 kHz
- baR / mINIBar: continuous level detector (short releases work), smoothed make-up and mix, bypass keeps the look-ahead delay
- ziNC: 16 bands from 100 Hz to 14 kHz, even level across Q, faster envelopes; liMonADe: no harmonic drop at octave boundaries, no click on table changes
- HCTIP: bigger FFT at high sample rates; ForK: cleaner carriers; TiARE: better decimator; EMILE: same pitch at every sample rate
CPU
- Control-rate work (knobs, buttons, lights) every 16 samples in the sequencers, mixers and utilities, with timing unchanged; ratEaU, dTrOY, bordL, ACnE, DIKTAT, µ several times cheaper
- REI, dFUZE, HCTIP, SpORE, liMonADe, EMILE skip work on silence or with no output patched; lIMbO, HCTIP, ForK, TiARE, maGma, sOUNAiRe lighter
- Cached displays (fLAME, liMonADe, PILOT, sOUNAiRe); the module browser no longer re-reads settings every frame
Fixes
- maGma, oAï: two channels triggered on the same sample that choke each other: the lower channel plays, the higher one is cut (both were cut a sample later)
- eDsaroS: no NaN at note-off with release 0; enCORE: tracks at speeds 3, 5, 6 or 7 no longer drift; DilEMO hysteresis; PILOT blink and curve display; antN fades
- sOUNAiRe: no lost stretch voices after many sample swaps, no crash on load during autosave, NaN-safe inputs
- Samples load while a module is bypassed; cANARd saving no longer freezes the UI
- Themes: Blue and Green removed (Light, Dark, Black); sOUNAiRe has a single look and no Theme menu

### v2.2.0 (05/11/2026)
New module
- sOUNAiRe: a 16-voice sampler (32 HP). KEYS mode plays one sample over a key range with loops and crossfades, reverse, legato, a multimode filter and Signalsmith time-stretch; SLICES mode cuts a loop at its transients (drums or tonal detector) and plays one slice per key or per trigger (TRIG, SLICE, RESET), each slice with its own play type, choke group, level, tune, pan and envelope. Clock sync, recording from its inputs, zoomable display with undo for the cuts, WAV export, sample kept in the patch as FLAC

Sequencers and clocks
- tOCAnTe: timing rewritten; RUN and RESET trigger inputs
- lATe: swing timing rewritten
- ChUTE: CV ranges fixed, gate on impact, polyphony
- µ: bipolar CV, 1V/oct BPM, can follow a clock, no CV dips
- ratEaU: rebuilt on a Trigger Riot style engine; RUN button and input for the internal clock; outputs named A1-A4 and B1-B4
- dTrOY and bordL: patterns read from their own data, armed reset, 1V/oct clock, bordL on dTrOY's engine; bordL slide, skip, play and count buttons can be MIDI mapped
- zOù MAï and enCORE (and expanders): many sequencing fixes, no crash on paste or page change, every control named, menu shortcuts fixed, RUN restarts every track
- πlOT and MOiRE: safe indexes, scenes spread over 0-10 V, polyphonic output, initialize resets everything, randomize can pick every control type; yellow WAIT fires one gate per arrival
- dIKT@T: root and scale CVs in range, polyphonic, root CV wraps, global mode works on any channel

Oscillators
- antN: streaming rewritten
- TiARE: oscillator core rewritten; pulse width, PWM and PW input on the panel (now 12 HP)
- liMonADe: wavetable oscillator rewritten; export to WAV and PNG; sync mode input works again; whole-table tools keep morphed tables as they reload
- ForK: vowels in tune, no DC, even level, 1V/oct formant CVs, polyphony; preset saved
- BanCoù: DUSG style slopes, real curves, 10 V, polyphony

Samplers
- OUAIve, eDsaroS, póUPrè, maGma, oAï, cANARd and EMILE reworked: shared slice engine, 1V/oct, worker threads for loading, many playback fixes
- cANARd: recordings and edited samples saved in the patch, WAV export exact
- eDsaroS: release plays to the end by default, link modes renamed

Effects, filters and utilities
- REI: now FadaBox's Prism reverb and shimmer; freeze on every algorithm
- dFUZE: stereo input, freeze restored
- baR and mINIBar: peak-hold detector, real meters, polyphonic mINIBar, bypass without lookahead delay
- pErCO, bAFIs, ziNC, lIMbO: soft limit, polyphony, fixed CVs and bands
- fREIN, SpOre, HCTIP, rabBIT, BISTROT, LoURdE, DilEMO, lambda, MS, Σ, dUKe, fLAME, HUITre: polyphony and fixes; rabBIT passes the signal clean with no bit switched
- Labels and tooltips corrected across the plugin

General
- Builds without warnings with Rack SDK 2.6.6, C++17, updated dr_wav, lodepng and AudioFile
- Documentation for every module in the wiki

### v2.1.1 (06/02/2023)
- zOù MAï and enCORE can copy/paste/randomize/erase per page thanks to Tavis

### v2.1.0 (12/01/2023)
- ANTn minimp3 config change for SIMD optimizations

### v2.0.29 (24/11/2022)
- ANTn minimp3 config change for OSX build compability

### v2.0.28 (22/11/2022)
- cANARd fix zoom issue

### v2.0.27 (30/10/2022)
- πlOT copy/paste scenes available on top and bottom scene buttons (can be applied between banks)
- πlOT controls behavior can be switched between "constant override" (default mode) i.e. it keeps its value until a scene is saved or the morph button is clicked, and not "constant override" meaning that the control jumps to its value when changing scene.

### v2.0.26 (11/10/2022)
- fix Zou triggers issue

### v2.0.25 (9/10/2022)
- ratEaU is available

### v2.0.24 (1/10/2022)
- πlOT fix OSX build

### v2.0.23 (1/10/2022)
- πlOT fix on gate mode
- rabBIT randomize implementation

### v2.0.22 (15/09/2022)
- πlOT can add masking tape on top of controls in order to display labels (use menu)

### v2.0.21 (07/09/2022)
- mem leaks fixes thanks to Filipe

### v2.0.20 (24/08/2022)
- pitchshifters sample rate change fix thanks to Filipe
- C++ fixes thanks to Filipe

### v2.0.19 (10/08/2022)
- all samplers are not case sensitive anymore on files extensions
- πlOT bank copy/paste capability
- enCORE fix PRE/!PRE probability issue
- antN better .pls .m3u links management
- dIKT@T root note on channels > 1 issue fixed
- cANARd sample position can be automated via index input in loop mode
- EMILE crash VCV under OS X when deleting module with an image loaded ... fixed

### v2.0.17 (21/07/2022)
- tOCAnTe upgrade for more divisions + dedicated output for enCORE sequencer
- DIKTAT some scales/modes added (all modules with quantization benefit from that upgrade)
- enCORE sequencer is a sibling of zOù MAï the difference is the clock management since enCORE needs 32 clock pulses per step so it is less precise in terms of step length/trim/retrig etc... but it does not suffer of bpm calculation changes on startup between the three first clock ticks.

### v2.0.16 (09/07/2022)
- zOù MAï pattern CV input rescaled

### v2.0.15 (06/07/2022)
- quantization issue in all modules fix
- zOù MAï track naming capability via menu

### v2.0.14 (19/05/2022)
- bordL random skip and slide fix
- liMonADe wave table and frame wav export, first step for wave table png export
- zOù MAï run button added
- zOù MAï CV1 and 2 range is now -10V +10V

### v2.0.13 (08/04/2022)
- changelog link update

### v2.0.12 (06/04/2022)
- rename VOID module class

### v2.0.10 (03/04/2021)
- liMonADe scan bug fixed when table is empty

### v2.0.9 (21/03/2022)
- last theme applied becomes the new default theme (modules are drawn accordingly in the browser once the application is restarted)
- bordL's skip, slide, play mode and count mode buttons should be OSC compliant
- bordL has some perf improvements

### v2.0.8 (27/01/2022)
- bAr and mINIBar attack and release params where inverted
- all modules have themes (light, dark, black, blue and green)

### v2.0.7 (08/01/2022)
- zOù MAï sync issues fixed
- zOù MAï VO quantization per track
- zOù MAï CV1 can be quantized too
- zOù MAï slide timing vs. rate constant per track
- zOù MAï slide base calculation per note, note length vs. 1 step length
- zOù MAï tools removed from interface, tools extended and moved in menu and accessible by "over" shortcuts (description/mapping in menu)
- zOù MAï expander with new features (each pattern has its own set of params) :
- zOù MAï Fill per track
- zOù MAï Force all rand steps per track
- zOù MAï Mute all rand steps (except dice  100% i.e. no rand) per track
- zOù MAï Transpose per track (0+10V or -4+6V base)
- zOù MAï Increase or decrease dice value for steps with rand dice less than 100% per track
- zOù MAï Rotate pattern left or right per track (rotate shift and length as params)

### v1.1.39 (26/09/2021)
- πlOT Orange mode for controls => Like in Red mode they are quantized in order to drive a VCO but they jump from note to note like in Yellow mode.
- πlOT sequencer can record controls values

### v1.1.38 (26/09/2021)
- πlOT gate time issue fixed
- πlOT sequencer mode 3 => negative snapshots fixed

### v1.1.37 (25/09/2021)
- πlOT UI redesigned
- πlOT red mode for controls = quantization to semi-tones + display of note when edited
- πlOT saving a scene release all the controls
- πlOT red wait mode for the sequencer = morph must reach min or max to change controls in yellow mode (jump) and to fire the gates of controls in green mode (gate)
- πlOT yellow wait mode for the sequencer = morph changes controls in yellow mode (jump) when a trig occurs and fires the gates of controls in green mode (gate)
- πlOT bezier curve morphing error fixed

### v1.1.36 (21/09/2021)
- πlOT custom morphing curve

### v1.1.32 (21/09/2021)
- MOiRE random button for target scene added
- πlOT is a kind of MOiRE++ with automorphing between scenes and more

### v1.1.31 (11/09/2021)
- maGma empty sample bug fix
- póUPrè empty sample bug fix

### v1.1.30 (30/05/2021)
- ACnE stereo link bug fixed

### v1.1.29 (03/03/2021)
- zOù MAï Sync issue on Reset and Pattern change fixed
- bordL & dTrOY lydian scale quantization fixed

### v1.1.28 (22/10/2020)
- ACnE ramp fix between snaphots
- ACnE ramp is applied on Solo and Mute
- zOù MAï run input reset clock fix

### v1.1.27 (13/10/2020)
- tOCAnTe reset and run outputs added
- zOù MAï run input

### v1.1.26 (02/10/2020)
- bAr and mINIBar compressor tag
- new tape stop module fREIN

### v1.1.25 (14/09/2020)
- zOù MAï latched buttons
- zOù MAï record V/O + gate capability

### v1.1.24 (12/09/2020)
- mINIBar shows side chain and input signals at the same time
- zOù MAï latched octave buttons

### v1.1.23 (12/09/2020)
- per-module manual URLs
- Res input added on FFilTr
- baR shows side chain and input signals at the same time

### v1.1.22 (31/05/2020)
- zOù MAï stops when clock is not active
- antN fix for some m3u

### v1.1.21 (03/05/2020)
- zOù MAï fix and optimization

### v1.1.20 (25/04/2020)
- dIKT@T is now polyphonic
- zOù MAï trig prob count issue fixed
- zOù MAï track randomization speed issue fixed

### v1.1.19 (25/04/2020)
- zOù MAï sync/reset/speed issues fixed
- zOù MAï PROB and SPEED are MIDI-MAP compliant

### v1.1.18 (20/04/2020)
- zOù MAï paste bug fixed
- zOù MAï track reset issue fixed

### v1.1.17 (20/04/2020)
- dIKT@T quantization fix
- dIKT@T note input display

### v1.1.16 (19/04/2020)
- liMonADe frame loading issue fixed
- 8re CV calibration issue fixed
- 8re gate type param added (full vs. pulse)
- NEW dIKT@T authoritarian quantizer
- all buttons skins redesigned

### v1.1.15 (04/04/2020)
- zOù MAï mini led buttons redesigned
- 8re (HUITre in french means oyster) is a pattern management tool for zOù MAï ... but not only

### v1.1.14 (27/03/2020)
- zOù MAï clock stability issue fixed
- pErCO cutoff issue at low sample rate

### v1.1.11 (08/02/2020)
- zOù MAï shift + left click fix for Mac OS X

### v1.1.10 (05/02/2020)
- MOiRE scene selection fixed
- ACnE autosave mode selection
- µ migration fixed
- liMonADe crash fix

### v1.1.9 (18/12/2019)
- cANARd can save samples again

### v1.1.8 (17/12/2019)
- maGma a channel can kill another one like oAï sampler
- maGma & póUPrè have 4 presets to slice their sample in 8, 16, 32 and 64 slices automatically

### v1.1.7 (17/12/2019)
- ACNe hold SHIFT pressed to move faders by pairs
- LoURdE skin reworked

### v1.1.6 (16/12/2019)
- All samplers i.e. OUAIve, cANARd, póUPrè, maGma and oAï now resample their samples accordingly to engine sample rate.
- ziNC inputs and controls are renamed (MOD vs. CARR)
- póUPrè, maGma and oAï parameters initialization fixed
- oAï KILL param added to stop another channel when trigged (CH/OH management for example)

### v1.1.5 (08/12/2019)
- zOù MAï shift + left click on track on/off button solo and select the track
- zOù MAï shift + left click on trig enable/disable the trig without using the mini keyboard

### v1.1.4 (07/12/2019)
- NEW oAï sampler

### v1.1.3 (05/12/2019)
- global skins cleaning
- NEW póUPrè sampler
- NEW maGma sampler

### v1.1.2 (30/11/2019)
- zOù MAï pattern change sync fix
- All plugins are v1.0 full compliant (getVoltage, getValue, setBrightness, ....)
- tOCAnTe Measure output fed by a PulseGenerator as other outputs

### v1.1.1 (21/11/2019)
- zOù MAï reborn

### v1.1.0 (12/11/2019)
- ACnE fix faders (trimpots) color gradient
- ACnE recording of faders movements doesn't jump anymore when using scenes automation
- ziNC fix frequency knobs color gradient depending on carrier signal

### v1.0.13 (25/10/2019)
- OUAIve, cANARd and liMonADe aiff format support
- liMonADe sync FFT computation after normalization and windowing

### v1.0.12 (21/10/2019)
- dTrOY add CV input for pitch sensitivity knob
- bordL add CV input for pitch sensitivity knob
- baR fix peak display
- mINIBar fix peak display
- bAFIs fix Drive param value display
- fix plugin.json Invalid license ID

### v1.0.11 (19/10/2019)
- *NEW* bAFIs (means mustache in Marseille's slang) Multiband distortion

### v1.0.10 (06/10/2019)
- dTrOY fix pitch knobs reset issue, and shift up and down buttons
- bordL fix pitch knobs reset issue, and shift up and down buttons

### v1.0.9 (03/10/2019)
- liMonADe fix framesize edition crash
- dTrOY fix pitch knobs range

### v1.0.8 (25/09/2019)
- liMonADe change frameSize text editor width
- baR add bypass led button
- mINIBar add bypass led button
- MOiRE fix jump led button click action

### v1.0.7 (27/08/2019)
- liMonADe fix inversion of waveform display
- OUAIve fix inversion of waveform display
- cANARd fix inversion of waveform display
- PeNEqUe fix inversion of waveform display
- OUAIve fix play speed x2 on stereo samples

### v1.0.6
- liMonADe small wavetables morphing issue fixed
- liMonADe frame number is now linked to the edited frame
- liMonADe frame size editor displays the correct value

### v1.0.5
- liMonADe wavetable loading bugfix
- OUAIve waveform display optimization

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

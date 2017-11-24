# Bidoo's plugins for [VCVRack](https://vcvrack.com)

<!-- Version and License Badges -->
![Version](https://img.shields.io/badge/version-0.5.0-green.svg?style=flat-square)
![License](https://img.shields.io/badge/license-BSD3-blue.svg?style=flat-square)
![Language](https://img.shields.io/badge/language-C++-yellow.svg?style=flat-square)

![pack](/images/pack.png?raw=true "pack")

## dTrOY

dTrOY is a single line sequencer but with nice features inspired by Metropolis hardware sequencer.
dTrOy = Detroit + Toy ...

Features:
- 8 trigs with pitch, number of pulses, slide and skip parameters
- Each trig has its own gate mode :
	- closed 0.0V on pulses
	- first pulse open 10.0V then closed
	- each pulse open according to gate time setting
	- fully open
	- fully open passing Gate 1 input (you can send gates or audio :) )
	- fully open passing Gate 2 input
- Up to 16 steps pattern
- 5 play modes : forward, backward, ping-pong, random, random neighbor
- 2 count modes : steps and pulses
- Scale quantization and root note
- Sync & reset sequencer

## OUAIve

OUAIve is no more a simple sample player ...

Features:
- TRIG Mode :
	- sample start position CV controlled
	- sample play rate CV controlled x0.5 to x10. No time stretching just vinyl like speed change
- GATE Mode :
	- free control on play head
- SLICE Mode :
	- up to 128 slices CV controlled (I know 128 is a little bit too much but can lead to produce strange sounds)
	- 3 slice play modes => forward, backward and repeat. CV controlled
	- sample play rate CV controlled x0.5 to bla bla bla ...
	- slice index selection follows trigs but can be overridden by "POS"

## ChUTE

ChUTE is a space/strange trigger based on the free fall and bouncing of an object.
You can change the drop altitude between 1cm and 3m, the gravity in between the Moon's one and Neptune's one. Earth is the default.
And you can adjust the C.O.R. meaning the coefficient of restitution.
Outputs provide a gate based on downside movements, the relative tension regarding the altitude ALT and TOP which exposes the tops of the rebounds.

## dUKe

dUKe is a simple but very efficient 4 x CV controller. Yo can choose between -5V/+5V or 0V/10V and set the MAX and MIN for each slider. If used CV will add signal on top of the slider.
Two nice buttons allow you to jump to both extremities of the 4 sliders at the same time.

## ACnE

ACnE is a very simple and compact patch bay/mixer. 16 in x 8 out.
For a faster patching, reset is still the right click on knobs but with ACnE center click set them to their maximum value.
You have 16 scenes you can choose from and a copy/paste function that allows you to quickly initialize them.
Mute on all outputs, Mute and Solo on all inputs. Main outputs have a dedicated volume control.

## MOiRE

Just 16 scenes of 16 CV controllers and a fader to morph between them. All outputs can be switched -5v/+5v and 0v/10v.
Last addition, each control can be switched between two modes:
- Morph the value of the control will be rescaled all along fader movements
- Target the value of the control will be updated only when the fader reaches the highest value

## vOId

vOId is the ultimate mastering tool for Rack. Once plugged in your Rack you gonna sound FAAAAT. Picture is taken from a very good LP of TERA MELOS.

The repository is based on development branch of Rack.

## License

The license is a BSD 3-Clause with the addition that any commercial use requires explicit permission of the author. That applies for the source code.

For the image resources (all SVG files), any use requires explicit permission of the author.

## Donate

If you enjoy those modules you can support the development by making a donation. Here's the link: [DONATE](https://paypal.me/sebastienbouffier)

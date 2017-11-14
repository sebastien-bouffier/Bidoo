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

OUAIve is a sample player and that's all. You choose the sample via the menu. You press play or you trig him at your convenience.
If he is in a good mood, he will remind the sample that you selected.
Last addition to OUAIve, you can chose in between "TRIG MODE" and "GATE MODE".
In "TRIG MODE" each impulse on the "GATE/TRIG" input will launch the sample from the "POS" position.
In "GATE MODE" when the "GATE/TRIG" input receives more than 0V the player reads the sample at "POS" waiting for you to move "POS" in the sample allowing pitch changes and other things.


## ChUTE

ChUTE is a space/strange trigger based on the free fall and bouncing of an object.
You can change the drop altitude between 10cm and 3m, the gravity in between the Moon's one and Neptune's one. Earth is the default.
And you can adjust the C.O.R. meaning the coefficient of restitution.
Outputs provide a gate based on downside movements, the relative tension regarding the altitude ALT and TOP which exposes the tops of the rebounds.

## dUKe

dUKe is a simple 4 x CV controller. Yo can choose between -5V/+5V or 0V/10V and set the MAX and MIN for each slider. If used CV will add signal on top of the slider.

## vOId

vOId is the ultimate mastering tool for Rack. Once plugged in your Rack you gonna sound FAAAAT. Picture is taken from a very good LP of TERA MELOS.

The repository is based on development branch of Rack.

## License

The license is a BSD 3-Clause with the addition that any commercial use requires explicit permission of the author. That applies for the source code.

For the image resources (all SVG files), any use requires explicit permission of the author.

## Donate

If you enjoy those modules you can support the development by making a donation. Here's the link: [DONATE](https://paypal.me/sebastienbouffier)

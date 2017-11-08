# Bidoo's plugins for [VCVRack](https://vcvrack.com) 

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
	- fully open passing Gate 1 input
	- fully open passing Gate 2 input
- Up to 16 steps pattern
- 5 play modes : forward, backward, ping-pong, random, random neighbour
- Scale quantization and root note
- Sync & reset sequencer
- All parameters saved

## OUAIve 

OUAIve is a sample player and that's all. You choose the sample via the menu. You press play or you trig him at your convenience.
If he is in a good mood, he will remind the sample that you selected.

## ChUTE 

ChUTE is a space/strange trigger based on the free fall and bouncing of an object. 
You can change the drop altitude between 10cm and 3m, the gravity in between the moon's one and neptune's one. Earth is the default. 
And you can adjust the C.O.R. meaning the coefficient of restitution.
Outputs provide a gate based on downside movments, the relative tension regarding the altitude ALT and TOP which exposes the tops of the rebounds.

The repository is based on development branch of Rack.

## License

The license is a BSD 3-Clause with the addition that any commercial use requires explicit permission of the author. That applies for the source code.

For the image resources (all SVG files), any use requires explicit permission of the author.

## Donate

If you enjoy this module you can support the development by making a donation. Here's the link: [DONATE](https://paypal.me/sebastienbouffier)

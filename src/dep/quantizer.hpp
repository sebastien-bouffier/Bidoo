#pragma once
#include <rack.hpp>
#include <iostream>

namespace quantizer {

  struct RootNote {
  	int note;
  	std::string label;
  };

  static constexpr int numNotes = 13;

  static const RootNote rootNotes[numNotes] = {
  	{-1, "No"},
  	{0,  "C"},
  	{1,  "C#"},
  	{2,  "D"},
  	{3,  "D#"},
  	{4,  "E"},
  	{5,  "F"},
  	{6,  "F#"},
  	{7,  "G"},
  	{8,  "G#"},
  	{9,  "A"},
  	{10,  "A#"},
  	{11,  "B"}
  };

  struct Scale {
  	int scale;
  	std::string label;
    int numNotes;
  	int intervals[12];
  };

  static constexpr int numScales = 27;

  static const Scale scales[numScales] = {
  	{0,  "No", 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
  	{1,  "Ionian", 7, {0, 2, 4, 5, 7, 9, 11, 0, 0, 0, 0, 0}},
  	{2,  "Dorian", 7, {0, 2, 3,	5, 7,	9, 10, 0, 0, 0, 0, 0}},
  	{3,  "Phrygian", 7, {0,	1, 3,	5, 7,	8, 10, 0, 0, 0, 0, 0}},
  	{4,  "Lydian", 7, {0,	2, 4,	6, 7,	9, 11, 0, 0, 0, 0, 0}},
  	{5,  "Mixolydian", 7, {0,	2, 4,	5, 7,	9, 10, 0, 0, 0, 0, 0}},
  	{6,  "Aeolian", 7, {0,	2, 3,	5, 7,	8, 10, 0, 0, 0, 0, 0}},
  	{7,  "Locrian", 7, {0,	1, 3,	5, 6,	8, 10, 0, 0, 0, 0, 0}},
  	{8,  "Ionian Min", 7, {0,	2, 3,	5, 7,	9, 11, 0, 0, 0, 0, 0}},
  	{9,  "Dorian ♭9", 7, {0,	1, 3,	5, 7,	9, 10, 0, 0, 0, 0, 0}},
  	{10, "Lydian Aug", 7, {0,	2, 4,	6, 8,	9, 11, 0, 0, 0, 0, 0}},
  	{11, "Lydian Dom", 7, {0,	2, 4,	6, 7,	9, 10, 0, 0, 0, 0, 0}},
  	{12, "Mixoly. ♭6", 7, {0,	2, 4,	5, 7,	8, 10, 0, 0, 0, 0, 0}},
  	{13, "Aeolian Dim", 7, {0,	2, 3,	5, 6,	8, 10, 0, 0, 0, 0, 0}},
  	{14, "Super Loc.", 7, {0,	1, 3,	4, 6,	8, 10, 0, 0, 0, 0, 0}},
  	{15, "Aeolian ♮7", 7, {0,	2, 3,	5, 7,	8, 11, 0, 0, 0, 0, 0}},
  	{16, "Locrian ♮6", 7, {0,	1, 3,	5, 6,	9, 10, 0, 0, 0, 0, 0}},
  	{17, "Ionian Aug", 7, {0,	2, 4,	5, 8,	9, 11, 0, 0, 0, 0, 0}},
  	{18, "Dorian ♯4", 7, {0,	2, 3,	6, 7,	9, 10, 0, 0, 0, 0, 0}},
  	{19, "Phry. Dom", 7, {0,	1, 4,	5, 7,	8, 10, 0, 0, 0, 0, 0}},
  	{20, "Lydian ♯9", 7, {0,	3, 4,	6, 7,	9, 11, 0, 0, 0, 0, 0}},
  	{21, "Super Loc. ♭♭7", 7, {0,	1, 3,	4, 6,	8, 9, 0, 0, 0, 0, 0}},
    {22, "Blues", 9, {0, 2, 3, 4, 5, 7, 9, 10, 11, 0, 0, 0}},
    {23, "Melodic Min", 9, {0, 2, 3, 5, 7, 8, 9, 10, 11, 0, 0, 0}},
    {24, "Pentatonic", 5, {0, 2, 4, 7, 9, 0, 0, 0, 0, 0, 0, 0}},
    {25, "Turkish", 7, {0, 1, 3, 5, 7, 10, 11, 0, 0, 0, 0, 0}},
    {26, "Chromatic", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}}
  };

  struct Chord {
    float tonic;
    float third;
    float fifth;
    float seventh;
    float ninth;
    float eleventh;
    float thirteenth;
  };


  struct Quantizer {

    float map[12][numScales][120] = {{{0.0f}}};

    Quantizer();

    std::tuple<float, int> quantize(float voltsIn);

    std::string noteName(float voltsIn);

    std::string scaleName(int scale);

    std::tuple<float, int> closestVoltageInScale(const float inVolts, const int rootNote, const int scale);

    Chord closestChordInScale(const float inVolts, const int rootNote, const int scale);

  };

}

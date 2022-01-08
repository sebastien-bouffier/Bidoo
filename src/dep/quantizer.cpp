#include "quantizer.hpp"

namespace quantizer {

  float quantize(float voltsIn) {
    float closestVal = 0.0f;
    float closestDist = 1.0f;
    int octaveInVolts = 0;
    if ((voltsIn >= 0.0f) || (voltsIn == (int)voltsIn)) {
      octaveInVolts = int(voltsIn);
    }
    else {
      octaveInVolts = int(voltsIn)-1;
    }
    for (int i = 0; i < 12; i++) {
      float scaleNoteInVolts = octaveInVolts + (float)i / 12.0f;
      float distAway = fabs(voltsIn - scaleNoteInVolts);
      if(distAway < closestDist) {
        closestVal = scaleNoteInVolts;
        closestDist = distAway;
      }
    }
    return closestVal;
  }

  std::string noteName(float inVolts) {
    int octaveInVolts = floor(inVolts);
    float closestDist = 3.0f;
    int noteNumber = 0;

    for (int i = 0; i < 12; i++) {
      float scaleNoteInVolts = octaveInVolts + (float)i / 12.0f;
      float distAway = fabs(inVolts - scaleNoteInVolts);
      if(distAway < closestDist) {
        closestDist = distAway;
        noteNumber = i;
      }
    }
    return rootNotes[noteNumber+1].label + std::to_string(octaveInVolts+4);
  }

  std::string scaleName(int scale) {
    return scales[scale].label;
  }

  float closestVoltageInScale(float inVolts, int rootNote, int scale) {
    if (scale == 0) {
      return inVolts;
    }
    else if (rootNote==-1) {
      return quantize(inVolts);
    }
    else {
      int octaveInVolts = floor(inVolts);
      float closestVal = -4.0f;
      float closestDist = 3.0f;

      for (int i = 0; i < 24; i++) {
        float scaleNoteInVolts = octaveInVolts + int(i/scales[scale].numNotes) + (rootNote / 12.0f) + scales[scale].intervals[i%scales[scale].numNotes] / 12.0f;
        float distAway = fabs(inVolts - scaleNoteInVolts);
        if(distAway < closestDist) {
          closestVal = scaleNoteInVolts;
          closestDist = distAway;
        }
        else { break; }
      }

      return closestVal;
    }
  }

  Chord closestChordInScale(float inVolts, int rootNote, int scale) {
    Chord result;
    if (scale == 0) {
      result.tonic = inVolts;
      result.third = inVolts;
      result.fifth = inVolts;
      result.seventh = inVolts;
      result.ninth = inVolts;
      result.eleventh = inVolts;
      result.thirteenth = inVolts;
    }
    else if (rootNote==-1) {
      result.tonic = quantize(inVolts);
      result.third = quantize(inVolts);
      result.fifth = quantize(inVolts);
      result.seventh = quantize(inVolts);
      result.ninth = quantize(inVolts);
      result.eleventh = quantize(inVolts);
      result.thirteenth = quantize(inVolts);
    }
    else {
      int octaveInVolts = floor(inVolts);
      float closestVal = -4.0f;
      float closestDist = 3.0f;
      int index = 0;
      for (int i = 0; i < 24; i++) {
        float scaleNoteInVolts = octaveInVolts + floor(i/scales[scale].numNotes) + (rootNote / 12.0f) + scales[scale].intervals[i%scales[scale].numNotes] / 12.0f;
        float distAway = fabs(inVolts - scaleNoteInVolts);
        if(distAway < closestDist) {
          closestVal = scaleNoteInVolts;
          closestDist = distAway;
          index = i;
        }
        else { break; }
      }
      result.tonic = closestVal;
      result.third = rack::math::clamp(octaveInVolts + floor((index+2)/scales[scale].numNotes) + (rootNote / 12.0f) + scales[scale].intervals[(index+2)%scales[scale].numNotes] / 12.0f,-4.0f,6.0f);
      result.fifth = rack::math::clamp(octaveInVolts + floor((index+4)/scales[scale].numNotes) + (rootNote / 12.0f) + scales[scale].intervals[(index+4)%scales[scale].numNotes] / 12.0f,-4.0f,6.0f);
      result.seventh = rack::math::clamp(octaveInVolts + floor((index+6)/scales[scale].numNotes) + (rootNote / 12.0f) + scales[scale].intervals[(index+6)%scales[scale].numNotes] / 12.0f,-4.0f,6.0f);
      result.ninth = rack::math::clamp(octaveInVolts + floor((index+8)/scales[scale].numNotes) + (rootNote / 12.0f) + scales[scale].intervals[(index+8)%scales[scale].numNotes] / 12.0f,-4.0f,6.0f);
      result.eleventh = rack::math::clamp(octaveInVolts + floor((index+10)/scales[scale].numNotes) + (rootNote / 12.0f) + scales[scale].intervals[(index+10)%scales[scale].numNotes] / 12.0f,-4.0f,6.0f);
      result.thirteenth = rack::math::clamp(octaveInVolts + floor((index+12)/scales[scale].numNotes) + (rootNote / 12.0f) + scales[scale].intervals[(index+12)%scales[scale].numNotes] / 12.0f,-4.0f,6.0f);
    }
    return result;
  }

  Quantizer::Quantizer() {
    for(int i=0; i<numScales; i++) {
      for (int j=0; j<10; j++) {
        for (int k=0; k<scales[i].numNotes;k++) {
          map[i][j*scales[i].numNotes+k]=-4.f+j+scales[i].intervals[k]/12.0f;
        }
      }
    }
  }

  float Quantizer::quantize(float voltsIn) {
    float closestVal = 0.0f;
    float closestDist = 1.0f;
    int octaveInVolts = 0;
    if ((voltsIn >= 0.0f) || (voltsIn == (int)voltsIn)) {
      octaveInVolts = int(voltsIn);
    }
    else {
      octaveInVolts = int(voltsIn)-1;
    }
    int sIndex = (octaveInVolts+4)*12;
    for (int i = 0; i < 12; i++) {
      float scaleNoteInVolts = map[26][sIndex+i];
      float distAway = fabs(voltsIn - scaleNoteInVolts);
      if(distAway < closestDist) {
        closestVal = scaleNoteInVolts;
        closestDist = distAway;
      }
    }
    return closestVal;
  }

  float Quantizer::closestVoltageInScale(float voltsIn, int rootNote, int scale) {
    if (scale == 0) {
      return voltsIn;
    }
    else if (rootNote==-1) {
      return Quantizer::quantize(voltsIn);
    }
    else {
      float closestVal = 0.0f;
      float closestDist = 1.0f;
      int octaveInVolts = 0;
      if ((voltsIn >= 0.0f) || (voltsIn == (int)voltsIn)) {
        octaveInVolts = int(voltsIn);
      }
      else {
        octaveInVolts = int(voltsIn)-1;
      }
      int sIndex = (octaveInVolts+4)*scales[scale].numNotes;
      float rShift = rootNote / 12.0f;
      for (int i = 0; i < 12; i++) {
        float scaleNoteInVolts = map[scale][sIndex+i] + rShift;
        float distAway = fabs(voltsIn - scaleNoteInVolts);
        if(distAway < closestDist) {
          closestVal = scaleNoteInVolts;
          closestDist = distAway;
        }
        else { break; }
      }

      return closestVal;
    }
  }

  Chord Quantizer::closestChordInScale(float voltsIn, int rootNote, int scale) {
    Chord result;
    if (scale == 0) {
      result.tonic = voltsIn;
      result.third = voltsIn;
      result.fifth = voltsIn;
      result.seventh = voltsIn;
      result.ninth = voltsIn;
      result.eleventh = voltsIn;
      result.thirteenth = voltsIn;
    }
    else if (rootNote==-1) {
      result.tonic = Quantizer::quantize(voltsIn);
      result.third = Quantizer::quantize(voltsIn);
      result.fifth = Quantizer::quantize(voltsIn);
      result.seventh = Quantizer::quantize(voltsIn);
      result.ninth = Quantizer::quantize(voltsIn);
      result.eleventh = Quantizer::quantize(voltsIn);
      result.thirteenth = Quantizer::quantize(voltsIn);
    }
    else {
      float closestVal = 0.0f;
      float closestDist = 1.0f;
      int octaveInVolts = 0;
      if ((voltsIn >= 0.0f) || (voltsIn == (int)voltsIn)) {
        octaveInVolts = int(voltsIn);
      }
      else {
        octaveInVolts = int(voltsIn)-1;
      }
      int sIndex = (octaveInVolts+4)*scales[scale].numNotes;
      float rShift = rootNote / 12.0f;
      for (int i = 0; i < 12; i++) {
        float scaleNoteInVolts = map[scale][sIndex] + rShift;
        float distAway = fabs(voltsIn - scaleNoteInVolts);
        sIndex++;
        if(distAway < closestDist) {
          closestVal = scaleNoteInVolts;
          closestDist = distAway;
        }
        else { break; }
      }
      result.tonic = closestVal;
      result.third = map[scale][sIndex+1] + rShift;
      result.fifth = map[scale][sIndex+3] + rShift;
      result.seventh = map[scale][sIndex+5] + rShift;
      result.ninth = map[scale][sIndex+7] + rShift;
      result.eleventh = map[scale][sIndex+9] + rShift;
      result.thirteenth = map[scale][sIndex+11] + rShift;
    }
    return result;
  }

}

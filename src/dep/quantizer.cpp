#include "quantizer.hpp"
#include <math.hpp>

namespace quantizer {

  Quantizer::Quantizer() {
    for (int l=0; l<12; l++) {
      for(int i=0; i<numScales; i++) {
        for (int j=0; j<10; j++) {
          for (int k=0; k<scales[i].numNotes;k++) {
            map[l][i][j*scales[i].numNotes+k]=-4.f + l/12.0f + j + scales[i].intervals[k]/12.0f;
          }
        }
      }
    }
  }

  std::tuple<float, int> getNearest(float x, float y, float target, int lower, int upper) {
    if (target - x >= y - target)
    {
      return std::make_tuple(y,upper);
    }
    else
    {
      return std::make_tuple(x,lower);
    }
  }

  std::tuple<float, int> getNearestElement(float arr[], int n, float target) {
   if (target <= arr[0]) {
     return std::make_tuple(arr[0],0);
   }

   if (target >= arr[n - 1]) {
     return std::make_tuple(arr[n - 1],n-1);
   }

   int left = 0, right = n, mid = 0;
   while (left < right) {
      mid = (left + right) / 2;
      if (arr[mid] == target) {
        return std::make_tuple(arr[mid],mid);
      }

      if (target < arr[mid]) {
        if ((mid > 0) && (target > arr[mid - 1])) {
          return getNearest(arr[mid - 1], arr[mid], target, mid-1, mid);
        }
        right = mid;
      }
      else
      {
       if ((mid < n - 1) && (target < arr[mid + 1])) {
         return getNearest(arr[mid], arr[mid + 1], target, mid, mid+1);
       }
       left = mid + 1;
      }
   }
   return std::make_tuple(arr[mid],mid);
  }

  std::tuple<float, int> Quantizer::quantize(float voltsIn) {
    return getNearestElement(map[0][26], scales[26].numNotes*10, voltsIn);
  }

  std::string Quantizer::noteName(float voltsIn) {
    int octaveInVolts = floor(voltsIn);
    float closestDist = 3.0f;
    int noteNumber = 0;

    for (int i = 0; i < 12; i++) {
      float scaleNoteInVolts = octaveInVolts + (float)i / 12.0f;
      float distAway = fabs(voltsIn - scaleNoteInVolts);
      if(distAway < closestDist) {
        closestDist = distAway;
        noteNumber = i;
      }
    }
    return rootNotes[noteNumber+1].label + std::to_string(octaveInVolts+4);
  }



  std::tuple<float, int> Quantizer::closestVoltageInScale(float voltsIn, int rootNote, int scale) {
    if (scale == 0) {
      return std::make_tuple(voltsIn,0);
    }
    else if (rootNote==-1) {
      return Quantizer::quantize(voltsIn);
    }
    else {
      return getNearestElement(map[rootNote][scale], scales[scale].numNotes*10, voltsIn);
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
      float pitch;
      int index;
      std::tie(pitch, index) = Quantizer::quantize(voltsIn);
      result.tonic = pitch;
      result.third = result.tonic;
      result.fifth = result.tonic;
      result.seventh = result.tonic;
      result.ninth = result.tonic;
      result.eleventh = result.tonic;
      result.thirteenth = result.tonic;
    }
    else {
      float pitch;
      int index;
      std::tie(pitch, index) = getNearestElement(map[rootNote][scale], scales[scale].numNotes*10, voltsIn);
      result.tonic = pitch;
      result.third = map[rootNote][scale][rack::math::clamp(index+2,0,scales[scale].numNotes*10)];
      result.fifth = map[rootNote][scale][rack::math::clamp(index+4,0,scales[scale].numNotes*10)];
      result.seventh = map[rootNote][scale][rack::math::clamp(index+6,0,scales[scale].numNotes*10)];
      result.ninth = map[rootNote][scale][rack::math::clamp(index+8,0,scales[scale].numNotes*10)];
      result.eleventh = map[rootNote][scale][rack::math::clamp(index+10,0,scales[scale].numNotes*10)];
      result.thirteenth = map[rootNote][scale][rack::math::clamp(index+12,0,scales[scale].numNotes*10)];
    }
    return result;
  }

}

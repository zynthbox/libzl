#pragma once

#include "JUCEHeaders.h"
#include "ClipAudioSource.h"

using namespace std;
using namespace juce;

class SyncTimer : public HighResolutionTimer {
  // HighResolutionTimer interface
 public:
  SyncTimer(int bpm);
  void hiResTimerCallback();
  void setCallback(void (*functionPtr)());

 private:
  int bpm;
  void (*callback)() = nullptr;
};

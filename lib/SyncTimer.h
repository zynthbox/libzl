#pragma once

#include <QQueue>

#include "ClipAudioSource.h"
#include "JUCEHeaders.h"

using namespace std;
using namespace juce;

class SyncTimer : public HighResolutionTimer {
  // HighResolutionTimer interface
 public:
  SyncTimer(int bpm);
  void hiResTimerCallback();
  void setCallback(void (*functionPtr)());
  void addClip(ClipAudioSource *clip);
  void start(int interval);
  void stop();

 private:
  int beat = 0;
  int bpm;
  void (*callback)() = nullptr;
  QQueue<ClipAudioSource *> clipsQueue;
};

#pragma once

#include <QQueue>

#include "ClipAudioSource.h"
#include "JUCEHeaders.h"

using namespace std;
using namespace juce;

class SyncTimer : public HighResolutionTimer {
  // HighResolutionTimer interface
 public:
  SyncTimer();
  void hiResTimerCallback();
  void setCallback(void (*functionPtr)());
  void addClip(ClipAudioSource *clip);
  void start(int interval);
  void stop();
  void stopClip(ClipAudioSource *clip);

 private:
  int beat = 0;
  void (*callback)() = nullptr;
  QQueue<ClipAudioSource *> clipsQueue;
  QQueue<ClipAudioSource *> clipsStopQueue;
};

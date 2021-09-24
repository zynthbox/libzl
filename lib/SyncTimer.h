#pragma once

#include <QList>
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
  void setCallback(void (*functionPtr)(int));
  void removeCallback(void (*functionPtr)(int));
  void queueClipToStart(ClipAudioSource *clip);
  void queueClipToStop(ClipAudioSource *clip);
  void start(int bpm);
  void stop();
  void stopClip(ClipAudioSource *clip);
  int getInterval(int bpm);
  int getMultiplier();

private:
  int playingClipsCount = 0;
  int beat = 0;
  int bpm = 0;
  int multiplier;
  QList<void (*)(int)> callbacks;
  QQueue<ClipAudioSource *> clipsStartQueue;
  QQueue<ClipAudioSource *> clipsStopQueue;
};

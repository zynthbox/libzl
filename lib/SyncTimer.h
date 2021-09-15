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
  void setCallbackOneFourth(void (*functionPtr)(int));
  void setCallbackOneEighth(void (*functionPtr)(int));
  void setCallbackOneSixteenth(void (*functionPtr)(int));
  void setCallbackOneThirtySecond(void (*functionPtr)(int));
  void queueClipToStart(ClipAudioSource *clip);
  void queueClipToStop(ClipAudioSource *clip);
  void start(int bpm);
  void stop();
  void stopClip(ClipAudioSource *clip);

private:
  int playingClipsCount = 0;
  int beat = 0;
  void (*oneFourthCallback)(int) = nullptr;
  void (*oneEighthCallback)(int) = nullptr;
  void (*oneSixteenthCallback)(int) = nullptr;
  void (*oneThirtySecondCallback)(int) = nullptr;
  QQueue<ClipAudioSource *> clipsStartQueue;
  QQueue<ClipAudioSource *> clipsStopQueue;
};

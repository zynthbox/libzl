#include "SyncTimer.h"

using namespace std;

SyncTimer::SyncTimer() {}

void SyncTimer::hiResTimerCallback() {
  if (beat == 0) {
    playingClipsCount = playingClipsCount - clipsStopQueue.size();
    while (!clipsStopQueue.isEmpty()) {
      clipsStopQueue.dequeue()->stop();
    }

    playingClipsCount = playingClipsCount + clipsStartQueue.size();
    while (!clipsStartQueue.isEmpty()) {
      clipsStartQueue.dequeue()->play();
    }
  }

  beat = (beat + 1) % 16;

  //  if (oneFourthCallback != nullptr && beat % 4 == 0) {
  //    oneFourthCallback(beat / 4);
  //  }
  //  if (oneEighthCallback != nullptr && beat % 2 == 0) {
  //    oneEighthCallback(beat / 2);
  //  }
  if (oneSixteenthCallback != nullptr) {
    oneSixteenthCallback(beat);
  }
  //  if (oneThirtySecondCallback != nullptr) {
  //    oneThirtySecondCallback(beat);
  //  }
}

void SyncTimer::setCallbackOneFourth(void (*functionPtr)(int)) {
  oneFourthCallback = functionPtr;
}

void SyncTimer::setCallbackOneEighth(void (*functionPtr)(int)) {
  oneEighthCallback = functionPtr;
}

void SyncTimer::setCallbackOneSixteenth(void (*functionPtr)(int)) {
  oneSixteenthCallback = functionPtr;
}

void SyncTimer::setCallbackOneThirtySecond(void (*functionPtr)(int)) {
  oneThirtySecondCallback = functionPtr;
}

void SyncTimer::queueClipToStart(ClipAudioSource *clip) {
  for (ClipAudioSource *c : clipsStopQueue) {
    if (c == clip) {
      cerr << "Found clip(" << c << ") in stop queue. Removing from stop queue"
           << endl;
      clipsStopQueue.removeOne(c);
    }
  }
  clipsStartQueue.enqueue(clip);
}

void SyncTimer::queueClipToStop(ClipAudioSource *clip) {
  for (ClipAudioSource *c : clipsStartQueue) {
    if (c == clip) {
      cerr << "Found clip(" << c
           << ") in start queue. Removing from start queue" << endl;
      clipsStartQueue.removeOne(c);
    }
  }
  clipsStopQueue.enqueue(clip);
}

void SyncTimer::start(int bpm) {
  // Calculate interval for 1/16
  int interval = floor(((60 / (float)bpm) / 4) * 1000);

  cerr << "#### Starting timer with bpm " << bpm << " and interval " << interval
       << endl;
  startTimer(interval);
}

void SyncTimer::stop() {
  cerr << "#### Stopping timer" << endl;

  stopTimer();
  beat = 0;
}

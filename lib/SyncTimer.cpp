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

  beat = (beat + 1) % 4;

  if (callback != nullptr) {
    callback();
  }
}

void SyncTimer::setCallback(void (*functionPtr)()) { callback = functionPtr; }

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

void SyncTimer::start(int interval) {
  cerr << "#### Starting timer with interval " << interval << endl;

  startTimer(interval);
}

void SyncTimer::stop() {
  cerr << "#### Stopping timer" << endl;

  stopTimer();
  beat = 0;
}

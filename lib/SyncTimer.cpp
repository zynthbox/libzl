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

  if (callback != nullptr) {
    callback(beat);
  }

  beat = (beat + 1) % 16;
}

void SyncTimer::setCallback(void (*functionPtr)(int)) {
  callback = functionPtr;
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
  int interval = 60000 / (bpm * 4);

  cerr << "#### Starting timer with bpm " << bpm << " and interval " << interval
       << endl;
  startTimer(interval);
}

void SyncTimer::stop() {
  cerr << "#### Stopping timer" << endl;

  stopTimer();
  beat = 0;
}

#include "SyncTimer.h"

using namespace std;

SyncTimer::SyncTimer() {}

void SyncTimer::hiResTimerCallback() {
  if (beat == 0) {
    while (!clipsQueue.isEmpty()) {
      clipsQueue.dequeue()->play();
    }
  }

  beat = (beat + 1) % 4;

  if (callback != nullptr) {
    callback();
  }
}

void SyncTimer::setCallback(void (*functionPtr)()) { callback = functionPtr; }

void SyncTimer::addClip(ClipAudioSource *clip) {
  this->clipsQueue.enqueue(clip);
}

void SyncTimer::start(int interval) {
  cerr << "#### Starting timer with interval " << interval << endl;

  beat = 0;
  startTimer(interval);
}

void SyncTimer::stop() {
  cerr << "#### Stopping timer" << endl;

  stopTimer();
}

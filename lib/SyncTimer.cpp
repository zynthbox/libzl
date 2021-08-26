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

  while (!clipsStopQueue.isEmpty()) {
    clipsStopQueue.dequeue()->stop();
  }
}

void SyncTimer::setCallback(void (*functionPtr)()) { callback = functionPtr; }

void SyncTimer::addClip(ClipAudioSource *clip) {
  this->clipsQueue.enqueue(clip);
}

void SyncTimer::start(int interval) {
  beat = 0;
  startTimer(interval);
}

void SyncTimer::stop() { stopTimer(); }

void SyncTimer::stopClip(ClipAudioSource *clip) {
  this->clipsStopQueue.enqueue(clip);
}

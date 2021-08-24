#include "SyncTimer.h"

using namespace std;

SyncTimer::SyncTimer(int bpm) : bpm(bpm) {}

void SyncTimer::hiResTimerCallback() {
  beat = (beat + 1) % 4;

  if (beat == 0) {
    while (!clipsToPlay.isEmpty()) {
      auto clip = clipsToPlay.dequeue();
      clip->play();
      playingClips.append(clip);
    }
  }

  if (callback != nullptr) {
    callback();
  }
}

void SyncTimer::setCallback(void (*functionPtr)()) { callback = functionPtr; }

void SyncTimer::addClip(ClipAudioSource *clip) { clipsToPlay.enqueue(clip); }

void SyncTimer::removeClip(ClipAudioSource *clip) {
  clip->stop();
  playingClips.removeOne(clip);
}

void SyncTimer::removeAllClips() {
  while (!playingClips.empty()) {
    auto clip = playingClips.first();
    clip->stop();
    playingClips.removeFirst();
  }
}

void SyncTimer::start(int interval) {
  beat = 0;
  startTimer(interval);
}

void SyncTimer::stop() { stopTimer(); }

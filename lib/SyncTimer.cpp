#include "SyncTimer.h"

using namespace std;

SyncTimer::SyncTimer(int bpm) : bpm(bpm) {}

void SyncTimer::hiResTimerCallback() {
  beat = (beat + 1) % 4;

  if (beat == 0) {
    for (ClipAudioSource *clip : clips) {
      clip->play(false);
    }
  }

  cerr << "   C++ Current Beat : " << beat << endl;

  if (callback != nullptr) {
    callback();
  }
}

void SyncTimer::setCallback(void (*functionPtr)()) { callback = functionPtr; }

void SyncTimer::addClip(ClipAudioSource *clip) { this->clips.append(clip); }

void SyncTimer::removeClip(ClipAudioSource *clip) {
  this->clips.removeOne(clip);
}

#include "SyncTimer.h"

SyncTimer::SyncTimer(int bpm) : bpm(bpm) {}

void SyncTimer::hiResTimerCallback() {
  if (callback != nullptr) {
    callback();
  }
}

void SyncTimer::setCallback(void (*functionPtr)()) { callback = functionPtr; }

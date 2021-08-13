/*
  ==============================================================================

    libzl.cpp
    Created: 9 Aug 2021 6:28:51pm
    Author:  root

  ==============================================================================
*/

#include "libzl.h"

#include <iostream>

#include "JUCEHeaders.h"
#include "SyncTimer.h"
#include "ZynthiLoopsComponent.h"

using namespace std;

SyncTimer syncTimer(120);

ZynthiLoopsComponent* ZynthiLoopsComponent_new(const char* filepath) {
  return new ZynthiLoopsComponent(filepath);
}

void ZynthiLoopsComponent_play(ZynthiLoopsComponent* c) { c->play(); }

void ZynthiLoopsComponent_stop(ZynthiLoopsComponent* c) { c->stop(); }

float ZynthiLoopsComponent_getDuration(ZynthiLoopsComponent* c) {
  return c->getDuration();
}

const char* ZynthiLoopsComponent_getFileName(ZynthiLoopsComponent* c) {
  return c->getFileName();
}

void ZynthiLoopsComponent_setStartPosition(ZynthiLoopsComponent* c,
                                           float startPositionInSeconds) {
  c->setStartPosition(startPositionInSeconds);
}

void ZynthiLoopsComponent_setLength(ZynthiLoopsComponent* c,
                                    float lengthInSeconds) {
  c->setLength(lengthInSeconds);
}

void testLoop() {
  ZynthiLoopsComponent* clip1 =
      new ZynthiLoopsComponent("/zynthian/zynthian-my-data/capture/main.wav");
  clip1->setLength(6);

  ZynthiLoopsComponent* clip2 =
      new ZynthiLoopsComponent("/zynthian/zynthian-my-data/capture/drums.wav");

  clip1->play();
  clip2->play();
}

void registerTimerCallback(void (*functionPtr)()) {
  syncTimer.setCallback(functionPtr);
}

void startTimer(int interval) { syncTimer.startTimer(interval); }

void stopTimer() { syncTimer.stopTimer(); }

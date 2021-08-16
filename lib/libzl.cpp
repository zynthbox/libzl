/*
  ==============================================================================

    libzl.cpp
    Created: 9 Aug 2021 6:28:51pm
    Author:  root

  ==============================================================================
*/

#include "libzl.h"

#include <iostream>

#include "ClipAudioSource.h"
#include "JUCEHeaders.h"
#include "SyncTimer.h"

using namespace std;

juce::ScopedJuceInitialiser_GUI platform;
SyncTimer syncTimer(120);

//////////////
/// ClipAudioSource API Bridge
//////////////
ClipAudioSource* ClipAudioSource_new(const char* filepath) {
  return new ClipAudioSource(filepath);
}

void ClipAudioSource_play(ClipAudioSource* c) { c->play(); }

void ClipAudioSource_stop(ClipAudioSource* c) { c->stop(); }

float ClipAudioSource_getDuration(ClipAudioSource* c) {
  return c->getDuration();
}

const char* ClipAudioSource_getFileName(ClipAudioSource* c) {
  return c->getFileName();
}

void ClipAudioSource_setStartPosition(ClipAudioSource* c,
                                      float startPositionInSeconds) {
  c->setStartPosition(startPositionInSeconds);
}

void ClipAudioSource_setLength(ClipAudioSource* c, float lengthInSeconds) {
  c->setLength(lengthInSeconds);
}
//////////////
/// END ClipAudioSource API Bridge
//////////////

void registerTimerCallback(void (*functionPtr)()) {
  syncTimer.setCallback(functionPtr);
}

void startTimer(int interval) { syncTimer.startTimer(interval); }

void stopTimer() { syncTimer.stopTimer(); }

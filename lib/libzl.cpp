/*
  ==============================================================================

    libzl.cpp
    Created: 9 Aug 2021 6:28:51pm
    Author:  root

  ==============================================================================
*/

#include "libzl.h"

#include <iostream>

#include "../tracktion_engine/examples/common/Utilities.h"
#include "ClipAudioSource.h"
#include "JUCEHeaders.h"
#include "SyncTimer.h"

using namespace std;

class JuceEventLoopThread : public Thread {
 public:
  JuceEventLoopThread() : Thread("Juce EventLoop Thread") {}

  void run() override { MessageManager::getInstance()->runDispatchLoop(); }
};

ScopedJuceInitialiser_GUI* initializer = nullptr;
SyncTimer syncTimer(120);
JuceEventLoopThread elThread;

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

void startLoop() {
  ScopedJuceInitialiser_GUI libraryInitialiser;

  te::Engine engine{"libzl"};
  te::Edit edit{engine, te::createEmptyEdit(engine), te::Edit::forEditing,
                nullptr, 0};
  te::TransportControl& transport{edit.getTransport()};

  auto wavFile = File("/zynthian/zynthian-my-data/capture/c4.wav");
  const File editFile("/tmp/editfile");
  auto clip = EngineHelpers::loadAudioFileAsClip(edit, wavFile);

  te::TimeStretcher ts;

  for (auto mode : ts.getPossibleModes(engine, true)) {
    cerr << "Mode : " << mode << endl;
  }

  clip->setAutoTempo(false);
  clip->setAutoPitch(false);
  clip->setTimeStretchMode(te::TimeStretcher::defaultMode);

  EngineHelpers::loopAroundClip(*clip);
  clip->setSpeedRatio(2.0);
  clip->setPitchChange(12);
  EngineHelpers::loopAroundClip(*clip);

  MessageManager::getInstance()->runDispatchLoop();
}

void ClipAudioSource_setSpeedRatio(ClipAudioSource* c, float speedRatio) {
  c->setSpeedRatio(speedRatio);
}

void ClipAudioSource_setPitch(ClipAudioSource* c, float pitchChange) {
  c->setPitch(pitchChange);
}

void initJuce() {
  initializer = new ScopedJuceInitialiser_GUI();
  elThread.startThread();
}

void shutdownJuce() {
  elThread.stopThread(500);
  initializer = nullptr;
}

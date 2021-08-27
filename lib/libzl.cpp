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
#include "Helper.h"
#include "JUCEHeaders.h"
#include "SyncTimer.h"
#include "WaveFormItem.h"

using namespace std;

ScopedJuceInitialiser_GUI* initializer = nullptr;
SyncTimer* syncTimer = new SyncTimer();

class JuceEventLoopThread : public Thread {
 public:
  JuceEventLoopThread() : Thread("Juce EventLoop Thread") {}

  void run() override {
    if (initializer == nullptr) initializer = new ScopedJuceInitialiser_GUI();

    MessageManager::getInstance()->runDispatchLoop();
  }

  void playClip(ClipAudioSource* c) { c->play(); }

  void stopClip(ClipAudioSource* c) { c->stop(); }

  void setClipLength(ClipAudioSource* c, float lengthInSeconds) {
    c->setLength(lengthInSeconds);
  }

  void setClipStartPosition(ClipAudioSource* c, float startPositionInSeconds) {
    c->setStartPosition(startPositionInSeconds);
  }

  void setClipSpeedRatio(ClipAudioSource* c, float speedRatio) {
    c->setSpeedRatio(speedRatio);
  }

  void setClipPitch(ClipAudioSource* c, float pitchChange) {
    c->setPitch(pitchChange);
  }

  void stopClips(int size, ClipAudioSource** clips) {
    for (int i = 0; i < size; i++) {
      ClipAudioSource* clip = clips[i];

      cerr << "Stopping clip arr[" << i << "] : " << clips[i] << endl;
      clip->stop();
    }
  }

  void destroyClip(ClipAudioSource* c) { delete c; }
};

JuceEventLoopThread elThread;

//////////////
/// ClipAudioSource API Bridge
//////////////
ClipAudioSource* ClipAudioSource_new(const char* filepath) {
  ClipAudioSource* sClip;

  Helper::callFunctionOnMessageThread(
      [&]() { sClip = new ClipAudioSource(syncTimer, filepath); }, true);

  return sClip;
}

void ClipAudioSource_play(ClipAudioSource* c) {
  //  Helper::callFunctionOnMessageThread([&]() { c->play(); }, true);
  elThread.playClip(c);
}

void ClipAudioSource_stop(ClipAudioSource* c) {
  cerr << "libzl : Stop Clip " << c;

  //  Helper::callFunctionOnMessageThread([&]() { c->stop(); });  //, true);
  //  c->stop();

  elThread.stopClip(c);
}

float ClipAudioSource_getDuration(ClipAudioSource* c) {
  return c->getDuration();
}

const char* ClipAudioSource_getFileName(ClipAudioSource* c) {
  return c->getFileName();
}

void ClipAudioSource_setStartPosition(ClipAudioSource* c,
                                      float startPositionInSeconds) {
  //  Helper::callFunctionOnMessageThread(
  //      [&]() { c->setStartPosition(startPositionInSeconds); }, true);
  elThread.setClipStartPosition(c, startPositionInSeconds);
}

void ClipAudioSource_setLength(ClipAudioSource* c, float lengthInSeconds) {
  //  Helper::callFunctionOnMessageThread([&]() { c->setLength(lengthInSeconds);
  //  },
  //                                      true);
  elThread.setClipLength(c, lengthInSeconds);
}

void ClipAudioSource_setSpeedRatio(ClipAudioSource* c, float speedRatio) {
  //  Helper::callFunctionOnMessageThread([&]() { c->setSpeedRatio(speedRatio);
  //  },
  //                                      true);
  elThread.setClipSpeedRatio(c, speedRatio);
}

void ClipAudioSource_setPitch(ClipAudioSource* c, float pitchChange) {
  //  Helper::callFunctionOnMessageThread([&]() { c->setPitch(pitchChange); },
  //                                      true);

  elThread.setClipPitch(c, pitchChange);
}

void ClipAudioSource_destroy(ClipAudioSource* c) { elThread.destroyClip(c); }
//////////////
/// END ClipAudioSource API Bridge
//////////////

//////////////
/// SynTimer API Bridge
//////////////
void SyncTimer_startTimer(int interval) { syncTimer->start(interval); }

void SyncTimer_stopTimer() { syncTimer->stop(); }

void SyncTimer_registerTimerCallback(void (*functionPtr)()) {
  syncTimer->setCallback(functionPtr);
}

void SyncTimer_addClip(ClipAudioSource* clip) {
  Helper::callFunctionOnMessageThread([&]() { syncTimer->addClip(clip); },
                                      true);
}
//////////////
/// END SyncTimer API Bridge
//////////////

void initJuce() {
  cerr << "### INIT JUCE\n";
  elThread.startThread();
}

void shutdownJuce() {
  elThread.stopThread(500);
  initializer = nullptr;
}

void registerGraphicTypes() {
  qmlRegisterType<WaveFormItem>("JuceGraphics", 1, 0, "WaveFormItem");
}

void stopClips(int size, ClipAudioSource** clips) {
  elThread.stopClips(size, clips);
}

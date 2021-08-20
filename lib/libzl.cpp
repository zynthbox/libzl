/*
  ==============================================================================

    libzl.cpp
    Created: 9 Aug 2021 6:28:51pm
    Author:  root

  ==============================================================================
*/

#include "libzl.h"

#include <iostream>
#include <unistd.h>

#include "../tracktion_engine/examples/common/Utilities.h"
#include "ClipAudioSource.h"
#include "JUCEHeaders.h"
#include "SyncTimer.h"

using namespace std;

class PluginEngineBehaviour : public te::EngineBehaviour
{
public:
  bool autoInitialiseDeviceManager() override { return false; }
};

struct EngineWrapper
{
  EngineWrapper()
  : audioInterface (engine.getDeviceManager().getHostedAudioDeviceInterface())
  {
    JUCE_ASSERT_MESSAGE_THREAD
    audioInterface.initialise ({});
  }
  
  te::Engine engine { "libzl", nullptr, std::make_unique<PluginEngineBehaviour>() };
  te::Edit edit { engine, te::createEmptyEdit (engine), te::Edit::forEditing, nullptr, 0 };
  te::TransportControl& transport { edit.getTransport() };
  te::HostedAudioDeviceInterface& audioInterface;
  te::ExternalPlayheadSynchroniser playheadSynchroniser { edit };
};

class JuceEventLoopThread : public Thread {
 public:
  JuceEventLoopThread() : Thread("Juce EventLoop Thread") {}

  void run() override { MessageManager::getInstance()->runDispatchLoop(); }
};

ScopedJuceInitialiser_GUI* initializer = nullptr;
SyncTimer syncTimer(120);
JuceEventLoopThread elThread;
std::unique_ptr<EngineWrapper> engineWrapper;

template<typename Function>
void callFunctionOnMessageThread (Function&& func)
{
  if (MessageManager::getInstance()->isThisTheMessageThread())
  {
    func();
  }
  else
  {
    jassert (! MessageManager::getInstance()->currentThreadHasLockedMessageManager());
    WaitableEvent finishedSignal;
    MessageManager::callAsync ([&]
    {
      func();
      finishedSignal.signal();
    });
    finishedSignal.wait (-1);
  }
}

void ensureEngineCreatedOnMessageThread()
{
  if (! engineWrapper)
    callFunctionOnMessageThread ([&] { engineWrapper = std::make_unique<EngineWrapper>(); });
}

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

void startLoop(const char* filepath) {
  ScopedJuceInitialiser_GUI libraryInitialiser;

//   te::Engine engine{"libzl"};
//   te::Edit edit{engine, te::createEmptyEdit(engine), te::Edit::forEditing,
//                 nullptr, 0};
//   te::TransportControl& transport{edit.getTransport()};
// 
  auto wavFile = File(filepath);
//   const File editFile("/tmp/editfile");
  auto clip = EngineHelpers::loadAudioFileAsClip(engineWrapper->edit, wavFile);
// 
//   te::TimeStretcher ts;
// 
//   for (auto mode : ts.getPossibleModes(engine, true)) {
//     cerr << "Mode : " << mode << endl;
//   }
// 
  clip->setAutoTempo(false);
  clip->setAutoPitch(false);
  clip->setTimeStretchMode(te::TimeStretcher::defaultMode);
// 
  EngineHelpers::loopAroundClip(*clip);
  clip->setSpeedRatio(2.0);
  clip->setPitchChange(12);
  EngineHelpers::loopAroundClip(*clip);

//   MessageManager::getInstance()->runDispatchLoop();
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
  sleep(2);
  ensureEngineCreatedOnMessageThread();
}

void shutdownJuce() {
  elThread.stopThread(500);
  initializer = nullptr;
}

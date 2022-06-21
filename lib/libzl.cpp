/*
  ==============================================================================

    libzl.cpp
    Created: 9 Aug 2021 6:28:51pm
    Author:  root

  ==============================================================================
*/

#include "libzl.h"

#include <unistd.h>
#include <iostream>
#include <chrono>
using namespace std::chrono;

#include <jack/jack.h>

#include <QDebug>
#include <QTimer>
#include <QtQml/qqml.h>
#include <QQmlEngine>
#include <QQmlContext>

#include "ClipAudioSource.h"
#include "Helper.h"
#include "JUCEHeaders.h"
#include "SamplerSynth.h"
#include "SyncTimer.h"
#include "WaveFormItem.h"
#include "AudioLevels.h"

using namespace std;

ScopedJuceInitialiser_GUI *initializer = nullptr;
SyncTimer *syncTimer = new SyncTimer();
te::Engine *tracktionEngine{nullptr};
QList<ClipAudioSource*> createdClips;
AudioLevels *audioLevelsInstance{nullptr};

class JuceEventLoopThread : public Thread {
public:
  JuceEventLoopThread() : Thread("Juce EventLoop Thread") {}

  void run() override {
    if (initializer == nullptr)
      initializer = new ScopedJuceInitialiser_GUI();

    MessageManager::getInstance()->runDispatchLoop();
  }

  void playClip(ClipAudioSource *c, bool loop) { c->play(loop); }

  void stopClip(ClipAudioSource *c) { c->stop(); }

  void playClipOnChannel(ClipAudioSource *c, bool loop, int midiChannel) { c->play(loop, midiChannel); }

  void stopClipOnChannel(ClipAudioSource *c, int midiChannel) { c->stop(midiChannel); }

  void setClipLength(ClipAudioSource *c, float beat, int bpm) {
    c->setLength(beat, bpm);
  }

  void setClipStartPosition(ClipAudioSource *c, float startPositionInSeconds) {
    c->setStartPosition(startPositionInSeconds);
  }

  void setClipSpeedRatio(ClipAudioSource *c, float speedRatio) {
    c->setSpeedRatio(speedRatio);
  }

  void setClipPitch(ClipAudioSource *c, float pitchChange) {
    c->setPitch(pitchChange);
  }

  void setClipGain(ClipAudioSource *c, float db) { c->setGain(db); }

  void setClipVolume(ClipAudioSource *c, float vol) { c->setVolume(vol); }

  void stopClips(int size, ClipAudioSource **clips) {
    for (int i = 0; i < size; i++) {
      ClipAudioSource *clip = clips[i];

      cerr << "Stopping clip arr[" << i << "] : " << clips[i] << endl;
      clip->stop();
    }
  }

  void destroyClip(ClipAudioSource *c) { delete c; }
};

JuceEventLoopThread elThread;

//////////////
/// ClipAudioSource API Bridge
//////////////
ClipAudioSource *ClipAudioSource_byID(int id) {
    ClipAudioSource *clip{nullptr};
    for (ClipAudioSource *needle : createdClips) {
        if (needle->id() == id) {
            clip = needle;
            break;
        }
    }
    return clip;
}

ClipAudioSource *ClipAudioSource_new(const char *filepath, bool muted) {
  ClipAudioSource *sClip = new ClipAudioSource(tracktionEngine, syncTimer, filepath, muted, qApp);
  sClip->moveToThread(qApp->thread());

  static int clipID{1};
  sClip->setId(clipID);
  ++clipID;

  createdClips << sClip;
  return sClip;
}

void ClipAudioSource_play(ClipAudioSource *c, bool loop) {
  elThread.playClip(c, loop);
}

void ClipAudioSource_stop(ClipAudioSource *c) {
  cerr << "libzl : Stop Clip " << c;

  elThread.stopClip(c);
}

void ClipAudioSource_playOnChannel(ClipAudioSource *c, bool loop, int midiChannel) {
  elThread.playClipOnChannel(c, loop, midiChannel);
}

void ClipAudioSource_stopOnChannel(ClipAudioSource *c, int midiChannel) {
  cerr << "libzl : Stop Clip " << c;

  elThread.stopClipOnChannel(c, midiChannel);
}

float ClipAudioSource_getDuration(ClipAudioSource *c) {
  return c->getDuration();
}

const char *ClipAudioSource_getFileName(ClipAudioSource *c) {
  return c->getFileName();
}

void ClipAudioSource_setProgressCallback(ClipAudioSource *c,
                                         void (*functionPtr)(float)) {
  c->setProgressCallback(functionPtr);
}

void ClipAudioSource_setStartPosition(ClipAudioSource *c,
                                      float startPositionInSeconds) {
  elThread.setClipStartPosition(c, startPositionInSeconds);
}

void ClipAudioSource_setLength(ClipAudioSource *c, float beat, int bpm) {
  elThread.setClipLength(c, beat, bpm);
}

void ClipAudioSource_setSpeedRatio(ClipAudioSource *c, float speedRatio) {
  elThread.setClipSpeedRatio(c, speedRatio);
}

void ClipAudioSource_setPitch(ClipAudioSource *c, float pitchChange) {
  elThread.setClipPitch(c, pitchChange);
}

void ClipAudioSource_setGain(ClipAudioSource *c, float db) {
  elThread.setClipGain(c, db);
}

void ClipAudioSource_setVolume(ClipAudioSource *c, float vol) {
  elThread.setClipVolume(c, vol);
}

void ClipAudioSource_setAudioLevelChangedCallback(ClipAudioSource *c,
                                                  void (*functionPtr)(float)) {
  c->setAudioLevelChangedCallback(functionPtr);
}

void ClipAudioSource_setSlices(ClipAudioSource *c, int slices) {
    c->setSlices(slices);
}

int ClipAudioSource_keyZoneStart(ClipAudioSource *c) {
  return c->keyZoneStart();
}

void ClipAudioSource_setKeyZoneStart(ClipAudioSource *c, int keyZoneStart) {
  c->setKeyZoneStart(keyZoneStart);
}

int ClipAudioSource_keyZoneEnd(ClipAudioSource *c) {
  return c->keyZoneEnd();
}

void ClipAudioSource_setKeyZoneEnd(ClipAudioSource *c, int keyZoneEnd) {
  c->setKeyZoneEnd(keyZoneEnd);
}

int ClipAudioSource_rootNote(ClipAudioSource *c) {
  return c->rootNote();
}

void ClipAudioSource_setRootNote(ClipAudioSource *c, int rootNote) {
  c->setRootNote(rootNote);
}

void ClipAudioSource_destroy(ClipAudioSource *c) {
  ClipAudioSource *clip = qobject_cast<ClipAudioSource*>(c);
  if (clip) {
    createdClips.removeAll(clip);
  }
  elThread.destroyClip(c);
}

int ClipAudioSource_id(ClipAudioSource *c) { return c->id(); }
//////////////
/// END ClipAudioSource API Bridge
//////////////

//////////////
/// SynTimer API Bridge
//////////////
QObject *SyncTimer_instance() { return syncTimer; }

void SyncTimer_startTimer(int interval) { syncTimer->start(interval); }

void SyncTimer_stopTimer() { syncTimer->stop(); }

void SyncTimer_registerTimerCallback(void (*functionPtr)(int)) {
  syncTimer->addCallback(functionPtr);
}

void SyncTimer_deregisterTimerCallback(void (*functionPtr)(int)) {
  syncTimer->removeCallback(functionPtr);
}

void SyncTimer_queueClipToStart(ClipAudioSource *clip) {
  Helper::callFunctionOnMessageThread(
      [&]() { syncTimer->queueClipToStart(clip); }, true);
}

void SyncTimer_queueClipToStartOnChannel(ClipAudioSource *clip, int midiChannel) {
  Helper::callFunctionOnMessageThread(
      [&]() { syncTimer->queueClipToStartOnChannel(clip, midiChannel); }, true);
}

void SyncTimer_queueClipToStop(ClipAudioSource *clip) {
  Helper::callFunctionOnMessageThread(
      [&]() { syncTimer->queueClipToStop(clip); }, true);
}

void SyncTimer_queueClipToStopOnChannel(ClipAudioSource *clip, int midiChannel) {
  Helper::callFunctionOnMessageThread(
      [&]() { syncTimer->queueClipToStopOnChannel(clip, midiChannel); }, true);
}
//////////////
/// END SyncTimer API Bridge
//////////////

class ZLEngineBehavior : public te::EngineBehaviour {
  bool autoInitialiseDeviceManager() override { return false; }
};

void initJuce() {
  qDebug() << "### JUCE initialisation start";
  elThread.startThread();
  qDebug() << "Started juce event loop, initialising...";

  bool initialisationCompleted{false};
  auto juceInitialiser = [&](){
    qDebug() << "Getting us an engine";
    tracktionEngine = new te::Engine("libzl", nullptr, std::make_unique<ZLEngineBehavior>());
    qDebug() << "Setting device type to JACK";
    tracktionEngine->getDeviceManager().deviceManager.setCurrentAudioDeviceType("JACK", true);
    qDebug() << "Initialising device manager";
    tracktionEngine->getDeviceManager().initialise(0, 2);
    qDebug() << "Initialising SamplerSynth";
    SamplerSynth::instance()->initialize(tracktionEngine);
    qDebug() << "Initialisation completed";
    initialisationCompleted = true;
  };
  auto start = high_resolution_clock::now();
  while (!initialisationCompleted) {
    Helper::callFunctionOnMessageThread(juceInitialiser, true, 10000);
    if (!initialisationCompleted) {
      qWarning() << "Failed to initialise juce in 10 seconds, retrying...";
      if (tracktionEngine) {
        delete tracktionEngine;
        tracktionEngine = nullptr;
      }
    }
  }
  auto duration = duration_cast<milliseconds>(high_resolution_clock::now() - start);
  qDebug() << "### JUCE initialisation took" << duration.count() << "ms";

  if (audioLevelsInstance == nullptr) {
    audioLevelsInstance = new AudioLevels();
    QQmlEngine::setObjectOwnership(audioLevelsInstance, QQmlEngine::CppOwnership);
  }

  qmlRegisterSingletonType<AudioLevels>("libzl", 1, 0, "AudioLevels", [](QQmlEngine */*engine*/, QJSEngine *scriptEngine) -> QObject * {
    Q_UNUSED(scriptEngine)

    return audioLevelsInstance;
  });
}

void shutdownJuce() {
  elThread.stopThread(500);
  initializer = nullptr;
}

void registerGraphicTypes() {
  qmlRegisterType<WaveFormItem>("JuceGraphics", 1, 0, "WaveFormItem");
}

void stopClips(int size, ClipAudioSource **clips) {
  elThread.stopClips(size, clips);
}

float dBFromVolume(float vol) { return te::volumeFaderPositionToDB(vol); }

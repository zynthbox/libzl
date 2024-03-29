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
#include <QString>

#include "ClipAudioSource.h"
#include "Helper.h"
#include "JUCEHeaders.h"
#include "SamplerSynth.h"
#include "SyncTimer.h"
#include "WaveFormItem.h"
#include "AudioLevels.h"
#include "MidiRouter.h"
#include "JackPassthrough.h"

using namespace std;

ScopedJuceInitialiser_GUI *initializer = nullptr;
SyncTimer *syncTimer{nullptr};
te::Engine *tracktionEngine{nullptr};
QList<ClipAudioSource*> createdClips;

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

  void setClipPan(ClipAudioSource *c, float pan) {
    c->setPan(pan);
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

  void destroyClip(ClipAudioSource *c) {
    SamplerSynth::instance()->unregisterClip(c);
    c->deleteLater();
  }
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
  cerr << "libzl : Start Clip " << c << std::endl;
  Helper::callFunctionOnMessageThread(
      [&]() {
        elThread.playClip(c, loop);
      }, true);
}

void ClipAudioSource_stop(ClipAudioSource *c) {
  cerr << "libzl : Stop Clip " << c << std::endl;
  Helper::callFunctionOnMessageThread(
      [&]() {
        elThread.stopClip(c);
      }, true);
}

void ClipAudioSource_playOnChannel(ClipAudioSource *c, bool loop, int midiChannel) {
  cerr << "libzl : Play Clip " << c << " on channel " << midiChannel << std::endl;
  Helper::callFunctionOnMessageThread(
      [&]() {
        elThread.playClipOnChannel(c, loop, midiChannel);
      }, true);
}

void ClipAudioSource_stopOnChannel(ClipAudioSource *c, int midiChannel) {
  cerr << "libzl : Stop Clip " << c << " on channel " << midiChannel << std::endl;
  Helper::callFunctionOnMessageThread(
      [&]() {
        elThread.stopClipOnChannel(c, midiChannel);
      }, true);
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
  Helper::callFunctionOnMessageThread(
      [&]() {
        elThread.setClipStartPosition(c, startPositionInSeconds);
      }, true);
}

void ClipAudioSource_setLength(ClipAudioSource *c, float beat, int bpm) {
  Helper::callFunctionOnMessageThread(
      [&]() {
        elThread.setClipLength(c, beat, bpm);
      }, true);
}

void ClipAudioSource_setPan(ClipAudioSource *c, float pan) {
  Helper::callFunctionOnMessageThread(
      [&]() {
        elThread.setClipPan(c, pan);
      }, true);
}

void ClipAudioSource_setSpeedRatio(ClipAudioSource *c, float speedRatio) {
  Helper::callFunctionOnMessageThread(
      [&]() {
        elThread.setClipSpeedRatio(c, speedRatio);
      }, true);
}

void ClipAudioSource_setPitch(ClipAudioSource *c, float pitchChange) {
  Helper::callFunctionOnMessageThread(
      [&]() {
        elThread.setClipPitch(c, pitchChange);
      }, true);
}

void ClipAudioSource_setGain(ClipAudioSource *c, float db) {
  Helper::callFunctionOnMessageThread(
      [&]() {
        elThread.setClipGain(c, db);
      }, true);
}

void ClipAudioSource_setVolume(ClipAudioSource *c, float vol) {
  Helper::callFunctionOnMessageThread(
      [&]() {
        elThread.setClipVolume(c, vol);
      }, true);
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
  Helper::callFunctionOnMessageThread(
      [&]() {
        elThread.destroyClip(c);
      }, true);
}

int ClipAudioSource_id(ClipAudioSource *c) { return c->id(); }

float ClipAudioSource_adsrAttack(ClipAudioSource *c)
{
  return c->adsrAttack();
}
void ClipAudioSource_setADSRAttack(ClipAudioSource *c, float newValue)
{
  c->setADSRAttack(newValue);
}
float ClipAudioSource_adsrDecay(ClipAudioSource *c)
{
  return c->adsrDecay();
}
void ClipAudioSource_setADSRDecay(ClipAudioSource *c, float newValue)
{
  c->setADSRDecay(newValue);
}
float ClipAudioSource_adsrSustain(ClipAudioSource *c)
{
  return c->adsrSustain();
}
void ClipAudioSource_setADSRSustain(ClipAudioSource *c, float newValue)
{
  c->setADSRSustain(newValue);
}
float ClipAudioSource_adsrRelease(ClipAudioSource *c)
{
  return c->adsrRelease();
}
void ClipAudioSource_setADSRRelease(ClipAudioSource *c, float newValue)
{
  c->setADSRRelease(newValue);
}

//////////////
/// END ClipAudioSource API Bridge
//////////////

//////////////
/// SynTimer API Bridge
//////////////
QObject *SyncTimer_instance() { return syncTimer; }

void SyncTimer_startTimer(int interval) { syncTimer->start(interval); }

void SyncTimer_setBpm(uint bpm) { syncTimer->setBpm(bpm); }

int SyncTimer_getMultiplier() { return syncTimer->getMultiplier(); }

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
  cerr << "libzl : Queue Clip " << clip << " to start on channel " << midiChannel << std::endl;
  Helper::callFunctionOnMessageThread(
      [&]() { syncTimer->queueClipToStartOnChannel(clip, midiChannel); }, true);
}

void SyncTimer_queueClipToStop(ClipAudioSource *clip) {
  Helper::callFunctionOnMessageThread(
      [&]() { syncTimer->queueClipToStop(clip); }, true);
}

void SyncTimer_queueClipToStopOnChannel(ClipAudioSource *clip, int midiChannel) {
  cerr << "libzl : Queue Clip " << clip << " to stop on channel " << midiChannel << std::endl;
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
    qDebug() << "Instantiating tracktion engine";
    tracktionEngine = new te::Engine("libzl", nullptr, std::make_unique<ZLEngineBehavior>());
    qDebug() << "Setting device type to JACK";
    tracktionEngine->getDeviceManager().deviceManager.setCurrentAudioDeviceType("JACK", true);
    qDebug() << "Initialising device manager";
    tracktionEngine->getDeviceManager().initialise(0, 2);
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

  qDebug() << "Initialising SyncTimer";
  syncTimer = SyncTimer::instance();

  qDebug() << "Initialising MidiRouter";
  MidiRouter::instance();

  QObject::connect(MidiRouter::instance(), &MidiRouter::addedHardwareInputDevice, syncTimer, &SyncTimer::addedHardwareInputDevice);
  QObject::connect(MidiRouter::instance(), &MidiRouter::removedHardwareInputDevice, syncTimer, &SyncTimer::removedHardwareInputDevice);
  QObject::connect(MidiRouter::instance(), &MidiRouter::addedHardwareOutputDevice, syncTimer, &SyncTimer::addedHardwareOutputDevice);
  QObject::connect(MidiRouter::instance(), &MidiRouter::removedHardwareOutputDevice, syncTimer, &SyncTimer::removedHardwareOutputDevice);

  qDebug() << "Initialising SamplerSynth";
  SamplerSynth::instance()->initialize(tracktionEngine);

  // Make sure to have the AudioLevels instantiated by explicitly calling instance
  AudioLevels::instance();

  qmlRegisterSingletonType<AudioLevels>("libzl", 1, 0, "AudioLevels", [](QQmlEngine */*engine*/, QJSEngine *scriptEngine) -> QObject * {
    Q_UNUSED(scriptEngine)

    return AudioLevels::instance();
  });
}

void shutdownJuce() {
  elThread.stopThread(500);
  initializer = nullptr;
}

void reloadZynthianConfiguration() {
  MidiRouter::instance()->reloadConfiguration();
}

void registerGraphicTypes() {
  qmlRegisterType<WaveFormItem>("JuceGraphics", 1, 0, "WaveFormItem");
}

void stopClips(int size, ClipAudioSource **clips) {
  elThread.stopClips(size, clips);
}

float dBFromVolume(float vol) { return te::volumeFaderPositionToDB(vol); }

bool AudioLevels_isRecording() {
  return AudioLevels::instance()->isRecording();
}

void AudioLevels_setRecordGlobalPlayback(bool shouldRecord) {
  AudioLevels::instance()->setRecordGlobalPlayback(shouldRecord);
}

void AudioLevels_setGlobalPlaybackFilenamePrefix(const char *fileNamePrefix) {
  AudioLevels::instance()->setGlobalPlaybackFilenamePrefix(QString::fromUtf8(fileNamePrefix));
}

void AudioLevels_startRecording() {
  AudioLevels::instance()->startRecording();
}

void AudioLevels_stopRecording() {
  AudioLevels::instance()->stopRecording();
}

void AudioLevels_setRecordPortsFilenamePrefix(const char *fileNamePrefix)
{
  AudioLevels::instance()->setRecordPortsFilenamePrefix(QString::fromUtf8(fileNamePrefix));
}

void AudioLevels_addRecordPort(const char *portName, int channel)
{
  AudioLevels::instance()->addRecordPort(QString::fromUtf8(portName), channel);
}

void AudioLevels_removeRecordPort(const char *portName, int channel)
{
  AudioLevels::instance()->removeRecordPort(QString::fromUtf8(portName), channel);
}

void AudioLevels_clearRecordPorts()
{
  AudioLevels::instance()->clearRecordPorts();
}

void AudioLevels_setShouldRecordPorts(bool shouldRecord)
{
  AudioLevels::instance()->setShouldRecordPorts(shouldRecord);
}

void JackPassthrough_setPanAmount(int channel, float amount)
{
  if (channel == -1) {
    qobject_cast<JackPassthrough*>(MidiRouter::instance()->globalPlaybackClient())->setPanAmount(amount);
  } else if (channel > -1 && channel < 10) {
    qobject_cast<JackPassthrough*>(MidiRouter::instance()->channelPassthroughClients().at(channel))->setPanAmount(amount);
  }
}

float JackPassthrough_getPanAmount(int channel)
{
  float amount{0.0f};
  if (channel == -1) {
    amount = qobject_cast<JackPassthrough*>(MidiRouter::instance()->globalPlaybackClient())->panAmount();
  } else if (channel > -1 && channel < 10) {
    amount = qobject_cast<JackPassthrough*>(MidiRouter::instance()->channelPassthroughClients().at(channel))->panAmount();
  }
  return amount;
}

float JackPassthrough_getWetFx1Amount(int channel)
{
    float amount{0.0f};
    if (channel == -1) {
      amount = qobject_cast<JackPassthrough*>(MidiRouter::instance()->globalPlaybackClient())->wetFx1Amount();
    } else if (channel > -1 && channel < 10) {
      amount = qobject_cast<JackPassthrough*>(MidiRouter::instance()->channelPassthroughClients().at(channel))->wetFx1Amount();
    }
    return amount;
}

void JackPassthrough_setWetFx1Amount(int channel, float amount)
{
    if (channel == -1) {
      qobject_cast<JackPassthrough*>(MidiRouter::instance()->globalPlaybackClient())->setWetFx1Amount(amount);
    } else if (channel > -1 && channel < 10) {
      qobject_cast<JackPassthrough*>(MidiRouter::instance()->channelPassthroughClients().at(channel))->setWetFx1Amount(amount);
    }
}

float JackPassthrough_getWetFx2Amount(int channel)
{
    float amount{0.0f};
    if (channel == -1) {
      amount = qobject_cast<JackPassthrough*>(MidiRouter::instance()->globalPlaybackClient())->wetFx2Amount();
    } else if (channel > -1 && channel < 10) {
      amount = qobject_cast<JackPassthrough*>(MidiRouter::instance()->channelPassthroughClients().at(channel))->wetFx2Amount();
    }
    return amount;
}

void JackPassthrough_setWetFx2Amount(int channel, float amount)
{

    if (channel == -1) {
      qobject_cast<JackPassthrough*>(MidiRouter::instance()->globalPlaybackClient())->setWetFx2Amount(amount);
    } else if (channel > -1 && channel < 10) {
      qobject_cast<JackPassthrough*>(MidiRouter::instance()->channelPassthroughClients().at(channel))->setWetFx2Amount(amount);
    }
}

float JackPassthrough_getDryAmount(int channel)
{
    float amount{0.0f};
    if (channel == -1) {
      amount = qobject_cast<JackPassthrough*>(MidiRouter::instance()->globalPlaybackClient())->dryAmount();
    } else if (channel > -1 && channel < 10) {
      amount = qobject_cast<JackPassthrough*>(MidiRouter::instance()->channelPassthroughClients().at(channel))->dryAmount();
    }
    return amount;
}

void JackPassthrough_setDryAmount(int channel, float amount)
{
    if (channel == -1) {
      qobject_cast<JackPassthrough*>(MidiRouter::instance()->globalPlaybackClient())->setDryAmount(amount);
    } else if (channel > -1 && channel < 10) {
      qobject_cast<JackPassthrough*>(MidiRouter::instance()->channelPassthroughClients().at(channel))->setDryAmount(amount);
    }
}

float JackPassthrough_getMuted(int channel)
{
    bool muted{false};
    if (channel == -1) {
      muted = qobject_cast<JackPassthrough*>(MidiRouter::instance()->globalPlaybackClient())->muted();
    } else if (channel > -1 && channel < 10) {
      muted = qobject_cast<JackPassthrough*>(MidiRouter::instance()->channelPassthroughClients().at(channel))->muted();
    }
    return muted;
}

void JackPassthrough_setMuted(int channel, bool muted)
{
    if (channel == -1) {
      qobject_cast<JackPassthrough*>(MidiRouter::instance()->globalPlaybackClient())->setMuted(muted);
    } else if (channel > -1 && channel < 10) {
      qobject_cast<JackPassthrough*>(MidiRouter::instance()->channelPassthroughClients().at(channel))->setMuted(muted);
    }
}

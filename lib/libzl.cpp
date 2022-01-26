/*
  ==============================================================================

    libzl.cpp
    Created: 9 Aug 2021 6:28:51pm
    Author:  root

  ==============================================================================
*/

#include "libzl.h"

#include <iostream>
#include <jack/jack.h>
#include <QDebug>

#include "ClipAudioSource.h"
#include "Helper.h"
#include "JUCEHeaders.h"
#include "SyncTimer.h"
#include "WaveFormItem.h"

using namespace std;

ScopedJuceInitialiser_GUI *initializer = nullptr;
SyncTimer *syncTimer = new SyncTimer();
jack_client_t* zlJackClient{nullptr};
jack_status_t zlJackStatus{};
jack_port_t* capturePortA{nullptr};
jack_port_t* capturePortB{nullptr};
float db;
void (*recordingAudioLevelCallback)(float leveldB) = nullptr;

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

  void setClipLength(ClipAudioSource *c, int beat, int bpm) {
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
ClipAudioSource *ClipAudioSource_new(const char *filepath, bool muted) {
  ClipAudioSource *sClip;

  Helper::callFunctionOnMessageThread(
      [&]() { sClip = new ClipAudioSource(syncTimer, filepath, muted); }, true);

  return sClip;
}

void ClipAudioSource_play(ClipAudioSource *c, bool loop) {
  elThread.playClip(c, loop);
}

void ClipAudioSource_stop(ClipAudioSource *c) {
  cerr << "libzl : Stop Clip " << c;

  elThread.stopClip(c);
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

void ClipAudioSource_setLength(ClipAudioSource *c, int beat, int bpm) {
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

void ClipAudioSource_destroy(ClipAudioSource *c) { elThread.destroyClip(c); }
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

void SyncTimer_queueClipToStop(ClipAudioSource *clip) {
  Helper::callFunctionOnMessageThread(
      [&]() { syncTimer->queueClipToStop(clip); }, true);
}
//////////////
/// END SyncTimer API Bridge
//////////////

float convertTodbFS(float raw) {
  if (raw <= 0) {
      return -200;
  }

  float fValue = 20 * log10f(raw);
  if (fValue < -200) {
      return -200;
  }

  return fValue;
}

float peakdBFSFromJackOutput(jack_port_t* port, jack_nframes_t nframes) {
  jack_default_audio_sample_t *buf;
  float peak = 0.0f;
  int i;

  buf = (jack_default_audio_sample_t *)jack_port_get_buffer(port, nframes);

  for (i=0; i<nframes; i++) {
      const float sample = fabs(buf[i]) * 0.2;
      if (sample > peak) {
          peak = sample;
      }
  }

  return convertTodbFS(peak);
}

static int _zlJackProcessCb(jack_nframes_t nframes, void* arg) {
  Q_UNUSED(arg)

  float dbLeft = peakdBFSFromJackOutput(capturePortA, nframes);
  float dbRight = peakdBFSFromJackOutput(capturePortB, nframes);

  if (dbLeft <= -200 && dbRight <= -200) {
    db = -200;
  } else {
    db = 10 * log10f(pow(10, dbLeft/10) + pow(10, dbRight/10));
  }

  if (recordingAudioLevelCallback != nullptr) {
    recordingAudioLevelCallback(db);
  }

  return 0;
}

void initJuce() {
  cerr << "### INIT JUCE\n";
  elThread.startThread();

  zlJackClient = jack_client_open(
    "zynthiloops_client",
    JackNullOption,
    &zlJackStatus
  );

  if (zlJackClient) {
    cerr << "Initialized ZL Jack Client zynthiloops_client";

    capturePortA = jack_port_register(
      zlJackClient,
      "capture_port_a",
      JACK_DEFAULT_AUDIO_TYPE,
      JackPortIsInput,
      0
    );
    capturePortB = jack_port_register(
      zlJackClient,
      "capture_port_b",
      JACK_DEFAULT_AUDIO_TYPE,
      JackPortIsInput,
      0
    );

    if (
      jack_set_process_callback(
        zlJackClient,
        _zlJackProcessCb,
        nullptr
      ) != 0
    ) {
      cerr << "Failed to set the ZL Jack Client processing callback" << endl;
    } else {
      if (jack_activate(zlJackClient) == 0) {
        cerr << "Successfully created and set up the ZL Jack client" << endl;
      } else {
        cerr << "Failed to activate ZL Jack client" << endl;
      }
    }
  } else {
    cerr << "Error initializing ZL Jack Client zynthiloops_client" << endl;
  }
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

void setRecordingAudioLevelCallback(void (*functionPtr)(float)) {
  recordingAudioLevelCallback = functionPtr;
}

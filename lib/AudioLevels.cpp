/*
  ==============================================================================

    AudioLevels.cpp
    Created: 8 Feb 2022
    Author:  Anupam Basak <anupam.basak27@gmail.com>

  ==============================================================================
*/

#include "AudioLevels.h"

#include <cmath>
#include <QDebug>

static int audioLevelsJackProcessCb(jack_nframes_t nframes, void* arg) {
    return static_cast<AudioLevels*>(arg)->_audioLevelsJackProcessCb(nframes);
}

AudioLevels::AudioLevels(QObject *parent) : QObject(parent) {
    audioLevelsJackClient = jack_client_open(
      "zynthiloops_audio_levels_client",
      JackNullOption,
      &audioLevelsJackStatus
    );

    if (audioLevelsJackClient) {
      qWarning() << "Initialized Audio Levels Jack Client zynthiloops_client";

      capturePortA = jack_port_register(
        audioLevelsJackClient,
        "capture_port_a",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      capturePortB = jack_port_register(
        audioLevelsJackClient,
        "capture_port_b",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );

      if (
        jack_set_process_callback(
          audioLevelsJackClient,
          audioLevelsJackProcessCb,
          static_cast<void*>(this)
        ) != 0
      ) {
        qWarning() << "Failed to set the Audio Levels Jack Client processing callback" << endl;
      } else {
        if (jack_activate(audioLevelsJackClient) == 0) {
          qWarning() << "Successfully created and set up the Audio Levels Jack client" << endl;

          if (jack_connect(audioLevelsJackClient, "system:capture_1", jack_port_name(capturePortA)) == 0) {
              qDebug() << "Successfully connected audio level jack output to the system capture port A";
          } else {
              qWarning() << "Failed to connect audio level jack output to the system capture port A";
          }

          if (jack_connect(audioLevelsJackClient, "system:capture_2", jack_port_name(capturePortB)) == 0) {
              qDebug() << "Successfully connected audio level jack output to the system capture port B";
          } else {
              qWarning() << "Failed to connect audio level jack output to the system capture port B";
          }
        } else {
          qWarning() << "Failed to activate Audio Levels Jack client" << endl;
        }
      }
    } else {
      qWarning() << "Error initializing Audio Levels Jack Client zynthiloops_client" << endl;
    }

    startTimerHz(30);
}

inline float AudioLevels::convertTodbFS(float raw) {
    if (raw <= 0) {
        return -200;
    }

    float fValue = 20 * log10f(raw);
    if (fValue < -200) {
        return -200;
    }

    return fValue;
}

void AudioLevels::timerCallback() {
    Q_EMIT audioLevelsChanged();
}

int AudioLevels::_audioLevelsJackProcessCb(jack_nframes_t nframes) {
    float peakA{0.0f}, peakB{0.0f};
    float dbA, dbB;
    jack_default_audio_sample_t *bufA = (jack_default_audio_sample_t *)jack_port_get_buffer(capturePortA, nframes);
    jack_default_audio_sample_t *bufB = (jack_default_audio_sample_t *)jack_port_get_buffer(capturePortB, nframes);

    for (jack_nframes_t i=0; i<nframes; i++) {
        const float sampleA = fabs(bufA[i]) * 0.2;
        const float sampleB = fabs(bufB[i]) * 0.2;

        if (sampleA > peakA) {
            peakA = sampleA;
        }
        if (sampleB > peakB) {
            peakB = sampleB;
        }
    }

    dbA = convertTodbFS(peakA);
    dbB = convertTodbFS(peakB);

    capture1 = dbA <= -200 ? -200 : dbA;
    capture2 = dbB <= -200 ? -200 : dbB;

    return 0;
}

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
#include <QString>
#include <QVariantList>

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

      playbackPortA = jack_port_register(
        audioLevelsJackClient,
        "playback_port_a",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      playbackPortB = jack_port_register(
        audioLevelsJackClient,
        "playback_port_b",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );

      // TRACKS PORT A/B REGISTRATION
      for (int i=0; i<TRACKS_COUNT; i++) {
          tracksPortA[i] = jack_port_register(
            audioLevelsJackClient,
            QString("T%1A").arg(i+1).toStdString().c_str(),
            JACK_DEFAULT_AUDIO_TYPE,
            JackPortIsInput,
            0
          );
          tracksPortB[i] = jack_port_register(
            audioLevelsJackClient,
            QString("T%1B").arg(i+1).toStdString().c_str(),
            JACK_DEFAULT_AUDIO_TYPE,
            JackPortIsInput,
            0
          );
      }

      if (
        jack_set_process_callback(
          audioLevelsJackClient,
          audioLevelsJackProcessCb,
          static_cast<void*>(this)
        ) != 0
      ) {
        qWarning() << "Failed to set the Audio Levels Jack Client processing callback";
      } else {
        if (jack_activate(audioLevelsJackClient) == 0) {
          qWarning() << "Successfully created and set up the Audio Levels Jack client";

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

          if (jack_connect(audioLevelsJackClient, "system:playback_1", jack_port_name(playbackPortA)) == 0) {
              qDebug() << "Successfully connected audio level jack output to the system playback port A";
          } else {
              qWarning() << "Failed to connect audio level jack output to the system playback port A";
          }

          if (jack_connect(audioLevelsJackClient, "system:playback_2", jack_port_name(playbackPortB)) == 0) {
              qDebug() << "Successfully connected audio level jack output to the system playback port B";
          } else {
              qWarning() << "Failed to connect audio level jack output to the system playback port B";
          }
        } else {
          qWarning() << "Failed to activate Audio Levels Jack client";
        }
      }
    } else {
      qWarning() << "Error initializing Audio Levels Jack Client zynthiloops_client";
    }

    startTimerHz(30);
}

inline float AudioLevels::convertTodbFS(float raw) {
    if (raw <= 0) {
        return -200;
    }

    const float fValue = 20 * log10f(raw);
    if (fValue < -200) {
        return -200;
    }

    return fValue;
}

void AudioLevels::timerCallback() {
    Q_EMIT audioLevelsChanged();
}

int AudioLevels::_audioLevelsJackProcessCb(jack_nframes_t nframes) {
    capturePeakA = 0.0f;
    capturePeakB = 0.0f;
    playbackPeakA = 0.0f;
    playbackPeakB = 0.0f;

    jack_default_audio_sample_t *captureBufA{(jack_default_audio_sample_t *)jack_port_get_buffer(capturePortA, nframes)},
                                *captureBufB{(jack_default_audio_sample_t *)jack_port_get_buffer(capturePortB, nframes)},
                                *playbackBufA{(jack_default_audio_sample_t *)jack_port_get_buffer(playbackPortA, nframes)},
                                *playbackBufB{(jack_default_audio_sample_t *)jack_port_get_buffer(playbackPortB, nframes)},
                                *tracksBufA[TRACKS_COUNT] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
                                *tracksBufB[TRACKS_COUNT] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

    for (int i=0; i<TRACKS_COUNT; i++) {
        tracksPeakA[i] = 0.0f;
        tracksPeakB[i] = 0.0f;
        tracksBufA[i] = (jack_default_audio_sample_t *)jack_port_get_buffer(tracksPortA[i], nframes);
        tracksBufB[i] = (jack_default_audio_sample_t *)jack_port_get_buffer(tracksPortB[i], nframes);
    }

    for (jack_nframes_t i=0; i<nframes; i++) {
        const float captureSampleA = fabs(captureBufA[i]) * 0.2,
                    captureSampleB = fabs(captureBufB[i]) * 0.2,
                    playbackSampleA = fabs(playbackBufA[i]) * 0.2,
                    playbackSampleB = fabs(playbackBufB[i]) * 0.2;

        if (captureSampleA > capturePeakA) {
            capturePeakA = captureSampleA;
        }
        if (captureSampleB > capturePeakB) {
            capturePeakB = captureSampleB;
        }

        if (playbackSampleA > playbackPeakA) {
            playbackPeakA = playbackSampleA;
        }
        if (playbackSampleB > playbackPeakB) {
            playbackPeakB = playbackSampleB;
        }

        for (int j=0; j<TRACKS_COUNT; j++) {
            const float tracksSampleA = fabs(tracksBufA[j][i]) * 0.2,
                        tracksSampleB = fabs(tracksBufB[j][i]) * 0.2;

            if (tracksSampleA > tracksPeakA[j]) {
                tracksPeakA[j] = tracksSampleA;
            }
            if (tracksSampleB > tracksPeakB[j]) {
                tracksPeakB[j] = tracksSampleB;
            }
        }
    }

    const float captureDbA{convertTodbFS(capturePeakA)},
                captureDbB{convertTodbFS(capturePeakB)},
                playbackDbA{convertTodbFS(playbackPeakA)},
                playbackDbB{convertTodbFS(playbackPeakB)};

    captureA = captureDbA <= -200 ? -200 : captureDbA;
    captureB = captureDbB <= -200 ? -200 : captureDbB;
    playbackA = playbackDbA <= -200 ? -200 : playbackDbA;
    playbackB = playbackDbB <= -200 ? -200 : playbackDbB;

    for (int i=0; i<TRACKS_COUNT; i++) {
        const float dbA = convertTodbFS(tracksPeakA[i]),
                    dbB = convertTodbFS(tracksPeakB[i]);

        tracksA[i] = dbA <= -200 ? -200 : dbA;
        tracksB[i] = dbB <= -200 ? -200 : dbB;
    }

    return 0;
}

float AudioLevels::add(float db1, float db2) {
    return 10 * log10f(pow(10, db1/10) + pow(10, db2/10));
}

const QVariantList AudioLevels::getTracksAudioLevels() {
    QVariantList levels;

    for (int i=0; i<TRACKS_COUNT; i++) {
        levels << add(tracksA[i], tracksB[i]);
    }

    return levels;
}

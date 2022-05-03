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

      synthPortA = jack_port_register(
        audioLevelsJackClient,
        "synth_port_a",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      synthPortB = jack_port_register(
        audioLevelsJackClient,
        "synth_port_b",
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

      // TRACK PORTS
      T1Port = jack_port_register(
        audioLevelsJackClient,
        "T1",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      T2Port = jack_port_register(
        audioLevelsJackClient,
        "T2",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      T3Port = jack_port_register(
        audioLevelsJackClient,
        "T3",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      T4Port = jack_port_register(
        audioLevelsJackClient,
        "T4",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      T5Port = jack_port_register(
        audioLevelsJackClient,
        "T5",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      T6Port = jack_port_register(
        audioLevelsJackClient,
        "T6",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      T7Port = jack_port_register(
        audioLevelsJackClient,
        "T7",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      T8Port = jack_port_register(
        audioLevelsJackClient,
        "T8",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      T9Port = jack_port_register(
        audioLevelsJackClient,
        "T9",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      T10Port = jack_port_register(
        audioLevelsJackClient,
        "T10",
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
    synthPeakA = 0.0f;
    synthPeakB = 0.0f;
    playbackPeakA = 0.0f;
    playbackPeakB = 0.0f;
    T1Peak = 0.0f;
    T2Peak = 0.0f;
    T3Peak = 0.0f;
    T4Peak = 0.0f;
    T5Peak = 0.0f;
    T6Peak = 0.0f;
    T7Peak = 0.0f;
    T8Peak = 0.0f;
    T9Peak = 0.0f;
    T10Peak = 0.0f;

    jack_default_audio_sample_t *captureBufA{(jack_default_audio_sample_t *)jack_port_get_buffer(capturePortA, nframes)},
                                *captureBufB{(jack_default_audio_sample_t *)jack_port_get_buffer(capturePortB, nframes)},
                                *synthBufA{(jack_default_audio_sample_t *)jack_port_get_buffer(synthPortA, nframes)},
                                *synthBufB{(jack_default_audio_sample_t *)jack_port_get_buffer(synthPortB, nframes)},
                                *playbackBufA{(jack_default_audio_sample_t *)jack_port_get_buffer(playbackPortA, nframes)},
                                *playbackBufB{(jack_default_audio_sample_t *)jack_port_get_buffer(playbackPortB, nframes)},
                                *T1Buf{(jack_default_audio_sample_t *)jack_port_get_buffer(T1Port, nframes)},
                                *T2Buf{(jack_default_audio_sample_t *)jack_port_get_buffer(T2Port, nframes)},
                                *T3Buf{(jack_default_audio_sample_t *)jack_port_get_buffer(T3Port, nframes)},
                                *T4Buf{(jack_default_audio_sample_t *)jack_port_get_buffer(T4Port, nframes)},
                                *T5Buf{(jack_default_audio_sample_t *)jack_port_get_buffer(T5Port, nframes)},
                                *T6Buf{(jack_default_audio_sample_t *)jack_port_get_buffer(T6Port, nframes)},
                                *T7Buf{(jack_default_audio_sample_t *)jack_port_get_buffer(T7Port, nframes)},
                                *T8Buf{(jack_default_audio_sample_t *)jack_port_get_buffer(T8Port, nframes)},
                                *T9Buf{(jack_default_audio_sample_t *)jack_port_get_buffer(T9Port, nframes)},
                                *T10Buf{(jack_default_audio_sample_t *)jack_port_get_buffer(T10Port, nframes)};

    for (jack_nframes_t i=0; i<nframes; i++) {
        const float captureSampleA = fabs(captureBufA[i]) * 0.2,
                    captureSampleB = fabs(captureBufB[i]) * 0.2,
                    synthSampleA = fabs(synthBufA[i]) * 0.2,
                    synthSampleB = fabs(synthBufB[i]) * 0.2,
                    playbackSampleA = fabs(playbackBufA[i]) * 0.2,
                    playbackSampleB = fabs(playbackBufB[i]) * 0.2,
                    T1Sample = fabs(T1Buf[i]) * 0.2,
                    T2Sample = fabs(T2Buf[i]) * 0.2,
                    T3Sample = fabs(T3Buf[i]) * 0.2,
                    T4Sample = fabs(T4Buf[i]) * 0.2,
                    T5Sample = fabs(T5Buf[i]) * 0.2,
                    T6Sample = fabs(T6Buf[i]) * 0.2,
                    T7Sample = fabs(T7Buf[i]) * 0.2,
                    T8Sample = fabs(T8Buf[i]) * 0.2,
                    T9Sample = fabs(T9Buf[i]) * 0.2,
                    T10Sample = fabs(T10Buf[i]) * 0.2;

        if (captureSampleA > capturePeakA) {
            capturePeakA = captureSampleA;
        }
        if (captureSampleB > capturePeakB) {
            capturePeakB = captureSampleB;
        }

        if (synthSampleA > synthPeakA) {
            synthPeakA = synthSampleA;
        }
        if (synthSampleB > synthPeakB) {
            synthPeakB = synthSampleB;
        }

        if (playbackSampleA > playbackPeakA) {
            playbackPeakA = playbackSampleA;
        }
        if (playbackSampleB > playbackPeakB) {
            playbackPeakB = playbackSampleB;
        }

        if (T1Sample > T1Peak) {
            T1Peak = T1Sample;
        }
        if (T2Sample > T2Peak) {
            T2Peak = T2Sample;
        }
        if (T3Sample > T3Peak) {
            T3Peak = T3Sample;
        }
        if (T4Sample > T4Peak) {
            T4Peak = T4Sample;
        }
        if (T5Sample > T5Peak) {
            T5Peak = T5Sample;
        }
        if (T6Sample > T6Peak) {
            T6Peak = T6Sample;
        }
        if (T7Sample > T7Peak) {
            T7Peak = T7Sample;
        }
        if (T8Sample > T8Peak) {
            T8Peak = T8Sample;
        }
        if (T9Sample > T9Peak) {
            T9Peak = T9Sample;
        }
        if (T10Sample > T10Peak) {
            T10Peak = T10Sample;
        }
    }

    const float captureDbA{convertTodbFS(capturePeakA)},
                captureDbB{convertTodbFS(capturePeakB)},
                synthDbA{convertTodbFS(synthPeakA)},
                synthDbB{convertTodbFS(synthPeakB)},
                playbackDbA{convertTodbFS(playbackPeakA)},
                playbackDbB{convertTodbFS(playbackPeakB)},
                T1Db{convertTodbFS(T1Peak)},
                T2Db{convertTodbFS(T2Peak)},
                T3Db{convertTodbFS(T3Peak)},
                T4Db{convertTodbFS(T4Peak)},
                T5Db{convertTodbFS(T5Peak)},
                T6Db{convertTodbFS(T6Peak)},
                T7Db{convertTodbFS(T7Peak)},
                T8Db{convertTodbFS(T8Peak)},
                T9Db{convertTodbFS(T9Peak)},
                T10Db{convertTodbFS(T10Peak)};

    captureA = captureDbA <= -200 ? -200 : captureDbA;
    captureB = captureDbB <= -200 ? -200 : captureDbB;
    synthA = synthDbA <= -200 ? -200 : synthDbA;
    synthB = synthDbB <= -200 ? -200 : synthDbB;
    playbackA = playbackDbA <= -200 ? -200 : playbackDbA;
    playbackB = playbackDbB <= -200 ? -200 : playbackDbB;

    T1 = T1Db <= -200 ? -200 : T1Db;
    T2 = T2Db <= -200 ? -200 : T2Db;
    T3 = T3Db <= -200 ? -200 : T3Db;
    T4 = T4Db <= -200 ? -200 : T4Db;
    T5 = T5Db <= -200 ? -200 : T5Db;
    T6 = T6Db <= -200 ? -200 : T6Db;
    T7 = T7Db <= -200 ? -200 : T7Db;
    T8 = T8Db <= -200 ? -200 : T8Db;
    T9 = T9Db <= -200 ? -200 : T9Db;
    T10 = T10Db <= -200 ? -200 : T10Db;

    return 0;
}

float AudioLevels::add(float db1, float db2) {
    return 10 * log10f(pow(10, db1/10) + pow(10, db2/10));
}

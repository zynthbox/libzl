/*
  ==============================================================================

    AudioLevels.cpp
    Created: 8 Feb 2022
    Author:  Anupam Basak <anupam.basak27@gmail.com>

  ==============================================================================
*/

#pragma once

#include <QObject>
#include <QTimer>
#include <QStringList>
#include <jack/jack.h>
#include <juce_events/juce_events.h>

class AudioLevels : public QObject,
                    public juce::Timer {
Q_OBJECT
    Q_PROPERTY(float captureA MEMBER captureA NOTIFY audioLevelsChanged)
    Q_PROPERTY(float captureB MEMBER captureB NOTIFY audioLevelsChanged)
    Q_PROPERTY(float synthA MEMBER synthA NOTIFY audioLevelsChanged)
    Q_PROPERTY(float synthB MEMBER synthB NOTIFY audioLevelsChanged)
    Q_PROPERTY(float playbackA MEMBER playbackA NOTIFY audioLevelsChanged)
    Q_PROPERTY(float playbackB MEMBER playbackB NOTIFY audioLevelsChanged)

public:
    AudioLevels(QObject *parent = nullptr);
    int _audioLevelsJackProcessCb(jack_nframes_t nframes);

public slots:
    float add(float db1, float db2);

Q_SIGNALS:
    void audioLevelsChanged();

private:
    float convertTodbFS(float raw);
    float peakdBFSFromJackOutput(jack_port_t* port, jack_nframes_t nframes);

    jack_client_t* audioLevelsJackClient{nullptr};
    jack_status_t audioLevelsJackStatus{};
    jack_port_t* capturePortA{nullptr};
    jack_port_t* capturePortB{nullptr};
    jack_port_t* synthPortA{nullptr};
    jack_port_t* synthPortB{nullptr};
    jack_port_t* playbackPortA{nullptr};
    jack_port_t* playbackPortB{nullptr};

    float capturePeakA{0.0f},
          capturePeakB{0.0f},
          synthPeakA{0.0f},
          synthPeakB{0.0f},
          playbackPeakA{0.0f},
          playbackPeakB{0.0f};

    float captureA{-200.0f}, captureB{-200.0f};
    float synthA{-200.0f}, synthB{-200.0f};
    float playbackA{-200.0f}, playbackB{-200.0f};

    void timerCallback() override;
};

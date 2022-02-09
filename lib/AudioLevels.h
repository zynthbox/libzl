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
    Q_PROPERTY(float capture1 MEMBER capture1 NOTIFY audioLevelsChanged)
    Q_PROPERTY(float capture2 MEMBER capture2 NOTIFY audioLevelsChanged)

public:
    AudioLevels(QObject *parent = nullptr);
    int _audioLevelsJackProcessCb(jack_nframes_t nframes);

Q_SIGNALS:
    void audioLevelsChanged();

private:
    float convertTodbFS(float raw);
    float peakdBFSFromJackOutput(jack_port_t* port, jack_nframes_t nframes);

    jack_client_t* audioLevelsJackClient{nullptr};
    jack_status_t audioLevelsJackStatus{};
    jack_port_t* capturePortA{nullptr};
    jack_port_t* capturePortB{nullptr};

    float capture1{};
    float capture2{};

    void timerCallback() override;
};

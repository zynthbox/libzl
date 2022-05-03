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

/**
 * @brief The AudioLevels class provides a way to read audio levels of different ports
 *
 * This class exposes some Q_PROPERTY which reports respective audio levels in decibel
 * It also provides a helper method to add multiple decibel values
 *
 * To use this class in qml import libzl and use read the properties as follows :
 * <code>
 * import libzl 1.0 as ZL
 * </code>
 *
 * <code>
 * console.log(ZL.AudioLevels.synthA)
 * </code>
 */
class AudioLevels : public QObject,
                    public juce::Timer {
Q_OBJECT
    /**
     * \brief Left Capture channel audio level in decibels
     */
    Q_PROPERTY(float captureA MEMBER captureA NOTIFY audioLevelsChanged)

    /**
     * \brief Right Capture channel audio level in decibels
     */
    Q_PROPERTY(float captureB MEMBER captureB NOTIFY audioLevelsChanged)

    /**
     * \brief Left Synth playback channel audio level in decibels
     */
    Q_PROPERTY(float synthA MEMBER synthA NOTIFY audioLevelsChanged)

    /**
     * \brief Right Synth playback channel audio level in decibels
     */
    Q_PROPERTY(float synthB MEMBER synthB NOTIFY audioLevelsChanged)

    /**
     * \brief Left system playback channel audio level in decibels
     */
    Q_PROPERTY(float playbackA MEMBER playbackA NOTIFY audioLevelsChanged)

    /**
     * \brief Right system playback channel audio level in decibels
     */
    Q_PROPERTY(float playbackB MEMBER playbackB NOTIFY audioLevelsChanged)

    /**
     * \brief T1 audio level in decibels
     */
    Q_PROPERTY(float T1 MEMBER T1 NOTIFY audioLevelsChanged)
    /**
     * \brief T2 audio level in decibels
     */
    Q_PROPERTY(float T2 MEMBER T2 NOTIFY audioLevelsChanged)
    /**
     * \brief T3 audio level in decibels
     */
    Q_PROPERTY(float T3 MEMBER T3 NOTIFY audioLevelsChanged)
    /**
     * \brief T4 audio level in decibels
     */
    Q_PROPERTY(float T4 MEMBER T4 NOTIFY audioLevelsChanged)
    /**
     * \brief T5 audio level in decibels
     */
    Q_PROPERTY(float T5 MEMBER T5 NOTIFY audioLevelsChanged)
    /**
     * \brief T6 audio level in decibels
     */
    Q_PROPERTY(float T6 MEMBER T6 NOTIFY audioLevelsChanged)
    /**
     * \brief T7 audio level in decibels
     */
    Q_PROPERTY(float T7 MEMBER T7 NOTIFY audioLevelsChanged)
    /**
     * \brief T8 audio level in decibels
     */
    Q_PROPERTY(float T8 MEMBER T8 NOTIFY audioLevelsChanged)
    /**
     * \brief T audio level in decibels
     */
    Q_PROPERTY(float T9 MEMBER T9 NOTIFY audioLevelsChanged)
    /**
     * \brief T10 audio level in decibels
     */
    Q_PROPERTY(float T10 MEMBER T10 NOTIFY audioLevelsChanged)

public:
    AudioLevels(QObject *parent = nullptr);
    int _audioLevelsJackProcessCb(jack_nframes_t nframes);

public slots:
    /**
     * \brief Add two decibel values
     * @param db1 Audio level in decibels
     * @param db2 Audio level in decibels
     * @return db1+db2
     */
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
    jack_port_t* T1Port{nullptr};
    jack_port_t* T2Port{nullptr};
    jack_port_t* T3Port{nullptr};
    jack_port_t* T4Port{nullptr};
    jack_port_t* T5Port{nullptr};
    jack_port_t* T6Port{nullptr};
    jack_port_t* T7Port{nullptr};
    jack_port_t* T8Port{nullptr};
    jack_port_t* T9Port{nullptr};
    jack_port_t* T10Port{nullptr};

    float capturePeakA{0.0f},
          capturePeakB{0.0f},
          synthPeakA{0.0f},
          synthPeakB{0.0f},
          playbackPeakA{0.0f},
          playbackPeakB{0.0f},
          T1Peak{0.0f},
          T2Peak{0.0f},
          T3Peak{0.0f},
          T4Peak{0.0f},
          T5Peak{0.0f},
          T6Peak{0.0f},
          T7Peak{0.0f},
          T8Peak{0.0f},
          T9Peak{0.0f},
          T10Peak{0.0f};

    float captureA{-200.0f}, captureB{-200.0f};
    float synthA{-200.0f}, synthB{-200.0f};
    float playbackA{-200.0f}, playbackB{-200.0f};
    float T1{0.0f},
          T2{0.0f},
          T3{0.0f},
          T4{0.0f},
          T5{0.0f},
          T6{0.0f},
          T7{0.0f},
          T8{0.0f},
          T9{0.0f},
          T10{0.0f};

    void timerCallback() override;
};

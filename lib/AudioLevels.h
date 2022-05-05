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

#define TRACKS_COUNT 10

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
     * \brief Tracks audio level in decibels as an array of 10 elements
     */
    Q_PROPERTY(QVariantList tracks READ getTracksAudioLevels NOTIFY audioLevelsChanged)

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
    const QVariantList getTracksAudioLevels();

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
    jack_port_t* tracksPortA[10] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    jack_port_t* tracksPortB[10] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

    float capturePeakA{0.0f},
          capturePeakB{0.0f},
          synthPeakA{0.0f},
          synthPeakB{0.0f},
          playbackPeakA{0.0f},
          playbackPeakB{0.0f},
          tracksPeakA[TRACKS_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
          tracksPeakB[TRACKS_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    float captureA{-200.0f}, captureB{-200.0f};
    float synthA{-200.0f}, synthB{-200.0f};
    float playbackA{-200.0f}, playbackB{-200.0f};
    float tracksA[TRACKS_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
          tracksB[TRACKS_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    void timerCallback() override;
};

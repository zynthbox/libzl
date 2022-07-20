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

class AudioLevelsPrivate;
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

    /**
     * \brief Set whether or not to record the global playback
     * @param shouldRecord Whether or not to include the global playback when calling startRecording
     */
    Q_PROPERTY(bool recordGlobalPlayback READ recordGlobalPlayback WRITE setRecordGlobalPlayback NOTIFY recordGlobalPlaybackChanged)
    /**
     * \brief A list of the track indices of the tracks marked to be included when recording
     */
    Q_PROPERTY(QVariantList tracksToRecord READ tracksToRecord NOTIFY tracksToRecordChanged)

public:
    AudioLevels(QObject *parent = nullptr);
    int _audioLevelsJackProcessCb(jack_nframes_t nframes);

public Q_SLOTS:
    /**
     * \brief Add two decibel values
     * @param db1 Audio level in decibels
     * @param db2 Audio level in decibels
     * @return db1+db2
     */
    float add(float db1, float db2);

    void setRecordGlobalPlayback(bool shouldRecord = true);
    bool recordGlobalPlayback() const;
    /**
     * \brief Set the first part of the filename used when recording the global playback
     * This should be the full first part of the filename, path and all. The recorder will then append
     * a timestamp and the file suffix (.wav). You should also ensure that the path exists before calling
     * @param fileNamePrefix The prefix you wish to use as the basis for the global playback recording's filenames
     */
    void setGlobalPlaybackFilenamePrefix(const QString& fileNamePrefix);

    /**
     * \brief Sets whether or not a track should be included when recording
     * @param track The index of the track you wish to change the recording status of
     * @param shouldRecord Whether or not the track should be recorded
     */
    void setTrackToRecord(int track, bool shouldRecord = true);
    /**
     * \brief Returns a list of track indices for tracks marked to be recorded
     * @see setTrackToRecord(int, bool)
     */
    QVariantList tracksToRecord() const;
    /**
     * \brief Set the first part of the filename used when recording
     * This should be the full first part of the filename, path and all. The recorder will then append
     * a timestamp and the file suffix (.wav). You should also ensure that the path exists before calling
     * startRecording.
     * @param track The index of the track you wish to change the filename prefix for
     * @param fileNamePrefix The prefix you wish to use as the basis of the given track's filenames
     */
    void setTrackFilenamePrefix(int track, const QString& fileNamePrefix);

    /**
     * \brief Start the recording process on all enabled tracks
     *
     * The logical progression of doing semi-automated multi-tracked recording is:
     * - Mark all the tracks that need including for recording and those that shouldn't be (setTrackToRecord and setRecordGlobalPlayback)
     * - Set the filename prefixes for all the tracks that will be included (you can also set the others, it has no negative side effects)
     * - Start the recording
     * - Start playback after the recording, to ensure everything is included
     * - Stop recording when needed
     * - Stop playback
     */
    void startRecording();
    /**
     * \brief Stop any ongoing recordings
     */
    void stopRecording();
Q_SIGNALS:
    void audioLevelsChanged();
    void recordGlobalPlaybackChanged();
    void tracksToRecordChanged();

private:
    const QVariantList getTracksAudioLevels();

    float convertTodbFS(float raw);

    jack_client_t* audioLevelsJackClient{nullptr};
    jack_status_t audioLevelsJackStatus{};
    jack_port_t* capturePortA{nullptr};
    jack_port_t* capturePortB{nullptr};
    jack_port_t* playbackPortA{nullptr};
    jack_port_t* playbackPortB{nullptr};
    jack_port_t* tracksPortA[10] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    jack_port_t* tracksPortB[10] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

    float capturePeakA{0.0f},
          capturePeakB{0.0f},
          playbackPeakA{0.0f},
          playbackPeakB{0.0f},
          tracksPeakA[TRACKS_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
          tracksPeakB[TRACKS_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    float captureA{-200.0f}, captureB{-200.0f};
    float playbackA{-200.0f}, playbackB{-200.0f};
    float tracksA[TRACKS_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
          tracksB[TRACKS_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    void timerCallback() override;
    AudioLevelsPrivate *d{nullptr};
};

/*
  ==============================================================================

    ClipAudioSource.h
    Created: 9 Aug 2021 6:25:01pm
    Author:  Anupam Basak <anupam.basak27@gmail.com>

  ==============================================================================
*/

#pragma once

#include <QObject>

#include <iostream>

#include <juce_events/juce_events.h>

class SyncTimer;
class ClipAudioSourcePositionsModel;
namespace tracktion_engine {
    class AudioFile;
    class Engine;
}
using namespace std;

//==============================================================================
class ClipAudioSource : public QObject {
    Q_OBJECT
    Q_PROPERTY(int id READ id WRITE setId NOTIFY idChanged)
    /**
     * \brief The current audio level in dB as a float (might be anywhere from -200 to 30, but can exist above that level as well)
     */
    Q_PROPERTY(float audioLevel READ audioLevel NOTIFY audioLevelChanged)
    /**
     * \brief The current playback position (of the first position in the positions model) in seconds
     */
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    /**
     * \brief A model which contains the current positions at which the clip is being played back in SamplerSynth
     */
    Q_PROPERTY(QObject* playbackPositions READ playbackPositions CONSTANT)
    /**
     * \brief How many slices should the Clip have
     * Setting this to a lower number than the current will remove entries from the positions list
     * and you will not be able to restore that value by returning it to the previous, larger value.
     * The default value is 16, meaning you have slices with the index 0 through 15
     * @default 16
     */
    Q_PROPERTY(int slices READ slices WRITE setSlices NOTIFY slicesChanged)
    /**
     * \brief The starting positions of each slice in Clip
     * Logically, all slices end at the start position of the next slice, and the last ends at the end of the clip
     * The values are double values, from 0 through 1 (usually there will not be a 1, as the last slice would then have length 0)
     */
    Q_PROPERTY(QVariantList slicePositions READ slicePositions WRITE setSlicePositions NOTIFY slicePositionsChanged)
    /**
     * \brief The midi note used to calculate the rotating positions of slices (this midi note will be slice 0)
     * @default 60
     */
    Q_PROPERTY(int sliceBaseMidiNote READ sliceBaseMidiNote WRITE setSliceBaseMidiNote NOTIFY sliceBaseMidiNoteChanged)
    /**
     * \brief The first midi note this clip should be used for (by default, a clip's keyzone is all midi notes)
     * @default 0
     */
    Q_PROPERTY(int keyZoneStart READ keyZoneStart WRITE setKeyZoneStart NOTIFY keyZoneStartChanged)
    /**
     * \brief The last midi note this clip should be used for (by default, a clip's keyzone is all midi notes)
     * @default 127
     */
    Q_PROPERTY(int keyZoneEnd READ keyZoneEnd WRITE setKeyZoneEnd NOTIFY keyZoneEndChanged)
    /**
     * \brief The midi note this clip plays at un-pitched when used by the sampler synth (only used for trig mode, not slice)
     * @default 60
     */
    Q_PROPERTY(int rootNote READ rootNote WRITE setRootNote NOTIFY rootNoteChanged)
public:
  explicit ClipAudioSource(tracktion_engine::Engine *engine, SyncTimer *syncTimer, const char *filepath,
                  bool muted = false, QObject *parent = nullptr);
  ~ClipAudioSource() override;

  void setProgressCallback(void (*functionPtr)(float));
  void syncProgress();
  void setStartPosition(float startPositionInSeconds);
  float getStartPosition(int slice = -1) const;
  float getStopPosition(int slice = -1) const;
  void setLooping(bool looping);
  bool getLooping() const;
  void setLength(float beat, int bpm);
  void setPitch(float pitchChange, bool immediate = false);
  void setSpeedRatio(float speedRatio, bool immediate = false);
  void setGain(float db);
  void setVolume(float vol);
  /**
   * \brief Set the volume by "slider position" (0.0 through 1.0)
   * @param vol The volume you wish to set, using tracktion's slider position notation (0.0 through 1.0)
   */
  void setVolumeAbsolute(float vol);
  void setAudioLevelChangedCallback(void (*functionPtr)(float));
  void play(bool loop = true);
  void stop();
  float getDuration();
  const char *getFileName() const;
  const char *getFilePath() const;
  void updateTempoAndPitch();

  tracktion_engine::AudioFile getPlaybackFile() const;
  Q_SIGNAL void playbackFileChanged();

  int id() const;
  void setId(int id);
  Q_SIGNAL void idChanged();

  float audioLevel() const;
  Q_SIGNAL void audioLevelChanged();

  double position() const;
  Q_SIGNAL void positionChanged();

  QObject *playbackPositions();
  ClipAudioSourcePositionsModel *playbackPositionsModel();

  int slices() const;
  void setSlices(int slices);
  Q_SIGNAL void slicesChanged();

  QVariantList slicePositions() const;
  void setSlicePositions(const QVariantList &slicePositions);
  /**
   * \brief Get the position of a specific slice
   * @param slice The slice you wish to get the position of (slices are 0-indexed)
   * @return A double precision value between 0 and 1 (for an invalid slice, 0 will be returned)
   */
  double slicePosition(int slice) const;
  /**
   * \brief Set the position of a specific slice
   * @param slice The slice you wish to change the position of (slices are 0-indexed)
   * @param position The new position (from 0 through 1). The value will be clamped to fit inside the area available for this slice (that is, between the positions of slice - 1 and slice + 1)
   */
  void setSlicePosition(int slice, float position);
  Q_SIGNAL void slicePositionsChanged();

  int sliceBaseMidiNote() const;
  void setSliceBaseMidiNote(int sliceBaseMidiNote);
  Q_SIGNAL void sliceBaseMidiNoteChanged();
  /**
   * \brief Get the appropriate slice for the given midi note, based on the current slice base midi note
   * @param midiNote The midi note you wish to get a slice index for
   * @return The slice index matching the given midi note
   */
  int sliceForMidiNote(int midiNote) const;

  int keyZoneStart() const;
  void setKeyZoneStart(int keyZoneStart);
  Q_SIGNAL void keyZoneStartChanged();

  int keyZoneEnd() const;
  void setKeyZoneEnd(int keyZoneEnd);
  Q_SIGNAL void keyZoneEndChanged();

  int rootNote() const;
  void setRootNote(int rootNote);
  Q_SIGNAL void rootNoteChanged();
private:
  class Private;
  Private *d;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipAudioSource)
};

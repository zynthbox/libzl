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
    class Engine;
}
using namespace std;

//==============================================================================
class ClipAudioSource : public QObject, public juce::Timer {
    Q_OBJECT
    Q_PROPERTY(int id READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QObject* playbackPositions READ playbackPositions CONSTANT)
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

  QObject *playbackPositions();
  ClipAudioSourcePositionsModel *playbackPositionsModel();

  int id() const;
  void setId(int id);
  Q_SIGNAL void idChanged();
private:
  void timerCallback() override;
  class Private;
  Private *d;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipAudioSource)
};

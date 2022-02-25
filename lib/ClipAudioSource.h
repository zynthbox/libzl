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
using namespace std;

//==============================================================================
class ClipAudioSource : public QObject, public juce::Timer {
    Q_OBJECT
    Q_PROPERTY(int id READ id WRITE setId NOTIFY idChanged)
public:
  explicit ClipAudioSource(SyncTimer *syncTimer, const char *filepath,
                  bool muted = false, QObject *parent = nullptr);
  ~ClipAudioSource() override;

  void setProgressCallback(void (*functionPtr)(float));
  void syncProgress();
  void setStartPosition(float startPositionInSeconds);
  void setLooping(bool looping);
  bool getLooping() const;
  void setLength(int beat, int bpm);
  void setPitch(float pitchChange);
  void setSpeedRatio(float speedRatio);
  void setGain(float db);
  void setVolume(float vol);
  void setAudioLevelChangedCallback(void (*functionPtr)(float));
  void play(bool loop = true);
  void stop();
  float getDuration();
  const char *getFileName() const;
  const char *getFilePath() const;
  void updateTempoAndPitch();

  int id() const;
  void setId(int id);
  Q_SIGNAL void idChanged();
private:
  void timerCallback() override;
  class Private;
  Private *d;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipAudioSource)
};

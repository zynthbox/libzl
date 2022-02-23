/*
  ==============================================================================

    ClipAudioSource.h
    Created: 9 Aug 2021 6:25:01pm
    Author:  Anupam Basak <anupam.basak27@gmail.com>

  ==============================================================================
*/

#pragma once

#include <iostream>

#include <juce_events/juce_events.h>

class SyncTimer;
using namespace std;

//==============================================================================
class ClipAudioSource : public juce::Timer {
public:
  ClipAudioSource(SyncTimer *syncTimer, const char *filepath,
                  bool muted = false);
  ~ClipAudioSource();

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

private:
  void timerCallback();
  class Private;
  Private *d;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipAudioSource)
};

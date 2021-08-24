/*
  ==============================================================================

    ClipAudioSource.h
    Created: 9 Aug 2021 6:25:01pm
    Author:  Anupam Basak <anupam.basak27@gmail.com>

  ==============================================================================
*/

#pragma once

#include <iostream>

#include "JUCEHeaders.h"

using namespace std;

//==============================================================================
class ClipAudioSource {
 public:
  ClipAudioSource(const char* filepath);
  ~ClipAudioSource();

  void setStartPosition(float startPositionInSeconds);
  void setLength(float lengthInSeconds);
  void setPitch(float pitchChange);
  void setSpeedRatio(float speedRatio);
  void play();
  void stop();
  float getDuration();
  const char* getFileName();
  void updateTempoAndPitch();
  te::WaveAudioClip::Ptr getClip();

 private:
  te::Engine engine{"libzl"};
  std::unique_ptr<te::Edit> edit;

  juce::String chosenPath;
  juce::String fileName;

  float startPositionInSeconds = 0;
  float lengthInSeconds = -1;
  float pitchChange = 0;
  float speedRatio = 1.0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipAudioSource)
};

/*
  ==============================================================================

    ClipAudioSource.h
    Created: 9 Aug 2021 6:25:01pm
    Author:  Anupam Basak <anupam.basak27@gmail.com>

  ==============================================================================
*/

#pragma once

#include <iostream>

#include "../tracktion_engine/modules/tracktion_engine/tracktion_engine.h"
#include "JUCEHeaders.h"

using namespace std;
using namespace juce;

namespace te = tracktion_engine;

//==============================================================================
class ClipAudioSource {
 public:
  ClipAudioSource(const char* filepath);
  ~ClipAudioSource();

  void setStartPosition(float startPositionInSeconds);
  void setLength(float lengthInSeconds);
  void play();
  void stop();
  float getDuration();
  const char* getFileName();

 private:
  te::Engine engine{"libzl"};
  std::unique_ptr<te::Edit> edit;

  juce::String chosenPath;
  juce::String fileName;

  float startPositionInSeconds = 0;
  float lengthInSeconds = -1;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipAudioSource)
};

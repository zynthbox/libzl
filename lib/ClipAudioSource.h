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

namespace EngineHelpers {
te::Project::Ptr createTempProject(te::Engine& engine);
void removeAllClips(te::AudioTrack& track);
te::AudioTrack* getOrInsertAudioTrackAt(te::Edit& edit, int index);
te::WaveAudioClip::Ptr loadAudioFileAsClip(te::Edit& edit, const File& file);
template <typename ClipType>
typename ClipType::Ptr loopAroundClip(ClipType& clip);
void togglePlay(te::Edit& edit);
void toggleRecord(te::Edit& edit);
void armTrack(te::AudioTrack& t, bool arm, int position);
bool isTrackArmed(te::AudioTrack& t, int position);
bool isInputMonitoringEnabled(te::AudioTrack& t, int position);
void enableInputMonitoring(te::AudioTrack& t, bool im, int position);
bool trackHasInput(te::AudioTrack& t, int position);

inline std::unique_ptr<juce::KnownPluginList::PluginTree> createPluginTree(
    te::Engine& engine);
}  // namespace EngineHelpers

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

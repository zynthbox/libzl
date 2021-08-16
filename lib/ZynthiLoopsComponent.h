/*
  ==============================================================================

    ZynthiLoopsComponent.h
    Created: 9 Aug 2021 6:25:01pm
    Author:  Anupam Basak <anupam.basak27@gmail.com>

  ==============================================================================
*/

#pragma once

#include <iostream>

#include "JUCEHeaders.h"

using namespace std;
using namespace juce;

//==============================================================================
class ZynthiLoopsComponent : public juce::AudioSource, private juce::Thread {
 public:
  class ReferenceCountedBuffer : public juce::ReferenceCountedObject {
   public:
    typedef juce::ReferenceCountedObjectPtr<ReferenceCountedBuffer> Ptr;

    ReferenceCountedBuffer(const juce::String& nameToUse, int numChannels,
                           int numSamples);
    ~ReferenceCountedBuffer();

    juce::AudioSampleBuffer* getAudioSampleBuffer();

    int startPosition = 0;
    int endPosition = -1;
    int position = startPosition;

   private:
    juce::String name;
    juce::AudioSampleBuffer buffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReferenceCountedBuffer)
  };

  ZynthiLoopsComponent(const char* filepath);
  ~ZynthiLoopsComponent() override;

  void setStartPosition(float startPositionInSeconds);
  void setLength(float lengthInSeconds);
  void prepareToPlay(int, double);
  void getNextAudioBlock(
      const juce::AudioSourceChannelInfo& bufferToFill) override;
  void releaseResources() override;
  void play();
  void stop();
  float getDuration();
  const char* getFileName();
  void setAudioChannels(int numInputChannels, int numOutputChannels,
                        const XmlElement* const xml = nullptr);
  void shutdownAudio();

  AudioDeviceManager deviceManager;
  AudioSourcePlayer audioSourcePlayer;
  //////////////

 private:
  void run() override;

  void checkForPathToOpen();

  //==========================================================================

  juce::AudioFormatManager formatManager;
  ReferenceCountedBuffer::Ptr buffer;

  ReferenceCountedBuffer::Ptr currentBuffer;
  juce::String chosenPath;

  float duration = -1;
  int totalLengthInSamples;
  double sampleRate;
  juce::String fileName;

  float startPositionInSeconds = 0;
  bool startPositionInSecondsChanged = true;

  float lengthInSeconds = -1;
  bool lengthInSecondsChanged = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZynthiLoopsComponent)
};

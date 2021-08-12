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
class ZynthiLoopsComponent : public juce::AudioAppComponent,
                             private juce::Thread {
 public:
  class ReferenceCountedBuffer : public juce::ReferenceCountedObject {
   public:
    typedef juce::ReferenceCountedObjectPtr<ReferenceCountedBuffer> Ptr;

    ReferenceCountedBuffer(const juce::String& nameToUse, int numChannels,
                           int numSamples)
        : name(nameToUse), buffer(numChannels, numSamples) {
      cout << juce::String("Buffer named '") + name +
                  "' constructed. numChannels = " + juce::String(numChannels) +
                  ", numSamples = " + juce::String(numSamples)
           << "\n";
    }

    ~ReferenceCountedBuffer() {
      cout << juce::String("Buffer named '") + name + "' destroyed"
           << "\n";
    }

    juce::AudioSampleBuffer* getAudioSampleBuffer() { return &buffer; }

    int position = 0;

   private:
    juce::String name;
    juce::AudioSampleBuffer buffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReferenceCountedBuffer)
  };

  ZynthiLoopsComponent() : Thread("Background Thread") {
    deviceManager.initialiseWithDefaultDevices(2, 2);
    formatManager.registerBasicFormats();
    setAudioChannels(0, 2);

    startThread();
  }

  ~ZynthiLoopsComponent() override {
    stopThread(4000);
    shutdownAudio();
  }

  void prepareToPlay(int, double) override {}

  void getNextAudioBlock(
      const juce::AudioSourceChannelInfo& bufferToFill) override {
    ReferenceCountedBuffer::Ptr retainedCurrentBuffer(currentBuffer);

    if (retainedCurrentBuffer == nullptr) {
      bufferToFill.clearActiveBufferRegion();
      return;
    }

    auto* currentAudioSampleBuffer =
        retainedCurrentBuffer->getAudioSampleBuffer();
    auto position = retainedCurrentBuffer->position;

    auto numInputChannels = currentAudioSampleBuffer->getNumChannels();
    auto numOutputChannels = bufferToFill.buffer->getNumChannels();

    auto outputSamplesRemaining = bufferToFill.numSamples;
    auto outputSamplesOffset = 0;

    while (outputSamplesRemaining > 0) {
      auto bufferSamplesRemaining =
          currentAudioSampleBuffer->getNumSamples() - position;
      auto samplesThisTime =
          juce::jmin(outputSamplesRemaining, bufferSamplesRemaining);

      for (auto channel = 0; channel < numOutputChannels; ++channel) {
        bufferToFill.buffer->copyFrom(
            channel, bufferToFill.startSample + outputSamplesOffset,
            *currentAudioSampleBuffer, channel % numInputChannels, position,
            samplesThisTime);
      }

      outputSamplesRemaining -= samplesThisTime;
      outputSamplesOffset += samplesThisTime;
      position += samplesThisTime;

      if (position == currentAudioSampleBuffer->getNumSamples()) position = 0;
    }

    retainedCurrentBuffer->position = position;
  }

  void releaseResources() override { currentBuffer = nullptr; }

  void resized() override {}

  void play() {
    auto file = juce::File("/zynthian/zynthian-my-data/capture/c4.wav");
    auto path = file.getFullPathName();
    chosenPath.swapWith(path);
    notify();
  }

  void stop() { currentBuffer = nullptr; }

  int getDuration() { return duration; }

 private:
  void run() override {
    while (!threadShouldExit()) {
      checkForPathToOpen();
      checkForBuffersToFree();
      wait(500);
    }
  }

  void checkForBuffersToFree() {
    for (auto i = buffers.size(); --i >= 0;) {
      ReferenceCountedBuffer::Ptr buffer(buffers.getUnchecked(i));

      if (buffer->getReferenceCount() == 2) buffers.remove(i);
    }
  }

  void checkForPathToOpen() {
    juce::String pathToOpen;
    pathToOpen.swapWith(chosenPath);

    if (pathToOpen.isNotEmpty()) {
      juce::File file(pathToOpen);
      std::unique_ptr<juce::AudioFormatReader> reader(
          formatManager.createReaderFor(file));

      if (reader.get() != nullptr) {
        for (String key : reader->metadataValues.getAllKeys()) {
          cout << "Key: " + key + ", Value: " +
                      reader->metadataValues.getValue(key, "unknown")
               << "\n";
        }

        duration = (float)reader->lengthInSamples / reader->sampleRate;

        ReferenceCountedBuffer::Ptr newBuffer = new ReferenceCountedBuffer(
            file.getFileName(), (int)reader->numChannels,
            (int)reader->lengthInSamples);

        reader->read(newBuffer->getAudioSampleBuffer(), 0,
                     (int)reader->lengthInSamples, 0, true, true);
        currentBuffer = newBuffer;
        buffers.add(newBuffer);
      }
    }
  }

  //==========================================================================

  juce::AudioFormatManager formatManager;
  juce::ReferenceCountedArray<ReferenceCountedBuffer> buffers;

  ReferenceCountedBuffer::Ptr currentBuffer;
  juce::String chosenPath;
  int duration = -1;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZynthiLoopsComponent)
};

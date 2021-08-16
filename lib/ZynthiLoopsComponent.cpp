/*
  ==============================================================================

    ZynthiLoopsComponent.cpp
    Created: 9 Aug 2021 7:44:30pm
    Author:  root

  ==============================================================================
*/

#include "ZynthiLoopsComponent.h"

ZynthiLoopsComponent::ZynthiLoopsComponent(const char* filepath)
    : Thread("Background Thread") {
  deviceManager.initialiseWithDefaultDevices(2, 2);
  formatManager.registerBasicFormats();
  setAudioChannels(0, 2);

  startThread();

  chosenPath = filepath;
  notify();

  cerr << "Component thread started for file: " << filepath;
}

ZynthiLoopsComponent::~ZynthiLoopsComponent() {
  stopThread(4000);
  shutdownAudio();
}

void ZynthiLoopsComponent::setStartPosition(float startPositionInSeconds) {
  this->startPositionInSeconds = startPositionInSeconds;
  this->startPositionInSecondsChanged = true;
}

void ZynthiLoopsComponent::setLength(float lengthInSeconds) {
  this->lengthInSeconds = lengthInSeconds;
  this->lengthInSecondsChanged = true;
}

void ZynthiLoopsComponent::prepareToPlay(int, double) {}

void ZynthiLoopsComponent::getNextAudioBlock(
    const AudioSourceChannelInfo& bufferToFill) {
  ReferenceCountedBuffer::Ptr retainedCurrentBuffer(currentBuffer);

  if (retainedCurrentBuffer == nullptr) {
    bufferToFill.clearActiveBufferRegion();
    return;
  }

  if (startPositionInSecondsChanged) {
    retainedCurrentBuffer->startPosition = sampleRate * startPositionInSeconds;
    startPositionInSecondsChanged = false;
  }

  if (lengthInSecondsChanged) {
    if (lengthInSeconds == -1) {
      retainedCurrentBuffer->endPosition = totalLengthInSamples;
    } else {
      retainedCurrentBuffer->endPosition =
          retainedCurrentBuffer->startPosition + sampleRate * lengthInSeconds;
    }
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

    int endPosition;

    if (retainedCurrentBuffer->endPosition == -1)
      endPosition = currentAudioSampleBuffer->getNumSamples();
    else
      endPosition = retainedCurrentBuffer->endPosition;

    if (position >= endPosition) {
      // Looping logic
      position = retainedCurrentBuffer->startPosition;

      // Stop at end logic
      //        position = retainedCurrentBuffer->endPosition;
      //        break;
    }
  }

  retainedCurrentBuffer->position = position;
}

void ZynthiLoopsComponent::releaseResources() { currentBuffer = nullptr; }

void ZynthiLoopsComponent::play() {
  cerr << buffer;

  if (startPositionInSecondsChanged) {
    buffer->startPosition = sampleRate * startPositionInSeconds;
    buffer->position = buffer->startPosition;

    startPositionInSecondsChanged = false;
  }

  if (lengthInSecondsChanged) {
    if (lengthInSeconds == -1) {
      buffer->endPosition = totalLengthInSamples;
    } else {
      buffer->endPosition =
          buffer->startPosition + sampleRate * lengthInSeconds;
    }
  }

  cerr << "Total : " << totalLengthInSamples
       << ", Start : " << buffer->startPosition
       << ", End : " << buffer->endPosition << endl;

  currentBuffer = buffer;
}

void ZynthiLoopsComponent::stop() {
  currentBuffer = nullptr;
  buffer->position = buffer->startPosition;
}

float ZynthiLoopsComponent::getDuration() { return duration; }

const char* ZynthiLoopsComponent::getFileName() {
  return static_cast<const char*>(fileName.toUTF8());
}

void ZynthiLoopsComponent::setAudioChannels(int numInputChannels,
                                            int numOutputChannels,
                                            const XmlElement* const xml) {
  String audioError;

  audioError =
      deviceManager.initialise(numInputChannels, numOutputChannels, xml, true);

  jassert(audioError.isEmpty());

  deviceManager.addAudioCallback(&audioSourcePlayer);
  audioSourcePlayer.setSource(this);
}

void ZynthiLoopsComponent::shutdownAudio() {
  audioSourcePlayer.setSource(nullptr);
  deviceManager.removeAudioCallback(&audioSourcePlayer);

  deviceManager.closeAudioDevice();
}

void ZynthiLoopsComponent::run() {
  while (!threadShouldExit()) {
    checkForPathToOpen();
    wait(500);
  }
}

void ZynthiLoopsComponent::checkForPathToOpen() {
  juce::String pathToOpen;
  pathToOpen.swapWith(chosenPath);

  if (pathToOpen.isNotEmpty()) {
    juce::File file(pathToOpen);
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));

    if (reader.get() != nullptr) {
      for (String key : reader->metadataValues.getAllKeys()) {
        cerr << "Key: " + key + ", Value: " +
                    reader->metadataValues.getValue(key, "unknown")
             << "\n";
      }

      sampleRate = reader->sampleRate;
      duration = (float)reader->lengthInSamples / reader->sampleRate;
      totalLengthInSamples = (int)reader->lengthInSamples;
      fileName = file.getFileName();

      buffer = new ReferenceCountedBuffer(file.getFileName(),
                                          (int)reader->numChannels,
                                          (int)reader->lengthInSamples);

      buffer->startPosition = sampleRate * startPositionInSeconds;
      buffer->position = buffer->startPosition;

      if (lengthInSeconds == -1) {
        buffer->endPosition = totalLengthInSamples;
      } else {
        buffer->endPosition =
            buffer->startPosition + sampleRate * lengthInSeconds;
      }

      reader->read(buffer->getAudioSampleBuffer(), 0,
                   (int)reader->lengthInSamples, 0, true, true);
    }
  }
}

ZynthiLoopsComponent::ReferenceCountedBuffer::ReferenceCountedBuffer(
    const String& nameToUse, int numChannels, int numSamples)
    : name(nameToUse), buffer(numChannels, numSamples) {
  cout << juce::String("Buffer named '") + name +
              "' constructed. numChannels = " + juce::String(numChannels) +
              ", numSamples = " + juce::String(numSamples)
       << "\n";
}

ZynthiLoopsComponent::ReferenceCountedBuffer::~ReferenceCountedBuffer() {
  cout << juce::String("Buffer named '") + name + "' destroyed"
       << "\n";
}

AudioSampleBuffer*
ZynthiLoopsComponent::ReferenceCountedBuffer::getAudioSampleBuffer() {
  return &buffer;
}

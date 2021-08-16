/*
  ==============================================================================

    ClipAudioSource.cpp
    Created: 9 Aug 2021 7:44:30pm
    Author:  root

  ==============================================================================
*/

#include "ClipAudioSource.h"

#include "../tracktion_engine/examples/common/Utilities.h"
#include "../tracktion_engine/modules/tracktion_engine/tracktion_engine.h"

namespace te = tracktion_engine;

ClipAudioSource::ClipAudioSource(const char* filepath) {
  engine.getDeviceManager().initialise(0, 2);

  cerr << "Opening file : " << filepath << endl;

  juce::File file(filepath);
  const File editFile("/tmp/editfile");

  edit = te::createEmptyEdit(engine, editFile);
  auto clip = EngineHelpers::loadAudioFileAsClip(*edit, file);
  auto& transport = edit->getTransport();

  this->fileName = file.getFileName();
  this->lengthInSeconds = edit->getLength();

  transport.setLoopRange(te::EditTimeRange::withStartAndLength(
      startPositionInSeconds, lengthInSeconds));
  transport.looping = true;
}

ClipAudioSource::~ClipAudioSource() {}

void ClipAudioSource::setStartPosition(float startPositionInSeconds) {
  this->startPositionInSeconds = startPositionInSeconds;

  auto& transport = edit->getTransport();
  transport.setLoopRange(te::EditTimeRange::withStartAndLength(
      startPositionInSeconds, lengthInSeconds));

  if (transport.isPlaying()) {
    stop();
    play();
  }
}

void ClipAudioSource::setLength(float lengthInSeconds) {
  this->lengthInSeconds = lengthInSeconds;

  auto& transport = edit->getTransport();
  transport.setLoopRange(te::EditTimeRange::withStartAndLength(
      startPositionInSeconds, lengthInSeconds));

  if (transport.isPlaying()) {
    stop();
    play();
  }
}

float ClipAudioSource::getDuration() { return edit->getLength(); }

const char* ClipAudioSource::getFileName() {
  return static_cast<const char*>(fileName.toUTF8());
}

void ClipAudioSource::play() { edit->getTransport().play(false); }

void ClipAudioSource::stop() { edit->getTransport().stop(false, false); }

/*
  ==============================================================================

    ClipAudioSource.cpp
    Created: 9 Aug 2021 7:44:30pm
    Author:  root

  ==============================================================================
*/

#include "ClipAudioSource.h"

#include <iostream>

#include "../tracktion_engine/examples/common/Utilities.h"
#include "../tracktion_engine/modules/tracktion_engine/tracktion_engine.h"

namespace te = tracktion_engine;
using namespace std;

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
  cerr << "Setting start position : " << startPositionInSeconds << endl;

  this->startPositionInSeconds = startPositionInSeconds;

  //  auto& transport = edit->getTransport();
  //  transport.setLoopRange(te::EditTimeRange::withStartAndLength(
  //      startPositionInSeconds, lengthInSeconds));

  //  if (transport.isPlaying()) {
  //    stop();
  //    play(shouldLoop);
  //  }
}

void ClipAudioSource::setLength(float lengthInSeconds) {
  cerr << "Setting length : " << lengthInSeconds << endl;

  this->lengthInSeconds = lengthInSeconds;

  //  auto& transport = edit->getTransport();
  //  transport.setLoopRange(te::EditTimeRange::withStartAndLength(
  //      startPositionInSeconds, lengthInSeconds));

  //  if (transport.isPlaying()) {
  //    stop();
  //    play(shouldLoop);
  //  }
}

float ClipAudioSource::getDuration() { return edit->getLength(); }

const char* ClipAudioSource::getFileName() {
  return static_cast<const char*>(fileName.toUTF8());
}

void ClipAudioSource::play(bool loop) {
  shouldLoop = loop;
  auto& transport = edit->getTransport();

  transport.looping = loop;

  transport.playSectionAndReset(
      te::EditTimeRange::withStartAndLength(this->startPositionInSeconds, 0.1));

  //  if (loop) {
  //    cerr << "Looping from " << startPositionInSeconds << " to "
  //         << lengthInSeconds;
  //    transport.play(false);
  //  } else {
  //    cerr << "Playing once from " << startPositionInSeconds << " to "
  //         << lengthInSeconds;
  //    transport.playSectionAndReset(
  //        te::EditTimeRange::withStartAndLength(0.8, 0.5));
  //  }
}

void ClipAudioSource::stop() { edit->getTransport().stop(false, false); }

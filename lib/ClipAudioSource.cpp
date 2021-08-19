/*
  ==============================================================================

    ClipAudioSource.cpp
    Created: 9 Aug 2021 7:44:30pm
    Author:  root

  ==============================================================================
*/

#include "ClipAudioSource.h"

#include <unistd.h>

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
  //  auto& transport = edit->getTransport();

  clip->setAutoTempo(false);
  clip->setAutoPitch(false);
  clip->setTimeStretchMode(te::TimeStretcher::defaultMode);

  EngineHelpers::loopAroundClip(*clip);

  this->fileName = file.getFileName();
  this->lengthInSeconds = edit->getLength();

  //  transport.setLoopRange(te::EditTimeRange::withStartAndLength(
  //      startPositionInSeconds, lengthInSeconds));
  //  transport.looping = true;
}

ClipAudioSource::~ClipAudioSource() {}

void ClipAudioSource::setStartPosition(float startPositionInSeconds) {
  this->startPositionInSeconds = startPositionInSeconds;

  //  auto& transport = edit->getTransport();
  //  transport.setLoopRange(te::EditTimeRange::withStartAndLength(
  //      startPositionInSeconds, lengthInSeconds));

  //  if (transport.isPlaying()) {
  //    stop();
  //    play();
  //  }
}

void ClipAudioSource::setPitch(float pitchChange) {
  this->pitchChange = pitchChange;
  updateTempoAndPitch();
}

void ClipAudioSource::setSpeedRatio(float speedRatio) {
  this->speedRatio = speedRatio;
  updateTempoAndPitch();
}

void ClipAudioSource::setLength(float lengthInSeconds) {
  this->lengthInSeconds = lengthInSeconds;

  //  auto& transport = edit->getTransport();
  //  transport.setLoopRange(te::EditTimeRange::withStartAndLength(
  //      startPositionInSeconds, lengthInSeconds));

  //  if (transport.isPlaying()) {
  //    stop();
  //    play();
  //  }
}

float ClipAudioSource::getDuration() { return edit->getLength(); }

const char* ClipAudioSource::getFileName() {
  return static_cast<const char*>(fileName.toUTF8());
}

void ClipAudioSource::updateTempoAndPitch() {
  if (auto clip = getClip()) {
    cerr << "Setting speedRatio and pitch" << endl;

    clip->setSpeedRatio(this->speedRatio);
    clip->setPitchChange(this->pitchChange);

    auto& transport = edit->getTransport();

    if (transport.isPlaying()) {
      EngineHelpers::loopAroundClip(*clip);
    }
  }
}

te::WaveAudioClip::Ptr ClipAudioSource::getClip() {
  if (auto track = EngineHelpers::getOrInsertAudioTrackAt(*edit, 0))
    if (auto clip = dynamic_cast<te::WaveAudioClip*>(track->getClips()[0]))
      return *clip;

  return {};
}

void ClipAudioSource::play() {
  auto clip = getClip();
  EngineHelpers::loopAroundClip(*clip);
}

void ClipAudioSource::stop() { edit->getTransport().stop(false, false); }

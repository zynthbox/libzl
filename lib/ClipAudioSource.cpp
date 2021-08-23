/*
  ==============================================================================

    ClipAudioSource.cpp
    Created: 9 Aug 2021 7:44:30pm
    Author:  root

  ==============================================================================
*/

#include "ClipAudioSource.h"

#include <unistd.h>

#include "../tracktion_engine/examples/common/Utilities.h"
#include "Helper.h"

using namespace std;

ClipAudioSource::ClipAudioSource(const char* filepath) {
  engine.getDeviceManager().initialise(0, 2);

  cerr << "Opening file : " << filepath << endl;

  juce::File file(filepath);
  const File editFile("/tmp/editfile");

  edit = te::createEmptyEdit(engine, editFile);
  auto clip = Helper::loadAudioFileAsClip(*edit, file);
  auto& transport = edit->getTransport();

  clip->setAutoTempo(false);
  clip->setAutoPitch(false);
  clip->setTimeStretchMode(te::TimeStretcher::defaultMode);

  //  EngineHelpers::loopAroundClip(*clip);
  //  clip->setPitchChange(12);
  //  EngineHelpers::loopAroundClip(*clip);

  this->fileName = file.getFileName();
  this->lengthInSeconds = edit->getLength();

  transport.setLoopRange(te::EditTimeRange::withStartAndLength(
      startPositionInSeconds, lengthInSeconds));
  transport.looping = true;
}

ClipAudioSource::~ClipAudioSource() {}

void ClipAudioSource::setStartPosition(float startPositionInSeconds) {
  cerr << "Setting Start Position to " << startPositionInSeconds << endl;
  this->startPositionInSeconds = startPositionInSeconds;
  updateTempoAndPitch();
}

void ClipAudioSource::setPitch(float pitchChange) {
  cerr << "Setting Pitch to " << pitchChange << endl;
  this->pitchChange = pitchChange;
  updateTempoAndPitch();
}

void ClipAudioSource::setSpeedRatio(float speedRatio) {
  cerr << "Setting Speed to " << speedRatio << endl;
  this->speedRatio = speedRatio;
  updateTempoAndPitch();
}

void ClipAudioSource::setLength(float lengthInSeconds) {
  cerr << "Setting Length to " << lengthInSeconds << endl;
  this->lengthInSeconds = lengthInSeconds;
  updateTempoAndPitch();
}

float ClipAudioSource::getDuration() { return edit->getLength(); }

const char* ClipAudioSource::getFileName() {
  return static_cast<const char*>(fileName.toUTF8());
}

void ClipAudioSource::updateTempoAndPitch() {
  if (auto clip = getClip()) {
    auto& transport = clip->edit.getTransport();

    cerr << "Updating speedRatio and pitch" << endl;

    clip->setSpeedRatio(this->speedRatio);
    clip->setPitchChange(this->pitchChange);

    transport.setLoopRange(te::EditTimeRange::withStartAndLength(
        startPositionInSeconds, lengthInSeconds));
    transport.looping = true;
  }
}

te::WaveAudioClip::Ptr ClipAudioSource::getClip() {
  if (auto track = Helper::getOrInsertAudioTrackAt(*edit, 0))
    if (auto clip = dynamic_cast<te::WaveAudioClip*>(track->getClips()[0]))
      return *clip;

  return {};
}

void ClipAudioSource::play() {
  auto clip = getClip();
  clip->edit.getTransport().play(false);
  //  clip->edit.getTransport().playSectionAndReset(
  //      te::EditTimeRange::withStartAndLength(this->startPositionInSeconds,
  //                                            this->lengthInSeconds));
}

void ClipAudioSource::stop() { edit->getTransport().stop(false, false); }

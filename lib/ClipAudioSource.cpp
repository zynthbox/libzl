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

  wavFile = File(filepath);
  const File editFile("/tmp/editfile");

  auto clip = EngineHelpers::loadAudioFileAsClip(edit, wavFile);
  //  auto& transport = edit.getTransport();

  clip->setAutoTempo(false);
  clip->setAutoPitch(false);
  clip->setTimeStretchMode(te::TimeStretcher::defaultMode);

  this->fileName = wavFile.getFileName();
  this->lengthInSeconds = edit.getLength();

  //  clip->setSpeedRatio(2.0);

  EngineHelpers::loopAroundClip(*clip);

  //  transport.setLoopRange(te::EditTimeRange::withStartAndLength(
  //      startPositionInSeconds, lengthInSeconds));
  //  transport.looping = true;

  //  updateTempoAndPitch();
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

float ClipAudioSource::getDuration() { return edit.getLength(); }

const char* ClipAudioSource::getFileName() {
  return static_cast<const char*>(fileName.toUTF8());
}

void ClipAudioSource::setPitch(float pitchChange) {
  this->pitchChange = pitchChange;
  updateTempoAndPitch();
}

te::WaveAudioClip::Ptr ClipAudioSource::getClip() {
  if (auto track = EngineHelpers::getOrInsertAudioTrackAt(edit, 0))
    if (auto clip = dynamic_cast<te::WaveAudioClip*>(track->getClips()[0]))
      return *clip;

  return {};
}

void ClipAudioSource::updateTempoAndPitch() {
  if (auto clip = getClip()) {
    const auto audioFileInfo = te::AudioFile(engine, wavFile).getInfo();
    const double baseTempo = 120.0;

    // First update the tempo based on the ratio between the root tempo and
    // tempo slider value
    if (baseTempo > 0.0) {
      cerr << "Setting tempo" << endl;

      const double ratio = (double)pitchChange / baseTempo;

      clip->setSpeedRatio(1.8);
      clip->setLength(
          audioFileInfo.getLengthInSeconds() / clip->getSpeedRatio(), true);

      cerr << "Speed ratio : " << ratio << endl;
      cerr << "Length : "
           << audioFileInfo.getLengthInSeconds() / clip->getSpeedRatio()
           << endl;
    }

    //    cerr << "Setting Pitch : " << pitchChange << endl;
    //    clip->setPitchChange(12);

    EngineHelpers::loopAroundClip(*clip);

    //    auto& transport = edit->getTransport();

    //    if (transport.isPlaying()) {
    //      transport.stop(false, false);
    //      transport.play(false);
    //    }
  }
}

void ClipAudioSource::play() {
  updateTempoAndPitch();
  //  edit->getTransport().play(false);
}

void ClipAudioSource::stop() { edit.getTransport().stop(false, false); }

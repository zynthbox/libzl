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
#include "SyncTimer.h"

using namespace std;

class ClipProgress : public ValueTree::Listener {
public:
  ClipProgress(ClipAudioSource *source)
      : ValueTree::Listener(), m_source(source) {}

  void valueTreePropertyChanged(ValueTree &,
                                const juce::Identifier &i) override {
    if (i != juce::Identifier("position")) {
      return;
    }
    m_source->syncProgress();
  }

private:
  ClipAudioSource *m_source = nullptr;
};

ClipAudioSource::ClipAudioSource(SyncTimer *syncTimer, const char *filepath)
    : syncTimer(syncTimer) {
  engine.getDeviceManager().initialise(0, 2);

  cerr << "Opening file : " << filepath << endl;

  juce::File file(filepath);

  const File editFile = File::createTempFile("editFile");

  edit = te::createEmptyEdit(engine, editFile);
  auto clip = Helper::loadAudioFileAsClip(*edit, file);
  auto &transport = edit->getTransport();

  transport.ensureContextAllocated(true);

  this->fileName = file.getFileName();
  this->lengthInSeconds = edit->getLength();

  if (clip) {
    clip->setAutoTempo(false);
    clip->setAutoPitch(false);
    clip->setTimeStretchMode(te::TimeStretcher::defaultMode);
  }

  transport.setLoopRange(te::EditTimeRange::withStartAndLength(
      startPositionInSeconds, lengthInSeconds));
  transport.looping = true;
  transport.state.addListener(new ClipProgress(this));

  auto track = Helper::getOrInsertAudioTrackAt(*edit, 0);
  auto levelMeasurerPlugin = track->getLevelMeterPlugin();
  levelMeasurerPlugin->measurer.addClient(levelClient);
  startTimerHz(30);
}

ClipAudioSource::~ClipAudioSource() {
  cerr << "Destroying Clip" << endl;
  stop();
  auto track = Helper::getOrInsertAudioTrackAt(*edit, 0);
  auto levelMeasurerPlugin = track->getLevelMeterPlugin();
  levelMeasurerPlugin->measurer.removeClient(levelClient);
  edit.reset();
  stopTimer();
}

void ClipAudioSource::setProgressCallback(void (*functionPtr)(float)) {
  progressChangedCallback = functionPtr;
}

void ClipAudioSource::syncProgress() {
  if (progressChangedCallback != nullptr) {
    progressChangedCallback(edit->getTransport().getCurrentPosition());
  }
}

void ClipAudioSource::setStartPosition(float startPositionInSeconds) {
  this->startPositionInSeconds = jmax(0.0f, startPositionInSeconds);
  cerr << "Setting Start Position to " << this->startPositionInSeconds << endl;
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

void ClipAudioSource::setGain(float db) {
  if (auto clip = getClip()) {
    cerr << "Setting gain : " << db;
    clip->setGainDB(db);
  }
}

void ClipAudioSource::setVolume(float vol) {
  if (auto clip = getClip()) {
    cerr << "Setting volume : " << vol;
    clip->edit.setMasterVolumeSliderPos(te::decibelsToVolumeFaderPosition(vol));
  }
}

void ClipAudioSource::setAudioLevelChangedCallback(void (*functionPtr)(float)) {
  audioLevelChangedCallback = functionPtr;
}

void ClipAudioSource::setLength(int beat, int bpm) {
  cerr << "Interval : " << syncTimer->getInterval(bpm) << endl;
  float lengthInSeconds = syncTimer->subbeatCountToSeconds((quint64)bpm, (quint64)(beat * syncTimer->getMultiplier()));
  cerr << "Setting Length to " << lengthInSeconds << endl;
  this->lengthInSeconds = lengthInSeconds;
  updateTempoAndPitch();
}

float ClipAudioSource::getDuration() { return edit->getLength(); }

const char *ClipAudioSource::getFileName() {
  return static_cast<const char *>(fileName.toUTF8());
}

void ClipAudioSource::updateTempoAndPitch() {
  if (auto clip = getClip()) {
    auto &transport = clip->edit.getTransport();
    bool isPlaying = transport.isPlaying();

    if (isPlaying) {
      transport.stop(false, false);
    }

    cerr << "Updating speedRatio(" << this->speedRatio << ") and pitch("
         << this->pitchChange << ")" << endl;

    clip->setSpeedRatio(this->speedRatio);
    clip->setPitchChange(this->pitchChange);

    cerr << "Setting loop range : " << startPositionInSeconds << " to "
         << (startPositionInSeconds + lengthInSeconds) << endl;

    transport.setLoopRange(te::EditTimeRange::withStartAndLength(
        startPositionInSeconds, lengthInSeconds));
    transport.setCurrentPosition(transport.loopPoint1);

    if (isPlaying) {
      syncTimer->queueClipToStart(this);
    }
  }
}

te::WaveAudioClip::Ptr ClipAudioSource::getClip() {
  if (auto track = Helper::getOrInsertAudioTrackAt(*edit, 0))
    if (auto clip = dynamic_cast<te::WaveAudioClip *>(track->getClips()[0]))
      return *clip;

  return {};
}

void ClipAudioSource::timerCallback() {
  prevLeveldB = currentLeveldB;

  currentLeveldB = levelClient.getAndClearAudioLevel(0).dB;

  // Now we give the level bar fading charcteristics.
  // And, the below coversions, decibelsToGain and gainToDecibels,
  // take care of 0dB, which will never fade!...but a gain of 1.0 (0dB) will.

  const auto prevLevel{Decibels::decibelsToGain(prevLeveldB)};

  if (prevLeveldB > currentLeveldB)
    currentLeveldB = Decibels::gainToDecibels(prevLevel * 0.94);

  // the test below may save some unnecessary paints
  if (currentLeveldB != prevLeveldB && audioLevelChangedCallback != nullptr) {
    // Callback
    audioLevelChangedCallback(currentLeveldB);
  }
}

void ClipAudioSource::play(bool loop) {
  auto clip = getClip();
  if (!clip) {
    return;
  }

  auto &transport = getClip()->edit.getTransport();

  transport.stop(false, false);
  transport.setCurrentPosition(transport.loopPoint1);

  if (loop) {
    transport.play(false);
  } else {
    transport.playSectionAndReset(te::EditTimeRange::withStartAndLength(
        startPositionInSeconds, lengthInSeconds));
  }
}

void ClipAudioSource::stop() {
  cerr << "libzl : Stopping clip " << this << endl;

  edit->getTransport().stop(false, false);
}

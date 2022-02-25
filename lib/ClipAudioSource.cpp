/*
  ==============================================================================

    ClipAudioSource.cpp
    Created: 9 Aug 2021 7:44:30pm
    Author:  root

  ==============================================================================
*/

#include "ClipAudioSource.h"

#include <unistd.h>

#include "JUCEHeaders.h"
#include "../tracktion_engine/examples/common/Utilities.h"
#include "Helper.h"
#include "SyncTimer.h"

#define DEBUG_CLIP true
#define IF_DEBUG_CLIP if (DEBUG_CLIP)

using namespace std;

class ClipAudioSource::Private {
public:
  Private(ClipAudioSource *qq) : q(qq) {};
  ClipAudioSource *q;
  const te::Engine &getEngine() const { return engine; };
  te::WaveAudioClip::Ptr getClip() {
    if (auto track = Helper::getOrInsertAudioTrackAt(*edit, 0))
        if (auto clip = dynamic_cast<te::WaveAudioClip *>(track->getClips()[0]))
        return *clip;

    return {};
  }

  te::Engine engine{"libzl"};
  std::unique_ptr<te::Edit> edit;

  SyncTimer *syncTimer;
  void (*progressChangedCallback)(float progress) = nullptr;
  void (*audioLevelChangedCallback)(float leveldB) = nullptr;

  juce::String chosenPath;
  juce::String fileName;
  juce::String filePath;

  te::LevelMeasurer::Client levelClient;

  float startPositionInSeconds = 0;
  float lengthInSeconds = -1;
  float pitchChange = 0;
  float speedRatio = 1.0;
  double currentLeveldB{0.0};
  double prevLeveldB{0.0};
};

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

ClipAudioSource::ClipAudioSource(SyncTimer *syncTimer, const char *filepath,
                                 bool muted, QObject *parent)
    : QObject(parent)
    , d(new Private(this)) {
  d->syncTimer = syncTimer;
  d->engine.getDeviceManager().initialise(0, 2);
  d->engine.getDeviceManager().deviceManager.setCurrentAudioDeviceType("JACK", true);

  IF_DEBUG_CLIP cerr << "Opening file : " << filepath << endl;

  juce::File file(filepath);

  const File editFile = File::createTempFile("editFile");

  d->edit = te::createEmptyEdit(d->engine, editFile);
  auto clip = Helper::loadAudioFileAsClip(*d->edit, file);
  auto &transport = d->edit->getTransport();

  transport.ensureContextAllocated(true);

  d->fileName = file.getFileName();
  d->filePath = filepath;
  d->lengthInSeconds = d->edit->getLength();

  if (clip) {
    clip->setAutoTempo(false);
    clip->setAutoPitch(false);
    clip->setTimeStretchMode(te::TimeStretcher::defaultMode);
  }

  transport.setLoopRange(te::EditTimeRange::withStartAndLength(
      d->startPositionInSeconds, d->lengthInSeconds));
  transport.looping = true;
  transport.state.addListener(new ClipProgress(this));

  auto track = Helper::getOrInsertAudioTrackAt(*d->edit, 0);

  if (muted) {
    IF_DEBUG_CLIP cerr << "Clip marked to be muted" << endl;
    setVolume(-100.0f);
  }

  auto levelMeasurerPlugin = track->getLevelMeterPlugin();
  levelMeasurerPlugin->measurer.addClient(d->levelClient);
  startTimerHz(30);
}

ClipAudioSource::~ClipAudioSource() {
  IF_DEBUG_CLIP cerr << "Destroying Clip" << endl;
  stop();
  auto track = Helper::getOrInsertAudioTrackAt(*d->edit, 0);
  auto levelMeasurerPlugin = track->getLevelMeterPlugin();
  levelMeasurerPlugin->measurer.removeClient(d->levelClient);
  d->edit.reset();
  stopTimer();
}

void ClipAudioSource::setProgressCallback(void (*functionPtr)(float)) {
  d->progressChangedCallback = functionPtr;
}

void ClipAudioSource::syncProgress() {
  if (d->progressChangedCallback != nullptr) {
    d->progressChangedCallback(d->edit->getTransport().getCurrentPosition());
  }
}

void ClipAudioSource::setLooping(bool looping) {
  auto &transport = d->edit->getTransport();
  if (transport.looping != looping) {
    transport.looping = looping;
  }
}

bool ClipAudioSource::getLooping() const
{
  const auto &transport = d->edit->getTransport();
  return transport.looping;
}

void ClipAudioSource::setStartPosition(float startPositionInSeconds) {
  d->startPositionInSeconds = jmax(0.0f, startPositionInSeconds);
  IF_DEBUG_CLIP cerr << "Setting Start Position to " << d->startPositionInSeconds << endl;
  updateTempoAndPitch();
}

void ClipAudioSource::setPitch(float pitchChange) {
  IF_DEBUG_CLIP cerr << "Setting Pitch to " << pitchChange << endl;
  d->pitchChange = pitchChange;
  updateTempoAndPitch();
}

void ClipAudioSource::setSpeedRatio(float speedRatio) {
  IF_DEBUG_CLIP cerr << "Setting Speed to " << speedRatio << endl;
  d->speedRatio = speedRatio;
  updateTempoAndPitch();
}

void ClipAudioSource::setGain(float db) {
  if (auto clip = d->getClip()) {
    IF_DEBUG_CLIP cerr << "Setting gain : " << db;
    clip->setGainDB(db);
  }
}

void ClipAudioSource::setVolume(float vol) {
  if (auto clip = d->getClip()) {
    IF_DEBUG_CLIP cerr << "Setting volume : " << vol << endl;
    clip->edit.setMasterVolumeSliderPos(te::decibelsToVolumeFaderPosition(vol));
  }
}

void ClipAudioSource::setAudioLevelChangedCallback(void (*functionPtr)(float)) {
  d->audioLevelChangedCallback = functionPtr;
}

void ClipAudioSource::setLength(int beat, int bpm) {
  IF_DEBUG_CLIP cerr << "Interval : " << d->syncTimer->getInterval(bpm) << endl;
  float lengthInSeconds = d->syncTimer->subbeatCountToSeconds(
      (quint64)bpm, (quint64)(beat * d->syncTimer->getMultiplier()));
  IF_DEBUG_CLIP cerr << "Setting Length to " << lengthInSeconds << endl;
  d->lengthInSeconds = lengthInSeconds;
  updateTempoAndPitch();
}

float ClipAudioSource::getDuration() { return d->edit->getLength(); }

const char *ClipAudioSource::getFileName() const {
  return static_cast<const char *>(d->fileName.toUTF8());
}

const char *ClipAudioSource::getFilePath() const {
    return static_cast<const char*>(d->filePath.toUTF8());
}

void ClipAudioSource::updateTempoAndPitch() {
  if (auto clip = d->getClip()) {
    auto &transport = clip->edit.getTransport();
    bool isPlaying = transport.isPlaying();

    if (isPlaying) {
      transport.stop(false, false);
    }

    IF_DEBUG_CLIP cerr << "Updating speedRatio(" << d->speedRatio << ") and pitch("
         << d->pitchChange << ")" << endl;

    clip->setSpeedRatio(d->speedRatio);
    clip->setPitchChange(d->pitchChange);

    IF_DEBUG_CLIP cerr << "Setting loop range : " << d->startPositionInSeconds << " to "
         << (d->startPositionInSeconds + d->lengthInSeconds) << endl;

    transport.setLoopRange(te::EditTimeRange::withStartAndLength(
        d->startPositionInSeconds, d->lengthInSeconds));
    transport.setCurrentPosition(transport.loopPoint1);

    if (isPlaying) {
      d->syncTimer->queueClipToStart(this);
    }
  }
}

void ClipAudioSource::timerCallback() {
  d->prevLeveldB = d->currentLeveldB;

  d->currentLeveldB = d->levelClient.getAndClearAudioLevel(0).dB;

  // Now we give the level bar fading charcteristics.
  // And, the below coversions, decibelsToGain and gainToDecibels,
  // take care of 0dB, which will never fade!...but a gain of 1.0 (0dB) will.

  const auto prevLevel{Decibels::decibelsToGain(d->prevLeveldB)};

  if (d->prevLeveldB > d->currentLeveldB)
    d->currentLeveldB = Decibels::gainToDecibels(prevLevel * 0.94);

  // the test below may save some unnecessary paints
  if (d->currentLeveldB != d->prevLeveldB && d->audioLevelChangedCallback != nullptr) {
    // Callback
    d->audioLevelChangedCallback(d->currentLeveldB);
  }
}

void ClipAudioSource::play(bool loop) {
  auto clip = d->getClip();
  IF_DEBUG_CLIP cerr << "libzl : Starting clip " << this << " which is really " << clip.get() << " in a " << (loop ? "looping" : "non-looping") << " manner from " << d->startPositionInSeconds << " and for " << d->lengthInSeconds << " seconds at volume " << (clip  && clip->edit.getMasterVolumePlugin().get() ? clip->edit.getMasterVolumePlugin()->volume : 0) << endl;
  if (!clip) {
    return;
  }

  auto &transport = d->getClip()->edit.getTransport();

  transport.stop(false, false);
  transport.setCurrentPosition(transport.loopPoint1);

  if (loop) {
    transport.play(false);
  } else {
    transport.playSectionAndReset(te::EditTimeRange::withStartAndLength(
        d->startPositionInSeconds, d->lengthInSeconds));
  }
}

void ClipAudioSource::stop() {
  IF_DEBUG_CLIP cerr << "libzl : Stopping clip " << this << endl;

  d->edit->getTransport().stop(false, false);
}

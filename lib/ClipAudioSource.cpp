/*
  ==============================================================================

    ClipAudioSource.cpp
    Created: 9 Aug 2021 7:44:30pm
    Author:  root

  ==============================================================================
*/

#include "ClipAudioSource.h"
#include "ClipAudioSourcePositionsModel.h"

#include <unistd.h>

#include "JUCEHeaders.h"
#include "../tracktion_engine/examples/common/Utilities.h"
#include "Helper.h"
#include "SamplerSynth.h"
#include "SyncTimer.h"

#define DEBUG_CLIP true
#define IF_DEBUG_CLIP if (DEBUG_CLIP)

using namespace std;

class ClipAudioSource::Private {
public:
  Private(ClipAudioSource *qq) : q(qq) {}
  ClipAudioSource *q;
  const te::Engine &getEngine() const { return *engine; };
  te::WaveAudioClip::Ptr getClip() {
    if (auto track = Helper::getOrInsertAudioTrackAt(*edit, 0)) {
      if (auto clip = dynamic_cast<te::WaveAudioClip *>(track->getClips()[0])) {
        return *clip;
      }
    }

    return {};
  }

  te::Engine *engine{nullptr};
  std::unique_ptr<te::Edit> edit;
  bool isRendering{false};

  SyncTimer *syncTimer;
  void (*progressChangedCallback)(float progress) = nullptr;
  void (*audioLevelChangedCallback)(float leveldB) = nullptr;

  juce::File givenFile;
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
  int id{0};
  ClipAudioSourcePositionsModel *positionsModel{nullptr};
  // Default is 16, but we also need to generate the positions, so that is set up in the ClipAudioSource ctor
  int slices{0};
  QVariantList slicePositions;
  QList<double> slicePositionsCache;
  int sliceBaseMidiNote{60};
  int keyZoneStart{0};
  int keyZoneEnd{127};
  int rootNote{60};
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

ClipAudioSource::ClipAudioSource(tracktion_engine::Engine *engine, SyncTimer *syncTimer, const char *filepath,
                                 bool muted, QObject *parent)
    : QObject(parent)
    , d(new Private(this)) {
  d->syncTimer = syncTimer;
  d->engine = engine;

  IF_DEBUG_CLIP cerr << "Opening file : " << filepath << endl;

  d->givenFile = juce::File(filepath);

  const File editFile = File::createTempFile("editFile");

  d->edit = te::createEmptyEdit(*d->engine, editFile);
  auto clip = Helper::loadAudioFileAsClip(*d->edit, d->givenFile);
  auto &transport = d->edit->getTransport();

  transport.ensureContextAllocated(true);

  d->fileName = d->givenFile.getFileName();
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

  d->positionsModel = new ClipAudioSourcePositionsModel(this);
  d->positionsModel->moveToThread(QCoreApplication::instance()->thread());
  SamplerSynth::instance()->registerClip(this);

  connect(this, &ClipAudioSource::slicePositionsChanged, this, [&](){
    d->slicePositionsCache.clear();
    for (const QVariant &position : d->slicePositions) {
        d->slicePositionsCache << position.toDouble();
    }
  });
  setSlices(16);
}

ClipAudioSource::~ClipAudioSource() {
  IF_DEBUG_CLIP cerr << "Destroying Clip" << endl;
  SamplerSynth::instance()->unregisterClip(this);
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

float ClipAudioSource::getStartPosition(int slice) const
{
    if (slice > -1 && slice < d->slicePositionsCache.length()) {
        return d->startPositionInSeconds + (d->lengthInSeconds * d->slicePositionsCache[slice]);
    } else {
        return d->startPositionInSeconds;
    }
}

float ClipAudioSource::getStopPosition(int slice) const
{
    if (slice > -1 && slice + 1 < d->slicePositionsCache.length()) {
        return d->startPositionInSeconds + (d->lengthInSeconds * d->slicePositionsCache[slice + 1]);
    } else {
        return d->startPositionInSeconds + d->lengthInSeconds;
    }
}

void ClipAudioSource::setPitch(float pitchChange, bool immediate) {
  IF_DEBUG_CLIP cerr << "Setting Pitch to " << pitchChange << endl;
  d->pitchChange = pitchChange;
  if (immediate) {
    if (auto clip = d->getClip()) {
      clip->setPitchChange(d->pitchChange);
    }
  } else {
    updateTempoAndPitch();
  }
}

void ClipAudioSource::setSpeedRatio(float speedRatio, bool immediate) {
  IF_DEBUG_CLIP cerr << "Setting Speed to " << speedRatio << endl;
  d->speedRatio = speedRatio;
  if (immediate) {
    if (auto clip = d->getClip()) {
      clip->setSpeedRatio(d->speedRatio);
    }
  } else {
    updateTempoAndPitch();
  }
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

void ClipAudioSource::setVolumeAbsolute(float vol)
{
  if (auto clip = d->getClip()) {
    IF_DEBUG_CLIP cerr << "Setting volume absolutely : " << vol << endl;
    clip->edit.setMasterVolumeSliderPos(qMax(0.0f, qMin(vol, 1.0f)));
  }
}

void ClipAudioSource::setAudioLevelChangedCallback(void (*functionPtr)(float)) {
  d->audioLevelChangedCallback = functionPtr;
}

void ClipAudioSource::setLength(float beat, int bpm) {
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

tracktion_engine::AudioFile ClipAudioSource::getPlaybackFile() const {
    if (const auto& clip = d->getClip()) {
        return clip->getAudioFile();
    }
    return te::AudioFile(*d->engine);
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

  if (auto clip = d->getClip()) {
    if (clip->needsRender()) {
        d->isRendering = true;
    } else if (d->isRendering) {
        d->isRendering = false;
        Q_EMIT playbackFileChanged();
    }
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

int ClipAudioSource::id() const
{
    return d->id;
}

void ClipAudioSource::setId(int id)
{
    if (d->id != id) {
        d->id = id;
        Q_EMIT idChanged();
    }
}

QObject *ClipAudioSource::playbackPositions()
{
    return d->positionsModel;
}

ClipAudioSourcePositionsModel *ClipAudioSource::playbackPositionsModel()
{
    return d->positionsModel;
}

int ClipAudioSource::slices() const
{
    return d->slices;
}

void ClipAudioSource::setSlices(int slices)
{
    if (d->slices != slices) {
        if (slices == 0) {
            // Special casing clearing, because simple case, why not make it fast
            d->slicePositions.clear();
            Q_EMIT slicePositionsChanged();
        } else if (d->slices > slices) {
            // Just remove the slices that are too many
            while (d->slicePositions.length() > slices) {
                d->slicePositions.removeLast();
            }
            Q_EMIT slicePositionsChanged();
        } else if (d->slices < slices) {
            // Fit the new number of slices evenly into the available space
            double lastSlicePosition{0.0f};
            if (d->slicePositions.count() > 0) {
                lastSlicePosition = d->slicePositions.last().toDouble();
            }
            double positionIncrement{(1.0f - lastSlicePosition) / (slices - d->slices)};
            double newPosition{lastSlicePosition + positionIncrement};
            if (d->slicePositions.count() == 0) {
                d->slicePositions << QVariant::fromValue<double>(0.0f);
            }
            while (d->slicePositions.length() < slices) {
                d->slicePositions << QVariant::fromValue<double>(newPosition);
                newPosition += positionIncrement;
            }
            Q_EMIT slicePositionsChanged();
        }
        d->slices = slices;
        Q_EMIT slicesChanged();
    }
}

QVariantList ClipAudioSource::slicePositions() const
{
    return d->slicePositions;
}

void ClipAudioSource::setSlicePositions(const QVariantList &slicePositions)
{
    if (d->slicePositions != slicePositions) {
        d->slicePositions = slicePositions;
        Q_EMIT slicePositionsChanged();
        d->slices = slicePositions.length();
        Q_EMIT slicesChanged();
    }
}

double ClipAudioSource::slicePosition(int slice) const
{
    double position{0.0f};
    if (slice > -1 && slice < d->slicePositionsCache.length()) {
        position = d->slicePositionsCache[slice];
    }
    return position;
}

void ClipAudioSource::setSlicePosition(int slice, float position)
{
    if (slice > -1 && slice < d->slicePositions.length()) {
        d->slicePositions[slice] = position;
        Q_EMIT slicePositionsChanged();
    }
}

int ClipAudioSource::sliceBaseMidiNote() const
{
    return d->sliceBaseMidiNote;
}

void ClipAudioSource::setSliceBaseMidiNote(int sliceBaseMidiNote)
{
    if (d->sliceBaseMidiNote != sliceBaseMidiNote) {
        d->sliceBaseMidiNote = sliceBaseMidiNote;
        Q_EMIT sliceBaseMidiNoteChanged();
    }
}

int ClipAudioSource::sliceForMidiNote(int midiNote) const
{
    return ((d->slices - (d->sliceBaseMidiNote % d->slices)) + midiNote) % d->slices;
}

int ClipAudioSource::keyZoneStart() const
{
  return d->keyZoneStart;
}

void ClipAudioSource::setKeyZoneStart(int keyZoneStart)
{
  if (d->keyZoneStart != keyZoneStart) {
    d->keyZoneStart = keyZoneStart;
    Q_EMIT keyZoneStartChanged();
  }
}

int ClipAudioSource::keyZoneEnd() const
{
  return d->keyZoneEnd;
}

void ClipAudioSource::setKeyZoneEnd(int keyZoneEnd)
{
  if (d->keyZoneEnd != keyZoneEnd) {
    d->keyZoneEnd = keyZoneEnd;
    Q_EMIT keyZoneEndChanged();
  }
}

int ClipAudioSource::rootNote() const
{
  return d->rootNote;
}

void ClipAudioSource::setRootNote(int rootNote)
{
  if (d->rootNote != rootNote) {
    d->rootNote = rootNote;
    Q_EMIT rootNoteChanged();
  }
}

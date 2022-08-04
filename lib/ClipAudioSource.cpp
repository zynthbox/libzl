/*
  ==============================================================================

    ClipAudioSource.cpp
    Created: 9 Aug 2021 7:44:30pm
    Author:  root

  ==============================================================================
*/

#include "ClipAudioSource.h"
#include "ClipAudioSourcePositionsModel.h"

#include <QDateTime>
#include <QDebug>

#include <unistd.h>

#include "JUCEHeaders.h"
#include "../tracktion_engine/examples/common/Utilities.h"
#include "Helper.h"
#include "ClipCommand.h"
#include "SamplerSynth.h"
#include "SyncTimer.h"

#define DEBUG_CLIP true
#define IF_DEBUG_CLIP if (DEBUG_CLIP)

using namespace std;

class ClipAudioSource::Private : public juce::Timer {
public:
  Private(ClipAudioSource *qq) : q(qq) {}
  ClipAudioSource *q;
  const te::Engine &getEngine() const { return *engine; };
  te::WaveAudioClip::Ptr getClip() {
    if (edit) {
      if (auto track = Helper::getOrInsertAudioTrackAt(*edit, 0)) {
        if (auto clip = dynamic_cast<te::WaveAudioClip *>(track->getClips()[0])) {
          return *clip;
        }
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
  QString filePath;

  te::LevelMeasurer::Client levelClient;

  float startPositionInSeconds = 0;
  float lengthInSeconds = -1;
  float lengthInBeats = -1;
  float volumeAbsolute{-1.0f}; // This is a cached value
  float pitchChange = 0;
  float speedRatio = 1.0;
  float pan{0.0f};
  double currentLeveldB{-400.0};
  double prevLeveldB{-400.0};
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

  qint64 nextPositionUpdateTime{0};
  double firstPositionProgress{0};

  qint64 nextGainUpdateTime{0};
  void syncAudioLevel() {
    if (nextGainUpdateTime < QDateTime::currentMSecsSinceEpoch()) {
      prevLeveldB = currentLeveldB;

      currentLeveldB = qMax(Decibels::gainToDecibels(positionsModel->peakGain()), levelClient.getAndClearAudioLevel(0).dB);

      // Now we give the level bar fading charcteristics.
      // And, the below coversions, decibelsToGain and gainToDecibels,
      // take care of 0dB, which will never fade!...but a gain of 1.0 (0dB) will.

      const auto prevLevel{Decibels::decibelsToGain(prevLeveldB)};

      if (prevLeveldB > currentLeveldB)
        currentLeveldB = Decibels::gainToDecibels(prevLevel * 0.94);

      // Only notify when the value actually changes by some noticeable kind of amount
      if (abs(currentLeveldB - prevLeveldB) > 0.1) {
        // Because emitting from a thread that's not the object's own is a little dirty, so make sure it's done queued
        QMetaObject::invokeMethod(q, &ClipAudioSource::audioLevelChanged, Qt::QueuedConnection);
        if (audioLevelChangedCallback != nullptr) {
          audioLevelChangedCallback(currentLeveldB);
        }
      }
      nextGainUpdateTime = QDateTime::currentMSecsSinceEpoch() + 30;
    }
  }
private:
  void timerCallback() override;
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

  Helper::callFunctionOnMessageThread(
      [&]() {
        d->givenFile = juce::File(filepath);

        const File editFile = File::createTempFile("editFile");

        d->edit = te::createEmptyEdit(*d->engine, editFile);
        auto clip = Helper::loadAudioFileAsClip(*d->edit, d->givenFile);
        auto &transport = d->edit->getTransport();

        transport.ensureContextAllocated(true);

        d->fileName = d->givenFile.getFileName();
        d->filePath = QString::fromUtf8(filepath);
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
        d->startTimerHz(30);
      }, true);

  d->positionsModel = new ClipAudioSourcePositionsModel(this);
  d->positionsModel->moveToThread(QCoreApplication::instance()->thread());
  connect(d->positionsModel, &ClipAudioSourcePositionsModel::peakGainChanged, this, [&](){ d->syncAudioLevel(); });
  connect(d->positionsModel, &QAbstractItemModel::dataChanged, this, [&](const QModelIndex& topLeft, const QModelIndex& /*bottomRight*/, const QVector< int >& roles = QVector<int>()){
    if (topLeft.row() == 0 && roles.contains(ClipAudioSourcePositionsModel::PositionProgressRole)) {
      syncProgress();
    }
  });
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
  stop();
  SamplerSynth::instance()->unregisterClip(this);
  Helper::callFunctionOnMessageThread(
    [&]() {
      d->stopTimer();
      auto track = Helper::getOrInsertAudioTrackAt(*d->edit, 0);
      auto levelMeasurerPlugin = track->getLevelMeterPlugin();
      levelMeasurerPlugin->measurer.removeClient(d->levelClient);
      d->edit.reset();
    }, true);
}

void ClipAudioSource::setProgressCallback(void (*functionPtr)(float)) {
  d->progressChangedCallback = functionPtr;
}

void ClipAudioSource::syncProgress() {
  if (d->nextPositionUpdateTime < QDateTime::currentMSecsSinceEpoch()) {
    double newPosition = d->startPositionInSeconds / getDuration();
    if (d->progressChangedCallback != nullptr && d->positionsModel && d->positionsModel->rowCount(QModelIndex()) > 0) {
      newPosition = d->positionsModel->data(d->positionsModel->index(0), ClipAudioSourcePositionsModel::PositionProgressRole).toDouble();
    }
    if (abs(d->firstPositionProgress - newPosition) > 0.001) {
      d->firstPositionProgress = newPosition;
      Q_EMIT positionChanged();
      d->progressChangedCallback(d->firstPositionProgress * getDuration());
      /// TODO This really wants to be 16, so we can get to 60 updates per second, but that tears to all heck without compositing, so... for now
      // (tested with higher rates, but it tears, so while it looks like an arbitrary number, afraid it's as high as we can go)
      d->nextPositionUpdateTime = QDateTime::currentMSecsSinceEpoch() + 100; // 10 updates per second, this is loooow...
    }
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
  d->isRendering = true;
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
  d->isRendering = true;
}

void ClipAudioSource::setGain(float db) {
  if (auto clip = d->getClip()) {
    IF_DEBUG_CLIP cerr << "Setting gain : " << db;
    clip->setGainDB(db);
  }
  d->isRendering = true;
}

void ClipAudioSource::setVolume(float vol) {
  if (auto clip = d->getClip()) {
    IF_DEBUG_CLIP cerr << "Setting volume : " << vol << endl;
    // Knowing that -40 is our "be quiet now thanks" volume level, but tracktion thinks it should be -100, we'll just adjust that a bit
    // It means the last step is a bigger jump than perhaps desirable, but it'll still be more correct
    if (vol <= -40.0f) {
      clip->edit.setMasterVolumeSliderPos(0);
    } else {
      clip->edit.setMasterVolumeSliderPos(te::decibelsToVolumeFaderPosition(vol));
    }
    d->volumeAbsolute = clip->edit.getMasterVolumePlugin()->getSliderPos();
    Q_EMIT volumeAbsoluteChanged();
  }
}

void ClipAudioSource::setVolumeAbsolute(float vol)
{
  if (auto clip = d->getClip()) {
    IF_DEBUG_CLIP cerr << "Setting volume absolutely : " << vol << endl;
    clip->edit.setMasterVolumeSliderPos(qMax(0.0f, qMin(vol, 1.0f)));
    d->volumeAbsolute = clip->edit.getMasterVolumePlugin()->getSliderPos();
    Q_EMIT volumeAbsoluteChanged();
  }
}

float ClipAudioSource::volumeAbsolute() const
{
  if (d->volumeAbsolute < 0) {
    if (auto clip = d->getClip()) {
      d->volumeAbsolute = clip->edit.getMasterVolumePlugin()->getSliderPos();
    }
  }
  return d->volumeAbsolute;
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
  d->lengthInBeats = beat;
  updateTempoAndPitch();
}

float ClipAudioSource::getLengthInBeats() const
{
  return d->lengthInBeats;
}

float ClipAudioSource::getDuration() { return d->edit->getLength(); }

const char *ClipAudioSource::getFileName() const {
  return static_cast<const char *>(d->fileName.toUTF8());
}

const char *ClipAudioSource::getFilePath() const {
    return d->filePath.toUtf8();
}

tracktion_engine::AudioFile ClipAudioSource::getPlaybackFile() const {
    if (const auto& clip = d->getClip()) {
        return clip->getPlaybackFile();
    }
    return te::AudioFile(*d->engine);
}

void ClipAudioSource::updateTempoAndPitch() {
  if (auto clip = d->getClip()) {
    auto &transport = clip->edit.getTransport();

    IF_DEBUG_CLIP cerr << "Updating speedRatio(" << d->speedRatio << ") and pitch("
         << d->pitchChange << ")" << endl;

    clip->setSpeedRatio(d->speedRatio);
    clip->setPitchChange(d->pitchChange);

    IF_DEBUG_CLIP cerr << "Setting loop range : " << d->startPositionInSeconds << " to "
         << (d->startPositionInSeconds + d->lengthInSeconds) << endl;

    transport.setLoopRange(te::EditTimeRange::withStartAndLength(
        d->startPositionInSeconds, d->lengthInSeconds));
    transport.setCurrentPosition(transport.loopPoint1);
    syncProgress();
  }
}

void ClipAudioSource::Private::timerCallback() {
  syncAudioLevel();

  if (auto clip = getClip()) {
    if (!clip->needsRender() && isRendering) {
        isRendering = false;
        Q_EMIT q->playbackFileChanged();
    }
  }
}

void ClipAudioSource::play(bool loop, int midiChannel) {
  auto clip = d->getClip();
  IF_DEBUG_CLIP qDebug() << "libzl : Starting clip " << this << d->filePath << " which is really " << clip.get() << " in a " << (loop ? "looping" : "non-looping") << " manner from " << d->startPositionInSeconds << " and for " << d->lengthInSeconds << " seconds at volume " << (clip  && clip->edit.getMasterVolumePlugin().get() ? clip->edit.getMasterVolumePlugin()->volume : 0);

  ClipCommand *command = ClipCommand::trackCommand(this, midiChannel);
  command->midiNote = 60;
  command->changeVolume = true;
  command->volume = 1.0f;
  command->looping = loop;
  if (loop) {
    command->stopPlayback = true; // this stops any current loop plays, and immediately starts a new one
  }
  command->startPlayback = true;
  d->syncTimer->scheduleClipCommand(command, 0);
}

void ClipAudioSource::stop(int midiChannel) {
  IF_DEBUG_CLIP qDebug() << "libzl : Stopping clip " << this << " on channel" << midiChannel << " path: " << d->filePath;
  if (midiChannel > -3) {
    ClipCommand *command = ClipCommand::trackCommand(this, midiChannel);
    command->midiNote = 60;
    command->stopPlayback = true;
    d->syncTimer->scheduleClipCommand(command, 0);
  } else {
    ClipCommand *command = ClipCommand::noEffectCommand(this);
    command->stopPlayback = true;
    d->syncTimer->scheduleClipCommand(command, 0);
    // Less than the best thing - having to do this to ensure we stop the ones looper
    // queued for starting as well, otherwise they'll get missed for stopping... We'll
    // want to handle this more precisely later, but for now this should do the trick.
    command = ClipCommand::effectedCommand(this);
    command->stopPlayback = true;
    d->syncTimer->scheduleClipCommand(command, 0);
    for (int i = 0; i < 10; ++i) {
      command = ClipCommand::trackCommand(this, i);
      command->midiNote = 60;
      command->stopPlayback = true;
      d->syncTimer->scheduleClipCommand(command, 0);
    }
  }
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

float ClipAudioSource::audioLevel() const
{
  return d->currentLeveldB;
}

double ClipAudioSource::position() const
{
  return d->firstPositionProgress;
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

float ClipAudioSource::pan() {
    return d->pan;
}

void ClipAudioSource::setPan(float pan) {
  if (auto clip = d->getClip() and d->pan != pan) {
    IF_DEBUG_CLIP cerr << "Setting pan : " << pan;
    d->pan = pan;
    Q_EMIT panChanged();
  }
}

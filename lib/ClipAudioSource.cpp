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


class ClipProgress: public ValueTree::Listener
{
public:
    ClipProgress(ClipAudioSource *source)
        : ValueTree::Listener()
        , m_source(source)
    {}

    void valueTreePropertyChanged (ValueTree&, const juce::Identifier& i) override
    {
        if (i != juce::Identifier("position")) {
            return;
        }
        m_source->syncProgress();
    }

private:
    ClipAudioSource *m_source = nullptr;
};

ClipAudioSource::ClipAudioSource(SyncTimer* syncTimer, const char* filepath)
    : syncTimer(syncTimer)
{
  engine.getDeviceManager().initialise(0, 2);

  cerr << "Opening file : " << filepath << endl;

  juce::File file(filepath);

  const File editFile = File::createTempFile("editFile");

  edit = te::createEmptyEdit(engine, editFile);
  auto clip = Helper::loadAudioFileAsClip(*edit, file);
  auto& transport = edit->getTransport();

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
}

ClipAudioSource::~ClipAudioSource() {
  cerr << "Destroying Clip" << endl;
  stop();
  edit.reset();
}

void ClipAudioSource::setProgressCallback(void *obj, void (*functionPtr)(void *))
{
    zl_clip = obj;
    zl_progress_callback = functionPtr;
}

void ClipAudioSource::syncProgress()
{
    if (!zl_clip || !zl_progress_callback) {
        return;
    }

    zl_progress_callback(zl_clip);
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

void ClipAudioSource::setLength(float lengthInSeconds) {
  cerr << "Setting Length to " << lengthInSeconds << endl;
  this->lengthInSeconds = lengthInSeconds;
  updateTempoAndPitch();
}

float ClipAudioSource::getDuration() {
  //cerr << "Getting Duration : " << edit->getLength();
  return edit->getLength();
}

float ClipAudioSource::getProgress() const
{
    return edit->getTransport().getCurrentPosition();
}

const char* ClipAudioSource::getFileName() {
  return static_cast<const char*>(fileName.toUTF8());
}

void ClipAudioSource::updateTempoAndPitch() {
  if (auto clip = getClip()) {
    auto& transport = clip->edit.getTransport();
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
    if (auto clip = dynamic_cast<te::WaveAudioClip*>(track->getClips()[0]))
      return *clip;

  return {};
}

void ClipAudioSource::play(bool loop) {
  auto clip = getClip();
  if (!clip) {
      return;
  }

  auto& transport = getClip()->edit.getTransport();

  transport.stop(false, false);
  transport.setCurrentPosition(transport.loopPoint1);

  if (loop) {
      transport.play(false);
  } else {
      transport.playSectionAndReset(te::EditTimeRange::withStartAndLength(0.0f, lengthInSeconds));
  }
}

void ClipAudioSource::stop() {
  cerr << "libzl : Stopping clip " << this << endl;

  edit->getTransport().stop(false, false);
}

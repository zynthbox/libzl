#include "SyncTimer.h"
#include "ClipAudioSource.h"

#include "JUCEHeaders.h"

using namespace std;
using namespace juce;

class SyncTimer::Private : public HighResolutionTimer {
public:
  Private() : HighResolutionTimer() {}
  ~Private() override {}
  int playingClipsCount = 0;
  int beat = 0;
  int bpm = 0;
  int multiplier;
  QList<void (*)(int)> callbacks;
  QQueue<ClipAudioSource *> clipsStartQueue;
  QQueue<ClipAudioSource *> clipsStopQueue;

  void hiResTimerCallback() override {
    if (beat == 0) {
        playingClipsCount = playingClipsCount - clipsStopQueue.size();
        while (!clipsStopQueue.isEmpty()) {
          clipsStopQueue.dequeue()->stop();
        }

        playingClipsCount = playingClipsCount + clipsStartQueue.size();
        while (!clipsStartQueue.isEmpty()) {
          clipsStartQueue.dequeue()->play();
        }
    }

    for (auto cb : callbacks) {
        cb(beat);
    }

    beat = (beat + 1) % (multiplier * 4);
  }
};

SyncTimer::SyncTimer(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->multiplier = 32;
}

void SyncTimer::addCallback(void (*functionPtr)(int)) {
  cerr << "Adding callback " << functionPtr;
  d->callbacks.append(functionPtr);
}

void SyncTimer::removeCallback(void (*functionPtr)(int)) {
  bool result = d->callbacks.removeOne(functionPtr);
  cerr << "Removing callback " << functionPtr << " : " << result;
}

void SyncTimer::queueClipToStart(ClipAudioSource *clip) {
  for (ClipAudioSource *c : d->clipsStopQueue) {
    if (c == clip) {
      cerr << "Found clip(" << c << ") in stop queue. Removing from stop queue"
           << endl;
      d->clipsStopQueue.removeOne(c);
    }
  }
  d->clipsStartQueue.enqueue(clip);
}

void SyncTimer::queueClipToStop(ClipAudioSource *clip) {
  for (ClipAudioSource *c : d->clipsStartQueue) {
    if (c == clip) {
      cerr << "Found clip(" << c
           << ") in start queue. Removing from start queue" << endl;
      d->clipsStartQueue.removeOne(c);
    }
  }
  d->clipsStopQueue.enqueue(clip);
}

void SyncTimer::start(int bpm) {
  cerr << "#### Starting timer with bpm " << bpm << " and interval "
       << getInterval(bpm) << endl;
  d->startTimer(getInterval(bpm));
  Q_EMIT timerRunningChanged();
}

void SyncTimer::stop() {
  cerr << "#### Stopping timer" << endl;

  if(d->isTimerRunning()) {
    d->stopTimer();
    Q_EMIT timerRunningChanged();
  }
  d->beat = 0;
}

int SyncTimer::getInterval(int bpm) {
  // Calculate interval
  return 60000 / (bpm * d->multiplier);
}

int SyncTimer::getMultiplier() {
  return d->multiplier;
}

bool SyncTimer::timerRunning() {
    return d->isTimerRunning();
}

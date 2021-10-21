#include "SyncTimer.h"
#include "ClipAudioSource.h"

#include "JUCEHeaders.h"

#include <QMutex>
#include <QMutexLocker>
#include <RtMidi.h>

using namespace std;
using namespace juce;

class SyncTimer::Private : public HighResolutionTimer {
public:
    Private() : HighResolutionTimer() {
      // RtMidiOut constructor
      try {
        midiout = new RtMidiOut();
      }
      catch ( RtMidiError &error ) {
        error.printMessage();
        midiout = nullptr;
      }
      if (midiout) {
        // Check outputs.
        unsigned int nPorts = midiout->getPortCount();
        std::string portName;
        std::cout << "\nThere are " << nPorts << " MIDI output ports available.\n";
        for (unsigned int i = 0; i < nPorts; ++i) {
          try {
            portName = midiout->getPortName(i);
            if (portName.rfind("Midi Through", 0) == 0) {
              std::cout << "Using output port " << i << " named " << portName << endl;
              midiout->openPort(i);
              break;
            }
          }
          catch (RtMidiError &error) {
            error.printMessage();
            delete midiout;
          }
        }
      }
    }
  ~Private() override {}
  int playingClipsCount = 0;
  int beat = 0;
  int bpm = 0;
  int multiplier;
  QList<void (*)(int)> callbacks;
  QQueue<ClipAudioSource *> clipsStartQueue;
  QQueue<ClipAudioSource *> clipsStopQueue;

  QList<std::vector<unsigned char> > offNotes;
  QList<std::vector<unsigned char> > onNotes;
  RtMidiOut *midiout{nullptr};

  QMutex mutex;
  int i{0};
  void hiResTimerCallback() override {
    QMutexLocker locker(&mutex);
    // Stop things that want stopping
    if (midiout) {
      for (const std::vector<unsigned char> &offNote : qAsConst(offNotes)) {
        midiout->sendMessage(&offNote);
      }
    }
    if (beat == 0) {
      for (ClipAudioSource *clip : clipsStopQueue) {
        clip->stop();
      }
    }

    // Now play things which want playing
    if (midiout) {
      for (const std::vector<unsigned char> &onNote : qAsConst(onNotes)) {
        midiout->sendMessage(&onNote);
      }
    }
    if (beat == 0) {
      for (ClipAudioSource *clip : clipsStartQueue) {
        clip->play();
      }
    }

    // Now that we're done doing performance intensive things, we can clean up
    offNotes.clear();
    onNotes.clear();
    if (beat == 0) {
      clipsStopQueue.clear();
      clipsStartQueue.clear();
    }

    // Since we're likely to be doing things in the callbacks which schedule stuff to be
    // done, and we are done with the lists and queues, unlock the locked stuff
    locker.unlock();

    // Logically, we consider these low-priority (if you need high precision output, things should be scheduled for next beat)
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
  QMutexLocker locker(&d->mutex);
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
  QMutexLocker locker(&d->mutex);
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
  if (d->midiout) {
      std::vector<unsigned char> message;
      message.push_back(0x7B);
      message.push_back(0);
      d->midiout->sendMessage(&message);
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

int SyncTimer::beat() const {
  return d->beat;
}

void SyncTimer::scheduleNote(unsigned char midiNote, unsigned char midiChannel, bool setOn, unsigned char velocity, int duration, int delay)
{
  // Not using this one yet... but we shall!
  Q_UNUSED(delay)
  Q_UNUSED(duration)
  std::vector<unsigned char> note;
  if (setOn) {
    note.push_back(0x90 + midiChannel);
  } else {
    note.push_back(0x80 + midiChannel);
  }
  note.push_back(midiNote);
  note.push_back(velocity);
  QMutexLocker locker(&d->mutex);
  if (setOn) {
    d->onNotes.append(note);
  } else {
    d->offNotes.append(note);
  }
}

bool SyncTimer::timerRunning() {
  return d->isTimerRunning();
}

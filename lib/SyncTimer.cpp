#include "SyncTimer.h"
#include "ClipAudioSource.h"
#include "libzl.h"
#include "Helper.h"

#include <QDebug>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <RtMidi.h>

#include "JUCEHeaders.h"

using namespace std;
using namespace juce;

using frame_clock = std::conditional_t<
    std::chrono::high_resolution_clock::is_steady,
    std::chrono::high_resolution_clock,
    std::chrono::steady_clock>;

#define NanosecondsPerMinute 60000000000
#define BeatSubdivisions 32
class SyncTimerThread : public QThread {
    Q_OBJECT
public:
    SyncTimerThread(SyncTimer *q)
        : QThread(q)
    {}

    void waitTill(frame_clock::time_point till) {
        //spinTimeMs is used to adjust for scheduler inaccuracies. default is 2.1 milliseconds. anything lower makes fps jump around
        auto waitTime = std::chrono::duration_cast<std::chrono::microseconds>(till - frame_clock::now() - spinTime);
        if (waitTime.count() > 0) { //only sleep if waitTime is positive
            usleep((long unsigned int)waitTime.count());
        } else {
            // overrun situation this is bad, we should tell someone!
            qWarning() << "The playback synchronisation timer had a falling out with reality and ended up asked to wait for a time in the past. This is not awesome, so now we make it even slower by outputting this message complaining about it.";
        }
        while (till > frame_clock::now()) {
            //spin till actual timepoint
        }
    }

    void run() override {
        auto start = frame_clock::now();
        quint64 count{0};
        quint64 minuteCount{0};
        std::chrono::nanoseconds nanosecondsPerMinute{NanosecondsPerMinute};
        qDebug() << "Starting Sync Timer";
        while (true) {
            if (aborted) {
                break;
            }
            auto nextMinute = start + ((minuteCount + 1) * nanosecondsPerMinute);
            qDebug() << "Sync timer reached minute:" << minuteCount << "with interval" << interval.count();
            while (nextMinute > frame_clock::now()) {
                if (aborted) {
                    break;
                }
                Q_EMIT timeout(); // Do the thing!
                ++count;
                waitTill(start + (nanosecondsPerMinute * minuteCount) + (interval * count));
            }
            qDebug() << "Reached" << count << "ticks in this minute, and we should have" << bpm * BeatSubdivisions;
            count = 0; // Reset the count each minute
            ++minuteCount;
        }
    }

    Q_SIGNAL void timeout();

    void setBPM(quint64 bpm) {
        this->bpm = bpm;
        interval = chrono::high_resolution_clock::duration(subbeatCountToNanoseconds(bpm, 1));
    }
    const quint64 getBpm() const {
        return bpm;
    }

    quint64 subbeatCountToNanoseconds(const quint64 &bpm, const quint64 &subBeatCount) const
    {
        const quint64 nanosecondsPerBeat = NanosecondsPerMinute / (bpm * BeatSubdivisions);
        return nanosecondsPerBeat * subBeatCount;
    };
    void requestAbort() {
        aborted = true;
    }
private:
    quint64 bpm{0};
    std::chrono::nanoseconds interval;
    bool aborted{false};

    // This is equivalent to .1 ms
    const frame_clock::duration spinTime{frame_clock::duration(100000)};
};

class SyncTimer::Private {
public:
    Private(SyncTimer *q)
        : timerThread(new SyncTimerThread(q))
    {
        // Dangerzone - direct connection from another thread. Yes, dangerous, but also we need the precision, so we need to dill whit it
        QObject::connect(timerThread, &SyncTimerThread::timeout, q, [this](){ hiResTimerCallback(); }, Qt::DirectConnection);
        QObject::connect(timerThread, &QThread::started, q, [q](){ Q_EMIT q->timerRunningChanged(); });
        QObject::connect(timerThread, &QThread::finished, q, [q](){ Q_EMIT q->timerRunningChanged(); });
    }
    ~Private() {}
    SyncTimerThread *timerThread;
    int playingClipsCount = 0;
    int beat = 0;
    QList<void (*)(int)> callbacks;
    QQueue<ClipAudioSource *> clipsStartQueue;
    QQueue<ClipAudioSource *> clipsStopQueue;

    quint64 cumulativeBeat = 0;
    QHash<quint64, QList<std::vector<unsigned char> > > offQueue;
    QHash<quint64, QList<std::vector<unsigned char> > > onQueue;
    QList<std::vector<unsigned char> > offNotes;
    QList<std::vector<unsigned char> > onNotes;
    RtMidiOut *midiout{nullptr};

    ClipAudioSource* clip{nullptr};
    std::unique_ptr<te::Edit> edit;

    QMutex mutex;
    int i{0};
    void hiResTimerCallback() {
        QMutexLocker locker(&mutex);
        /// =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
        ///      Performance Intensive Stuff Goes Below Here
        /// avoid allocations, list changes, etc if at all possible
        /// =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
        /// {

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

        /// }
        /// =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
        ///      Performance Intensive Stuff Goes Above Here
        /// avoid allocations, list changes, etc if at all possible
        /// =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

        // Now that we're done doing performance intensive things, we can clean up
        if (beat == 0) {
            clipsStopQueue.clear();
            clipsStartQueue.clear();
        }

        // Logically, we consider these low-priority (if you need high precision output, things should be scheduled for next beat)
        for (auto cb : callbacks) {
        cb(beat);
        }

        beat = (beat + 1) % (BeatSubdivisions * 4);
        ++cumulativeBeat;

        // Finally, queue up the next lot of notes by taking the next beat positions from
        // the queues (or the default constructed value returned by take)
        // (this also is why we're not clearing them above, no need)
        onNotes = onQueue.take(cumulativeBeat);
        offNotes = offQueue.take(cumulativeBeat);

        // Sync tracktion's position with out own every 16 ticks
        if (cumulativeBeat % 16 == 0) {
            auto &engine = clip->getEngine();
            auto &activeEdits = engine.getActiveEdits();
            for (auto *edit : activeEdits.getEdits()) {
                auto& transport = edit->getTransport();
                if (auto playhead = transport.getCurrentPlayhead()) {
                    // N.B. Because we don't have full tempo sequence info from the host, we have
                    // to assume that the tempo is constant and just sync to that
                    // We could sync to a single bar by subtracting the ppqPositionOfLastBarStart from ppqPosition here
                    const double timeOffset = cumulativeBeat * (NanosecondsPerMinute / timerThread->getBpm()) / (float)1000000000;
                    const double blockSizeInSeconds = edit->engine.getDeviceManager().getBlockSizeMs() / 1000.0;
                    const double currentPositionInSeconds = playhead->getPosition() + blockSizeInSeconds;
                    if (std::abs (timeOffset - currentPositionInSeconds) > (blockSizeInSeconds / 2.0)) {
                        qWarning() << "Overriding playhead position from, changing from" << currentPositionInSeconds << "to" << timeOffset;
                        playhead->overridePosition(timeOffset);
                    } else {
                        qWarning() << "Within tolerances, we're good, keep going!" << currentPositionInSeconds << "when we should have" << timeOffset;
                    }
                    break;
                } else {
                    qWarning() << "Ow, no playhead...";
                }
            }
        }
    }
};

SyncTimer::SyncTimer(QObject *parent)
    : QObject(parent)
    , d(new Private(this))
{
}

void SyncTimer::addCallback(void (*functionPtr)(int)) {
    cerr << "Adding callback " << functionPtr << endl;
    d->callbacks.append(functionPtr);
}

void SyncTimer::removeCallback(void (*functionPtr)(int)) {
    bool result = d->callbacks.removeOne(functionPtr);
    cerr << "Removing callback " << functionPtr << " : " << result << endl;
}

void SyncTimer::queueClipToStart(ClipAudioSource *clip) {
    QMutexLocker locker(&d->mutex);
    for (ClipAudioSource *c : d->clipsStopQueue) {
        if (c == clip) {
            qWarning() << "Found clip(" << c << ") in stop queue. Removing from stop queue";
            d->clipsStopQueue.removeOne(c);
        }
    }
    d->clipsStartQueue.enqueue(clip);
}

void SyncTimer::queueClipToStop(ClipAudioSource *clip) {
    QMutexLocker locker(&d->mutex);
    for (ClipAudioSource *c : d->clipsStartQueue) {
        if (c == clip) {
            qWarning() << "Found clip(" << c << ") in start queue. Removing from start queue";
            d->clipsStartQueue.removeOne(c);
        }
    }
    d->clipsStopQueue.enqueue(clip);
}

void SyncTimer::start(int bpm) {
    qDebug() << "#### Starting timer with bpm " << bpm << " and interval " << getInterval(bpm);
    if (!d->midiout) {
        // RtMidiOut constructor - can't stick this in the pimpl ctor, as the output we need won't be ready yet at that time
        try {
            d->midiout = new RtMidiOut(RtMidi::UNIX_JACK);
        }
        catch ( RtMidiError &error ) {
            error.printMessage();
            d->midiout = nullptr;
        }
        if (d->midiout) {
        // Check outputs.
        unsigned int nPorts = d->midiout->getPortCount();
        std::string portName;
        std::cout << "\nThere are " << nPorts << " MIDI output ports available.\n";
            for (unsigned int i = 0; i < nPorts; ++i) {
                try {
                    portName = d->midiout->getPortName(i);
                    if (portName.rfind("ZynMidiRouter:seq", 0) == 0) {
                        std::cout << "Using output port " << i << " named " << portName << endl;
                        d->midiout->openPort(i);
                        break;
                    }
                }
                catch (RtMidiError &error) {
                    error.printMessage();
                    delete d->midiout;
                }
            }
        }
    }
    if (!d->clip) {
        d->clip = ClipAudioSource_new("a wholly empty bit of something just used to get the play-head");
    }
    // If we've got any notes queued for beat 0, grab those out of the queue
    if (d->onQueue.contains(0)) {
        d->onNotes = d->onQueue.take(0);
    }
    if (d->offQueue.contains(0)) {
        d->offNotes = d->offQueue.take(0);
    }
    d->timerThread->setBPM(quint64(bpm));
    d->timerThread->start();
    Q_EMIT timerRunningChanged();
}

void SyncTimer::stop() {
    cerr << "#### Stopping timer" << endl;

    if(d->timerThread->isRunning()) {
        d->timerThread->requestAbort();
        d->timerThread->wait();
        Q_EMIT timerRunningChanged();
    }
    if (d->midiout) {
        for (const std::vector<unsigned char> &offNote : qAsConst(d->offNotes)) {
            d->midiout->sendMessage(&offNote);
        }
        for (const auto &offNotes : qAsConst(d->offQueue)) {
            for (const std::vector<unsigned char> &offNote : offNotes) {
                d->midiout->sendMessage(&offNote);
            }
        }
    }
    d->beat = 0;
    d->cumulativeBeat = 0;
    d->onQueue.clear();
    d->onNotes.clear();
    d->offQueue.clear();
    d->offNotes.clear();
}

int SyncTimer::getInterval(int bpm) {
    // Calculate interval
    return 60000 / (bpm * BeatSubdivisions);
}

float SyncTimer::subbeatCountToSeconds(quint64 bpm, quint64 beats) const
{
    return d->timerThread->subbeatCountToNanoseconds(bpm, beats) / (float)1000000000;
}

int SyncTimer::getMultiplier() {
    return BeatSubdivisions;
}

int SyncTimer::beat() const {
    return d->beat;
}

quint64 SyncTimer::cumulativeBeat() const {
    return d->cumulativeBeat;
}

void SyncTimer::scheduleNote(unsigned char midiNote, unsigned char midiChannel, bool setOn, unsigned char velocity, quint64 duration, quint64 delay)
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
    if (setOn) {
        if (d->onQueue.contains(d->cumulativeBeat + delay)) {
            d->onQueue[d->cumulativeBeat + delay].append(note);
        } else {
            QList<std::vector<unsigned char> > list;
            list.append(note);
            d->onQueue[d->cumulativeBeat + delay] = list;
        }
    } else {
        if (d->offQueue.contains(d->cumulativeBeat + delay)) {
            d->offQueue[d->cumulativeBeat + delay].append(note);
        } else {
            QList<std::vector<unsigned char> > list;
            list.append(note);
            d->offQueue[d->cumulativeBeat + delay] = list;
        }
    }
    if (setOn && duration > 0) {
        // Schedule an off note for that position
        scheduleNote(midiNote, midiChannel, false, 64, 0, delay + duration);
    }
}

bool SyncTimer::timerRunning() {
    return d->timerThread->isRunning();
}

#include "SyncTimer.moc"

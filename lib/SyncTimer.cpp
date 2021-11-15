#include "SyncTimer.h"
#include "ClipAudioSource.h"
#include "libzl.h"
#include "Helper.h"

#include <QDebug>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>
#include <RtMidi.h>

#include "JUCEHeaders.h"

using namespace std;
using namespace juce;

using frame_clock = std::conditional_t<
    std::chrono::high_resolution_clock::is_steady,
    std::chrono::high_resolution_clock,
    std::chrono::steady_clock>;

#define NanosecondsPerMinute 60000000000
#define NanosecondsPerSecond 1000000000
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
        std::chrono::time_point< std::chrono::_V2::steady_clock, std::chrono::duration< long long unsigned int, std::ratio< 1, NanosecondsPerSecond > > > nextMinute;
        qDebug() << "Starting Sync Timer";
        while (true) {
            if (aborted) {
                break;
            }
            nextMinute = start + ((minuteCount + 1) * nanosecondsPerMinute);
            qDebug() << "Sync timer reached minute:" << minuteCount << "with interval" << interval.count();
            while (nextMinute > frame_clock::now()) {
                mutex.lock();
                if (paused)
                {
                    qDebug() << "Out sync timer thread is paused, let's wait...";
                    waitCondition.wait(&mutex);
                    qDebug() << "Unpaused, let's goooo!";
                    count = 0;
                    minuteCount = 0;
                    start = frame_clock::now();
                    nextMinute = start + nanosecondsPerMinute;
                }
                mutex.unlock();
                if (aborted) {
                    break;
                }
                Q_EMIT timeout(); // Do the thing!
                ++count;
                waitTill(frame_clock::duration(adjustment) + start + (nanosecondsPerMinute * minuteCount) + (interval * count));
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

    Q_SLOT void pause() { setPaused(true); }
    Q_SLOT void resume() { setPaused(false); }
    bool isPaused() const {
        return paused;
    }
    void setPaused(bool shouldPause) {
        qDebug() << "Attempting to set paused state to" << shouldPause;
        mutex.lock();
        qDebug() << "Locked mutex";
        paused=shouldPause;
        if (!paused)
            waitCondition.wakeAll();
        mutex.unlock();
        Q_EMIT pausedChanged();
    }
    Q_SIGNAL void pausedChanged();

    void addAdjustmentBySeconds(double seconds) {
        mutex.lock();
        adjustment += (NanosecondsPerSecond * seconds);
        mutex.unlock();
    }
private:
    qint64 adjustment{0};

    quint64 bpm{0};
    std::chrono::nanoseconds interval;
    bool aborted{false};

    bool paused{true};
    QMutex mutex;
    QWaitCondition waitCondition;

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
        QObject::connect(timerThread, &SyncTimerThread::pausedChanged, q, [q](){ q->timerRunningChanged(); });
        timerThread->start(QThread::TimeCriticalPriority);
    }
    ~Private() {
        timerThread->requestAbort();
        timerThread->wait();
    }
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

        // Sync tracktion's position with out own every 64 ticks, offset by 17 from the start for a bit of ease of tracking
        if (beat == 17 || beat == 81) {
            if (auto actualClip = clip->getClip()) {
                auto& transport = actualClip->edit.getTransport();
                if (auto playhead = transport.getCurrentPlayhead()) {
                    // N.B. Because we don't have full tempo sequence info from the host, we have
                    // to assume that the tempo is constant and just sync to that
                    // We could sync to a single bar by subtracting the ppqPositionOfLastBarStart from ppqPosition here
                    const double timeOffset = (beat * (NanosecondsPerMinute / (double)(BeatSubdivisions * timerThread->getBpm()))) / (double)NanosecondsPerSecond;
                    const double currentPositionInSeconds = playhead->getPosition();
//                     qDebug() << "Clip is currently at position" << currentPositionInSeconds << "the clip things its duration is" << clip->getDuration() << "and we think we should be at" << timeOffset;
                    static const double acceptableDeviation = 0.1f;
                    if (std::abs (timeOffset - currentPositionInSeconds) > acceptableDeviation) {
                        qWarning() << "Adjusting playback position, deviation was at" << timeOffset - currentPositionInSeconds << " changing from" << currentPositionInSeconds << "to" << timeOffset;
                        timerThread->addAdjustmentBySeconds(timeOffset - currentPositionInSeconds);
                    } else {
                        qWarning() << "Within tolerances, we're good, keep going! Deviation is at" << timeOffset - currentPositionInSeconds;
                    }
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
    d->timerThread->setBPM(quint64(bpm));
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
        // Get literally any thing and then set muted to true so we are running but not outputting the sound
        d->clip = ClipAudioSource_new("/zynthian/zynthian-ui/zynqtgui/zynthiloops/assets/click_track_4-4.wav", true);
        d->clip->setLength(4, d->timerThread->getBpm());
    }
    d->clip->play(true);
    // If we've got any notes queued for beat 0, grab those out of the queue
    if (d->onQueue.contains(0)) {
        d->onNotes = d->onQueue.take(0);
    }
    if (d->offQueue.contains(0)) {
        d->offNotes = d->offQueue.take(0);
    }
    d->timerThread->resume();
}

void SyncTimer::stop() {
    cerr << "#### Stopping timer" << endl;

    if(!d->timerThread->isPaused()) {
        d->timerThread->pause();
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
    d->clip->stop();
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
    return !d->timerThread->isPaused();
}

#include "SyncTimer.moc"

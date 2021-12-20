#include <sched.h>
#include "SyncTimer.h"
#include "ClipAudioSource.h"
#include "libzl.h"
#include "Helper.h"

#include <QDebug>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include "QProcess"
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

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
            // qWarning() << "The playback synchronisation timer had a falling out with reality and ended up asked to wait for a time in the past. This is not awesome, so now we make it even slower by outputting this message complaining about it.";
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
        while (true) {
            if (aborted) {
                break;
            }
            nextMinute = start + ((minuteCount + 1) * nanosecondsPerMinute);
            while (count < bpm * BeatSubdivisions) {
                mutex.lock();
                if (paused)
                {
                    qDebug() << "SyncTimer thread is paused, let's wait...";
                    waitCondition.wait(&mutex);
                    qDebug() << "Unpaused, let's goooo!";

                    // Set thread policy to SCHED_FIFO with maximum possible priority
                    struct sched_param param;
                    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
                    sched_setscheduler(0, SCHED_FIFO, &param);

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
            qDebug() << "Sync timer reached minute:" << minuteCount << "with interval" << interval.count();
            qDebug() << "The most recent pseudo-minute took an extra" << (frame_clock::now() - nextMinute).count() << "nanoseconds";
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
        mutex.lock();
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

    quint64 bpm{120};
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
        timerThread->start();

        midiBridge = new QProcess(q);
        // Using the https://github.com/free-creations/a2jmidi tool
        // NB: Note the difference between this and a2jmidid! (this is considerably lower latency, which seems pretty important)
        midiBridge->setProgram("a2jmidi");
        // We are creating a new output in jack called SyncTimer, based on any Alsa midi out called SyncTimer Out.
        // a2jmidi will sit quietly in the background until such time that device is created, at which time it will
        // start listening and push the events through to jack.
        midiBridge->setArguments({"--name", "SyncTimer", "--connect", "SyncTimer Out"});
        connect(midiBridge, &QProcess::started, q, [](){
            qDebug() << "SyncTimer Midi bridge started - now hook it up to ZynMidiRouter";
            // Connect our SyncTimer:SyncTimer jack output to where ZynMidiRouter expects step sequencer input to arrive
            // This has to happen a little delayed, just to be sure we don't attempt to connect a partially-created port
            QTimer::singleShot(1000, [](){
                QProcess::startDetached("jack_connect", {"SyncTimer:SyncTimer", "ZynMidiRouter:step_in"});
            });
        });
        connect(midiBridge, &QProcess::errorOccurred, q, [](QProcess::ProcessError error){
            qDebug() << "SyncTimer Midi bridge had an error" << error;
        });
//         connect(midiBridge, &QProcess::stateChanged, q, [](QProcess::ProcessState state){
//             qDebug() << "SyncTimer Midi bridge state changed:" << state;
//         });
//         connect(midiBridge, &QProcess::readyReadStandardError, q, [this](){
//             qDebug() << "SyncTimer Midi bridge errored:" << midiBridge->readAllStandardError();
//         });
//         connect(midiBridge, &QProcess::readyReadStandardOutput, q, [this](){
//             qDebug() << "SyncTimer Midi bridge said:" << midiBridge->readAllStandardOutput();
//         });
    }
    ~Private() {
        timerThread->requestAbort();
        timerThread->wait();
        midiBridge->terminate();
        if (!midiBridge->waitForFinished(2000)) {
            // Let's not hang around too long...
            midiBridge->kill();
        }
    }
    SyncTimerThread *timerThread;
    QProcess *midiBridge;
    int playingClipsCount = 0;
    int beat = 0;
    QList<void (*)(int)> callbacks;
    QQueue<ClipAudioSource *> clipsStartQueue;
    QQueue<ClipAudioSource *> clipsStopQueue;

    quint64 cumulativeBeat = 0;
    QHash<quint64, MidiBuffer> midiMessageQueues;
    MidiBuffer nextMidiMessages;
    std::unique_ptr<MidiOutput> juceMidiOut{nullptr};

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

        if (juceMidiOut) {
            juceMidiOut->sendBlockOfMessagesNow(nextMidiMessages);
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
//            clipsStopQueue.clear();
            clipsStartQueue.clear();
        }

        // Logically, we consider these low-priority (if you need high precision output, things should be scheduled for next beat)
        for (auto cb : callbacks) {
            cb(beat);
        }

        // Sync our position with tracktion's every 64 ticks, offset by 17 from the start for a bit of ease of tracking
        // We do this because tracktion will slide a bit in a fairly regular manner (buffer glitches, that sort of thing),
        // and since SyncTimerThread is a real-time-aligned timer, we need to adjust it to match tracktion, or we end up
        // out of sync, which would make the name of our timer a lie.
        if (beat == 17 || beat == 81) {
            if (clip) {
                if (auto actualClip = clip->getClip()) {
                    auto& transport = actualClip->edit.getTransport();
                    if (auto playhead = transport.getCurrentPlayhead()) {
                        const double timeOffset = (beat * (NanosecondsPerMinute / (double)(BeatSubdivisions * timerThread->getBpm()))) / (double)NanosecondsPerSecond;
                        // qDebug() << "Clip thinks its duration is" << clip->getDuration();
                        // qDebug() << "Adjusting playback position, deviation was at" << timeOffset - playhead->getPosition() << " changing from" << playhead->getPosition() << "to" << timeOffset;
                        timerThread->addAdjustmentBySeconds(timeOffset - playhead->getPosition());
                    } else {
                        qWarning() << "Ow, no playhead...";
                    }
                }
            }
        }

        beat = (beat + 1) % (BeatSubdivisions * 4);
        ++cumulativeBeat;

        // Finally, queue up the next lot of notes by taking the next beat positions from
        // the queue (or the default constructed value returned by take)
        // (this also is why we're not clearing them above, no need)
        nextMidiMessages = midiMessageQueues.take(cumulativeBeat);
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
    if (!d->juceMidiOut) {
        QTimer::singleShot(1, this, [this](){
            qDebug() << "Attempting to create a special midi out for SyncTimer";
            d->juceMidiOut = MidiOutput::createNewDevice("SyncTimer Out");
            if (!d->juceMidiOut) {
                qWarning() << "Failed to create SyncTimer's Juce-based midi-out";
            } else {
                // Start our midi bridge - we could do it earlier, but it also needs
                // to happen /after/ ZynMidiRouter is initialised, and so we might as
                // well wait until now to do it.
                d->midiBridge->start();
            }
            if (!d->clip) {
                // Get literally any thing and then set muted to true so we are running but not outputting the sound
                d->clip = ClipAudioSource_new("/zynthian/zynthian-ui/zynqtgui/zynthiloops/assets/click_track_4-4.wav", true);
            }
        });
    }
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
    if (d->clip) {
        d->clip->setLength(4, d->timerThread->getBpm());
        d->clip->play(true);
    } else {
        qWarning() << "We do not have a Juce audio clip to sync against - the timer synchronisation will not work!";
    }
    // If we've got any notes queued for beat 0, grab those out of the queue
    if (d->midiMessageQueues.contains(0)) {
        d->nextMidiMessages = d->midiMessageQueues.take(0);
    }
    d->timerThread->resume();
}

void SyncTimer::stop() {
    cerr << "#### Stopping timer" << endl;

    if(!d->timerThread->isPaused()) {
        d->timerThread->pause();
    }

    for (ClipAudioSource *clip : d->clipsStopQueue) {
        clip->stop();
    }

    if (d->juceMidiOut) {
        for (const auto &message : qAsConst(d->nextMidiMessages)) {
            // We have designated position 0 as off notes, so turn all those off
            if (message.samplePosition == 0) {
                d->juceMidiOut->sendMessageNow(message.getMessage());
            }
        }
        for (const auto &messages : qAsConst(d->midiMessageQueues)) {
            for (const auto &message : messages) {
                // We have designated position 0 as off notes, so turn all those off
                if (message.samplePosition == 0) {
                    d->juceMidiOut->sendMessageNow(message.getMessage());
                }
            }
        }
    }

    // Stop all clips immediately when timer is stopped
    if (d->clip) {
        d->clip->stop();
    }

    d->beat = 0;
    d->cumulativeBeat = 0;
    d->midiMessageQueues.clear();
    d->nextMidiMessages.clear();
    d->clipsStopQueue.clear();
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
    unsigned char note[3];
    if (setOn) {
        note[0] = 0x90 + midiChannel;
    } else {
        note[0] = 0x80 + midiChannel;
    }
    note[1] = midiNote;
    note[2] = velocity;
    const int onOrOff = setOn ? 1 : 0;
    if (d->midiMessageQueues.contains(d->cumulativeBeat + delay)) {
        d->midiMessageQueues[d->cumulativeBeat + delay].addEvent(note, 3, onOrOff);
    } else {
        MidiBuffer buffer;
        buffer.addEvent(note, 3, onOrOff);
        d->midiMessageQueues[d->cumulativeBeat + delay] = buffer;
    }
    if (setOn && duration > 0) {
        // Schedule an off note for that position
        scheduleNote(midiNote, midiChannel, false, 64, 0, delay + duration);
    }
}

void SyncTimer::scheduleMidiBuffer(const juce::MidiBuffer& buffer, quint64 delay)
{
    if (d->midiMessageQueues.contains(d->cumulativeBeat + delay)) {
        d->midiMessageQueues[d->cumulativeBeat + delay].addEvents(buffer, 0, -1, 0);
    } else {
        d->midiMessageQueues[d->cumulativeBeat + delay] = buffer;
    }
}

void SyncTimer::sendNoteImmediately(unsigned char midiNote, unsigned char midiChannel, bool setOn, unsigned char velocity)
{
    if (d->juceMidiOut) {
        if (setOn) {
            d->juceMidiOut->sendMessageNow(juce::MidiMessage::noteOn(midiChannel + 1, midiNote, juce::uint8(velocity)));
        } else {
            d->juceMidiOut->sendMessageNow(juce::MidiMessage::noteOff(midiChannel + 1, midiNote));
        }
    }
}

void SyncTimer::sendMidiBufferImmediately(const juce::MidiBuffer& buffer)
{
    if (d->juceMidiOut) {
        d->juceMidiOut->sendBlockOfMessagesNow(buffer);
    }
}

bool SyncTimer::timerRunning() {
    return !d->timerThread->isPaused();
}

#include "SyncTimer.moc"

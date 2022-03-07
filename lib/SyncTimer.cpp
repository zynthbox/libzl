#include <sched.h>
#include "SyncTimer.h"
#include "ClipAudioSource.h"
#include "libzl.h"
#include "Helper.h"
#include "SamplerSynth.h"

#include <QDebug>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include "QProcess"
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

#include <jack/jack.h>
#include <jack/statistics.h>
#include <jack/midiport.h>

#include "JUCEHeaders.h"

// Defining this will cause the sync timer to collect the intervals of each beat, and output them when you call stop
// It will also make the timer thread output the discrepancies and internal counter states on a per-pseudo-minute basis
// #define DEBUG_SYNCTIMER_TIMING

// Defining this will make the jack process call output a great deal of information about each frame, and is likely to
// itself cause xruns (that is, it considerably increases the amount of processing for each step, including text output)
// Use this to find note oddity and timing issues where note delivery is concerned.
// #define DEBUG_SYNCTIMER_JACK

using namespace std;
using namespace juce;

using frame_clock = std::conditional_t<
    std::chrono::high_resolution_clock::is_steady,
    std::chrono::high_resolution_clock,
    std::chrono::steady_clock>;

#define NanosecondsPerMinute 60000000000
#define NanosecondsPerSecond 1000000000
#define NanosecondsPerMillisecond 1000000
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
        startTime = frame_clock::now();
        std::chrono::time_point< std::chrono::_V2::steady_clock, std::chrono::duration< long long unsigned int, std::ratio< 1, NanosecondsPerSecond > > > nextMinute;
        while (true) {
            if (aborted) {
                break;
            }
            nextMinute = startTime + ((minuteCount + 1) * nanosecondsPerMinute);
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

                    nextExtraTickAt = 0;
                    adjustment = 0;
                    count = 0;
                    cumulativeCount = 0;
                    minuteCount = 0;
                    startTime = frame_clock::now();
                    nextMinute = startTime + nanosecondsPerMinute;
                }
                mutex.unlock();
                if (aborted) {
                    break;
                }
                Q_EMIT timeout(); // Do the thing!
                ++count;
                ++cumulativeCount;
                waitTill(frame_clock::duration(adjustment) + startTime + frame_clock::duration(subbeatCountToNanoseconds(bpm, cumulativeCount)));
            }
#ifdef DEBUG_SYNCTIMER_TIMING
            qDebug() << "Sync timer reached minute:" << minuteCount << "with interval" << interval.count();
            qDebug() << "The most recent pseudo-minute took an extra" << (frame_clock::now() - nextMinute).count() << "nanoseconds";
#endif
            count = 0; // Reset the count each minute
            ++minuteCount;
        }
    }

    Q_SIGNAL void timeout();

    void setBPM(quint64 bpm) {
        this->bpm = bpm;
        interval = frame_clock::duration(subbeatCountToNanoseconds(bpm, 1));
    }
    const quint64 getBpm() const {
        return bpm;
    }

    quint64 subbeatCountToNanoseconds(const quint64 &bpm, const quint64 &subBeatCount) const
    {
        return (subBeatCount * NanosecondsPerMinute) / (bpm * BeatSubdivisions);
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

    void addAdjustmentByMicroseconds(qint64 microSeconds) {
        mutex.lock();
        if (adjustment == 0) {
            currentExtraTick = 0;
        }
        adjustment += (1000 * microSeconds);
        // When we adjust past another "there should have been a beat here" amount for
        // the adjustment, schedule an extra run of the logic in the timer callback
        while (nextExtraTickAt < adjustment) {
            QMetaObject::invokeMethod(this, "timeout", Qt::QueuedConnection);
            ++currentExtraTick;
            nextExtraTickAt = qint64(subbeatCountToNanoseconds(bpm, currentExtraTick));
        }
        mutex.unlock();
    }
    const qint64 getAdjustment() const {
        return adjustment;
    }
    const quint64 getExtraTickCount() const {
        return currentExtraTick;
    }

    const frame_clock::time_point adjustedCumulativeRuntime() const {
        return frame_clock::duration(adjustment) + startTime + (nanosecondsPerMinute * minuteCount) + (interval * count);
    }
    const frame_clock::time_point adjustedRuntimeForTick(const quint64 tick) const {
        return frame_clock::duration(adjustment) + startTime + (interval * tick);
    }
    const frame_clock::time_point getStartTime() const {
        return startTime;
    }
    const std::chrono::nanoseconds getInterval() {
        return interval;
    }
private:
    qint64 nextExtraTickAt{0};
    quint64 currentExtraTick{0};
    qint64 adjustment{0};
    quint64 count{0};
    quint64 cumulativeCount{0};
    quint64 minuteCount{0};
    frame_clock::time_point startTime;

    quint64 bpm{120};
    std::chrono::nanoseconds interval;
    bool aborted{false};

    bool paused{true};
    QMutex mutex;
    QWaitCondition waitCondition;

    // This is equivalent to .1 ms
    const frame_clock::duration spinTime{frame_clock::duration(100000)};
    const std::chrono::nanoseconds nanosecondsPerMinute{NanosecondsPerMinute};
};

class SyncTimerPrivate {
public:
    SyncTimerPrivate(SyncTimer *q)
        : timerThread(new SyncTimerThread(q))
    {
        samplerSynth = SamplerSynth::instance();
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
        QObject::connect(midiBridge, &QProcess::started, q, [](){
            qDebug() << "SyncTimer Midi bridge started - now hook it up to ZynMidiRouter";
            // Connect our SyncTimer:SyncTimer jack output to where ZynMidiRouter expects step sequencer input to arrive
            // This has to happen a little delayed, just to be sure we don't attempt to connect a partially-created port
            QTimer::singleShot(1000, [](){
                QProcess::startDetached("jack_connect", {"SyncTimer:SyncTimer", "ZynMidiRouter:step_in"});
            });
        });
        QObject::connect(midiBridge, &QProcess::errorOccurred, q, [](QProcess::ProcessError error){
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
    ~SyncTimerPrivate() {
        timerThread->requestAbort();
        timerThread->wait();
        midiBridge->terminate();
        if (!midiBridge->waitForFinished(2000)) {
            // Let's not hang around too long...
            midiBridge->kill();
        }
        if (jackClient) {
            jack_client_close(jackClient);
        }
    }
    SamplerSynth *samplerSynth{nullptr};
    SyncTimerThread *timerThread;
    QProcess *midiBridge;
    int playingClipsCount = 0;
    int beat = 0;
    QList<void (*)(int)> callbacks;
    QHash<quint64, QList<ClipCommand *> > clipStartQueues;
    QHash<quint64, QList<ClipAudioSource *> > clipStopQueues;
    QQueue<ClipAudioSource *> clipsStartQueue;
    QQueue<ClipAudioSource *> clipsStopQueue;

    quint64 cumulativeBeat = 0;
    QHash<quint64, MidiBuffer> midiMessageQueues;
    MidiBuffer nextMidiMessages;
    std::unique_ptr<MidiOutput> juceMidiOut{nullptr};

#ifdef DEBUG_SYNCTIMER_TIMING
    frame_clock::time_point lastRound;
    QList<long> intervals;
#endif
    QMutex mutex;
    int i{0};
    void hiResTimerCallback() {
#ifdef DEBUG_SYNCTIMER_TIMING
        frame_clock::time_point thisRound = frame_clock::now();
        intervals << (thisRound - lastRound).count();
        lastRound = thisRound;
#endif
        /// =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
        ///      Performance Intensive Stuff Goes Below Here
        /// avoid allocations, list changes, etc if at all possible
        /// =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
        /// {

        if (beat == 0) {
            for (ClipAudioSource *clip : clipsStartQueue) {
                clip->play();
            }
        }
        if (clipStopQueues.contains(cumulativeBeat)) {
            const QList<ClipAudioSource *> &clips = clipStopQueues[cumulativeBeat];
            for (ClipAudioSource *clip : clips) {
                clip->stop();
            }
        }
        if (clipStartQueues.contains(cumulativeBeat)) {
            const QList<ClipCommand *> &clips = clipStartQueues[cumulativeBeat];
            for (ClipCommand *clipCommand : clips) {
                samplerSynth->handleClipCommand(clipCommand);
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
            clipStopQueues.remove(cumulativeBeat);
            clipStartQueues.remove(cumulativeBeat);
        }

        // Logically, we consider these low-priority (if you need high precision output, things should be scheduled for next beat)
        for (auto cb : callbacks) {
            cb(beat);
        }

        beat = (beat + 1) % (BeatSubdivisions * 4);
        ++cumulativeBeat;

        // Finally, remove old queues that are sufficiently far behind us in
        // time. If our latency is big enough that a tail this long causes problems,
        // then there's not a lot we can reasonably do without eating all the memory.
        if (mutex.tryLock(timerThread->getInterval().count() / 5000000)) {
            quint64 i{16};
            while (midiMessageQueues.contains(cumulativeBeat - i)) {
                midiMessageQueues.remove(cumulativeBeat - i);
                ++i;
            }
            mutex.unlock();
        }
    }

    jack_client_t* jackClient{nullptr};
    jack_port_t* jackPort{nullptr};
    quint64 jackPlayhead{0};
    jack_time_t jackMostRecentNextUsecs{0};
    jack_time_t jackUsecDeficit{0};
    jack_time_t jackStartTime{0};
    quint64 skipHowMany{0};
    int process(jack_nframes_t nframes) {
        if (!timerThread->isPaused()) {
#ifdef DEBUG_SYNCTIMER_JACK
            quint64 stepCount = 0;
            QList<int> commandValues;
            QList<int> noteValues;
            QList<int> velocities;
            QList<quint64> framePositions;
            QList<quint64> frameSteps;
#endif
            quint64 eventCount = 0;

            jack_nframes_t current_frames;
            jack_time_t current_usecs;
            jack_time_t next_usecs;
            float period_usecs;
            jack_get_cycle_times(jackClient, &current_frames, &current_usecs, &next_usecs, &period_usecs);

            // Attempt to lock, but don't wait longer than half the available period, or we'll end up in trouble
            if (mutex.tryLock(period_usecs / 4000)) {
                auto buffer = jack_port_get_buffer(jackPort, nframes);
                jack_midi_clear_buffer(buffer);

                const jack_time_t subbeatLengthInMicroseconds = timerThread->subbeatCountToNanoseconds(timerThread->getBpm(), 1) / 1000;
                const quint64 microsecondsPerFrame = (next_usecs - current_usecs) / nframes;
                if (jackPlayhead == 0) {
                    // first run for this playback session, let's do a touch of setup
                    jackStartTime = current_usecs;
                    jackUsecDeficit = 0;
                    skipHowMany = 0;
                } else {
                    if (jackMostRecentNextUsecs < current_usecs) {
                        // That means we have skipped some cycles somehow - let's work out what to do about that!
                        const qint64 adjustment = qint64(current_usecs - jackMostRecentNextUsecs);
                        jackUsecDeficit += quint64(adjustment);
                        timerThread->addAdjustmentByMicroseconds(adjustment);
                        qDebug() << "Somehow, we have ended up skipping cycles, and we are in total deficit of" << jackUsecDeficit << "microseconds - sync timer adjusted to match by adding" << adjustment << "microseconds and the sync timer now has" << timerThread->getExtraTickCount() << "extra ticks";
                    } else {
                        // TODO We will need to treat Jack as the canonical time reference or risk
                        // ending in in more trouble. In short, we are going to have to never skip
                        // anything here, but rather ask SyncTimer to adjust itself, and then also
                        // ensure that Juce's playback is synchronised with Jack's position, not
                        // the other way around. Since we are recording through jack, that is the
                        // time that is actually important (otherwise the recording will be unsynced,
                        // which is the more critical path). The code below makes Jack synchronise
                        // to the real-time timer, but the problem with doing that is we will end
                        // up with incorrect timing in any recorded data, which is not what we want.
                        // So, adjust Juce /and/ SyncTimerThread here, not Jack.
                        static const quint64 maxPlayheadDeviation = 1;
                        if (jackPlayhead > cumulativeBeat && jackPlayhead - cumulativeBeat > maxPlayheadDeviation) {
                            qDebug() << "We are ahead of the timer - our playback position is at" << jackPlayhead << "and the most recent tick of the timer is" << cumulativeBeat;
                            // This will cause a short pause in playback, which at 1 subbeat (less
                            // than 4ms at 120bpm) should be imperceptible, and help make the playback
                            // stay in sync. Why this happens, i have no idea, but here we are.
                            skipHowMany += 1;
                        }
                        // No need to handle being behind, the while loop below handles that already
                    }
                }
                jackMostRecentNextUsecs = next_usecs;

                jack_time_t nextPlaybackPosition = jackStartTime + (timerThread->subbeatCountToNanoseconds(timerThread->getBpm(), jackPlayhead + skipHowMany) / 1000);
                jack_nframes_t firstAvailableFrame{0};
                jack_nframes_t relativePosition{0};
                // As long as the next playback position fits inside this frame, and we have space for it, let's post some events
                while (nextPlaybackPosition < next_usecs && firstAvailableFrame < nframes) {
                    const juce::MidiBuffer &juceBuffer = midiMessageQueues[jackPlayhead];
                    if (!juceBuffer.isEmpty()) {
                        // If the notes are in the past, they need to be scheduled as soon as we can, so just put those on position 0, and if we are here, that means that ending up in the future is a rounding error, so clamp that
                        if (nextPlaybackPosition <= current_usecs) {
                            relativePosition = firstAvailableFrame;
                            ++firstAvailableFrame;
                        } else {
                            relativePosition = std::clamp<jack_nframes_t>((nextPlaybackPosition - current_usecs) / microsecondsPerFrame, firstAvailableFrame, nframes - 1);
                            firstAvailableFrame = relativePosition + 1;
                        }
                        for (const juce::MidiMessageMetadata &juceMessage : juceBuffer) {
                            if (jack_midi_event_write(
                                buffer,
                                relativePosition,
                                const_cast<jack_midi_data_t*>(juceMessage.data), // this might seems odd, but it's really only because juce's internal store is const here, and the data types are otherwise the same
                                size_t(juceMessage.numBytes) // this changes signedness, but from a lesser space (int) to a larger one (unsigned long)
                            ) == ENOBUFS) {
                                qWarning() << "Ran out of space while writing events!";
                            } else {
                                ++eventCount;
#ifdef DEBUG_SYNCTIMER_JACK
                                commandValues << juceMessage.data[0];
                                noteValues << juceMessage.data[1];
                                velocities << juceMessage.data[2];
#endif
                            }
                        }
#ifdef DEBUG_SYNCTIMER_JACK
                        framePositions << relativePosition;
                        frameSteps << jackPlayhead;
#endif
                    }
                    ++jackPlayhead;
                    nextPlaybackPosition += subbeatLengthInMicroseconds;
#ifdef DEBUG_SYNCTIMER_JACK
                    ++stepCount;
#endif
                }
                if (eventCount > 0) {
                    if (uint32_t lost = jack_midi_get_lost_event_count(buffer)) {
                        qDebug() << "Lost some notes:" << lost;
                    }
#ifdef DEBUG_SYNCTIMER_JACK
                    qDebug() << "We advanced jack playback by" << stepCount << "steps, and are now at position" << jackPlayhead << "and we filled up jack with" << eventCount << "events" << nframes << subbeatLengthInMicroseconds << frameSteps << framePositions << commandValues << noteValues << velocities;
                } else {
                    qDebug() << "We advanced jack playback by" << stepCount << "steps, and are now at position" << jackPlayhead << "and scheduled no notes";
#endif
                }
                mutex.unlock();
            } else {
                qDebug() << "Failed to lock for reading in reasonable time, postponing until next run, waited for" << period_usecs / 4000 << "ms";
            }
        }
        return 0;
    }
    int xrun() {
#ifdef DEBUG_SYNCTIMER_JACK
        qDebug() << "SyncTimer detected XRun";
#endif
        return 0;
    }
};

static int client_process(jack_nframes_t nframes, void* arg) {
    return static_cast<SyncTimerPrivate*>(arg)->process(nframes);
}
static int client_xrun(void* arg) {
    return static_cast<SyncTimerPrivate*>(arg)->xrun();
}

SyncTimer::SyncTimer(QObject *parent)
    : QObject(parent)
    , d(new SyncTimerPrivate(this))
{
}

void SyncTimer::addCallback(void (*functionPtr)(int)) {
    cerr << "Adding callback " << functionPtr << endl;
    d->callbacks.append(functionPtr);
    if (!d->jackClient) {
        // Open the client.
        jack_status_t real_jack_status{};
        d->jackClient = jack_client_open(
            "SyncTimerOut",
            JackNullOption,
            &real_jack_status
        );
        if (d->jackClient) {
            // Register the MIDI output port.
            d->jackPort = jack_port_register(
                d->jackClient,
                "main_out",
                JACK_DEFAULT_MIDI_TYPE,
                JackPortIsOutput,
                0
            );
            if (d->jackPort) {
                // Set the process callback.
                if (
                    jack_set_process_callback(
                        d->jackClient,
                        client_process,
                        static_cast<void*>(d)
                    ) != 0
                ) {
                    qWarning() << "Failed to set the SyncTimer Jack processing callback";
                } else {
                    jack_set_xrun_callback(d->jackClient, client_xrun, static_cast<void*>(d));
                    // Activate the client.
                    if (jack_activate(d->jackClient) == 0) {
                        qDebug() << "Successfully created and set up the SyncTimer's Jack client";
                        if (jack_connect(d->jackClient, "SyncTimerOut:main_out", "ZynMidiRouter:step_in") == 0) {
                            qDebug() << "Successfully created and hooked up the sync timer's jack output to the midi router's step input port";
                        } else {
                            qWarning() << "Failed to connect the SyncTimer's Jack output to ZynMidiRouter:step_in";
                        }
                    } else {
                        qWarning() << "Failed to activate SyncTimer Jack client";
                    }
                }
            } else {
                qWarning() << "Could not register SyncTimer Jack output port";
            }
        } else {
            qWarning() << "Could not create SyncTimer Jack client.";
        }

    }
//     if (!d->juceMidiOut) {
//         QTimer::singleShot(1, this, [this](){
//             qDebug() << "Attempting to create a special midi out for SyncTimer";
//             d->juceMidiOut = MidiOutput::createNewDevice("SyncTimer Out");
//             if (!d->juceMidiOut) {
//                 qWarning() << "Failed to create SyncTimer's Juce-based midi-out";
//             } else {
//                 // Start our midi bridge - we could do it earlier, but it also needs
//                 // to happen /after/ ZynMidiRouter is initialised, and so we might as
//                 // well wait until now to do it.
//                 d->midiBridge->start();
//             }
//         });
//     }
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
    // Immediately stop clips when queued to stop
    clip->stop();
}

void SyncTimer::start(int bpm) {
    qDebug() << "#### Starting timer with bpm " << bpm << " and interval " << getInterval(bpm);
    d->timerThread->setBPM(quint64(bpm));
#ifdef DEBUG_SYNCTIMER_TIMING
    d->intervals.clear();
    d->lastRound = frame_clock::now();
#endif
    d->jackPlayhead = 0;
    d->timerThread->resume();
}

void SyncTimer::stop() {
    cerr << "#### Stopping timer" << endl;

    QMutexLocker locker(&d->mutex);
    if(!d->timerThread->isPaused()) {
        d->timerThread->pause();
    }

    for (ClipAudioSource *clip : d->clipsStopQueue) {
        clip->stop();
    }

    QHashIterator<quint64, QList<ClipAudioSource *> > iterator(d->clipStopQueues);
    while (iterator.hasNext()) {
        iterator.next();
        for (ClipAudioSource *clip : iterator.value()) {
            clip->stop();
        }
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
    d->beat = 0;
    d->cumulativeBeat = 0;
    d->midiMessageQueues.clear();
    d->nextMidiMessages.clear();
    for (QList<ClipCommand *> startQueue : d->clipStartQueues) {
        qDeleteAll(startQueue);
    }
    d->clipStartQueues.clear();
    d->clipStopQueues.clear();
#ifdef DEBUG_SYNCTIMER_TIMING
    qDebug() << d->intervals;
#endif
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

void SyncTimer::scheduleClipCommand(ClipCommand *clip, quint64 delay)
{
    d->mutex.lock();
    if (!d->clipStartQueues.contains(d->cumulativeBeat + delay)) {
        d->clipStartQueues[d->cumulativeBeat + delay] = QList<ClipCommand*>{};
    }
    QList<ClipCommand*> &clips = d->clipStartQueues[d->cumulativeBeat + delay];
    bool foundExisting{false};
    for (ClipCommand *clipCommand : clips) {
        if (clipCommand->clip == clip->clip && (clipCommand->midiNote == clip->midiNote || (clipCommand->changeSlice && clipCommand->slice == clip->slice))) {
            if (clip->changeLooping) {
                clipCommand->looping = clip->looping;
                clipCommand->changeLooping = true;
            }
            if (clip->changePitch) {
                clipCommand->pitchChange = clip->pitchChange;
                clipCommand->changePitch = true;
            }
            if (clip->changeSpeed) {
                clipCommand->speedRatio = clip->speedRatio;
                clipCommand->changeSpeed = true;
            }
            if (clip->changeGainDb) {
                clipCommand->gainDb = clip->gainDb;
                clipCommand->changeGainDb = true;
            }
            if (clip->changeVolume) {
                clipCommand->volume = clip->volume;
                clipCommand->changeVolume = true;
            }
            if (clip->startPlayback) {
                clipCommand->startPlayback = true;
            }
            foundExisting = true;
        }
    }
    if (foundExisting) {
        clips << clip;
    } else {
        delete clip;
    }
    d->mutex.unlock();
}

void SyncTimer::scheduleClipToStop(ClipAudioSource *clip, quint64 delay)
{
    d->mutex.lock();
    if (!d->clipStopQueues.contains(d->cumulativeBeat + delay)) {
        d->clipStopQueues[d->cumulativeBeat + delay] = QList<ClipAudioSource*>{};
    }
    QList<ClipAudioSource*> &clips = d->clipStopQueues[d->cumulativeBeat + delay];
    if (!clips.contains(clip)) {
        clips << clip;
    }
    d->mutex.unlock();
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
    d->mutex.lock();
    if (d->midiMessageQueues.contains(d->cumulativeBeat + delay)) {
        d->midiMessageQueues[d->cumulativeBeat + delay].addEvent(note, 3, onOrOff);
    } else {
        MidiBuffer buffer;
        buffer.addEvent(note, 3, onOrOff);
        d->midiMessageQueues[d->cumulativeBeat + delay] = buffer;
    }
    d->mutex.unlock();
    if (setOn && duration > 0) {
        // Schedule an off note for that position
        scheduleNote(midiNote, midiChannel, false, 64, 0, delay + duration);
    }
}

void SyncTimer::scheduleMidiBuffer(const juce::MidiBuffer& buffer, quint64 delay)
{
    d->mutex.lock();
    const bool queueContainsPosition{d->midiMessageQueues.contains(d->cumulativeBeat + delay)};
    d->mutex.unlock();
    if (queueContainsPosition) {
        d->mutex.lock();
        juce::MidiBuffer &addToThis = d->midiMessageQueues[d->cumulativeBeat + delay];
        d->mutex.unlock();
        for (const juce::MidiMessageMetadata &newMeta : buffer) {
            bool alreadyExists{false};
            for (const juce::MidiMessageMetadata &existingMeta : addToThis) {
                if (existingMeta.samplePosition == newMeta.samplePosition
                    && existingMeta.numBytes == newMeta.numBytes
                ) {
                    if (newMeta.numBytes == 2 && existingMeta.data[0] == newMeta.data[0] && existingMeta.data[1] == newMeta.data[1]) {
                        alreadyExists = true;
                        break;
                    } else if (newMeta.numBytes == 3 && existingMeta.data[0] == newMeta.data[0] && existingMeta.data[1] == newMeta.data[1] && existingMeta.data[2] == newMeta.data[2]) {
                        alreadyExists = true;
                        break;
                    }
                }
                if (alreadyExists) {
                    break;
                }
            }
            if (!alreadyExists) {
                addToThis.addEvent(newMeta.data, newMeta.numBytes, newMeta.samplePosition);
            }
        }
    } else {
        d->mutex.lock();
        d->midiMessageQueues[d->cumulativeBeat + delay] = buffer;
        d->mutex.unlock();
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

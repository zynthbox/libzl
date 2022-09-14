#include <sched.h>
#include "SyncTimer.h"
#include "ClipAudioSource.h"
#include "ClipCommand.h"
#include "libzl.h"
#include "Helper.h"
#include "MidiRouter.h"
#include "SamplerSynth.h"
#include "TimerCommand.h"

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

#define BPM_MINIMUM 50
#define BPM_MAXIMUM 200

// Defining this will cause the sync timer to collect the intervals of each beat, and output them when you call stop
// It will also make the timer thread output the discrepancies and internal counter states on a per-pseudo-minute basis
// #define DEBUG_SYNCTIMER_TIMING

// Defining this will make the jack process call output a great deal of information about each frame, and is likely to
// itself cause xruns (that is, it considerably increases the amount of processing for each step, including text output)
// Use this to find note oddity and timing issues where note delivery is concerned.
// #define DEBUG_SYNCTIMER_JACK

using namespace std;
using namespace juce;

struct StepData {
    StepData() {
        midiBuffers << juce::MidiBuffer();
    }
    ~StepData() {
        qDeleteAll(timerCommands);
        qDeleteAll(clipCommands);
    }
    // Call this before accessing the data to ensure that it is fresh
    void ensureFresh() {
        if (isPlaying) {
            qWarning() << "Attempted to ensure a thing is fresh that is currently playing";
        }
        if (played) {
            played = false;
            // It's our job to delete the timer commands, so do that first
            for (TimerCommand* command : timerCommands) {
                delete command;
            }
            // The clip commands, once sent out, become owned by SampelerSynth, so leave them alone
            timerCommands.clear();
            clipCommands.clear();
            midiBuffers.clear();
            // Create one midi buffer to insert the base message into
            midiBuffers << juce::MidiBuffer();
        }
    }
    QList<TimerCommand*> timerCommands;
    QList<ClipCommand*> clipCommands;
    QList<juce::MidiBuffer> midiBuffers;

    // SyncTimer sets this true to mark that it has played the step
    bool played{false};
    // Basic spin lock
    bool isPlaying{false};
};

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
                waitTill(frame_clock::now() + frame_clock::duration(subbeatCountToNanoseconds(bpm, 1)));
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
    inline const quint64 getBpm() const {
        return bpm;
    }

    static inline quint64 subbeatCountToNanoseconds(const quint64 &bpm, const quint64 &subBeatCount)
    {
        return (subBeatCount * NanosecondsPerMinute) / (bpm * BeatSubdivisions);
    };
    static inline float nanosecondsToSubbeatCount(const quint64 &bpm, const quint64 &nanoseconds)
    {
        return nanoseconds / (NanosecondsPerMinute / (bpm * BeatSubdivisions));
    };
    void requestAbort() {
        aborted = true;
    }

    Q_SLOT void pause() { setPaused(true); }
    Q_SLOT void resume() { setPaused(false); }
    inline const bool &isPaused() const {
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
        : q(q)
        ,timerThread(new SyncTimerThread(q))
    {
        for (quint64 i = 0; i < stepRingCount; ++i) {
            stepRing << new StepData;
        }
        samplerSynth = SamplerSynth::instance();
        // Dangerzone - direct connection from another thread. Yes, dangerous, but also we need the precision, so we need to dill whit it
        QObject::connect(timerThread, &SyncTimerThread::timeout, q, [this](){ hiResTimerCallback(); }, Qt::DirectConnection);
        QObject::connect(timerThread, &QThread::started, q, [q](){ Q_EMIT q->timerRunningChanged(); });
        QObject::connect(timerThread, &QThread::finished, q, [q](){ Q_EMIT q->timerRunningChanged(); });
        QObject::connect(timerThread, &SyncTimerThread::pausedChanged, q, [q](){ q->timerRunningChanged(); });
        timerThread->start();
    }
    ~SyncTimerPrivate() {
        timerThread->requestAbort();
        timerThread->wait();
        if (jackClient) {
            jack_client_close(jackClient);
        }
        qDeleteAll(stepRing);
    }
    SyncTimer *q{nullptr};
    SamplerSynth *samplerSynth{nullptr};
    SyncTimerThread *timerThread;
    int playingClipsCount = 0;
    int beat = 0;
    quint64 cumulativeBeat = 0;
    QList<void (*)(int)> callbacks;
    QList<ClipCommand*> sentOutClips;
    QList<juce::MidiBuffer> buffersForImmediateDispatch;

    // The next step to be read in the step ring
    quint64 stepReadHead{0};
    quint64 stepNextPlaybackPosition{0};
    static const quint64 stepRingCount{32768};
    QList<StepData*> stepRing;
    /**
     * \brief Get the ring buffer position based on the given delay from the current playback position (cumulativeBeat if playing, or stepReadHead if not playing)
     * @param delay The delay of the position to use
     * @return The stepRing position to use for the given delay
     */
    inline int delayedStep(quint64 delay) {
        quint64 step{0};
        if (timerThread->isPaused()) {
            // If paused, base the delay on the current stepReadHead
            step = (stepReadHead + delay + 1) % stepRingCount;
        } else {
            // If running, base the delay on the current cumulativeBeat (adjusted to at least stepReadHead, just in case)
            step = (stepReadHeadOnStart + qMax(cumulativeBeat + delay, jackPlayhead + 1)) % stepRingCount;
        }
        if (stepRing.at(step)->isPlaying) {
            step = (step + 1) % stepRingCount;
        }
        return step;
    }

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
        while (cumulativeBeat < (jackPlayhead + (q->scheduleAheadAmount() * 2))) {
            // Call any callbacks registered to us
            for (auto cb : qAsConst(callbacks)) {
                cb(beat);
            }

            // Spit out a touch of useful information on beat zero
            if (beat == 0 && samplerSynth->engine()) {
                qDebug() << "Current tracktion/juce CPU usage:" << samplerSynth->engine()->getDeviceManager().getCpuUsage() << "with total jack process call saturation at:" << samplerSynth->cpuLoad();
            }

            // Increase the current beat as we understand it
            beat = (beat + 1) % (BeatSubdivisions * 4);
            ++cumulativeBeat;
        }

        // Finally, remove old queues that are sufficiently far behind us in time.
        // That is to say, get rid of any queues that are older than the current jack playback position
        if (mutex.tryLock(timerThread->getInterval().count() / 5000000)) {
            // Finally, notify any listeners that commands have been sent out
            for (ClipCommand *clipCommand : qAsConst(sentOutClips)) {
                Q_EMIT q->clipCommandSent(clipCommand);
            }
            // You must not delete the commands themselves here, as SamplerSynth takes ownership of them
            sentOutClips.clear();
            mutex.unlock();
        }
    }

    jack_client_t* jackClient{nullptr};
    jack_port_t* jackPort{nullptr};
    quint64 jackPlayhead{0};
    quint64 stepReadHeadOnStart{0};
    jack_time_t jackMostRecentNextUsecs{0};
    jack_time_t jackUsecDeficit{0};
    jack_time_t jackStartTime{0};
    quint64 jackNextPlaybackPosition{0};
    quint64 jackSubbeatLengthInMicroseconds{0};
    quint64 jackLatency{0};
    int process(jack_nframes_t nframes) {
        auto buffer = jack_port_get_buffer(jackPort, nframes);
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

        if (!timerThread->isPaused()) {
            if (jackPlayhead == 0) {
                // first run for this playback session, let's do a touch of setup
                jackNextPlaybackPosition = current_usecs;
                jackUsecDeficit = 0;
            }
            jackMostRecentNextUsecs = next_usecs;
        }
        if (stepNextPlaybackPosition == 0) {
            stepNextPlaybackPosition = current_usecs;
        }
        jack_nframes_t firstAvailableFrame{0};
        jack_nframes_t relativePosition{0};
        // As long as the next playback position fits inside this frame, and we have space for it, let's post some events
        const quint64 microsecondsPerFrame = (next_usecs - current_usecs) / nframes;

        // Find the first /real/ available frame, in case MidiRouter's not done reading things out yet
        // This logic does make it possible for events to end up being asked to get scheduled into the next period,
        // however we already compress the events into the time period they're supposed to exist in, so that ought
        // to be safe.
        uint32_t currentEventCount = jack_midi_get_event_count(buffer);
        if (currentEventCount > 0) {
            jack_midi_event_t event;
            if (int err = jack_midi_event_get(&event, buffer, currentEventCount - 1)) {
                qWarning() << "SyncTimer: jack_midi_event_get failed, presumably it was just cleared, so ignore. Reported error was:" << err << strerror(err);
            } else {
                firstAvailableFrame = event.time + 1;
            }
        }
        // Firstly, send out any buffers we've been told to send out immediately
        bool buffersDispatched{false};
        for (const juce::MidiBuffer &juceBuffer : buffersForImmediateDispatch) {
            relativePosition = firstAvailableFrame;
            ++firstAvailableFrame;
            for (const juce::MidiMessageMetadata &juceMessage : juceBuffer) {
                if (jack_midi_event_write(buffer, relativePosition,
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
            if (firstAvailableFrame >= nframes) {
                break;
            }
            buffersDispatched = true;
        }
        if (buffersDispatched) {
            buffersForImmediateDispatch.clear();
        }

        while (stepNextPlaybackPosition < next_usecs && firstAvailableFrame < nframes) {
            StepData *stepData = stepRing.at(stepReadHead);
            // Next roll for next time (also do it now, as we're reading out of it)
            stepReadHead = (stepReadHead + 1) % stepRingCount;
            // In case we're cycling through stuff we've already played, let's just... not do anything with that
            // Basically that just means nobody else has attempted to do stuff with the step since we last played it
            if (!stepData->played) {
                stepData->isPlaying = true;
                // If the notes are in the past, they need to be scheduled as soon as we can, so just put those on position 0, and if we are here, that means that ending up in the future is a rounding error, so clamp that
                if (stepNextPlaybackPosition <= current_usecs) {
                    relativePosition = firstAvailableFrame;
                    ++firstAvailableFrame;
                } else {
                    relativePosition = std::clamp<jack_nframes_t>((stepNextPlaybackPosition - current_usecs) / microsecondsPerFrame, firstAvailableFrame, nframes - 1);
                    firstAvailableFrame = relativePosition;
                }
                // First, let's get the midi messages sent out
                for (const juce::MidiBuffer &juceBuffer : qAsConst(stepData->midiBuffers)) {
                    if (firstAvailableFrame >= nframes) {
                        qWarning() << "First available frame is in the future - that's a problem";
                        break;
                    }
                    for (const juce::MidiMessageMetadata &juceMessage : juceBuffer) {
                        if (jack_midi_event_write(buffer, relativePosition,
                            const_cast<jack_midi_data_t*>(juceMessage.data), // this might seems odd, but it's really only because juce's internal store is const here, and the data types are otherwise the same
                            size_t(juceMessage.numBytes) // this changes signedness, but from a lesser space (int) to a larger one (unsigned long)
                        ) == ENOBUFS) {
                            qWarning() << "Ran out of space while writing events!";
                        } else {
                            ++eventCount;
#ifdef DEBUG_SYNCTIMER_JACK
                            commandValues << juceMessage.data[0]; noteValues << juceMessage.data[1]; velocities << juceMessage.data[2];
#endif
                        }
                    }
                }

                // Then do direct-control samplersynth things
                if (stepData->clipCommands.count() > 0) {
                    for (ClipCommand *clipCommand : qAsConst(stepData->clipCommands)) {
                        // Using the protected function, which only we (and SamplerSynth) can use, to ensure less locking
                        samplerSynth->handleClipCommand(clipCommand, jackPlayhead);
                    }
                    sentOutClips.append(stepData->clipCommands);
                }

                // Do playback control things as the last thing, otherwise we might end up affecting things
                // currently happening (like, if we stop playback on the last step of a thing, we still want
                // notes on that step to have been played and so on)
                for (TimerCommand *command : qAsConst(stepData->timerCommands)) {
                    Q_EMIT q->timerCommand(command);
                    if (command->operation == TimerCommand::StartClipLoopOperation || command->operation == TimerCommand::StopClipLoopOperation) {
                        ClipCommand *clipCommand = static_cast<ClipCommand *>(command->variantParameter.value<void*>());
                        if (clipCommand) {
                            samplerSynth->handleClipCommand(clipCommand, jackPlayhead);
                            sentOutClips.append(clipCommand);
                        } else {
                            qWarning() << Q_FUNC_INFO << "Failed to retrieve clip command from clip based timer command";
                        }
                        command->variantParameter.clear();
                    } else if (command->operation == TimerCommand::RegisterCASOperation || command->operation == TimerCommand::UnregisterCASOperation) {
                        ClipAudioSource *clip = static_cast<ClipAudioSource*>(command->variantParameter.value<void*>());
                        if (clip) {
                            if (command->operation == TimerCommand::RegisterCASOperation) {
                                samplerSynth->registerClip(clip);
                            } else {
                                samplerSynth->unregisterClip(clip);
                            }
                        } else {
                            qWarning() << Q_FUNC_INFO << "Failed to retrieve clip from clip registration timer command";
                        }
                    } else if (command->operation == TimerCommand::SamplerChannelEnabledStateOperation) {
                        samplerSynth->setChannelEnabled(command->parameter, command->parameter2);
                    }
                }
                stepData->played = true;
                stepData->isPlaying = false;
            }
            if (!timerThread->isPaused()) {
                // Next roll for next time
                ++jackPlayhead;
                jackNextPlaybackPosition += jackSubbeatLengthInMicroseconds;
#ifdef DEBUG_SYNCTIMER_JACK
                    ++stepCount;
#endif
            }
            stepNextPlaybackPosition += jackSubbeatLengthInMicroseconds;
        }
        if (eventCount > 0) {
            if (uint32_t lost = jack_midi_get_lost_event_count(buffer)) {
                qDebug() << "Lost some notes:" << lost;
            }
#ifdef DEBUG_SYNCTIMER_JACK
            qDebug() << "We advanced jack playback by" << stepCount << "steps, and are now at position" << jackPlayhead << "and we filled up jack with" << eventCount << "events" << nframes << jackSubbeatLengthInMicroseconds << frameSteps << framePositions << commandValues << noteValues << velocities;
        } else {
            qDebug() << "We advanced jack playback by" << stepCount << "steps, and are now at position" << jackPlayhead << "and scheduled no notes";
#endif
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
void client_latency_callback(jack_latency_callback_mode_t mode, void *arg)
{
    if (mode == JackPlaybackLatency) {
        SyncTimerPrivate *d = static_cast<SyncTimerPrivate*>(arg);
        jack_latency_range_t range;
        jack_port_get_latency_range (d->jackPort, JackPlaybackLatency, &range);
        if (range.max != d->jackLatency) {
            jack_nframes_t bufferSize = jack_get_buffer_size(d->jackClient);
            jack_nframes_t sampleRate = jack_get_sample_rate(d->jackClient);
            quint64 newLatency = (1000 * (double)qMax(bufferSize, range.max)) / (double)sampleRate;
            if (newLatency != d->jackLatency) {
                d->jackLatency = newLatency;
                qDebug() << "Latency changed, max is now" << range.max << "That means we will now suggest scheduling things" << d->q->scheduleAheadAmount() << "steps into the future";
                Q_EMIT d->q->scheduleAheadAmountChanged();
            }
        }
    }
}

SyncTimer::SyncTimer(QObject *parent)
    : QObject(parent)
    , d(new SyncTimerPrivate(this))
{
    d->jackSubbeatLengthInMicroseconds = d->timerThread->subbeatCountToNanoseconds(d->timerThread->getBpm(), 1) / 1000;
}

void SyncTimer::addCallback(void (*functionPtr)(int)) {
    cerr << "Adding callback " << functionPtr << endl;
    d->callbacks.append(functionPtr);
    if (!d->jackClient) {
        // First instantiate out midi router, so we can later connect to it...
        MidiRouter::instance();
        connect(MidiRouter::instance(), &MidiRouter::addedHardwareInputDevice, this, &SyncTimer::addedHardwareInputDevice);
        connect(MidiRouter::instance(), &MidiRouter::removedHardwareInputDevice, this, &SyncTimer::removedHardwareInputDevice);
        // Open the client.
        jack_status_t real_jack_status{};
        d->jackClient = jack_client_open(
            "SyncTimer",
            JackNullOption,
            &real_jack_status
        );
        if (d->jackClient) {
            // Register the MIDI output port.
            d->jackPort = jack_port_register(
                d->jackClient,
                "midi_out",
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
                    jack_set_latency_callback (d->jackClient, client_latency_callback, static_cast<void*>(d));
                    // Activate the client.
                    if (jack_activate(d->jackClient) == 0) {
                        qDebug() << "Successfully created and set up the SyncTimer's Jack client";
                        if (jack_connect(d->jackClient, "SyncTimer:midi_out", "ZLRouter:SyncTimerIn") == 0) {
                            qDebug() << "Successfully created and hooked up the sync timer's jack output to the midi router's input port";
                            jack_latency_range_t range;
                            jack_port_get_latency_range (d->jackPort, JackPlaybackLatency, &range);
                            jack_nframes_t bufferSize = jack_get_buffer_size(d->jackClient);
                            jack_nframes_t sampleRate = jack_get_sample_rate(d->jackClient);
                            d->jackLatency = (1000 * (double)qMax(bufferSize, range.max)) / (double)sampleRate;
                            qDebug() << "Buffer size is supposed to be" << bufferSize << "but our maximum latency is" << range.max << "and we should be using that one to calculate how far out things should go, as that should include the amount of extra buffers alsa might (and likely does) use.";
                            qDebug() << "However, as that is sometimes zero, we use the highest of the two. That means we will now suggest scheduling things" << scheduleAheadAmount() << "steps into the future";
                            Q_EMIT scheduleAheadAmountChanged();
                        } else {
                            qWarning() << "Failed to connect the SyncTimer's Jack midi output to ZLRouter:SyncTimerIn";
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
}

void SyncTimer::removeCallback(void (*functionPtr)(int)) {
    bool result = d->callbacks.removeOne(functionPtr);
    cerr << "Removing callback " << functionPtr << " : " << result << endl;
}

void SyncTimer::queueClipToStartOnChannel(ClipAudioSource *clip, int midiChannel)
{
    ClipCommand *command = ClipCommand::channelCommand(clip, midiChannel);
    command->midiNote = 60;
    command->changeVolume = true;
    command->volume = 1.0;
    command->looping = true;
    // When explicity starting a clip in a looping state, we want to /restart/ the loop, not start multiple loops (to run multiple at the same time, sample-trig can do that for us)
    command->stopPlayback = true;
    command->startPlayback = true;

    const quint64 nextZeroBeat = d->timerThread->isPaused() ? 0 : (BeatSubdivisions * 4) - (d->cumulativeBeat % (BeatSubdivisions * 4));
//     qDebug() << "Queueing up" << clip << "to start, with jack and timer zero beats at" << nextZeroBeat << "at beats" << d->cumulativeBeat << "meaning we want positions" << (d->cumulativeBeat + nextZeroBeat < d->jackPlayhead ? nextZeroBeat + BeatSubdivisions * 4 : nextZeroBeat);
    scheduleClipCommand(command, d->cumulativeBeat + nextZeroBeat < d->jackPlayhead ? nextZeroBeat + BeatSubdivisions * 4 : nextZeroBeat);
}

void SyncTimer::queueClipToStopOnChannel(ClipAudioSource *clip, int midiChannel)
{
    QMutexLocker locker(&d->mutex);

    // First, remove any references to the clip that we're wanting to stop
    for (quint64 step = 0; step < d->stepRingCount; ++step) {
        StepData *stepData = d->stepRing.at(step);
        if (!stepData->played) {
            QMutableListIterator<ClipCommand *> stepIterator(stepData->clipCommands);
            while (stepIterator.hasNext()) {
                stepIterator.next();
                if (stepIterator.value()->clip == clip) {
                    delete stepIterator.value();
                    stepIterator.remove();
                    break;
                }
            }
        }
    }

    // Then stop it, now, because it should be now
    ClipCommand *command = ClipCommand::channelCommand(clip, midiChannel);
    command->midiNote = 60;
    command->stopPlayback = true;
    StepData *stepData = d->stepRing.at(d->delayedStep(0));
    stepData->clipCommands << command;
}

void SyncTimer::queueClipToStart(ClipAudioSource *clip) {
    queueClipToStartOnChannel(clip, -1);
}

void SyncTimer::queueClipToStop(ClipAudioSource *clip) {
    queueClipToStopOnChannel(clip, -1);
}

void SyncTimer::start(int bpm) {
    qDebug() << "#### Starting timer with bpm " << bpm << " and interval " << getInterval(bpm);
    setBpm(quint64(bpm));
#ifdef DEBUG_SYNCTIMER_TIMING
    d->intervals.clear();
    d->lastRound = frame_clock::now();
#endif
    d->stepReadHeadOnStart = d->stepReadHead;
    d->timerThread->resume();
}

void SyncTimer::stop() {
    cerr << "#### Stopping timer" << endl;

    QMutexLocker locker(&d->mutex);
    if(!d->timerThread->isPaused()) {
        d->timerThread->pause();
    }

    d->beat = 0;
    d->cumulativeBeat = 0;
    d->jackPlayhead = 0;

    // A touch of hackery to ensure we end immediately, and leave a clean state
    for (quint64 step = 0; step < d->stepRingCount; ++step) {
        StepData *stepData = d->stepRing.at((step + d->stepReadHead) % d->stepRingCount);
        if (!stepData->played) {
            // First, spit out all the queued midi messages immediately, but in strict order, and only off notes...
            juce::MidiBuffer onlyOffs;
            for (const juce::MidiBuffer& buffer : qAsConst(stepData->midiBuffers)) {
                for (const juce::MidiMessageMetadata& message : buffer) {
                    if (message.getMessage().isNoteOff()) {
                        onlyOffs.addEvent(message.getMessage(), 0);
                    }
                }
            }
            if (!onlyOffs.isEmpty()) {
                d->buffersForImmediateDispatch.append(onlyOffs);
            }
            // Now for the clip commands
            for (ClipCommand *clipCommand : qAsConst(stepData->clipCommands)) {
                // Actually run all the commands (so we don't end up in a weird state), but also
                // set all the volumes to 0 so we don't make the users' ears bleed
                clipCommand->changeVolume = true;
                clipCommand->volume = 0;
                scheduleClipCommand(clipCommand, 0);
                Q_EMIT clipCommandSent(clipCommand);
            }
            stepData->played = true;
        }
    }

    // Make sure we're actually informing about any clips that have been sent out, in case we
    // hit somewhere between a jack roll and a synctimer tick
    for (ClipCommand *clipCommand : d->sentOutClips) {
        Q_EMIT clipCommandSent(clipCommand);
    }
    d->sentOutClips.clear();
#ifdef DEBUG_SYNCTIMER_TIMING
    qDebug() << d->intervals;
#endif
}

int SyncTimer::getInterval(int bpm) {
    // Calculate interval
    return 60000 / (bpm * BeatSubdivisions);
}

float SyncTimer::subbeatCountToSeconds(quint64 bpm, quint64 beats) const
{
    return d->timerThread->subbeatCountToNanoseconds(qBound(quint64(BPM_MINIMUM), bpm, quint64(BPM_MAXIMUM)), beats) / (float)1000000000;
}

quint64 SyncTimer::secondsToSubbeatCount(quint64 bpm, float seconds) const
{
    return d->timerThread->nanosecondsToSubbeatCount(qBound(quint64(BPM_MINIMUM), bpm, quint64(BPM_MAXIMUM)), floor(seconds * (float)1000000000));
}

int SyncTimer::getMultiplier() {
    return BeatSubdivisions;
}

quint64 SyncTimer::getBpm() const
{
    return d->timerThread->getBpm();
}

void SyncTimer::setBpm(quint64 bpm)
{
    if (d->timerThread->getBpm() != bpm) {
        d->timerThread->setBPM(bpm);
        d->jackSubbeatLengthInMicroseconds = d->timerThread->subbeatCountToNanoseconds(d->timerThread->getBpm(), 1) / 1000;
        Q_EMIT bpmChanged();
        Q_EMIT scheduleAheadAmountChanged();
    }
}

quint64 SyncTimer::scheduleAheadAmount() const
{
    return (d->timerThread->nanosecondsToSubbeatCount(d->timerThread->getBpm(), d->jackLatency * (float)1000000)) + 1;
}

int SyncTimer::beat() const {
    return d->beat;
}

quint64 SyncTimer::cumulativeBeat() const {
    return d->cumulativeBeat;
}

const quint64 &SyncTimer::jackPlayhead() const
{
    if (d->timerThread->isPaused()) {
        return d->stepReadHead;
    }
    return d->jackPlayhead;
}

const quint64 &SyncTimer::jackPlayheadUsecs() const
{
    if (d->timerThread->isPaused()) {
        return d->stepNextPlaybackPosition;
    }
    return d->jackNextPlaybackPosition;
}

const quint64 &SyncTimer::jackSubbeatLengthInMicroseconds() const
{
    return d->jackSubbeatLengthInMicroseconds;
}

void SyncTimer::scheduleClipCommand(ClipCommand *clip, quint64 delay)
{
    StepData *stepData = d->stepRing.at(d->delayedStep(delay));
    stepData->ensureFresh();
    bool foundExisting{false};
    for (ClipCommand *clipCommand : qAsConst(stepData->clipCommands)) {
        if (clipCommand->equivalentTo(clip)) {
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
        delete clip;
    } else {
        stepData->clipCommands << clip;
    }
}

void SyncTimer::scheduleTimerCommand(quint64 delay, TimerCommand *command)
{
    StepData *stepData = d->stepRing.at(d->delayedStep(delay));
    stepData->ensureFresh();
    stepData->timerCommands << command;
}

void SyncTimer::scheduleTimerCommand(quint64 delay, int operation, int parameter1, int parameter2, int parameter3, const QVariant &variantParameter)
{
    TimerCommand* timerCommand = new TimerCommand;
    timerCommand->operation = static_cast<TimerCommand::Operation>(operation);
    timerCommand->parameter = parameter1;
    timerCommand->parameter2 = parameter2;
    timerCommand->parameter3 = parameter3;
    if (variantParameter.isValid()) {
        timerCommand->variantParameter = variantParameter;
    }
    scheduleTimerCommand(delay, timerCommand);
}

void SyncTimer::scheduleNote(unsigned char midiNote, unsigned char midiChannel, bool setOn, unsigned char velocity, quint64 duration, quint64 delay)
{
    StepData *stepData = d->stepRing.at(d->delayedStep(delay));
    stepData->ensureFresh();
    juce::MidiBuffer &addToThis = stepData->midiBuffers[0];
    unsigned char note[3];
    if (setOn) {
        note[0] = 0x90 + midiChannel;
    } else {
        note[0] = 0x80 + midiChannel;
    }
    note[1] = midiNote;
    note[2] = velocity;
    const int onOrOff = setOn ? 1 : 0;
    addToThis.addEvent(note, 3, onOrOff);
    if (setOn && duration > 0) {
        // Schedule an off note for that position
        scheduleNote(midiNote, midiChannel, false, 64, 0, delay + duration);
    }
}

void SyncTimer::scheduleMidiBuffer(const juce::MidiBuffer& buffer, quint64 delay)
{
//     qDebug() << Q_FUNC_INFO << "Adding buffer with" << buffer.getNumEvents() << "notes, with delay" << delay << "giving us ring step" << d->delayedStep(delay) << "at ring playhead" << d->stepReadHead << "with cumulative beat" << d->cumulativeBeat;
    StepData *stepData = d->stepRing.at(d->delayedStep(delay));
    stepData->ensureFresh();
    stepData->midiBuffers << buffer;
}

void SyncTimer::sendNoteImmediately(unsigned char midiNote, unsigned char midiChannel, bool setOn, unsigned char velocity)
{
    StepData *stepData = d->stepRing.at(d->delayedStep(0));
    stepData->ensureFresh();
    if (setOn) {
        stepData->midiBuffers << juce::MidiBuffer(juce::MidiMessage::noteOn(midiChannel + 1, midiNote, juce::uint8(velocity)));
    } else {
        stepData->midiBuffers << juce::MidiBuffer(juce::MidiMessage::noteOff(midiChannel + 1, midiNote));
    }
}

void SyncTimer::sendMidiBufferImmediately(const juce::MidiBuffer& buffer)
{
    StepData *stepData = d->stepRing.at(d->delayedStep(0));
    stepData->ensureFresh();
    stepData->midiBuffers << buffer;
}

bool SyncTimer::timerRunning() {
    return !d->timerThread->isPaused();
}

#include "SyncTimer.moc"

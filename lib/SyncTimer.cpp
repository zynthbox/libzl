#include <sched.h>
#include <sys/mman.h>

#include "SyncTimer.h"
#include "ClipAudioSource.h"
#include "ClipCommand.h"
#include "libzl.h"
#include "Helper.h"
#include "MidiRouter.h"
#include "SamplerSynth.h"
#include "TimerCommand.h"
#include "TransportManager.h"

#include <QDebug>
#include <QHash>
#include <QMutex>
#include <QProcess>
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

struct alignas(64) StepData {
    StepData() { }
    ~StepData() {
        qDeleteAll(timerCommands);
        qDeleteAll(clipCommands);
    }
    // Call this before accessing the data to ensure that it is fresh
    void ensureFresh() {
        if (played) {
            played = false;
            // It's our job to delete the timer commands, so do that first
            for (TimerCommand* command : timerCommands) {
                delete command;
            }
            // The clip commands, once sent out, become owned by SampelerSynth, so leave them alone
            timerCommands.clear();
            clipCommands.clear();
            midiBuffer.clear();
        }
    }
    void insertMidiBuffer(const juce::MidiBuffer &buffer) {
        midiBuffer.addEvents(buffer, 0, -1, midiBuffer.getLastEventTime());
    }
    juce::MidiBuffer midiBuffer;
    QList<ClipCommand*> clipCommands;
    QList<TimerCommand*> timerCommands;

    StepData *previous{nullptr};
    StepData *next{nullptr};

    quint64 index{0};

    // SyncTimer sets this true to mark that it has played the step
    // Conceptually, a step starts out having been played (meaning it is not interesting to the process call),
    // and it is set to false by ensureFresh above, which is called any time just before adding anything to a step.
    bool played{true};
};

struct alignas(32) ClipCommandRingEntry {
    ClipCommand *clipCommand{nullptr};
    ClipCommandRingEntry *previous{nullptr};
    ClipCommandRingEntry *next{nullptr};
};

using frame_clock = std::conditional_t<
    std::chrono::high_resolution_clock::is_steady,
    std::chrono::high_resolution_clock,
    std::chrono::steady_clock>;

#define NanosecondsPerMinute 60000000000
#define NanosecondsPerSecond 1000000000
#define NanosecondsPerMillisecond 1000000
#define BeatSubdivisions 96
#define BeatsPerBar 4
// The midi beat clock signal should go out at a rate of 24ppqn - at the current beat subdivision of 96, that makes it every 3rd tick of our step ring
#define TicksPerMidiBeatClock 3
static const jack_midi_data_t jackMidiBeatMessage{0xF8};
// There's BeatsPerBar * BeatSubdivisions ticks per bar
#define TicksPerBar 384
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

    QMutex mutex;
    QWaitCondition waitCondition;

    // This is equivalent to .1 ms
    const frame_clock::duration spinTime{frame_clock::duration(100000)};
    const std::chrono::nanoseconds nanosecondsPerMinute{NanosecondsPerMinute};

    bool aborted{false};
    bool paused{true};
};

using TimerCallback = void(*)(int);
#define CallbackSpaces 16

#define FreshCommandStashSize 4096
#define StepRingCount 32768
SyncTimerThread *timerThread{nullptr};
class SyncTimerPrivate {
public:
    SyncTimerPrivate(SyncTimer *q)
        : q(q)
    {
        transportManager = TransportManager::instance(q);
        timerThread = new SyncTimerThread(q);
        int result = mlock(stepRing, sizeof(StepData) * StepRingCount);
        if (result != 0) {
            qDebug() << Q_FUNC_INFO << "Error locking step ring memory" << strerror(result);
        }
        StepData* previous{&stepRing[StepRingCount - 1]};
        for (quint64 i = 0; i < StepRingCount; ++i) {
            stepRing[i].index = i;
            previous->next = &stepRing[i];
            stepRing[i].previous = previous;
            previous = &stepRing[i];
        }
        stepReadHead = stepRing;

        ClipCommandRingEntry* clipPrevious{&sentOutClipsRing[FreshCommandStashSize - 1]};
        for (quint64 i = 0; i < FreshCommandStashSize; ++i) {
            clipPrevious->next = &sentOutClipsRing[i];
            sentOutClipsRing[i].previous = clipPrevious;
            clipPrevious = &sentOutClipsRing[i];
        }
        sentOutClipsReadHead = sentOutClipsWriteHead = sentOutClipsRing;

        for (int i = 0; i < FreshCommandStashSize; ++i) {
            freshClipCommands << new ClipCommand;
            freshTimerCommands << new TimerCommand;
        }
        samplerSynth = SamplerSynth::instance();
        // Dangerzone - direct connection from another thread. Yes, dangerous, but also we need the precision, so we need to dill whit it
        QObject::connect(timerThread, &SyncTimerThread::timeout, q, [this](){ hiResTimerCallback(); }, Qt::DirectConnection);
        QObject::connect(timerThread, &QThread::started, q, [q](){ Q_EMIT q->timerRunningChanged(); });
        QObject::connect(timerThread, &QThread::finished, q, [q](){ Q_EMIT q->timerRunningChanged(); });
        QObject::connect(timerThread, &SyncTimerThread::pausedChanged, q, [q](){ q->timerRunningChanged(); });
        timerThread->start();

        objectGarbageHandler.setInterval(50);
        objectGarbageHandler.setSingleShot(true);
        QObject::connect(&objectGarbageHandler, &QTimer::timeout, q, [this](){
            // Stuff any commands we've been asked to delete back into the list, at a reasonable location, and cleaned up
            QMutableListIterator<TimerCommand*> freshTimerCommandsIterator(freshTimerCommands);
            while (freshTimerCommandsIterator.hasNext() && timerCommandsToDelete.count() > 0) {
                TimerCommand *value = freshTimerCommandsIterator.next();
                if (value == nullptr) {
                    TimerCommand* refreshedCommand = timerCommandsToDelete.takeFirst();
                    TimerCommand::clear(refreshedCommand);
                    freshTimerCommandsIterator.setValue(refreshedCommand);
                }
            }
            QMutableListIterator<ClipCommand*> freshClipCommandsIterator(freshClipCommands);
            while (freshClipCommandsIterator.hasNext() && clipCommandsToDelete.count() > 0) {
                ClipCommand *value = freshClipCommandsIterator.next();
                if (value == nullptr) {
                    ClipCommand *refreshedCommand = clipCommandsToDelete.takeFirst();
                    ClipCommand::clear(refreshedCommand);
                    freshClipCommandsIterator.setValue(refreshedCommand);
                }
            }
        });
    }
    ~SyncTimerPrivate() {
        timerThread->requestAbort();
        timerThread->wait();
        if (jackClient) {
            jack_client_close(jackClient);
        }
    }
    SyncTimer *q{nullptr};
    SamplerSynth *samplerSynth{nullptr};
    TransportManager *transportManager{nullptr};
    int playingClipsCount = 0;
    int beat = 0;
    quint64 cumulativeBeat = 0;
    int callbackCount{0};
    TimerCallback callbacks[CallbackSpaces];

    ClipCommandRingEntry sentOutClipsRing[FreshCommandStashSize];
    ClipCommandRingEntry *sentOutClipsReadHead{nullptr};
    ClipCommandRingEntry *sentOutClipsWriteHead{nullptr};

    StepData stepRing[StepRingCount];
    // The next step to be read in the step ring
    StepData* stepReadHead{nullptr};
    quint64 stepNextPlaybackPosition{0};
    /**
     * \brief Get the ring buffer position based on the given delay from the current playback position (cumulativeBeat if playing, or stepReadHead if not playing)
     * @param delay The delay of the position to use
     * @param ensureFresh Set this to false to disable the freshness insurance
     * @return The stepRing position to use for the given delay
     */
    inline StepData* delayedStep(quint64 delay, bool ensureFresh = true) {
        quint64 step{0};
        if (isPaused) {
            // If paused, base the delay on the current stepReadHead
            step = (stepReadHead->index + delay + 1) % StepRingCount;
        } else {
            // If running, base the delay on the current cumulativeBeat (adjusted to at least stepReadHead, just in case)
            step = (stepReadHeadOnStart + qMax(cumulativeBeat + delay, jackPlayhead + 1)) % StepRingCount;
        }
        StepData *stepData = &stepRing[step];
        if (ensureFresh) {
            stepData->ensureFresh();
        }
        return stepData;
    }

    QList<TimerCommand*> timerCommandsToDelete;
    QList<TimerCommand*> freshTimerCommands;
    QList<ClipCommand*> clipCommandsToDelete;
    QList<ClipCommand*> freshClipCommands;
    QTimer objectGarbageHandler;

    #ifdef DEBUG_SYNCTIMER_TIMING
    frame_clock::time_point lastRound;
    QList<long> intervals;
#endif
    int i{0};
    void hiResTimerCallback() {
#ifdef DEBUG_SYNCTIMER_TIMING
        frame_clock::time_point thisRound = frame_clock::now();
        intervals << (thisRound - lastRound).count();
        lastRound = thisRound;
#endif
        while (cumulativeBeat < (jackPlayhead + (scheduleAheadAmount * 2))) {
            // Call any callbacks registered to us
            for (int i = 0; i < callbackCount; ++i) {
                callbacks[i](beat);
            }

            // Spit out a touch of useful information on beat zero
            if (beat == 0 && samplerSynth->engine()) {
                qDebug() << "Current tracktion/juce CPU usage:" << samplerSynth->engine()->getDeviceManager().getCpuUsage() << "with total jack process call saturation at:" << samplerSynth->cpuLoad();
            }

            // Increase the current beat as we understand it
            beat = (beat + 1) % (BeatSubdivisions * 4);
            ++cumulativeBeat;
        }

        // Finally, notify any listeners that commands have been sent out
        // You must not delete the commands themselves here, as SamplerSynth takes ownership of them
        while (sentOutClipsReadHead->clipCommand) {
            Q_EMIT q->clipCommandSent(sentOutClipsReadHead->clipCommand);
            sentOutClipsReadHead->clipCommand = nullptr;
            sentOutClipsReadHead = sentOutClipsReadHead->next;
        }
    }

    jack_client_t* jackClient{nullptr};
    jack_port_t* jackPort{nullptr};
    quint64 jackPlayhead{0};
    // Used to calculate the quantized block rate BPM for the jack transport position's beats_per_minute field (jackBeatsPerMinute)
    double jackPlayheadBpm{120};
    int32_t jackBar{0};
    int32_t jackBeat{0};
    int32_t jackBeatTick{0};
    int32_t jackTick{0};
    int32_t jackBarStartTick{0};
    int32_t jackMidiBeatTick{0};
    double jackBeatsPerMinute{0.0};
    quint64 stepReadHeadOnStart{0};
    jack_time_t jackMostRecentNextUsecs{0};
    jack_time_t jackStartTime{0};
    quint64 jackNextPlaybackPosition{0};
    quint64 jackSubbeatLengthInMicroseconds{0};
    quint64 jackLatency{0};
    bool isPaused{true};

    quint64 jackPlayheadReturn{0};
    quint64 jackSubbeatLengthInMicrosecondsReturn{0};

// Temporarily leaving this behind - this was from when we used to call SyncTimer's process function explicitly from MidiRouter
//     int process(jack_nframes_t nframes, void *buffer, quint64 *jackPlayheadReturn, quint64 *jackSubbeatLengthInMicrosecondsReturn) {
// Clear the buffer that MidiRouter gives us, because we want to be sure we've got a blank slate to work with

    juce::MidiBuffer missingBitsBufferInstance;
    // This looks like a Jack process call, but it is in fact called explicitly by MidiRouter for insurance purposes (doing it like
    // this means we've got tighter control, and we really don't need to pass it through jack anyway)
    int process(jack_nframes_t nframes) {
        // const std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();
        void *buffer = jack_port_get_buffer(jackPort, nframes);
        jack_midi_clear_buffer(buffer);
#ifdef DEBUG_SYNCTIMER_JACK
        quint64 stepCount = 0;
        QList<int> commandValues;
        QList<int> noteValues;
        QList<int> velocities;
        QList<quint64> framePositions;
        QList<quint64> frameSteps;
        quint64 eventCount = 0;
#endif

        jack_nframes_t current_frames;
        jack_time_t current_usecs;
        jack_time_t next_usecs;
        float period_usecs;
        jack_get_cycle_times(jackClient, &current_frames, &current_usecs, &next_usecs, &period_usecs);
        const quint64 microsecondsPerFrame = (next_usecs - current_usecs) / nframes;

        double thisStepBpm{jackPlayheadBpm};
        double thisStepSubbeatLengthInMicroseconds{double(timerThread->subbeatCountToNanoseconds(jackPlayheadBpm, 1)) / 1000.0};

        // Setting here because we need the this-process value, not the next-process
        jackPlayheadReturn = jackPlayhead;
        jackSubbeatLengthInMicrosecondsReturn = thisStepSubbeatLengthInMicroseconds;

        if (!isPaused) {
            if (jackPlayhead == 0) {
                // first run for this playback session, let's do a touch of setup
                jackNextPlaybackPosition = current_usecs;
                jackBar = jackBeat = jackBeatTick = jackTick = 0;
                // We need to send out a beat clock tick on the first position as well, so let's make sure we do that
                jackMidiBeatTick = TicksPerMidiBeatClock - 1;
                transportManager->restartTransport();
            }
            jackMostRecentNextUsecs = next_usecs;
        }
        if (stepNextPlaybackPosition == 0) {
            stepNextPlaybackPosition = current_usecs;
        }

        jack_time_t currentStepUsecsStart{0};
        jack_time_t currentStepUsecsEnd = qMin(period_usecs, float(stepNextPlaybackPosition - current_usecs));
        double updatedJackBeatsPerMinute{0};
        jack_nframes_t firstAvailableFrame{0};
        jack_nframes_t relativePosition{0};
        int errorCode{0};
        juce::MidiBuffer *missingBitsBuffer{nullptr};
        // As long as the next playback position is before this period is supposed to end, and we have frames for it, let's post some events
        while (stepNextPlaybackPosition < next_usecs && firstAvailableFrame < nframes) {
            StepData *stepData = stepReadHead;
            // Next roll for next time (also do it now, as we're reading out of it)
            stepReadHead = stepReadHead->next;
            // If the notes are in the past, they need to be scheduled as soon as we can, so just put those on position 0, and if we are here, that means that ending up in the future is a rounding error, so clamp that
            if (stepNextPlaybackPosition <= current_usecs) {
                relativePosition = firstAvailableFrame;
                ++firstAvailableFrame;
            } else {
                relativePosition = std::clamp<jack_nframes_t>((stepNextPlaybackPosition - current_usecs) / microsecondsPerFrame, firstAvailableFrame, nframes - 1);
                firstAvailableFrame = relativePosition;
            }
            // Make sure there's a midi beat pulse going out if one is needed
            ++jackMidiBeatTick;
            if (jackMidiBeatTick == TicksPerMidiBeatClock) {
                jack_midi_event_write(buffer, relativePosition, &jackMidiBeatMessage, 1);
                jackMidiBeatTick = 0;
            }
            // In case we're cycling through stuff we've already played, let's just... not do anything with that
            // Basically that just means nobody else has attempted to do stuff with the step since we last played it
            if (!stepData->played) {
                // First, let's get the midi messages sent out
                for (const juce::MidiMessageMetadata &juceMessage : qAsConst(stepData->midiBuffer)) {
                    if (firstAvailableFrame >= nframes) {
                        qWarning() << "First available frame is in the future - that's a problem";
                        break;
                    }
                    errorCode = jack_midi_event_write(buffer, relativePosition,
                        const_cast<jack_midi_data_t*>(juceMessage.data), // this might seems odd, but it's really only because juce's internal store is const here, and the data types are otherwise the same
                        size_t(juceMessage.numBytes) // this changes signedness, but from a lesser space (int) to a larger one (unsigned long)
                    );
                    if (errorCode == ENOBUFS) {
                        qWarning() << "Ran out of space while writing events - scheduling the event there's not enough space for to be fired first next round";
                        if (!missingBitsBuffer) {
                            missingBitsBuffer = &missingBitsBufferInstance;
                        }
                        // Schedule the rest of the buffer for immediate dispatch on next go-around
                        missingBitsBuffer->addEvent(juceMessage.getMessage(), 0);
                    } else {
                        if (errorCode != 0) {
                            qWarning() << Q_FUNC_INFO << "Error writing midi event:" << -errorCode << strerror(-errorCode);
                        }
#ifdef DEBUG_SYNCTIMER_JACK
                        ++eventCount;
                        commandValues << juceMessage.data[0]; noteValues << juceMessage.data[1]; velocities << juceMessage.data[2];
#endif
                    }
                }

                // Then do direct-control samplersynth things
                for (ClipCommand *clipCommand : qAsConst(stepData->clipCommands)) {
                    // Using the protected function, which only we (and SamplerSynth) can use, to ensure less locking
                    samplerSynth->handleClipCommand(clipCommand, jackPlayhead);
                    sentOutClipsWriteHead->clipCommand = clipCommand;
                    sentOutClipsWriteHead = sentOutClipsWriteHead->next;
                }

                // Do playback control things as the last thing, otherwise we might end up affecting things
                // currently happening (like, if we stop playback on the last step of a thing, we still want
                // notes on that step to have been played and so on)
                for (TimerCommand *command : qAsConst(stepData->timerCommands)) {
                    Q_EMIT q->timerCommand(command);
                    switch (command->operation) {
                        case TimerCommand::StartPlaybackOperation:
                            Q_EMIT q->pleaseStartPlayback();
                            break;
                        case TimerCommand::StopPlaybackOperation:
                            Q_EMIT q->pleaseStopPlayback();
                            break;
                        case TimerCommand::StartClipLoopOperation:
                        case TimerCommand::StopClipLoopOperation:
                            {
                                ClipCommand *clipCommand = static_cast<ClipCommand *>(command->variantParameter.value<void*>());
                                if (clipCommand) {
                                    samplerSynth->handleClipCommand(clipCommand, jackPlayhead);
                                    sentOutClipsWriteHead->clipCommand = clipCommand;
                                    sentOutClipsWriteHead = sentOutClipsWriteHead->next;
                                } else {
                                    qWarning() << Q_FUNC_INFO << "Failed to retrieve clip command from clip based timer command";
                                }
                                command->variantParameter.clear();
                            }
                            break;
                        case TimerCommand::SamplerChannelEnabledStateOperation:
                            samplerSynth->setChannelEnabled(command->parameter, command->parameter2);
                            break;
                        case TimerCommand::ClipCommandOperation:
                            {
                                ClipCommand *clipCommand = static_cast<ClipCommand *>(command->dataParameter);
                                if (clipCommand) {
                                    samplerSynth->handleClipCommand(clipCommand, jackPlayhead);
                                    sentOutClipsWriteHead->clipCommand = clipCommand;
                                    sentOutClipsWriteHead = sentOutClipsWriteHead->next;
                                } else {
                                    qWarning() << Q_FUNC_INFO << "Failed to retrieve clip command from clip based timer command";
                                }
                                command->dataParameter = nullptr;
                            }
                            break;
                        case TimerCommand::SetBpmOperation:
                            {
                                const quint64 newBpm{std::clamp<quint64>(quint64(command->parameter), 50, 200)};
                                q->setBpm(newBpm);
                                thisStepBpm = newBpm;
                            }
                            break;
                        case TimerCommand::RegisterCASOperation:
                        case TimerCommand::UnregisterCASOperation:
                            {
                                ClipAudioSource *clip = static_cast<ClipAudioSource*>(command->dataParameter);
                                if (clip) {
                                    if (command->operation == TimerCommand::RegisterCASOperation) {
                                        samplerSynth->registerClip(clip);
                                    } else {
                                        samplerSynth->unregisterClip(clip);
                                    }
                                } else {
                                    qWarning() << Q_FUNC_INFO << "Failed to retrieve clip from clip registration timer command";
                                }
                            }
                            break;
                        case TimerCommand::StartPartOperation:
                        case TimerCommand::StopPartOperation:
                        case TimerCommand::InvalidOperation:
                        default:
                            break;
                    }
                }
                stepData->played = true;
            }
            // Update our internal BPM state, based on what we had on the previous step
            if (jackPlayheadBpm != thisStepBpm) {
                // update the playhead's BPM
                jackPlayheadBpm = thisStepBpm;
                // update the subbeat length in ms
                thisStepSubbeatLengthInMicroseconds = timerThread->subbeatCountToNanoseconds(jackPlayheadBpm, 1) / 1000;
            }
            // Add the amount of the BPM value appropriate to this step's duration inside the current period
            updatedJackBeatsPerMinute += jackPlayheadBpm * double(currentStepUsecsEnd - currentStepUsecsStart) / period_usecs;
            // qDebug() << Q_FUNC_INFO << "After a step between" << currentStepUsecsStart << "and" << currentStepUsecsEnd << "the updated jack bpm is" << updatedJackBeatsPerMinute;
            const quint64 nextStepUsecsEnd = qMin(float(currentStepUsecsEnd + thisStepSubbeatLengthInMicroseconds), period_usecs);
            currentStepUsecsStart = currentStepUsecsEnd;
            currentStepUsecsEnd = nextStepUsecsEnd;
            // Update our timecode data
            ++jackTick;
            ++jackBeatTick;
            if (jackBeatTick == BeatSubdivisions) {
                jackBeatTick = 0;
                ++jackBeat;
                if (jackBeat == BeatsPerBar) {
                    jackBeat = 0;
                    ++jackBar;
                    jackBarStartTick = jackTick;
                }
            }
            if (!isPaused) {
                // Next roll for next time
                ++jackPlayhead;
                jackNextPlaybackPosition += thisStepSubbeatLengthInMicroseconds;
#ifdef DEBUG_SYNCTIMER_JACK
                    ++stepCount;
#endif
            }
            // Now roll to the next step's playback position
            stepNextPlaybackPosition += thisStepSubbeatLengthInMicroseconds;
        }
        // Finally, update with whatever is left
        updatedJackBeatsPerMinute += jackPlayheadBpm * double(currentStepUsecsEnd - currentStepUsecsStart) / period_usecs;
        jackBeatsPerMinute = std::round(updatedJackBeatsPerMinute * 100.0) / 100.0; // Round to within the nearest two decimal points - otherwise we run into precision issues
        // qDebug() << Q_FUNC_INFO << "Final updated jack beats per minute:" << jackBeatsPerMinute;
        // If we've had anything added to the buffer for missing bits, make sure we append that for next time 'round.
        // As a note, this is most likely to be an extremely rare situation (that's kind of a lot of events), but just
        // in case, it's good to cover this base.
        if (missingBitsBuffer) {
            q->sendMidiBufferImmediately(*missingBitsBuffer);
            missingBitsBuffer->clear();
            missingBitsBuffer = nullptr;
        }
#ifdef DEBUG_SYNCTIMER_JACK
        if (eventCount > 0) {
            if (uint32_t lost = jack_midi_get_lost_event_count(buffer)) {
                qDebug() << "Lost some notes:" << lost;
            }
            qDebug() << "We advanced jack playback by" << stepCount << "steps, and are now at position" << jackPlayhead << "and we filled up jack with" << eventCount << "events" << nframes << jackSubbeatLengthInMicroseconds << frameSteps << framePositions << commandValues << noteValues << velocities;
        } else {
            qDebug() << "We advanced jack playback by" << stepCount << "steps, and are now at position" << jackPlayhead << "and scheduled no notes";
        }
#endif
        // const std::chrono::duration<double, std::milli> ms_double = std::chrono::high_resolution_clock::now() - t1;
        // if (ms_double.count() > 0.2) {
        //     qDebug() << Q_FUNC_INFO << ms_double.count() << "ms after" << belowThreshold << "runs under 0.2ms";
        //     belowThreshold = 0;
        // } else {
        //     ++belowThreshold;
        // }

        return 0;
    }
    int belowThreshold{0};
    int xrun() {
#ifdef DEBUG_SYNCTIMER_JACK
        qDebug() << "SyncTimer detected XRun";
#endif
        return 0;
    }

    quint64 scheduleAheadAmount{0};
    void updateScheduleAheadAmount() {
        scheduleAheadAmount = (timerThread->nanosecondsToSubbeatCount(timerThread->getBpm(), jackLatency * (float)1000000)) + 1;
        QMetaObject::invokeMethod(q, "scheduleAheadAmountChanged", Qt::QueuedConnection);
    }
};

static int client_process(jack_nframes_t nframes, void* arg) {
    // Just roll empty, we're not really processing anything for SyncTimer here, MidiRouter does that explicitly
    static_cast<SyncTimerPrivate*>(arg)->process(nframes);
    return 0;
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
                d->updateScheduleAheadAmount();
                qDebug() << "Latency changed, max is now" << range.max << "That means we will now suggest scheduling things" << d->q->scheduleAheadAmount() << "steps into the future";
            }
        }
    }
}

SyncTimer::SyncTimer(QObject *parent)
    : QObject(parent)
    , d(new SyncTimerPrivate(this))
{
    d->jackSubbeatLengthInMicroseconds = timerThread->subbeatCountToNanoseconds(timerThread->getBpm(), 1) / 1000;
    connect(timerThread, &SyncTimerThread::pausedChanged, this, [this](){
        d->isPaused = timerThread->isPaused();
    });
    // Open the client.
    jack_status_t real_jack_status{};
    d->jackClient = jack_client_open("SyncTimer", JackNullOption, &real_jack_status);
    if (d->jackClient) {
        // Register the MIDI output port.
        d->jackPort = jack_port_register(d->jackClient, "midi_out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
        if (d->jackPort) {
            // Set the process callback.
            if (jack_set_process_callback(d->jackClient, client_process, static_cast<void*>(d)) == 0) {
                jack_set_xrun_callback(d->jackClient, client_xrun, static_cast<void*>(d));
                jack_set_latency_callback (d->jackClient, client_latency_callback, static_cast<void*>(d));
                // Activate the client.
                if (jack_activate(d->jackClient) == 0) {
                    qInfo() << "Successfully created and set up the SyncTimer's Jack client";
                    jack_latency_range_t range;
                    jack_port_get_latency_range (d->jackPort, JackPlaybackLatency, &range);
                    jack_nframes_t bufferSize = jack_get_buffer_size(d->jackClient);
                    jack_nframes_t sampleRate = jack_get_sample_rate(d->jackClient);
                    d->jackLatency = (1000 * (double)qMax(bufferSize, range.max)) / (double)sampleRate;
                    d->updateScheduleAheadAmount();
                    qDebug() << "SyncTimer: Buffer size is supposed to be" << bufferSize << "but our maximum latency is" << range.max << "and we should be using that one to calculate how far out things should go, as that should include the amount of extra buffers alsa might (and likely does) use.";
                    qDebug() << "SyncTimer: However, as that is sometimes zero, we use the highest of the two. That means we will now suggest scheduling things" << scheduleAheadAmount() << "steps into the future";
                } else {
                    qWarning() << "SyncTimer: Failed to activate SyncTimer Jack client";
                }
            } else {
                qWarning() << "SyncTimer: Failed to set the SyncTimer Jack processing callback";
            }
        } else {
            qWarning() << "SyncTimer: Could not register SyncTimer Jack output port";
        }
    } else {
        qWarning() << "SyncTimer: Could not create SyncTimer Jack client.";
    }
}

SyncTimer::~SyncTimer() {
    delete d;
}

void SyncTimer::addCallback(void (*functionPtr)(int)) {
    qDebug() << Q_FUNC_INFO << "Adding callback" << functionPtr << "at position" << d->callbackCount;
    d->callbacks[d->callbackCount] = functionPtr;
    d->callbackCount++;
}

void SyncTimer::removeCallback(void (*functionPtr)(int)) {
    bool foundCallback{false};
    for (int i = 0; i < CallbackSpaces; ++i) {
        if (foundCallback) {
            if (i < CallbackSpaces - 1) {
                d->callbacks[i] = d->callbacks[i + 1];
            }
        } else {
            if (functionPtr == d->callbacks[i]) {
                foundCallback = true;
                d->callbacks[i] = nullptr;
            }
        }
    }
    qDebug() << Q_FUNC_INFO << "Removing callback" << functionPtr << " - found it to remove:" << foundCallback;
}

void SyncTimer::queueClipToStartOnChannel(ClipAudioSource *clip, int midiChannel)
{
    ClipCommand *command = getClipCommand();
    command->clip = clip;
    command->midiChannel = midiChannel;
    command->midiNote = 60;
    command->changeVolume = true;
    command->volume = 1.0;
    command->looping = true;
    // When explicity starting a clip in a looping state, we want to /restart/ the loop, not start multiple loops (to run multiple at the same time, sample-trig can do that for us)
    command->stopPlayback = true;
    command->startPlayback = true;

    const quint64 nextZeroBeat = timerThread->isPaused() ? 0 : (BeatSubdivisions * 4) - (d->cumulativeBeat % (BeatSubdivisions * 4));
//     qDebug() << "Queueing up" << clip << "to start, with jack and timer zero beats at" << nextZeroBeat << "at beats" << d->cumulativeBeat << "meaning we want positions" << (d->cumulativeBeat + nextZeroBeat < d->jackPlayhead ? nextZeroBeat + BeatSubdivisions * 4 : nextZeroBeat);
    scheduleClipCommand(command, d->cumulativeBeat + nextZeroBeat < d->jackPlayhead ? nextZeroBeat + BeatSubdivisions * 4 : nextZeroBeat);
}

void SyncTimer::queueClipToStopOnChannel(ClipAudioSource *clip, int midiChannel)
{
    // First, remove any references to the clip that we're wanting to stop
    for (quint64 step = 0; step < StepRingCount; ++step) {
        StepData *stepData = &d->stepRing[step];
        if (!stepData->played) {
            QMutableListIterator<ClipCommand *> stepIterator(stepData->clipCommands);
            while (stepIterator.hasNext()) {
                ClipCommand *stepCommand = stepIterator.next();
                if (stepCommand->clip == clip) {
                    deleteClipCommand(stepCommand);
                    stepIterator.remove();
                    break;
                }
            }
        }
    }

    // Then stop it, now, because it should be now
    ClipCommand *command = getClipCommand();
    command->clip = clip;
    command->midiChannel = midiChannel;
    command->midiNote = 60;
    command->stopPlayback = true;
    StepData *stepData{d->delayedStep(0)};
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
    d->stepReadHeadOnStart = (*d->stepReadHead).index;
    timerThread->resume();
}

void SyncTimer::stop() {
    cerr << "#### Stopping timer" << endl;

    if(!timerThread->isPaused()) {
        timerThread->pause();
    }

    d->beat = 0;
    d->cumulativeBeat = 0;
    d->jackPlayhead = 0;

    // A touch of hackery to ensure we end immediately, and leave a clean state
    for (quint64 step = 0; step < StepRingCount; ++step) {
        StepData *stepData = &d->stepRing[(step + d->stepReadHead->index) % StepRingCount];
        if (!stepData->played) {
            // First, spit out all the queued midi messages immediately, but in strict order, and only off notes...
            juce::MidiBuffer onlyOffs;
            for (const juce::MidiMessageMetadata& message : stepData->midiBuffer) {
                if (message.getMessage().isNoteOff()) {
                    onlyOffs.addEvent(message.getMessage(), 0);
                }
            }
            if (!onlyOffs.isEmpty()) {
                sendMidiBufferImmediately(onlyOffs);
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
    while (d->sentOutClipsReadHead->clipCommand) {
        Q_EMIT clipCommandSent(d->sentOutClipsReadHead->clipCommand);
        d->sentOutClipsReadHead->clipCommand = nullptr;
        d->sentOutClipsReadHead = d->sentOutClipsReadHead->next;
    }
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
    return timerThread->subbeatCountToNanoseconds(qBound(quint64(BPM_MINIMUM), bpm, quint64(BPM_MAXIMUM)), beats) / (float)1000000000;
}

quint64 SyncTimer::secondsToSubbeatCount(quint64 bpm, float seconds) const
{
    return timerThread->nanosecondsToSubbeatCount(qBound(quint64(BPM_MINIMUM), bpm, quint64(BPM_MAXIMUM)), floor(seconds * (float)1000000000));
}

int SyncTimer::getMultiplier() {
    return BeatSubdivisions;
}

quint64 SyncTimer::getBpm() const
{
    return timerThread->getBpm();
}

void SyncTimer::setBpm(quint64 bpm)
{
    if (timerThread->getBpm() != bpm) {
        timerThread->setBPM(bpm);
        d->jackSubbeatLengthInMicroseconds = timerThread->subbeatCountToNanoseconds(timerThread->getBpm(), 1) / 1000;
        d->updateScheduleAheadAmount();
        QMetaObject::invokeMethod(this, "bpmChanged", Qt::QueuedConnection);
        // Finally, let's schedule a timer command into the timer - this is to ensure that
        // the bpm is updated for jack transport calculation purposes as well, at the time
        // at which it would be expected. While this does involve adding more work, it is
        // vital that our bpm calculations are correct for syncing reasona, otherwise things
        // just start drifting out of time and that's not nice at all.
        // Optimally, we'd do all our bpm setting through the timer, but i'm pretty sure
        // that'd cause some havoc on the UI side of things... so just not doing that for
        // now.
        TimerCommand *timerCommand = getTimerCommand();
        timerCommand->operation = TimerCommand::SetBpmOperation;
        timerCommand->parameter = bpm;
        scheduleTimerCommand(0, timerCommand);
    }
}

quint64 SyncTimer::scheduleAheadAmount() const
{
    return d->scheduleAheadAmount;
}

int SyncTimer::beat() const {
    return d->beat;
}

quint64 SyncTimer::cumulativeBeat() const {
    return d->cumulativeBeat;
}

const quint64 &SyncTimer::jackPlayhead() const
{
    if (timerThread->isPaused()) {
        return (*d->stepReadHead).index;
    }
    return d->jackPlayhead;
}

const quint64 &SyncTimer::jackPlayheadUsecs() const
{
    if (timerThread->isPaused()) {
        return d->stepNextPlaybackPosition;
    }
    return d->jackNextPlaybackPosition;
}

const quint64 &SyncTimer::jackSubbeatLengthInMicroseconds() const
{
    return d->jackSubbeatLengthInMicroseconds;
}

void SyncTimer::scheduleClipCommand(ClipCommand *command, quint64 delay)
{
    StepData *stepData{d->delayedStep(delay)};
    bool foundExisting{false};
    for (ClipCommand *existingCommand : qAsConst(stepData->clipCommands)) {
        if (existingCommand->equivalentTo(command)) {
            if (command->changeLooping) {
                existingCommand->looping = command->looping;
                existingCommand->changeLooping = true;
            }
            if (command->changePitch) {
                existingCommand->pitchChange = command->pitchChange;
                existingCommand->changePitch = true;
            }
            if (command->changeSpeed) {
                existingCommand->speedRatio = command->speedRatio;
                existingCommand->changeSpeed = true;
            }
            if (command->changeGainDb) {
                existingCommand->gainDb = command->gainDb;
                existingCommand->changeGainDb = true;
            }
            if (command->changeVolume) {
                existingCommand->volume = command->volume;
                existingCommand->changeVolume = true;
            }
            if (command->startPlayback) {
                existingCommand->startPlayback = true;
            }
            foundExisting = true;
        }
    }
    if (foundExisting) {
        deleteClipCommand(command);
    } else {
        stepData->clipCommands << command;
    }
}

void SyncTimer::scheduleTimerCommand(quint64 delay, TimerCommand *command)
{
    StepData *stepData{d->delayedStep(delay)};
    stepData->timerCommands << command;
}

void SyncTimer::scheduleTimerCommand(quint64 delay, int operation, int parameter1, int parameter2, int parameter3, const QVariant &variantParameter)
{
    TimerCommand* timerCommand = getTimerCommand();
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
    StepData *stepData{d->delayedStep(delay)};
    juce::MidiBuffer &addToThis = stepData->midiBuffer;
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
    StepData *stepData{d->delayedStep(delay)};
    stepData->insertMidiBuffer(buffer);
}

void SyncTimer::sendNoteImmediately(unsigned char midiNote, unsigned char midiChannel, bool setOn, unsigned char velocity)
{
    StepData *stepData{d->delayedStep(0)};
    if (setOn) {
        stepData->insertMidiBuffer(juce::MidiBuffer(juce::MidiMessage::noteOn(midiChannel + 1, midiNote, juce::uint8(velocity))));
    } else {
        stepData->insertMidiBuffer(juce::MidiBuffer(juce::MidiMessage::noteOff(midiChannel + 1, midiNote)));
    }
}

void SyncTimer::sendMidiBufferImmediately(const juce::MidiBuffer& buffer)
{
    StepData *stepData{d->delayedStep(0)};
    stepData->insertMidiBuffer(buffer);
}

bool SyncTimer::timerRunning() {
    return !timerThread->isPaused();
}

ClipCommand * SyncTimer::getClipCommand()
{
    QMutableListIterator<ClipCommand*> freshClipCommandsIterator(d->freshClipCommands);
    while (freshClipCommandsIterator.hasNext()) {
        ClipCommand *command = freshClipCommandsIterator.next();
        if (command) {
            QMetaObject::invokeMethod(&d->objectGarbageHandler,"start", Qt::QueuedConnection);
            freshClipCommandsIterator.setValue(nullptr);
            return command;
        }
    }
    return nullptr;
}

void SyncTimer::deleteClipCommand(ClipCommand* command)
{
    d->clipCommandsToDelete << command;
    QMetaObject::invokeMethod(&d->objectGarbageHandler,"start", Qt::QueuedConnection);
}

TimerCommand * SyncTimer::getTimerCommand()
{
    QMutableListIterator<TimerCommand*> freshTimerCommandsIterator(d->freshTimerCommands);
    while (freshTimerCommandsIterator.hasNext()) {
        TimerCommand *command = freshTimerCommandsIterator.next();
        if (command) {
            QMetaObject::invokeMethod(&d->objectGarbageHandler,"start", Qt::QueuedConnection);
            freshTimerCommandsIterator.setValue(nullptr);
            return command;
        }
    }
    return nullptr;
}

void SyncTimer::deleteTimerCommand(TimerCommand* command)
{
    d->timerCommandsToDelete << command;
    QMetaObject::invokeMethod(&d->objectGarbageHandler,"start", Qt::QueuedConnection);
}

void SyncTimer::process(jack_nframes_t /*nframes*/, void */*buffer*/, quint64 *jackPlayhead, quint64 *jackSubbeatLengthInMicroseconds)
{
//     d->process(nframes, buffer, jackPlayhead, jackSubbeatLengthInMicroseconds);
    *jackPlayhead = d->jackPlayheadReturn;
    *jackSubbeatLengthInMicroseconds = d->jackSubbeatLengthInMicrosecondsReturn;
}

void SyncTimer::setPosition(jack_position_t* position) const
{
    position->bar = d->jackBar;
    position->beat = d->jackBeat;
    position->tick = d->jackBeatTick;
    position->bar_start_tick = d->jackBarStartTick;
    position->beats_per_bar = BeatsPerBar;
    position->beat_type = BeatsPerBar;
    position->ticks_per_beat = BeatSubdivisions;
    position->beats_per_minute = d->jackBeatsPerMinute;
}

#include "SyncTimer.moc"

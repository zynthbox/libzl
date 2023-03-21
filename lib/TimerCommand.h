#pragma once

#include <QVariant>

#include "SyncTimer.h"

/**
 * \brief Used to schedule various operations into the timer's playback queue
 */
struct alignas(64) TimerCommand {
    TimerCommand() {};
    // TODO Before shipping this, make sure these are sequential...
    enum Operation {
        InvalidOperation = 0, ///@< An invalid operation, ignored
        StartPlaybackOperation = 8, ///@< Start global playback
        StopPlaybackOperation = 1, ///@< Stop all playback
        StartPartOperation = 2, ///@< Start playing the given part. Pass channel index as parameter 1, track index as parameter2 and part index as parameter3
        StopPartOperation = 4, ///@< Stop playing the given part. Pass channel index as parameter 1, track index as parameter2 and part index as parameter3
        StartClipLoopOperation = 6, ///@< DEPRECATED Use ClipCommandOperation (originally, now handled by segmenthandler: Start playing a clip looped, parameter being the midi channel, parameter2 being the clip ID, and parameter3 being the note, and bigParameter can be used to define a timer offset value for adjusting the part's playback position relative to the timer's cumulative beat)
        StopClipLoopOperation = 7, ///@< DEPRECATED Use ClipCommandOperation (originally, now handled by segmenthandler: Stop playing a clip looping style, parameter being the midi channel to stop it on, parameter2 being the clip ID, and parameter3 being the note)
        SamplerChannelEnabledStateOperation = 8, ///@< Sets the state of a SamplerSynth channel to enabled or not enabled. parameter is the sampler channel (-2 through 9, -2 being uneffected global, -1 being effected global, and 0 through 9 being zl channels), and parameter2 is 0 for disabled, any other number for enabled
        ClipCommandOperation = 9, ///@< Handle a clip command at the given timer point (this could also be done by scheduling the clip command directly)
        RegisterCASOperation = 10001, ///@< INTERNAL - Register a ClipAudioSource with SamplerSynth, so it can be used for playback - dataParameter should contain a ClipAudioSource* object instance
        UnregisterCASOperation = 10002, ///@< INTERNAL - Unregister a ClipAudioSource with SamplerSynth, so it can be used for playback - dataParameter should contain a ClipAudioSource* object instance
    };
    Operation operation{InvalidOperation};
    int parameter{0};
    int parameter2{0};
    int parameter3{0};
    int parameter4{0};
    quint64 bigParameter{0};
    void *dataParameter{nullptr};
    // NOTE: To implementers: Use this sparingly, as QVariants can be expensive to use and this gets handled from a jack call
    QVariant variantParameter;

    static TimerCommand *cloneTimerCommand(const TimerCommand *other) {
        TimerCommand *clonedCommand = SyncTimer::instance()->getTimerCommand();
        clonedCommand->operation = other->operation;
        clonedCommand->parameter = other->parameter;
        clonedCommand->parameter2 = other->parameter2;
        clonedCommand->parameter3 = other->parameter3;
        clonedCommand->parameter4 = other->parameter4;
        clonedCommand->bigParameter = other->bigParameter;
        clonedCommand->dataParameter = other->dataParameter;
        if (other->variantParameter.isValid()) {
            clonedCommand->variantParameter = other->variantParameter;
        }
        return clonedCommand;
    }
};

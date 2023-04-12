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
        StartPlaybackOperation = 1, ///@< Start global playback
        StopPlaybackOperation = 2, ///@< Stop all playback
        StartPartOperation = 3, ///@< Start playing the given part. Pass channel index as parameter 1, track index as parameter2 and part index as parameter3
        StopPartOperation = 4, ///@< Stop playing the given part. Pass channel index as parameter 1, track index as parameter2 and part index as parameter3
        StartClipLoopOperation = 6, ///@< DEPRECATED Use ClipCommandOperation (now handled by segmenthandler, was originally: Start playing a clip looped, parameter being the midi channel, parameter2 being the clip ID, and parameter3 being the note, and bigParameter can be used to define a timer offset value for adjusting the part's playback position relative to the timer's cumulative beat)
        StopClipLoopOperation = 7, ///@< DEPRECATED Use ClipCommandOperation (now handled by segmenthandler, was originally: Stop playing a clip looping style, parameter being the midi channel to stop it on, parameter2 being the clip ID, and parameter3 being the note)
        SamplerChannelEnabledStateOperation = 8, ///@< Sets the state of a SamplerSynth channel to enabled or not enabled. parameter is the sampler channel (-2 through 9, -2 being uneffected global, -1 being effected global, and 0 through 9 being zl channels), and parameter2 is 0 for disabled, any other number for enabled
        ClipCommandOperation = 9, ///@< Handle a clip command at the given timer point (this could also be done by scheduling the clip command directly)
        SetBpmOperation = 10, ///@< Set the BPM of the timer to the value in stored in parameter (this will be clamped to fit between SyncTimer's allowed values)
        AutomationOperation = 11, ///@< Set the value of a given parameter on a given engine on a given channel to a given value. parameter contains the channel (-1 is global fx engines, 0 through 9 being zl channels), parameter2 contains the engine index, parameter3 is the parameter's index, parameter4 is the value
        PassthroughClientOperation = 12, ///@< Set the volume of the given volume channel to the given value. parameter is the channel (-1 is global playback, 0 through 9 being zl channels), parameter2 is the setting index in the list (dry, wetfx1, wetfx2, pan, muted), parameter3 being the left value, parameter4 being right value. If parameter2 is pan or muted, parameter4 is ignored. For volumes, parameter3 and parameter4 can be 0 through 100. For pan, -100 for all left through 100 for all right, with 0 being no pan. For muted, 0 is not muted, any other value is muted.
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

    static void clear(TimerCommand *command) {
        command->operation = InvalidOperation;
        command->parameter = command->parameter2 = command->parameter3 = command->parameter4 = 0;
        command->bigParameter = 0;
        command->dataParameter = nullptr;
        if (command->variantParameter.isValid()) {
            command->variantParameter.clear();
        }
    }
};

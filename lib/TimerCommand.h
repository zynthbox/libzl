#pragma once

#include <QVariant>

/**
 * \brief Used to schedule various operations into the timer's playback queue
 */
struct TimerCommand {
    TimerCommand() {};
    // TODO Before shipping this, make sure these are sequential...
    enum Operation {
        InvalidOperation = 0, ///@< An invalid operation, ignored
        StartPlaybackOperation = 8, ///@< Start global playback
        StopPlaybackOperation = 1, ///@< Stop all playback
        StartPartOperation = 2, ///@< Start playing the given part. Pass track index as parameter 1, sketch index as parameter2 and part index as parameter3
        StopPartOperation = 4, ///@< Stop playing the given part. Pass track index as parameter 1, sketch index as parameter2 and part index as parameter3
        StartClipLoopOperation = 6, ///@< Start playing a clip looped, parameter being the midi channel, parameter2 being the clip ID, and parameter3 being the note, and bigParameter can be used to define a timer offset value for adjusting the part's playback position relative to the timer's cumulative beat
        StopClipLoopOperation = 7, ///@< Stop playing a clop looping style, parameter being the midi channel to stop it on, parameter2 being the clip ID, and parameter3 being the note
        RegisterCASOperation = 10001, ///@< INTERNAL - Register a ClipAudioSource with SamplerSynth, so it can be used for playback - variantParameter should contain a ClipAudioSource* object instance
        UnregisterCASOperation = 10002, ///@< INTERNAL - Unregister a ClipAudioSource with SamplerSynth, so it can be used for playback - variantParameter should contain a ClipAudioSource* object instance
    };
    Operation operation{InvalidOperation};
    int parameter{0};
    int parameter2{0};
    int parameter3{0};
    int parameter4{0};
    quint64 bigParameter{0};
    // NOTE: To implementers: Use this sparingly, as QVariants can be expensive to use and this gets handled from a jack call
    QVariant variantParameter;
};

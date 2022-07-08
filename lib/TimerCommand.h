#pragma once

#include <QVariant>

/**
 * \brief Used to schedule various operations into the timer's playback queue
 */
struct TimerCommand {
    TimerCommand() {};
    enum Operation {
        InvalidOperation = 0, ///@< An invalid operation, ignored
        StopPlaybackOperation = 1, ///@< Stop all playback
        StartPartOperation = 2, ///@< Start playing the given part. Pass track index as parameter 1, sketch index as parameter2 and part index as parameter3
        StopPartOperation = 4, ///@< Stop playing the given part. Pass track index as parameter 1, sketch index as parameter2 and part index as parameter3
    };
    Operation operation{InvalidOperation};
    int parameter{0};
    int parameter2{0};
    int parameter3{0};
    // NOTE: To implementers: Use this sparingly, as QVariants can be expensive to use and this gets handled from a jack call
    QVariant variantParameter;
};

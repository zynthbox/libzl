#pragma once

class ClipAudioSource;
/**
 * \brief Used to schedule clips into the timer's playback queue
 *
 * Roughly equivalent to a midi message, but for clips
 */
struct ClipCommand {
    ClipAudioSource* clip{nullptr};
    int midiNote{-1};
    bool startPlayback{false};
    bool stopPlayback{false};
    // Which slice to use (-1 means no slice, play normal)
    bool changeSlice{false};
    int slice{-1};
    bool changeLooping{false};
    bool looping{false};
    bool changePitch{false};
    float pitchChange{0.0f};
    bool changeSpeed{false};
    float speedRatio{0.0f};
    bool changeGainDb{false};
    float gainDb{0.0f};
    bool changeVolume{false};
    float volume{0.0f};

    bool equivalentTo(ClipCommand *other) const {
        return clip == other->clip
            && (
                (changeSlice == true && other->changeSlice == true && slice == other->slice)
                || (changeSlice == false && other->changeSlice == false && midiNote == other->midiNote)
            );
    }
};

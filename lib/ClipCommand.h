#pragma once

class ClipAudioSource;
/**
 * \brief Used to schedule clips into the timer's playback queue
 *
 * Roughly equivalent to a midi message, but for clips
 */
struct ClipCommand {
    ClipCommand() {};
    ClipCommand(ClipAudioSource *clip, int midiNote) : clip(clip), midiNote(midiNote) {};
    ClipAudioSource* clip{nullptr};
    int midiNote{-1};
    int midiChannel{-1};
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
                || (changeSlice == false && other->changeSlice == false && midiNote == other->midiNote && midiChannel == other->midiChannel)
            );
    }

    /**
     * \brief Create a command on the no-effects global channel, defaulted to midi note 60
     */
    static ClipCommand* noEffectCommand(ClipAudioSource *clip)
    {
        ClipCommand *command = new ClipCommand();
        command->clip = clip;
        command->midiChannel = -2;
        command->midiNote = 60;
        return command;
    }
    /**
     * \brief Create a command on the effects-enabled global channel, defaulted to midi note 60
     */
    static ClipCommand* effectedCommand(ClipAudioSource *clip)
    {
        ClipCommand *command = new ClipCommand();
        command->clip = clip;
        command->midiChannel = -1;
        command->midiNote = 60;
        return command;
    }
    /**
     * \brief Create a command for a specific track
     */
    static ClipCommand* trackCommand(ClipAudioSource *clip, int trackID)
    {
        ClipCommand *command = new ClipCommand();
        command->clip = clip;
        command->midiChannel = trackID;
        return command;
    }
};

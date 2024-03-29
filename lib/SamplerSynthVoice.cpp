#include "SamplerSynthVoice.h"

#include "ClipAudioSourcePositionsModel.h"
#include "ClipCommand.h"
#include "libzl.h"
#include "SamplerSynthSound.h"
#include "SyncTimer.h"

#include <QDebug>

static inline float velocityToGain(const float &velocity) {
//     static const float sensibleMinimum{log10(1.0f/127.0f)};
//     if (velocity == 0) {
//         return 0;
//     }
//     return (sensibleMinimum-log10(velocity))/sensibleMinimum;
    return velocity;
}

class SamplerSynthVoicePrivate {
public:
    SamplerSynthVoicePrivate() {
        syncTimer = qobject_cast<SyncTimer*>(SyncTimer_instance());
    }

    SyncTimer *syncTimer{nullptr};
    ClipCommand *clipCommand{nullptr};
    ClipAudioSource *clip{nullptr};
    qint64 clipPositionId{-1};
    quint64 startTick{0};
    quint64 nextLoopTick{0};
    quint64 nextLoopUsecs{0};
    double maxSampleDeviation{0.0};
    double pitchRatio = 0;
    double sourceSamplePosition = 0;
    double sourceSampleLength = 0;
    float lgain = 0, rgain = 0;
    ADSR adsr;
};

SamplerSynthVoice::SamplerSynthVoice()
    : QObject()
    , juce::SamplerVoice()
    , d(new SamplerSynthVoicePrivate)
{
}

SamplerSynthVoice::~SamplerSynthVoice()
{
    delete d;
}

bool SamplerSynthVoice::canPlaySound (SynthesiserSound* sound)
{
    return dynamic_cast<const SamplerSynthSound*> (sound) != nullptr;
}

void SamplerSynthVoice::setCurrentCommand(ClipCommand *clipCommand)
{
    if (d->clipCommand) {
        // This means we're changing what we should be doing in playback, and we need to delete the old one
        if (clipCommand->changeLooping) {
            d->clipCommand->looping = clipCommand->looping;
            d->clipCommand->changeLooping = true;
        }
        if (clipCommand->changePitch) {
            d->clipCommand->pitchChange = clipCommand->pitchChange;
            d->clipCommand->changePitch = true;
        }
        if (clipCommand->changeSpeed) {
            d->clipCommand->speedRatio = clipCommand->speedRatio;
            d->clipCommand->changeSpeed = true;
        }
        if (clipCommand->changeGainDb) {
            d->clipCommand->gainDb = clipCommand->gainDb;
            d->clipCommand->changeGainDb = true;
        }
        if (clipCommand->changeVolume) {
            d->clipCommand->volume = clipCommand->volume;
            d->clipCommand->changeVolume = true;
            d->lgain = velocityToGain(d->clipCommand->volume);
            d->rgain = velocityToGain(d->clipCommand->volume);
        }
        if (clipCommand->changeSlice) {
            d->clipCommand->slice = clipCommand->slice;
        }
        if (clipCommand->startPlayback) {
            // This should be interpreted as "restart playback" in this case, so... reset the current position
            if (auto* playingSound = static_cast<SamplerSynthSound*> (getCurrentlyPlayingSound().get())) {
                d->sourceSamplePosition = (int) (d->clip->getStartPosition(d->clipCommand->slice) * playingSound->sourceSampleRate());
            }
        }
        d->syncTimer->deleteClipCommand(clipCommand);
    } else {
        d->clipCommand = clipCommand;
    }
    isPlaying = d->clipCommand;
}

ClipCommand *SamplerSynthVoice::currentCommand() const
{
    return d->clipCommand;
}

void SamplerSynthVoice::setStartTick(quint64 startTick)
{
    d->startTick = startTick;
}

void SamplerSynthVoice::startNote (int midiNoteNumber, float velocity, SynthesiserSound* s, int /*currentPitchWheelPosition*/)
{
    if (auto* sound = dynamic_cast<const SamplerSynthSound*> (s))
    {
        if (sound->isValid() && sound->clip()) {
            d->pitchRatio = std::pow (2.0, (midiNoteNumber - sound->rootMidiNote()) / 12.0)
                            * sound->sourceSampleRate() / getSampleRate();

            d->maxSampleDeviation = d->syncTimer->subbeatCountToSeconds(d->syncTimer->getBpm(), 1) * sound->sourceSampleRate();
            d->clip = sound->clip();
            d->sourceSampleLength = d->clip->getDuration() * sound->sourceSampleRate();
            d->sourceSamplePosition = (int) (d->clip->getStartPosition(d->clipCommand->slice) * sound->sourceSampleRate());

            d->nextLoopTick = d->startTick + d->clip->getLengthInBeats() * d->syncTimer->getMultiplier();
            d->nextLoopUsecs = 0;

            if (d->clipPositionId > -1) {
                d->clip->playbackPositionsModel()->removePosition(d->clipPositionId);
            }
            d->clipPositionId = d->clip->playbackPositionsModel()->createPositionID();

            d->lgain = velocityToGain(velocity);
            d->rgain = velocityToGain(velocity);

            d->adsr.reset();
            d->adsr.setSampleRate(sound->sourceSampleRate());
            d->adsr.setParameters(d->clip->adsrParameters());
            d->adsr.noteOn();
        }
    }
    else
    {
        jassertfalse; // this object can only play SamplerSynthSounds!
    }
}

void SamplerSynthVoice::stopNote (float /*velocity*/, bool allowTailOff)
{
    if (allowTailOff)
    {
        d->adsr.noteOff();
    }
    else
    {
        clearCurrentNote();
        d->adsr.reset();
        if (d->clip) {
            d->clip->playbackPositionsModel()->removePosition(d->clipPositionId);
            d->clip = nullptr;
            d->clipPositionId = -1;
        }
        if (d->clipCommand) {
            d->syncTimer->deleteClipCommand(d->clipCommand);
            d->clipCommand = nullptr;
            isPlaying = false;
        }
        d->nextLoopTick = 0;
        d->nextLoopUsecs = 0;
    }
}

void SamplerSynthVoice::pitchWheelMoved (int /*newValue*/) {}
void SamplerSynthVoice::controllerMoved (int /*controllerNumber*/, int /*newValue*/) {}

void SamplerSynthVoice::process(jack_default_audio_sample_t *leftBuffer, jack_default_audio_sample_t *rightBuffer, jack_nframes_t nframes, jack_nframes_t /*current_frames*/, jack_time_t current_usecs, jack_time_t next_usecs, float /*period_usecs*/)
{
    if (auto* playingSound = static_cast<SamplerSynthSound*> (getCurrentlyPlayingSound().get()))
    {
        if (playingSound->isValid() && d->clipCommand) {
            if (d->nextLoopUsecs == 0) {
                const quint64 differenceToPlayhead = d->nextLoopTick - d->syncTimer->jackPlayhead();
                d->nextLoopUsecs = d->syncTimer->jackPlayheadUsecs() + (differenceToPlayhead * d->syncTimer->jackSubbeatLengthInMicroseconds());
            }
            const double microsecondsPerFrame = (next_usecs - current_usecs) / nframes;
            float peakGain{0.0f};
            auto& data = *playingSound->audioData();
            const float* const inL = data.getReadPointer (0);
            const float* const inR = data.getNumChannels() > 1 ? data.getReadPointer (1) : nullptr;

            const float clipVolume = d->clip->volumeAbsolute();
            const int stopPosition = playingSound->stopPosition(d->clipCommand->slice);
            const int sampleDuration = playingSound->length() - 1;
            const float pan = (float) d->clip->pan();
            const float lPan = 0.5 * (1.0 + pan);
            const float rPan = 0.5 * (1.0 - pan);
            const double sourceSampleRate = playingSound->sourceSampleRate();
            const bool isLooping = d->clipCommand->looping;
            for(jack_nframes_t frame = 0; frame < nframes; ++frame) {
                const int pos = (int) d->sourceSamplePosition;
                const float alpha = (float) (d->sourceSamplePosition - pos);
                const float invAlpha = 1.0f - alpha;
                const float envelopeValue = d->adsr.getNextSample();

                // just using a very simple linear interpolation here..
                float l = sampleDuration > pos ? (inL[pos] * invAlpha + inL[pos + 1] * alpha * d->lgain * envelopeValue * clipVolume): 0;
                float r = (inR != nullptr && sampleDuration > pos) ? (inR[pos] * invAlpha + inR[pos + 1] * alpha * d->rgain * envelopeValue * clipVolume) : l;

                // Implement M/S Panning
                const float mSignal = 0.5 * (l + r);
                const float sSignal = l - r;
                l = lPan * mSignal + sSignal;
                r = rPan * mSignal - sSignal;

                const float newGain{l + r};
                if (newGain > peakGain) {
                    peakGain = newGain;
                }

                ++leftBuffer;
                *leftBuffer += l;
                ++rightBuffer;
                *rightBuffer += r;

                d->sourceSamplePosition += d->pitchRatio;

                if (isLooping) {
                    // beat align samples by reading clip duration in beats from clip, saving synctimer's jack playback positions in voice on startNote, and adjust in if (looping) section of process, and make sure the loop is restarted on that beat if deviation is sufficiently large (like... one timer tick is too much maybe?)
                    if (trunc(d->clip->getLengthInBeats()) == d->clip->getLengthInBeats()) {
                        // If the clip is actually a clean multiple of a number of beats, let's make sure it loops matching that beat position
                        // Work out next loop start point in usecs
                        // Once we hit the frame for that number of usecs after the most recent start, reset playback position to match.
                        // nb: Don't try and be clever, actually make sure to play the first sample in the sound - play past the end rather than before the start
                        if (current_usecs + jack_time_t(frame * microsecondsPerFrame) >= d->nextLoopUsecs) {
                            // Work out the position of the next loop, based on the most recent beat tick position, not the current position, as that might be slightly incorrect
                            const quint64 lengthInTicks = d->clip->getLengthInBeats() * d->syncTimer->getMultiplier();
                            d->nextLoopTick = d->nextLoopTick + lengthInTicks;
                            const quint64 differenceToPlayhead = d->nextLoopTick - d->syncTimer->jackPlayhead();
                            d->nextLoopUsecs = d->syncTimer->jackPlayheadUsecs() + (differenceToPlayhead * d->syncTimer->jackSubbeatLengthInMicroseconds());
//                             qDebug() << "Resetting - next tick" << d->nextLoopTick << "next usecs" << d->nextLoopUsecs << "difference to playhead" << differenceToPlayhead;

                            // Reset the sample playback position back to the start point
                            d->sourceSamplePosition = (int) (d->clip->getStartPosition(d->clipCommand->slice) * sourceSampleRate);
                        }
                    } else if (d->sourceSamplePosition >= stopPosition) {
                        // If we're not beat-matched, just loop "normally"
                        // TODO Switch start position for the loop position here
                        d->sourceSamplePosition = (int) (d->clip->getStartPosition(d->clipCommand->slice) * sourceSampleRate);
                    }
                } else {
                    if (d->sourceSamplePosition >= stopPosition)
                    {
                        stopNote (0.0f, false);
                        break;
                    } else if (d->sourceSamplePosition >= (stopPosition - (d->adsr.getParameters().release * sourceSampleRate))) {
                        // ...really need a way of telling that this has been done already (it's not dangerous, just not pretty, there's maths in there)
                        stopNote (0.0f, true);
                    }
                }
                if (!d->adsr.isActive()) {
                    stopNote(0.0f, false);
                    break;
                }
            }

            // Because it might have gone away after being stopped above, so let's try and not crash
            if (d->clip && d->clipPositionId > -1) {
                d->clip->playbackPositionsModel()->setPositionGainAndProgress(d->clipPositionId, peakGain * 0.5f, d->sourceSamplePosition / d->sourceSampleLength);
            }
        }
    }
}

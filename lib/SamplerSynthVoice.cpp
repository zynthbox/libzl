#include "SamplerSynthVoice.h"

#include "ClipAudioSourcePositionsModel.h"
#include "ClipCommand.h"
#include "libzl.h"
#include "SamplerSynthSound.h"
#include "SyncTimer.h"

#include <QDebug>
#include <QThread>

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
    QList<ClipCommand*> clipCommandsForDeleting;
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
        d->clipCommandsForDeleting << clipCommand;
    } else {
        d->clipCommand = clipCommand;
    }
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
            const qint64 rewindAmount = qint64(d->syncTimer->jackPlayhead()) - qint64(d->nextLoopTick);
            const qint64 rewindUsecs = qint64(d->syncTimer->jackPlayheadUsecs()) - (rewindAmount * qint64(d->syncTimer->jackSubbeatLengthInMicroseconds()));
            const qint64 lengthUsecs = d->syncTimer->subbeatCountToSeconds(d->syncTimer->getBpm(), d->clip->getLengthInBeats() * d->syncTimer->getMultiplier()) * 1000000;
            d->nextLoopUsecs = quint64(qint64(d->syncTimer->jackPlayheadUsecs()) - rewindUsecs + lengthUsecs);

            // Asynchronously request the creation of a new position ID - if we call directly (or blocking
            // queued), we may end up in deadlocky threading trouble, so... asynchronous api it is!
            ClipAudioSourcePositionsModel *positionsModel = d->clip->playbackPositionsModel();
            connect(d->clip->playbackPositionsModel(), &ClipAudioSourcePositionsModel::positionIDCreated, this, [this, positionsModel](void* createdFor, qint64 newPositionID){
                if (createdFor == this) {
                    if (d->clip && d->clip->playbackPositionsModel() == positionsModel) {
                        if (d->clipPositionId > -1) {
                            QMetaObject::invokeMethod(positionsModel, "removePosition", Qt::QueuedConnection, Q_ARG(qint64, d->clipPositionId));
                        }
                        d->clipPositionId = newPositionID;
                    } else {
                        // If we're suddenly playing something else, we didn't receive this quickly enough and should just get rid of it
                        QMetaObject::invokeMethod(positionsModel, "removePosition", Qt::QueuedConnection, Q_ARG(qint64, newPositionID));
                    }
                    positionsModel->disconnect(this);
                }
            }, Qt::QueuedConnection);
            QMetaObject::invokeMethod(positionsModel, "requestPositionID", Qt::QueuedConnection, Q_ARG(void*, this), Q_ARG(float, d->sourceSamplePosition / d->sourceSampleLength));

            d->lgain = velocityToGain(velocity);
            d->rgain = velocityToGain(velocity);

            d->adsr.setSampleRate (sound->sourceSampleRate());
            d->adsr.setParameters (sound->params());

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
            QMetaObject::invokeMethod(d->clip->playbackPositionsModel(), "removePosition", Qt::QueuedConnection, Q_ARG(qint64, d->clipPositionId));
            d->clip = nullptr;
            d->clipPositionId = -1;
        }
        if (d->clipCommand) {
            d->clipCommandsForDeleting << d->clipCommand;
            d->clipCommand = nullptr;
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
            const double microsecondsPerFrame = (next_usecs - current_usecs) / nframes;
            float peakGain{0.0f};
            auto& data = *playingSound->audioData();
            const float* const inL = data.getReadPointer (0);
            const float* const inR = data.getNumChannels() > 1 ? data.getReadPointer (1) : nullptr;

            const int stopPosition = playingSound->stopPosition(d->clipCommand->slice);
            const int sampleDuration = playingSound->length();
            for(jack_nframes_t frame = 0; frame < nframes; ++frame) {
                auto pos = (int) d->sourceSamplePosition;
                auto alpha = (float) (d->sourceSamplePosition - pos);
                auto invAlpha = 1.0f - alpha;

                // just using a very simple linear interpolation here..
                float l = sampleDuration > pos ? (inL[pos] * invAlpha + inL[pos + 1] * alpha) : 0;
                float r = (inR != nullptr && sampleDuration > pos) ? (inR[pos] * invAlpha + inR[pos + 1] * alpha) : l;

                auto envelopeValue = d->adsr.getNextSample();

                l *= d->lgain * envelopeValue;
                r *= d->rgain * envelopeValue;

                leftBuffer[frame] += l;
                rightBuffer[frame] += r;
                peakGain = qMax(peakGain, (l + r) * 0.5f);

                d->sourceSamplePosition += d->pitchRatio;

                if (d->clipCommand->looping) {
                    if (d->sourceSamplePosition >= stopPosition) {
                        // beat align samples by reading clip duration in beats from clip, saving synctimer's jack playback positions in voice on startNote, and adjust in if (looping) section of process, and make sure the loop is restarted on that beat if deviation is sufficiently large (like... one timer tick is too much maybe?)
                        if (trunc(d->clip->getLengthInBeats()) == d->clip->getLengthInBeats()) {
                            // If the clip is actually a clean multiple of a number of beats, let's make sure it loops matching that beat position
                            // Work out next loop start point in usecs
                            // Once we hit the frame for that number of usecs after the most recent start, reset playback position to match.
                            // nb: Don't try and be clever, actually make sure to play the first sample in the sound - play past the end rather than before the start
                            if (current_usecs + (frame * microsecondsPerFrame) >= d->nextLoopUsecs) {
                                // Work out the position of the next loop, based on the most recent beat tick position, not the current position, as that might be slightly incorrect
                                d->nextLoopTick = d->nextLoopTick + d->clip->getLengthInBeats() * d->syncTimer->getMultiplier();
                                const qint64 rewindAmount = qint64(d->syncTimer->jackPlayhead()) - qint64(d->nextLoopTick);
                                const qint64 rewindUsecs = qint64(d->syncTimer->jackPlayheadUsecs()) - (rewindAmount * qint64(d->syncTimer->jackSubbeatLengthInMicroseconds()));
                                const qint64 lengthUsecs = d->syncTimer->subbeatCountToSeconds(d->syncTimer->getBpm(), d->clip->getLengthInBeats() * d->syncTimer->getMultiplier()) * 1000000;
                                d->nextLoopUsecs = quint64(qint64(d->syncTimer->jackPlayheadUsecs()) - rewindUsecs + lengthUsecs);

                                // Reset the sample playback position back to the start point
                                d->sourceSamplePosition = (int) (d->clip->getStartPosition(d->clipCommand->slice) * playingSound->sourceSampleRate());
                            }
                        } else {
                            // If we're not beat-matched, just loop "normally"
                            // TODO Switch start position for the loop position here
                            d->sourceSamplePosition = (int) (d->clip->getStartPosition(d->clipCommand->slice) * playingSound->sourceSampleRate());
                        }
                    }
                } else {
                    if (d->sourceSamplePosition >= stopPosition)
                    {
                        stopNote (0.0f, false);
                        break;
                    } else if (d->sourceSamplePosition >= (stopPosition - (d->adsr.getParameters().release * playingSound->sourceSampleRate()))) {
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
                QMetaObject::invokeMethod(d->clip->playbackPositionsModel(), "setPositionProgress", Qt::QueuedConnection, Q_ARG(qint64, d->clipPositionId), Q_ARG(float, d->sourceSamplePosition / d->sourceSampleLength));
                QMetaObject::invokeMethod(d->clip->playbackPositionsModel(), "setPositionGain", Qt::QueuedConnection, Q_ARG(qint64, d->clipPositionId), Q_ARG(float, peakGain));
            }
        }
    }
    if (!d->clipCommandsForDeleting.isEmpty()) {
        qDeleteAll(d->clipCommandsForDeleting);
        d->clipCommandsForDeleting.clear();
    }
}

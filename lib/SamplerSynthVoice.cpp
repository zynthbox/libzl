#include "SamplerSynthVoice.h"

#include "SamplerSynthSound.h"
#include "ClipCommand.h"
#include "ClipAudioSourcePositionsModel.h"

#include <QThread>
#include <QTimer>

class SamplerSynthVoicePrivate {
public:
    SamplerSynthVoicePrivate() {}

    ClipCommand *clipCommand{nullptr};
    ClipAudioSource *clip{nullptr};
    qint64 clipPositionId{-1};
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
            d->lgain = d->clipCommand->volume;
            d->rgain = d->clipCommand->volume;
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
        QTimer::singleShot(1, this, [clipCommand](){ delete clipCommand; });
    } else {
        d->clipCommand = clipCommand;
    }
}

ClipCommand *SamplerSynthVoice::currentCommand() const
{
    return d->clipCommand;
}

void SamplerSynthVoice::startNote (int midiNoteNumber, float velocity, SynthesiserSound* s, int /*currentPitchWheelPosition*/)
{
    if (auto* sound = dynamic_cast<const SamplerSynthSound*> (s))
    {
        d->pitchRatio = std::pow (2.0, (midiNoteNumber - sound->rootMidiNote()) / 12.0)
                        * sound->sourceSampleRate() / getSampleRate();

        d->clip = sound->clip();
        d->sourceSampleLength = d->clip->getDuration() * sound->sourceSampleRate();
        d->sourceSamplePosition = (int) (d->clip->getStartPosition(d->clipCommand->slice) * sound->sourceSampleRate());

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
        QMetaObject::invokeMethod(d->clip->playbackPositionsModel(), "requestPositionID", Qt::QueuedConnection, Q_ARG(void*, this), Q_ARG(float, d->sourceSamplePosition / d->sourceSampleLength));

        d->lgain = velocity;
        d->rgain = velocity;

        d->adsr.setSampleRate (sound->sourceSampleRate());
        d->adsr.setParameters (sound->params());

        d->adsr.noteOn();
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
        QMetaObject::invokeMethod(d->clip->playbackPositionsModel(), "removePosition", Qt::QueuedConnection, Q_ARG(qint64, d->clipPositionId));
        d->clipPositionId = -1;
        d->clip = nullptr;
        ClipCommand *clipCommand = d->clipCommand;
        QTimer::singleShot(1, this, [clipCommand](){ delete clipCommand; });
        d->clipCommand = nullptr;
    }
}

void SamplerSynthVoice::pitchWheelMoved (int /*newValue*/) {}
void SamplerSynthVoice::controllerMoved (int /*controllerNumber*/, int /*newValue*/) {}

void SamplerSynthVoice::renderNextBlock (AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (auto* playingSound = static_cast<SamplerSynthSound*> (getCurrentlyPlayingSound().get()))
    {
        if (playingSound->isValid()) {
            auto& data = *playingSound->audioData();
            const float* const inL = data.getReadPointer (0);
            const float* const inR = data.getNumChannels() > 1 ? data.getReadPointer (1) : nullptr;

            float* outL = outputBuffer.getWritePointer (0, startSample);
            float* outR = outputBuffer.getNumChannels() > 1 ? outputBuffer.getWritePointer (1, startSample) : nullptr;

            const int stopPosition = playingSound->stopPosition(d->clipCommand->slice);

            while (--numSamples >= 0)
            {
                auto pos = (int) d->sourceSamplePosition;
                auto alpha = (float) (d->sourceSamplePosition - pos);
                auto invAlpha = 1.0f - alpha;

                // just using a very simple linear interpolation here..
                float l = (inL[pos] * invAlpha + inL[pos + 1] * alpha);
                float r = (inR != nullptr) ? (inR[pos] * invAlpha + inR[pos + 1] * alpha)
                                        : l;

                auto envelopeValue = d->adsr.getNextSample();

                l *= d->lgain * envelopeValue;
                r *= d->rgain * envelopeValue;

                if (outR != nullptr)
                {
                    *outL++ += l;
                    *outR++ += r;
                }
                else
                {
                    *outL++ += (l + r) * 0.5f;
                }

                d->sourceSamplePosition += d->pitchRatio;

                if (d->clipCommand->looping) {
                    if (d->sourceSamplePosition > stopPosition)
                    {
                        // TODO Switch start position for the loop position here
                        d->sourceSamplePosition = (int) (d->clip->getStartPosition(d->clipCommand->slice) * playingSound->sourceSampleRate());
                    }
                } else {
                    if (d->sourceSamplePosition > stopPosition)
                    {
                        stopNote (0.0f, false);
                        break;
                    } else if (d->sourceSamplePosition > (stopPosition - (d->adsr.getParameters().release * playingSound->sourceSampleRate()))) {
                        // ...really need a way of telling that this has been done already (it's not dangerous, just not pretty, there's maths in there)
                        stopNote (0.0f, true);
                    }
                }
            }
            // Because it might have gone away after being stopped above, so let's try and not crash
            if (d->clip && d->clipPositionId > -1) {
                QMetaObject::invokeMethod(d->clip->playbackPositionsModel(), "setPositionProgress", Qt::QueuedConnection, Q_ARG(qint64, d->clipPositionId), Q_ARG(float, d->sourceSamplePosition / d->sourceSampleLength));
            }
        }
    }
}

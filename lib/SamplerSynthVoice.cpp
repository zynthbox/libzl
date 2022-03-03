#include "SamplerSynthVoice.h"

#include "SamplerSynthSound.h"
#include "ClipAudioSourcePositionsModel.h"

class SamplerSynthVoicePrivate {
public:
    SamplerSynthVoicePrivate() {}

    ClipAudioSource *clip{nullptr};
    int clipPositionId{-1};
    double pitchRatio = 0;
    double sourceSamplePosition = 0;
    double sourceSampleLength = 0;
    float lgain = 0, rgain = 0;
    ADSR adsr;
};

SamplerSynthVoice::SamplerSynthVoice()
    : juce::SamplerVoice()
    , d(new SamplerSynthVoicePrivate)
{
}

SamplerSynthVoice::~SamplerSynthVoice()
{
}

bool SamplerSynthVoice::canPlaySound (SynthesiserSound* sound)
{
    return dynamic_cast<const SamplerSynthSound*> (sound) != nullptr;
}

void SamplerSynthVoice::startNote (int midiNoteNumber, float velocity, SynthesiserSound* s, int /*currentPitchWheelPosition*/)
{
    if (auto* sound = dynamic_cast<const SamplerSynthSound*> (s))
    {
        d->pitchRatio = std::pow (2.0, (midiNoteNumber - sound->rootMidiNote()) / 12.0)
                        * sound->sourceSampleRate() / getSampleRate();

        d->clip = sound->clip();
        d->sourceSampleLength = d->clip->getDuration() * sound->sourceSampleRate();
        d->sourceSamplePosition = (int) (d->clip->getStartPosition() * sound->sourceSampleRate());
        d->clipPositionId = d->clip->playbackPositionsModel()->createPositionID(d->sourceSamplePosition / d->sourceSampleLength);
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
    }
    d->clip->playbackPositionsModel()->removePosition(d->clipPositionId);
    d->clip = nullptr;
}

void SamplerSynthVoice::pitchWheelMoved (int /*newValue*/) {}
void SamplerSynthVoice::controllerMoved (int /*controllerNumber*/, int /*newValue*/) {}

void SamplerSynthVoice::renderNextBlock (AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (auto* playingSound = static_cast<SamplerSynthSound*> (getCurrentlyPlayingSound().get()))
    {
        auto& data = *playingSound->audioData();
        const float* const inL = data.getReadPointer (0);
        const float* const inR = data.getNumChannels() > 1 ? data.getReadPointer (1) : nullptr;

        float* outL = outputBuffer.getWritePointer (0, startSample);
        float* outR = outputBuffer.getNumChannels() > 1 ? outputBuffer.getWritePointer (1, startSample) : nullptr;

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

            if (d->sourceSamplePosition > playingSound->stopPosition())
            {
                stopNote (0.0f, false);
                break;
            }
        }
        // Because it might have gone away after being stopped above, so let's try and not crash
        if (d->clip) {
            d->clip->playbackPositionsModel()->setPositionProgress(d->clipPositionId, d->sourceSamplePosition / d->sourceSampleLength);
        }
    }
}

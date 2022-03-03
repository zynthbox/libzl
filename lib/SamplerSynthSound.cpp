
#include "SamplerSynthSound.h"
#include <QString>

class SamplerSynthSoundPrivate {
public:
    SamplerSynthSoundPrivate() {}

    int midiNoteForNormalPitch{60};
    String name;
    std::unique_ptr<AudioBuffer<float>> data;
    int length{0};
    double sourceSampleRate{0.0f};
    ADSR::Parameters params;

    ClipAudioSource *clip{nullptr};
};

SamplerSynthSound::SamplerSynthSound(ClipAudioSource *clip, const String& name, AudioFormatReader& source, int midiNoteForNormalPitch)
    : juce::SynthesiserSound()
    , d(new SamplerSynthSoundPrivate)
{
    d->midiNoteForNormalPitch = midiNoteForNormalPitch;
    d->name = name;
    d->clip = clip;
    d->sourceSampleRate = source.sampleRate;
    if (d->sourceSampleRate > 0 && source.lengthInSamples > 0)
    {
        d->length = (int) source.lengthInSamples;
        d->data.reset (new AudioBuffer<float> (jmin (2, (int) source.numChannels), d->length + 4));

        source.read (d->data.get(), 0, d->length + 4, 0, true, true);

        d->params.attack  = static_cast<float> (0);
        d->params.release = static_cast<float> (0);
    }
}

SamplerSynthSound::~SamplerSynthSound()
{
}

ClipAudioSource *SamplerSynthSound::clip() const
{
    return d->clip;
}

AudioBuffer<float> *SamplerSynthSound::audioData() const noexcept
{
    return d->data.get();
}

int SamplerSynthSound::length() const
{
    return d->length;
}

int SamplerSynthSound::startPosition() const
{
    return d->clip->getStartPosition() * d->sourceSampleRate;
}

int SamplerSynthSound::stopPosition() const
{
    return d->clip->getStopPosition() * d->sourceSampleRate;
}

int SamplerSynthSound::rootMidiNote() const
{
    return d->midiNoteForNormalPitch;
}

double SamplerSynthSound::sourceSampleRate() const
{
    return d->sourceSampleRate;
}

ADSR::Parameters &SamplerSynthSound::params() const
{
    return d->params;
}

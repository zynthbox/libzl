
#include "SamplerSynthSound.h"
#include <QString>

class SamplerSynthSoundPrivate {
public:
    SamplerSynthSoundPrivate() {}

    ClipAudioSource *clip{nullptr};
};

SamplerSynthSound::SamplerSynthSound(ClipAudioSource *clip, const String& name, AudioFormatReader& source, const BigInteger& midiNotes, int midiNoteForNormalPitch, double attackTimeSecs, double releaseTimeSecs, double maxSampleLengthSeconds)
    : juce::SamplerSound(name, source, midiNotes, midiNoteForNormalPitch, attackTimeSecs, releaseTimeSecs, maxSampleLengthSeconds)
    , d(new SamplerSynthSoundPrivate)
{
    d->clip = clip;
}

SamplerSynthSound::~SamplerSynthSound()
{
}

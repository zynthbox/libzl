#pragma once

#include "JUCEHeaders.h"
#include "ClipAudioSource.h"
#include <memory>

class SamplerSynthSoundPrivate;
class SamplerSynthSound : public juce::SamplerSound {
public:
    explicit SamplerSynthSound(ClipAudioSource *clip,
                               const String& name,
                               AudioFormatReader& source,
                               const BigInteger& midiNotes,
                               int midiNoteForNormalPitch,
                               double attackTimeSecs,
                               double releaseTimeSecs,
                               double maxSampleLengthSeconds);
    ~SamplerSynthSound() override;
    bool appliesToChannel ( int midiChannel ) override { return true; };
    bool appliesToNote ( int midiNoteNumber ) override { return true; };
private:
    std::unique_ptr<SamplerSynthSoundPrivate> d;
};

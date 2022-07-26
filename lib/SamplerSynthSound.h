#pragma once

#include "JUCEHeaders.h"
#include "ClipAudioSource.h"

class SamplerSynthSoundPrivate;
class SamplerSynthSound : public juce::SynthesiserSound {
public:
    explicit SamplerSynthSound(ClipAudioSource *clip);
    ~SamplerSynthSound() override;
    ClipAudioSource *clip() const;
    bool appliesToChannel ( int /*midiChannel*/ ) override { return true; };
    bool appliesToNote ( int /*midiNoteNumber*/ ) override { return true; };
    bool isValid() const;
    AudioBuffer<float>* audioData() const noexcept;
    int length() const;
    int startPosition(int slice = 0) const;
    int stopPosition(int slice = 0) const;
    int rootMidiNote() const;
    double sourceSampleRate() const;
    ADSR::Parameters &params() const;
private:
    SamplerSynthSoundPrivate *d{nullptr};
};

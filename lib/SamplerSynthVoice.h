#pragma once

#include "JUCEHeaders.h"
#include <memory>

class SamplerSynthVoicePrivate;
class SamplerSynthVoice : public juce::SamplerVoice
{
public:
    explicit SamplerSynthVoice();
    ~SamplerSynthVoice() override;

    bool canPlaySound (SynthesiserSound*) override;

    void startNote (int midiNoteNumber, float velocity, SynthesiserSound*, int pitchWheel) override;
    void stopNote (float velocity, bool allowTailOff) override;

    void pitchWheelMoved (int newValue) override;
    void controllerMoved (int controllerNumber, int newValue) override;

    void renderNextBlock (AudioBuffer<float>&, int startSample, int numSamples) override;
    using SynthesiserVoice::renderNextBlock;
private:
    std::unique_ptr<SamplerSynthVoicePrivate> d;
};

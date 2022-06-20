#pragma once

#include <QObject>
#include "JUCEHeaders.h"
#include <jack/jack.h>

struct ClipCommand;
class SamplerSynthVoicePrivate;
class SamplerSynthVoice : public QObject, public juce::SamplerVoice
{
    Q_OBJECT
public:
    explicit SamplerSynthVoice();
    ~SamplerSynthVoice() override;

    bool canPlaySound (SynthesiserSound*) override;

    void setCurrentCommand(ClipCommand *clipCommand);
    ClipCommand *currentCommand() const;

    void startNote (int midiNoteNumber, float velocity, SynthesiserSound*, int pitchWheel) override;
    void stopNote (float velocity, bool allowTailOff) override;

    void pitchWheelMoved (int newValue) override;
    void controllerMoved (int controllerNumber, int newValue) override;

    void process(jack_default_audio_sample_t *leftBuffer, jack_default_audio_sample_t *rightBuffer, jack_nframes_t nframes);
private:
    SamplerSynthVoicePrivate *d;
};

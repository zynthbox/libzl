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

    void setStartTick(quint64 startTick);

    void startNote (int midiNoteNumber, float velocity, SynthesiserSound*, int pitchWheel) override;
    void stopNote (float velocity, bool allowTailOff) override;

    void pitchWheelMoved (int newValue) override;
    void controllerMoved (int controllerNumber, int newValue) override;

    void process(jack_default_audio_sample_t *leftBuffer, jack_default_audio_sample_t *rightBuffer, jack_nframes_t nframes, jack_nframes_t current_frames, jack_time_t current_usecs, jack_time_t next_usecs, float period_usecs);

    bool isPlaying{false};
private:
    SamplerSynthVoicePrivate *d{nullptr};
};

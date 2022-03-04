
#include "SamplerSynth.h"

#include "JUCEHeaders.h"
#include "Helper.h"
#include "SamplerSynthSound.h"
#include "SamplerSynthVoice.h"
#include "SyncTimer.h"

#include <QDebug>
#include <QHash>

using namespace juce;

class SamplerSynthImpl;
class SamplerSynthPrivate : public juce::AudioProcessor {
public:
    SamplerSynthPrivate()
        : juce::AudioProcessor(BusesProperties()
            .withInput("Input", AudioChannelSet::stereo(), true)
            .withOutput("Output", AudioChannelSet::stereo(), true))
    {
    }

    const juce::String getName() const override { return "ZynthiloopsSamplerSynth"; }

    SamplerSynthImpl *synth{nullptr};
    QList<SamplerSynthVoice *> voices;
    AudioFormatManager formatManager;
    static const int numVoices{16};

    QHash<ClipAudioSource*, SamplerSynthSound*> clipSounds;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(AudioBuffer<double> &buffer, juce::MidiBuffer &midiMessages) override {
        process(buffer, midiMessages);
    }
    void processBlock(AudioBuffer<float> &buffer, juce::MidiBuffer &midiMessages) override {
        process(buffer, midiMessages);
    }
    template <typename Element>
    void process (AudioBuffer<Element>& buffer, MidiBuffer& midiMessages);

    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    void releaseResources() override { }
    juce::AudioProcessorEditor *createEditor() override {
        return nullptr;
    }
    int getNumPrograms() override {
        return 1;
    }
    int getCurrentProgram() override {
        return 0;
    }
    void setCurrentProgram(int index) override {
        Q_UNUSED(index)
    }
    const juce::String getProgramName(int index) override {
        Q_UNUSED(index)
        return "None";
    }
    void changeProgramName(int index, const juce::String &newName) override {
        Q_UNUSED(index)
        Q_UNUSED(newName)
    }
    void getStateInformation(juce::MemoryBlock &destData) override {
        Q_UNUSED(destData)
    }
    void setStateInformation(const void *data, int sizeInBytes) override {
        Q_UNUSED(data)
        Q_UNUSED(sizeInBytes)
    }

    juce::AudioProcessorPlayer* samplerProcessorPlayer{nullptr};
    te::Engine *engine{nullptr};
};

class SamplerSynthImpl : public juce::Synthesiser {
public:
    void handleCommand(ClipCommand *clipCommand) {
        if (d->clipSounds.contains(clipCommand->clip)) {
            SamplerSynthSound *sound = d->clipSounds[clipCommand->clip];
            if (clipCommand->stopPlayback || clipCommand->startPlayback) {
                if (clipCommand->stopPlayback) {
                    for (SamplerSynthVoice * voice : d->voices) {
                        if (voice->getCurrentlyPlayingSound().get() == sound && voice->getCurrentlyPlayingNote() == clipCommand->midiNote) {
                            voice->stopNote(0.0f, false);
                        }
                    }
                }
                if (clipCommand->startPlayback) {
                    for (SamplerSynthVoice *voice : d->voices) {
                        if (!voice->isVoiceActive()) {
                            voice->setCurrentCommand(clipCommand);
                            startVoice(voice, sound, 0, clipCommand->midiNote, clipCommand->volume);
                            break;
                        }
                    }
                }
            } else {
                for (SamplerSynthVoice * voice : d->voices) {
                    if (voice->getCurrentlyPlayingSound().get() == sound && voice->getCurrentlyPlayingNote() == clipCommand->midiNote) {
                        // Update the voice with the new command
                        voice->setCurrentCommand(clipCommand);
                    }
                }
            }
        }
    }
    SamplerSynthPrivate *d{nullptr};
};

void SamplerSynthPrivate::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    Q_UNUSED(maximumExpectedSamplesPerBlock)
    synth->setCurrentPlaybackSampleRate(sampleRate);
}

template<typename Element>
void SamplerSynthPrivate::process(AudioBuffer<Element> &buffer, juce::MidiBuffer &midiMessages)
{
    synth->renderNextBlock (buffer, midiMessages, 0, buffer.getNumSamples());
}

SamplerSynth *SamplerSynth::instance()  {
    static SamplerSynth *instance{nullptr};
    if (!instance) {
        instance = new SamplerSynth(qApp);
    }
    return instance;
}

SamplerSynth::SamplerSynth(QObject *parent)
    : QObject(parent)
    , d(new SamplerSynthPrivate)
{
    d->synth = new SamplerSynthImpl();
    d->synth->d = d.get();
    d->formatManager.registerBasicFormats();
}

SamplerSynth::~SamplerSynth()
{
    delete d->synth;
}

void SamplerSynth::initialize(tracktion_engine::Engine *engine)
{
    d->engine = engine;
    qDebug() << "Creating an Audio Processor Player for SamplerSynth";
    d->samplerProcessorPlayer = new juce::AudioProcessorPlayer();
    qDebug() << "Setting synth private class as processor";
    d->samplerProcessorPlayer->setProcessor(d.get());
    qDebug() << "Adding audio callback for the sample processor player";
    d->engine->getDeviceManager().deviceManager.addAudioCallback (d->samplerProcessorPlayer);
    qDebug() << "Adding" << d->numVoices << "voices to the synth";
    for (int i = 0; i < d->numVoices; ++i) {
        SamplerSynthVoice *voice = new SamplerSynthVoice();
        d->voices << voice;
        d->synth->addVoice(voice);
    }
}

void SamplerSynth::registerClip(ClipAudioSource *clip)
{
    if (!d->clipSounds.contains(clip)) {
        AudioFormatReader *format = d->formatManager.createReaderFor(juce::File(clip->getFilePath()));
        if (format) {
            SamplerSynthSound *sound = new SamplerSynthSound(clip, "Sound Clip", *format, 60);
            d->clipSounds[clip] = sound;
            d->synth->addSound(sound);
            delete format;
        } else {
            qWarning() << "Failed to create a format reader for" << clip->getFilePath();
        }
    }
}

void SamplerSynth::unregisterClip(ClipAudioSource *clip)
{
    if (d->clipSounds.contains(clip)) {
        d->clipSounds.remove(clip);
    }
}

void SamplerSynth::handleClipCommand(ClipCommand *clipCommand)
{
    d->synth->handleCommand(clipCommand);
}

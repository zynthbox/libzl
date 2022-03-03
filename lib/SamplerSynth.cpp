
#include "SamplerSynth.h"

#include "JUCEHeaders.h"
#include "Helper.h"
#include "SamplerSynthSound.h"
#include "SyncTimer.h"

#include <QDebug>
#include <QHash>

using namespace juce;

class SamplerSynthImpl : public juce::Synthesiser {
public:
    void startNote(int midiNoteNumber, float velocity, SynthesiserSound* sound, SynthesiserVoice* voice) {
        if (soundNotes.contains(sound)) {
            soundNotes[sound].append(midiNoteNumber);
        } else {
            QList<int> notes{midiNoteNumber};
            soundNotes[sound] = notes;
        }
        startVoice(voice, sound, 0, midiNoteNumber, velocity);
    }
    void stopNote(int midiNoteNumber, SynthesiserSound *sound, SynthesiserVoice *voice) {
    }
private:
    QHash<SynthesiserSound*, QList<int> > soundNotes;
};

class SamplerSynthPrivate : public juce::AudioProcessor {
public:
    SamplerSynthPrivate()
        : juce::AudioProcessor(BusesProperties()
            .withInput("Input", AudioChannelSet::stereo(), true)
            .withOutput("Output", AudioChannelSet::stereo(), true))
    {}

    const juce::String getName() const override { return "ZynthiloopsSamplerSynth"; }

    SamplerSynthImpl synth;
    QList<SamplerVoice *> voices;
    AudioFormatManager formatManager;
    static const int numVoices{16};

    QHash<ClipAudioSource*, SamplerSynthSound*> clipSounds;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override {
        Q_UNUSED(maximumExpectedSamplesPerBlock)
        synth.setCurrentPlaybackSampleRate(sampleRate);
    }
    void processBlock(AudioBuffer<double> &buffer, juce::MidiBuffer &midiMessages) override {
        process(buffer, midiMessages);
    }
    void processBlock(AudioBuffer<float> &buffer, juce::MidiBuffer &midiMessages) override {
        process(buffer, midiMessages);
    }
    template <typename Element>
    void process (AudioBuffer<Element>& buffer, MidiBuffer& midiMessages)
    {
//         juce::ScopedNoDenormals noDenormals;
//         auto totalNumInputChannels  = getTotalNumInputChannels();
//         auto totalNumOutputChannels = getTotalNumOutputChannels();

//         for (auto i = 0; i < totalNumInputChannels; ++i)
//             buffer.clear(i, 0, buffer.getNumSamples());

        synth.renderNextBlock (buffer, midiMessages, 0, buffer.getNumSamples());
    }

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
    d->formatManager.registerBasicFormats();
}

SamplerSynth::~SamplerSynth()
{
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
        SamplerVoice *voice = new SamplerVoice();
        d->voices << voice;
        d->synth.addVoice(voice);
    }
}

void SamplerSynth::registerClip(ClipAudioSource *clip)
{
    qDebug() << Q_FUNC_INFO;
    if (!d->clipSounds.contains(clip)) {
        AudioFormatReader *format = d->formatManager.createReaderFor(juce::File(clip->getFilePath()));
        if (format) {
            BigInteger range;
            range.setRange(0, 127, true);
            SamplerSynthSound *sound = new SamplerSynthSound(clip, "Sound Clip", *format, range, 60, 0.0, 0.0, 50);
            d->clipSounds[clip] = sound;
            d->synth.addSound(sound);
            delete format;
        } else {
            qDebug() << "Failed to create a format reader for" << clip->getFilePath();
        }
    }
}

void SamplerSynth::unregisterClip(ClipAudioSource *clip)
{
    qDebug() << Q_FUNC_INFO;
    if (d->clipSounds.contains(clip)) {
        d->clipSounds.remove(clip);
    }
}

void SamplerSynth::handleClipCommand(ClipCommand *clipCommand)
{
    qDebug() << Q_FUNC_INFO;
    if (d->clipSounds.contains(clipCommand->clip)) {
        SamplerSynthSound *sound = d->clipSounds[clipCommand->clip];
        qDebug() << "Doing things with" << sound;
        if (clipCommand->stopPlayback) {
            for (SamplerVoice * voice : d->voices) {
                if (voice->getCurrentlyPlayingSound().get() == sound && voice->getCurrentlyPlayingNote() == clipCommand->midiNote) {
                    qDebug() << "Stopping voice" << voice;
                    voice->stopNote(0.0f, false);
                }
            }
        }
        if (clipCommand->startPlayback) {
            for (SamplerVoice *voice : d->voices) {
                if (!voice->isVoiceActive()) {
                    qDebug() << "Starting clip" << clipCommand->clip << "on voice" << voice;
                    d->synth.startNote(clipCommand->midiNote, clipCommand->volume, sound, voice);
                    break;
                }
            }
        }
    }
}

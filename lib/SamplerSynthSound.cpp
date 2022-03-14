
#include "SamplerSynthSound.h"
#include <QDebug>
#include <QString>

class SamplerSynthSoundPrivate {
public:
    SamplerSynthSoundPrivate() {}

    int midiNoteForNormalPitch{60};
    std::unique_ptr<AudioBuffer<float>> data;
    int length{0};
    double sourceSampleRate{0.0f};
    ADSR::Parameters params;

    ClipAudioSource *clip{nullptr};

    void loadSoundData() {
        qDebug() << Q_FUNC_INFO << "Loading sound data for" << clip->getFilePath();
        AudioFormatReader *format{nullptr};
        juce::File file = clip->getPlaybackFile().getFile();
        tracktion_engine::AudioFileInfo fileInfo = clip->getPlaybackFile().getInfo();
        MemoryMappedAudioFormatReader *memoryFormat = fileInfo.format->createMemoryMappedReader(file);
        if (memoryFormat && memoryFormat->mapEntireFile()) {
            format = memoryFormat;
        }
        if (!format) {
            format = fileInfo.format->createReaderFor(file.createInputStream().release(), true);
        }
        if (format) {
            sourceSampleRate = format->sampleRate;
            if (sourceSampleRate > 0 && format->lengthInSamples > 0)
            {
                length = (int) format->lengthInSamples;
                data.reset (new AudioBuffer<float> (jmin (2, (int) format->numChannels), length));
                format->read (data.get(), 0, length, 0, true, true);
            }
            delete format;
        } else {
            qWarning() << "Failed to create a format reader for" << file.getFullPathName().toUTF8();
        }
    }
};

SamplerSynthSound::SamplerSynthSound(ClipAudioSource *clip)
    : juce::SynthesiserSound()
    , d(new SamplerSynthSoundPrivate)
{
    d->midiNoteForNormalPitch = 60;
    d->clip = clip;
    d->params.attack  = static_cast<float> (0);
    d->params.release = static_cast<float> (0);
    d->loadSoundData();
    QObject::connect(clip, &ClipAudioSource::playbackFileChanged, [this](){ d->loadSoundData(); });
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

int SamplerSynthSound::startPosition(int slice) const
{
    return d->clip->getStartPosition(slice) * d->sourceSampleRate;
}

int SamplerSynthSound::stopPosition(int slice) const
{
    return d->clip->getStopPosition(slice) * d->sourceSampleRate;
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

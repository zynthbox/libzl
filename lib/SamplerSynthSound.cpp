
#include "SamplerSynthSound.h"

#include <QDebug>
#include <QFileInfo>
#include <QString>
#include <QTimer>

class SamplerSynthSoundPrivate : public QObject {
    Q_OBJECT
public:
    SamplerSynthSoundPrivate() {
        soundLoader.setSingleShot(true);
        connect(&soundLoader, &QTimer::timeout, this, &SamplerSynthSoundPrivate::loadSoundData);
    }

    QTimer soundLoader;
    bool isValid{false};
    std::unique_ptr<AudioBuffer<float>> data;
    int length{0};
    double sourceSampleRate{0.0f};
    ADSR::Parameters params;

    ClipAudioSource *clip{nullptr};

    void loadSoundData() {
        if (QFileInfo(clip->getPlaybackFile().getFile().getFullPathName().toRawUTF8()).exists()) {
            qDebug() << Q_FUNC_INFO << "Loading sound data for" << clip->getFilePath();
            qDebug() << Q_FUNC_INFO << "Using playback file" << clip->getPlaybackFile().getFile().getFullPathName().toRawUTF8();
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
                    isValid = true;
                }
                delete format;
            } else {
                qWarning() << "Failed to create a format reader for" << file.getFullPathName().toUTF8();
            }
        } else {
            qDebug() << Q_FUNC_INFO << "Postponing loading sound data for" << clip->getFilePath() << "100ms as the playback file is not there yet...";
            soundLoader.start(100);
        }
    }
};

SamplerSynthSound::SamplerSynthSound(ClipAudioSource *clip)
    : juce::SynthesiserSound()
    , d(new SamplerSynthSoundPrivate)
{
    d->clip = clip;
    d->params.attack  = static_cast<float> (0);
    d->params.release = static_cast<float> (0.05f);
    d->loadSoundData();
    QObject::connect(clip, &ClipAudioSource::playbackFileChanged, &d->soundLoader, [this](){ d->soundLoader.start(1); }, Qt::QueuedConnection);
}

SamplerSynthSound::~SamplerSynthSound()
{
}

ClipAudioSource *SamplerSynthSound::clip() const
{
    return d->clip;
}

bool SamplerSynthSound::isValid() const
{
    return d->isValid;
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
    return d->clip->rootNote();
}

double SamplerSynthSound::sourceSampleRate() const
{
    return d->sourceSampleRate;
}

ADSR::Parameters &SamplerSynthSound::params() const
{
    return d->params;
}

// Since our pimpl is a qobject, let's make sure we do it properly
#include "SamplerSynthSound.moc"


#include "SamplerSynth.h"

#include "JUCEHeaders.h"
#include "Helper.h"
#include "SamplerSynthSound.h"
#include "SamplerSynthVoice.h"
#include "ClipCommand.h"
#include "libzl.h"
#include "SyncTimer.h"

#include <QDebug>
#include <QHash>
#include <QMutex>
#include <QThread>
#include <QTimer>

#include <jack/jack.h>
#include <jack/statistics.h>

using namespace juce;

#define SAMPLER_CHANNEL_VOICE_COUNT 8

struct SamplerCommand {
    quint64 timestamp;
    ClipCommand* clipCommand{nullptr};
    SamplerCommand* next{nullptr};
    SamplerCommand* previous{nullptr};
};

#define CommandQueueSize 256
class SamplerChannel
{
public:
    explicit SamplerChannel(const QString &clientName);
    ~SamplerChannel() {
        if (jackClient) {
            jack_client_close(jackClient);
        }
    }
    int process(jack_nframes_t nframes);
    inline void handleCommand(ClipCommand *clipCommand, quint64 currentTick);
    SamplerCommand commandRing[CommandQueueSize];
    SamplerCommand *readHead{nullptr};
    SamplerCommand *writeHead{nullptr};

    QString clientName;
    jack_client_t *jackClient{nullptr};
    jack_port_t *leftPort{nullptr};
    QString portNameLeft{"left_out"};
    jack_port_t *rightPort{nullptr};
    QString portNameRight{"right_out"};
    jack_port_t *midiInPort{nullptr};
    SamplerSynthVoice* voices[SAMPLER_CHANNEL_VOICE_COUNT];
    SamplerSynthPrivate* d{nullptr};
    int midiChannel{-1};
    float cpuLoad{0.0f};

    bool enabled{true};
};

static int client_process(jack_nframes_t nframes, void* arg) {
    return static_cast<SamplerChannel*>(arg)->process(nframes);
}

void jackConnect(jack_client_t* jackClient, const QString &from, const QString &to) {
    int result = jack_connect(jackClient, from.toUtf8(), to.toUtf8());
    if (result == 0 || result == EEXIST) {
//         qDebug() << "SamplerSynth:" << (result == EEXIST ? "Retaining existing connection from" : "Successfully created new connection from" ) << from << "to" << to;
    } else {
        qWarning() << "SamplerSynth: Failed to connect" << from << "with" << to << "with error code" << result;
        // This should probably reschedule an attempt in the near future, with a limit to how long we're trying for?
    }
}

SamplerChannel::SamplerChannel(const QString &clientName)
    : clientName(clientName)
{
    writeHead = readHead = commandRing;
    SamplerCommand *previous = &commandRing[CommandQueueSize - 1];
    for (quint64 i = 0; i < CommandQueueSize; ++i) {
        previous->next = &commandRing[i];
        commandRing[i].previous = previous;
        previous = &commandRing[i];
    }
    jack_status_t real_jack_status{};
    jackClient = jack_client_open(clientName.toUtf8(), JackNullOption, &real_jack_status);
    if (jackClient) {
        // Set the process callback.
        if (jack_set_process_callback(jackClient, client_process, this) == 0) {
            for (int voiceIndex = 0; voiceIndex < SAMPLER_CHANNEL_VOICE_COUNT; ++voiceIndex) {
                SamplerSynthVoice *voice = new SamplerSynthVoice();
                voices[voiceIndex] = voice;
            }
            midiInPort = jack_port_register(jackClient, "midiIn", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
            leftPort = jack_port_register(jackClient, portNameLeft.toUtf8(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
            rightPort = jack_port_register(jackClient, portNameRight.toUtf8(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
            // Activate the client.
            if (jack_activate(jackClient) == 0) {
                jackConnect(jackClient, QString("%1:%2").arg(clientName).arg(portNameLeft).toUtf8(), QLatin1String{"system:playback_1"});
                jackConnect(jackClient, QString("%1:%2").arg(clientName).arg(portNameRight).toUtf8(), QLatin1String{"system:playback_2"});
                jackConnect(jackClient, QLatin1String("ZynMidiRouter:midi_out"), QString("%1:midiIn").arg(clientName));
                qInfo() << Q_FUNC_INFO << "Successfully created and set up" << clientName;
            } else {
                qWarning() << Q_FUNC_INFO << "Failed to activate SamplerSynth Jack client" << clientName;
            }
        } else {
            qWarning() << Q_FUNC_INFO << "Failed to set the SamplerSynth Jack processing callback";
        }
    } else {
        qWarning() << Q_FUNC_INFO << "Failed to set up SamplerSynth Jack client" << clientName;
    }
}

int SamplerChannel::process(jack_nframes_t nframes) {
    // First handle any queued up commands (starting, stopping, changes to voice state, that sort of stuff)
    while (readHead->clipCommand) {
        handleCommand(readHead->clipCommand, readHead->timestamp);
        readHead->clipCommand = nullptr;
        readHead = readHead->next;
    }
    if (enabled) {
        jack_nframes_t current_frames;
        jack_time_t current_usecs;
        jack_time_t next_usecs;
        float period_usecs;
        jack_get_cycle_times(jackClient, &current_frames, &current_usecs, &next_usecs, &period_usecs);
        // Then, if we've actually got our ports set up, let's play whatever voices are active
        jack_default_audio_sample_t *leftBuffer{nullptr}, *rightBuffer{nullptr};
        if (leftPort && rightPort) {
            leftBuffer = (jack_default_audio_sample_t*)jack_port_get_buffer(leftPort, nframes);
            rightBuffer = (jack_default_audio_sample_t*)jack_port_get_buffer(rightPort, nframes);
            memset(leftBuffer, 0, nframes * sizeof (jack_default_audio_sample_t));
            memset(rightBuffer, 0, nframes * sizeof (jack_default_audio_sample_t));
            for (SamplerSynthVoice *voice : qAsConst(voices)) {
                if (voice->isPlaying) {
                    voice->process(leftBuffer, rightBuffer, nframes, current_frames, current_usecs, next_usecs, period_usecs);
                }
            }
        }
        // Micro-hackery - -2 is the first item in the list of channels, so might as well just go with that
        if (midiChannel == -2) {
            cpuLoad = jack_cpu_load(jackClient);
        }
    }
    return 0;
}

class SamplerSynthImpl;
class SamplerSynthPrivate {
public:
    SamplerSynthPrivate() {
        syncTimer = qobject_cast<SyncTimer*>(SyncTimer_instance());
    }
    ~SamplerSynthPrivate() {
        qDeleteAll(channels);
    }
    SyncTimer* syncTimer{nullptr};
    QMutex synthMutex;
    bool syncLocked{false};
    SamplerSynthImpl *synth{nullptr};
    static const int numVoices{128};

    QHash<ClipAudioSource*, SamplerSynthSound*> clipSounds;
    te::Engine *engine{nullptr};

    // An ordered list of Jack clients, one each for...
    // Global audio (midi "channel" -2, for e.g. the metronome and sample previews)
    // Global effects targeted audio (midi "channel" -1)
    // Channel 1 (midi channel 0, and the logical music channel called Channel 1 in a sketchpad)
    // Channel 2 (midi channel 1)
    // ...
    // Channel 10 (midi channel 9)
    QList<SamplerChannel *> channels;
};

class SamplerSynthImpl : public juce::Synthesiser {
public:
    void startVoiceImpl(juce::SynthesiserVoice* voice, juce::SynthesiserSound* sound, int midiChannel, int midiNoteNumber, float velocity)
    {
        startVoice(voice, sound, midiChannel, midiNoteNumber, velocity);
    }
    SamplerSynthPrivate *d{nullptr};
};

void SamplerChannel::handleCommand(ClipCommand *clipCommand, quint64 currentTick)
{
    SamplerSynthSound *sound = d->clipSounds[clipCommand->clip];
    if (clipCommand->stopPlayback || clipCommand->startPlayback) {
        if (clipCommand->stopPlayback) {
            if (midiChannel == clipCommand->midiChannel) {
                for (SamplerSynthVoice * voice : qAsConst(voices)) {
                    const ClipCommand *currentVoiceCommand = voice->currentCommand();
                    if (voice->getCurrentlyPlayingSound().get() == sound && currentVoiceCommand->equivalentTo(clipCommand)) {
                        voice->stopNote(0.0f, true);
                        // We may have more than one thing going for the same sound on the same note, which... shouldn't
                        // really happen, but it's ugly and we just need to deal with that when stopping, so, stop /all/
                        // the voices where both the sound and the command match.
                    }
                }
            }
        }
        if (clipCommand->startPlayback) {
            if (midiChannel == clipCommand->midiChannel) {
                for (SamplerSynthVoice *voice : qAsConst(voices)) {
                    if (!voice->isPlaying) {
                        voice->setCurrentCommand(clipCommand);
                        voice->setStartTick(currentTick);
                        d->synth->startVoiceImpl(voice, sound, clipCommand->midiChannel, clipCommand->midiNote, clipCommand->volume);
                        break;
                    }
                }
            }
        }
    } else {
        if (midiChannel == clipCommand->midiChannel) {
            for (SamplerSynthVoice * voice : qAsConst(voices)) {
                const ClipCommand *currentVoiceCommand = voice->currentCommand();
                if (voice->getCurrentlyPlayingSound().get() == sound && currentVoiceCommand->equivalentTo(clipCommand)) {
                    // Update the voice with the new command
                    voice->setCurrentCommand(clipCommand);
                    // We may have more than one thing going for the same sound on the same note, which... shouldn't
                    // really happen, but it's ugly and we just need to deal with that when stopping, so, update /all/
                    // the voices where both the sound and the command match.
                }
            }
        }
    }
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
    d->synth->d = d;
}

SamplerSynth::~SamplerSynth()
{
    delete d->synth;
    delete d;
}

void SamplerSynth::initialize(tracktion_engine::Engine *engine)
{
    d->engine = engine;
    qInfo() << Q_FUNC_INFO << "Registering ten (plus two global) channels, with 8 voices each";
    for (int channelIndex = 0; channelIndex < 12; ++channelIndex) {
        QString channelName;
        if (channelIndex == 0) {
            channelName = QString("SamplerSynth-global-uneffected");
        } else if (channelIndex == 1) {
            channelName = QString("SamplerSynth-global-effected");
        } else {
            channelName = QString("SamplerSynth-channel_%1").arg(QString::number(channelIndex -1));
        }
        SamplerChannel *channel = new SamplerChannel(channelName);
        channel->d = d;
        // Funny story, the actual channels have midi channels equivalent to their name, minus one. The others we can cheat with
        channel->midiChannel = channelIndex - 2;
        jack_nframes_t sampleRate = jack_get_sample_rate(channel->jackClient);
        d->synth->setCurrentPlaybackSampleRate(sampleRate);
        for (SamplerSynthVoice *voice : qAsConst(channel->voices)) {
            d->synth->addVoice(voice);
        }
        d->channels << channel;
    }
}

tracktion_engine::Engine *SamplerSynth::engine() const
{
    return d->engine;
}

void SamplerSynth::registerClip(ClipAudioSource *clip)
{
    QMutexLocker locker(&d->synthMutex);
    if (!d->clipSounds.contains(clip)) {
        SamplerSynthSound *sound = new SamplerSynthSound(clip);
        d->clipSounds[clip] = sound;
        d->synth->addSound(sound);
    } else {
        qDebug() << "Clip list already contains the clip up for registration" << clip << clip->getFilePath();
    }
}

void SamplerSynth::unregisterClip(ClipAudioSource *clip)
{
    QMutexLocker locker(&d->synthMutex);
    if (d->clipSounds.contains(clip)) {
        d->clipSounds.remove(clip);
        for (int i = 0; i < d->synth->getNumSounds(); ++i) {
            SynthesiserSound::Ptr sound = d->synth->getSound(i);
            if (auto *samplerSound = static_cast<SamplerSynthSound*> (sound.get())) {
                if (samplerSound->clip() == clip) {
                    d->synth->removeSound(i);
                    break;
                }
            }
        }
    }
}

void SamplerSynth::handleClipCommand(ClipCommand *clipCommand)
{
    qWarning() << Q_FUNC_INFO << "This function is not sufficiently safe - schedule notes using SyncTimer::scheduleClipCommand instead!";
    handleClipCommand(clipCommand, d->syncTimer ? d->syncTimer->jackPlayhead() : 0);
}

float SamplerSynth::cpuLoad() const
{
    if (d->channels.count() == 0) {
        return 0;
    }
    return d->channels[0]->cpuLoad;
}

void SamplerSynth::handleClipCommand(ClipCommand *clipCommand, quint64 currentTick)
{
    if (d->clipSounds.contains(clipCommand->clip) && clipCommand->midiChannel + 2 < d->channels.count()) {
        SamplerChannel *channel = d->channels[clipCommand->midiChannel + 2];
        if (channel->writeHead->clipCommand) {
            qWarning() << Q_FUNC_INFO << "Big problem! Attempted to add a clip command to the queue, but we've not handled the one that's already in the queue.";
        } else {
            SamplerCommand* ringPosition = channel->writeHead;
            channel->writeHead = channel->writeHead->next;
            ringPosition->clipCommand = clipCommand;
            ringPosition->timestamp = currentTick;
        }
    }
}

void SamplerSynth::setChannelEnabled(const int &channel, const bool &enabled) const
{
    if (channel > -3 && channel < 10) {
        if (d->channels[channel + 2]->enabled != enabled) {
            qDebug() << "Setting SamplerSynth channel" << channel << "to" << enabled;
            d->channels[channel + 2]->enabled = enabled;
        }
    }
}

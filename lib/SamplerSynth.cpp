
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

struct SamplerTrack {
    jack_port_t *leftPort{nullptr};
    QString portNameLeft;
    jack_port_t *rightPort{nullptr};
    QString portNameRight;
    int midiChannel{-1};
    QList<SamplerSynthVoice *> voices;
};

class SamplerSynthImpl;
class SamplerSynthPrivate {
public:
    SamplerSynthPrivate() {
        syncTimer = qobject_cast<SyncTimer*>(SyncTimer_instance());
    }
    ~SamplerSynthPrivate() {
        if (jackClient) {
            jack_client_close(jackClient);
        }
    }
    SyncTimer* syncTimer{nullptr};
    QMutex synthMutex;
    bool syncLocked{false};
    SamplerSynthImpl *synth{nullptr};
    static const int numVoices{128};
    float cpuLoad{0.0f};

    QHash<ClipAudioSource*, SamplerSynthSound*> clipSounds;
    te::Engine *engine{nullptr};

    jack_client_t *jackClient{nullptr};
    // An ordered list of ports, in pairs of left and right channels, with two each for:
    // Global audio (midi "channel" -2, for e.g. the metronome and sample previews)
    // Global effects targeted audio (midi "channel" -1)
    // Track 1 (midi channel 0)
    // Track 2 (midi channel 1)
    // ...
    // Track 10 (midi channel 9)
    QList<SamplerTrack> tracks;
    int process(jack_nframes_t nframes) {
        jack_nframes_t current_frames;
        jack_time_t current_usecs;
        jack_time_t next_usecs;
        float period_usecs;
        jack_get_cycle_times(jackClient, &current_frames, &current_usecs, &next_usecs, &period_usecs);
            // Attempt to lock, but don't wait longer than half the available period, or we'll end up in trouble
        if (synthMutex.tryLock(period_usecs / 4000)) {
            for(const SamplerTrack &track : qAsConst(tracks)) {
                jack_default_audio_sample_t* leftBuffer = (jack_default_audio_sample_t*)jack_port_get_buffer(track.leftPort, nframes);
                jack_default_audio_sample_t* rightBuffer = (jack_default_audio_sample_t*)jack_port_get_buffer(track.rightPort, nframes);
                for (jack_nframes_t j = 0; j < nframes; j++) {
                        leftBuffer[j] = 0.0f;
                        rightBuffer[j] = 0.0f;
                }
                for (SamplerSynthVoice *voice : track.voices) {
                    // If we don't have a command set, there's definitely nothing playing (it gets set
                    // before playback starts and cleared after playback ends), consequently there's no
                    // reason to process this voice
                    if (voice->currentCommand()) {
                        voice->process(leftBuffer, rightBuffer, nframes, current_frames, current_usecs, next_usecs, period_usecs);
                    }
                }
            }
            cpuLoad = jack_cpu_load(jackClient);
            synthMutex.unlock();
        } else {
            qWarning() << "Failed to lock the samplersynth mutex within a reasonable amount of time, being" << period_usecs / 4000 << "ms";
        }
        return 0;
    }
};

class SamplerSynthImpl : public juce::Synthesiser {
public:
    void handleCommand(ClipCommand *clipCommand, quint64 currentTick);
    SamplerSynthPrivate *d{nullptr};
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
    d->synth = new SamplerSynthImpl();
    d->synth->d = d.get();
}

SamplerSynth::~SamplerSynth()
{
    delete d->synth;
}

static int client_process(jack_nframes_t nframes, void* arg) {
    return static_cast<SamplerSynthPrivate*>(arg)->process(nframes);
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

void SamplerSynth::initialize(tracktion_engine::Engine *engine)
{
    d->engine = engine;

    qDebug() << "Setting up SamplerSynth Jack client";
    jack_status_t real_jack_status{};
    d->jackClient = jack_client_open("SamplerSynth", JackNullOption, &real_jack_status);
    if (d->jackClient) {
        jack_nframes_t sampleRate = jack_get_sample_rate(d->jackClient);
        d->synth->setCurrentPlaybackSampleRate(sampleRate);
        // Register all our tracks for output
        qDebug() << "Registering ten (plus two global) tracks, with 16 voices each";
        for (int trackIndex = 0; trackIndex < 12; ++trackIndex) {
            d->tracks << SamplerTrack();
            // Funny story, the actual tracks have midi channels equivalent to their name, minus one. The others we can cheat with
            d->tracks[trackIndex].midiChannel = trackIndex - 2;
            if (trackIndex == 0) {
                d->tracks[trackIndex].portNameLeft = QString("global-uneffected_left");
                d->tracks[trackIndex].portNameRight = QString("global-uneffected_right");
            } else if (trackIndex == 1) {
                d->tracks[trackIndex].portNameLeft = QString("global-effected_left");
                d->tracks[trackIndex].portNameRight = QString("global-effected_right");
            } else {
                d->tracks[trackIndex].portNameLeft = QString("track_%1_left").arg(QString::number(trackIndex-1));
                d->tracks[trackIndex].portNameRight = QString("track_%1_right").arg(QString::number(trackIndex-1));
            }
            for (int voiceIndex = 0; voiceIndex < 16; ++voiceIndex) {
                SamplerSynthVoice *voice = new SamplerSynthVoice();
                d->tracks[trackIndex].voices << voice;
                d->synth->addVoice(voice);
            }
            d->tracks[trackIndex].leftPort = jack_port_register(d->jackClient, d->tracks[trackIndex].portNameLeft.toUtf8(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
            d->tracks[trackIndex].rightPort = jack_port_register(d->jackClient, d->tracks[trackIndex].portNameRight.toUtf8(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        }
        // Set the process callback.
        if (jack_set_process_callback(d->jackClient, client_process, static_cast<void*>(d.get())) != 0) {
            qWarning() << "Failed to set the SamplerSynth Jack processing callback";
        } else {
            // Activate the client.
            if (jack_activate(d->jackClient) == 0) {
                for (const SamplerTrack& track : d->tracks) {
                    jackConnect(d->jackClient, QString("SamplerSynth:%1").arg(track.portNameLeft).toUtf8(), QLatin1String{"system:playback_1"});
                    jackConnect(d->jackClient, QString("SamplerSynth:%1").arg(track.portNameRight).toUtf8(), QLatin1String{"system:playback_2"});
                }
                qDebug() << "Successfully created and set up the SamplerSynth's Jack client";
            } else {
                qWarning() << "Failed to activate SamplerSynth Jack client";
            }
        }
    }
}

tracktion_engine::Engine *SamplerSynth::engine() const
{
    return d->engine;
}

void SamplerSynth::registerClip(ClipAudioSource *clip)
{
    d->synthMutex.lock();
    if (!d->clipSounds.contains(clip)) {
        SamplerSynthSound *sound = new SamplerSynthSound(clip);
        d->clipSounds[clip] = sound;
        d->synth->addSound(sound);
    } else {
        qDebug() << "Clip list already contains the clip up for registration" << clip << clip->getFilePath();
    }
    d->synthMutex.unlock();
}

void SamplerSynth::unregisterClip(ClipAudioSource *clip)
{
    d->synthMutex.lock();
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
    d->synthMutex.unlock();
}

void SamplerSynth::handleClipCommand(ClipCommand *clipCommand)
{
    if (d->synthMutex.tryLock(1)) {
        d->synth->handleCommand(clipCommand, d->syncTimer ? d->syncTimer->jackPlayhead() : 0);
        d->synthMutex.unlock();
    } else {
        // If we failed to lock the mutex, postpone this command until the next run of the event loop
        qWarning() << "Failed to lock synth mutex, postponing until next tick";
        QTimer::singleShot(0, this, [this, clipCommand](){ handleClipCommand(clipCommand); });
    }
}

void SamplerSynthImpl::handleCommand(ClipCommand *clipCommand, quint64 currentTick)
{
    if (d->clipSounds.contains(clipCommand->clip)) {
        SamplerSynthSound *sound = d->clipSounds[clipCommand->clip];
        if (clipCommand->stopPlayback || clipCommand->startPlayback) {
            if (clipCommand->stopPlayback) {
                for (const SamplerTrack& track : d->tracks) {
                    if (track.midiChannel == clipCommand->midiChannel) {
                        for (SamplerSynthVoice * voice : track.voices) {
                            const ClipCommand *currentVoiceCommand = voice->currentCommand();
                            if (voice->getCurrentlyPlayingSound().get() == sound && currentVoiceCommand->equivalentTo(clipCommand)) {
                                voice->stopNote(0.0f, true);
                                // We may have more than one thing going for the same sound on the same note, which... shouldn't
                                // really happen, but it's ugly and we just need to deal with that when stopping, so, stop /all/
                                // the voices where both the sound and the command match.
                            }
                        }
                        break;
                    }
                }
            }
            if (clipCommand->startPlayback) {
                for (const SamplerTrack& track : d->tracks) {
                    if (track.midiChannel == clipCommand->midiChannel) {
                        for (SamplerSynthVoice *voice : track.voices) {
                            if (!voice->isVoiceActive()) {
                                voice->setCurrentCommand(clipCommand);
                                voice->setStartTick(currentTick);
                                startVoice(voice, sound, clipCommand->midiChannel, clipCommand->midiNote, clipCommand->volume);
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        } else {
            for (const SamplerTrack& track : d->tracks) {
                if (track.midiChannel == clipCommand->midiChannel) {
                    for (SamplerSynthVoice * voice : track.voices) {
                        const ClipCommand *currentVoiceCommand = voice->currentCommand();
                        if (voice->getCurrentlyPlayingSound().get() == sound && currentVoiceCommand->equivalentTo(clipCommand)) {
                            // Update the voice with the new command
                            voice->setCurrentCommand(clipCommand);
                            // We may have more than one thing going for the same sound on the same note, which... shouldn't
                            // really happen, but it's ugly and we just need to deal with that when stopping, so, update /all/
                            // the voices where both the sound and the command match.
                        }
                    }
                    break;
                }
            }
        }
    }
}

float SamplerSynth::cpuLoad() const
{
    return d->cpuLoad;
}

void SamplerSynth::lock()
{
    if (d->synthMutex.tryLock(1)) {
        d->syncLocked = true;
    }
}

void SamplerSynth::handleClipCommand(ClipCommand *clipCommand, quint64 currentTick)
{
    if (d->syncLocked) {
        d->synth->handleCommand(clipCommand, currentTick);
    } else {
        // Super not cool, we're not locked, so we're going to have to postpone some things...
        qWarning() << "Did not achieve lock prior to calling, postponing until next tick";
        QTimer::singleShot(0, this, [this, clipCommand](){ handleClipCommand(clipCommand); });
    }
}

void SamplerSynth::unlock()
{
    if (d->syncLocked) {
        d->syncLocked = false;
        d->synthMutex.unlock();
    }
}

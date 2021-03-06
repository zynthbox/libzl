#include "MidiRouter.h"

#include <QDebug>
#include <QProcessEnvironment>
#include <QTimer>

#include <jack/jack.h>
#include <jack/midiport.h>

// Set this to true to emit a bunch more debug output when the router is operating
#define DebugZLRouter false

class InputDevice {
public:
    InputDevice(const QString &jackPortName)
        : jackPortName(jackPortName)
    {
        for (int i = 0; i < 128; ++i) {
            noteActivations[i] = 0;
            activeNoteChannel[i] = 0;
        }
    }
    ~InputDevice() {}
    // The string name which identifies this input device in jack
    QString jackPortName;
    // A human-readable name for the port (derived from the port's first alias, if one exists, otherwise it's the port name)
    QString humanReadableName;
    // The number of times we have received a note activation on this channel
    // After the first, assume that all other activations will go to the same destination
    QHash<int, int> noteActivations;
    // The channel which the most recent note activation went to
    // This will be written the first time the associated noteActivations position gets
    // incremented above 0 (all subsequent events read from this hash to determine the
    // destination instead)
    QHash<int, int> activeNoteChannel;
    // The jack port we connect to for reading events
    jack_port_t* port;
    // Whether or not we should read events from this device
    bool enabled{false};
};

// This is our translation from midi input channels to destinations. It contains
// information on what external output channel should be used if it's not a straight
// passthrough to the same channel the other side, and what channels should be
// targeted on the zynthian outputs.
struct TrackOutput {
    TrackOutput(int inputChannel)
        : inputChannel(inputChannel)
    {
        // By default we assume a straight passthrough for zynthian. This isn't
        // always true, but it's a sensible enough default, and if users need it
        // changed, that will need to happen later (like the external channel)
        zynthianChannels << inputChannel;
    }
    int inputChannel{-1};
    QString portName;
    jack_port_t *port;
    int externalChannel{-1};
    QList<int> zynthianChannels;
    MidiRouter::RoutingDestination destination{MidiRouter::ZynthianDestination};
    void *channelBuffer{nullptr};
};

class MidiRouterPrivate {
public:
    MidiRouterPrivate(MidiRouter *q)
        : q(q)
    {};
    ~MidiRouterPrivate() {
        if (jackClient) {
            jack_client_close(jackClient);
        }
        qDeleteAll(hardwareInputs);
        qDeleteAll(outputs);
    };

    MidiRouter *q;
    bool constructing{true};
    bool filterMidiOut{false};
    QStringList disabledMidiInPorts;
    QStringList enabledMidiOutPorts;
    QStringList enabledMidiFbPorts;

    int currentChannel{0};
    jack_client_t* jackClient{nullptr};
    jack_port_t *midiInPort{nullptr};

    jack_port_t *passthroughPort{nullptr};
    jack_port_t *internalPassthrough{nullptr};
    jack_port_t *hardwareInPassthrough{nullptr};
    jack_port_t *zynthianOutput{nullptr};
    jack_port_t *samplerOutput{nullptr};
    jack_port_t *externalOutput{nullptr};

    QList<InputDevice*> hardwareInputs;
    QList<TrackOutput *> outputs;

    void connectPorts(const QString &from, const QString &to) {
        int result = jack_connect(jackClient, from.toUtf8(), to.toUtf8());
        if (result == 0 || result == EEXIST) {
            if (DebugZLRouter) { qDebug() << "ZLRouter:" << (result == EEXIST ? "Retaining existing connection from" : "Successfully created new connection from" ) << from << "to" << to; }
        } else {
            qWarning() << "ZLRouter: Failed to connect" << from << "with" << to << "with error code" << result;
            // This should probably reschedule an attempt in the near future, with a limit to how long we're trying for?
        }
    }
    void disconnectPorts(const QString &from, const QString &to) {
        // Don't attempt to connect already connected ports
        int result = jack_disconnect(jackClient, from.toUtf8(), to.toUtf8());
        if (result == 0) {
            if (DebugZLRouter) { qDebug() << "ZLRouter: Successfully disconnected" << from << "from" << to; }
        } else {
            qWarning() << "ZLRouter: Failed to disconnect" << from << "from" << to << "with error code" << result;
        }
    }

    // Really just a convenience function because it makes it easier to read things below while retaining the errorhandling/debug stuff
    static inline void writeEventToBuffer(const jack_midi_event_t &event, void *buffer, int currentChannel, TrackOutput *output, int outputChannel = -1) {
        const int eventChannel = (event.buffer[0] & 0xf);
        if (outputChannel > -1) {
            event.buffer[0] = event.buffer[0] - eventChannel + outputChannel;
        }
        if (jack_midi_event_write(buffer, event.time, event.buffer, event.size) == ENOBUFS) {
            qWarning() << "ZLRouter: Ran out of space while writing events!";
        } else {
            if (DebugZLRouter) { qDebug() << "ZLRouter: Wrote event to buffer on channel" << currentChannel << "for port" << output->portName; }
        }
        if (outputChannel > -1) {
            event.buffer[0] = event.buffer[0] + eventChannel - outputChannel;
        }
    }

    int process(jack_nframes_t nframes) {
        // Get all our output channels' buffers and clear them, if there was one previously set.
        // A little overly protective, given how lightweight the functions are, but might as well
        // be lighter-weight about it.
        for (TrackOutput *output : qAsConst(outputs)) {
            if (output->channelBuffer) {
                jack_midi_clear_buffer(output->channelBuffer);
                output->channelBuffer = nullptr;
            }
        }
        // Get and clear our passthrough buffers
        void *passthroughBuffer = jack_port_get_buffer(passthroughPort, nframes);
        jack_midi_clear_buffer(passthroughBuffer);
        void *internalPassthroughBuffer = jack_port_get_buffer(internalPassthrough, nframes);
        jack_midi_clear_buffer(internalPassthroughBuffer);
        void *hardwareInPassthroughBuffer = jack_port_get_buffer(hardwareInPassthrough, nframes);
        jack_midi_clear_buffer(hardwareInPassthroughBuffer);
        void *zynthianOutputBuffer = jack_port_get_buffer(zynthianOutput, nframes);
        jack_midi_clear_buffer(zynthianOutputBuffer);
        void *samplerOutputBuffer = jack_port_get_buffer(samplerOutput, nframes);
        jack_midi_clear_buffer(samplerOutputBuffer);
        void *externalOutputBuffer = jack_port_get_buffer(externalOutput, nframes);
        jack_midi_clear_buffer(externalOutputBuffer);
        // First handle input coming from our own inputs, because we gotta start somewhere
        void *inputBuffer = jack_port_get_buffer(midiInPort, nframes);
        uint32_t events = jack_midi_get_event_count(inputBuffer);
        TrackOutput *output{nullptr};
        jack_midi_event_t event;
        int eventChannel{-1};
        for (uint32_t eventIndex = 0; eventIndex < events; ++eventIndex) {
            if (jack_midi_event_get(&event, inputBuffer, eventIndex)) {
                qWarning() << "ZLRouter: jack_midi_event_get failed, received note lost!";
                continue;
            }
            if ((event.buffer[0] & 0xf0) == 0xf0) {
                continue;
            }
            eventChannel = (event.buffer[0] & 0xf);
            if (eventChannel > -1 && eventChannel < outputs.count()) {
                output = outputs[eventChannel];
                if (!output->channelBuffer) {
                    output->channelBuffer = jack_port_get_buffer(output->port, nframes);
                }
                switch (output->destination) {
                    case MidiRouter::ZynthianDestination:
                        writeEventToBuffer(event, passthroughBuffer, currentChannel, output);
                        writeEventToBuffer(event, internalPassthroughBuffer, currentChannel, output);
                        writeEventToBuffer(event, zynthianOutputBuffer, eventChannel, output);
                        for (const int &zynthianChannel : output->zynthianChannels) {
                            writeEventToBuffer(event, output->channelBuffer, eventChannel, output, zynthianChannel);
                        }
                        break;
                    case MidiRouter::SamplerDestination:
                        writeEventToBuffer(event, passthroughBuffer, currentChannel, output);
                        writeEventToBuffer(event, internalPassthroughBuffer, currentChannel, output);
                        writeEventToBuffer(event, output->channelBuffer, eventChannel, output);
                        writeEventToBuffer(event, samplerOutputBuffer, eventChannel, output);
                        break;
                    case MidiRouter::ExternalDestination:
                    {
                        writeEventToBuffer(event, passthroughBuffer, currentChannel, output);
                        // Not writing to internal passthrough, as this is heading to an external device
                        int externalChannel = (output->externalChannel == -1) ? output->inputChannel : output->externalChannel;
                        writeEventToBuffer(event, externalOutputBuffer, eventChannel, output, externalChannel);
                        writeEventToBuffer(event, output->channelBuffer, eventChannel, output, externalChannel);
                    }
                    case MidiRouter::NoDestination:
                    default:
                        writeEventToBuffer(event, internalPassthroughBuffer, currentChannel, output);
                        // Do nothing here
                        break;
                }
            } else {
                qWarning() << "ZLRouter: Something's badly wrong and we've ended up with a message supposedly on channel" << eventChannel;
            }
        }
        jack_midi_clear_buffer(inputBuffer);
        // Now handle all the hardware input - magic for the ones we want to direct to external ports, and straight passthrough for ones aimed at zynthian
        if (currentChannel > -1 && currentChannel < outputs.count()) {
            int adjustedCurrentChannel{currentChannel};
            for (InputDevice *device : qAsConst(hardwareInputs)) {
                if (device->enabled) {
                    inputBuffer = jack_port_get_buffer(device->port, nframes);
                    output = outputs[currentChannel];
                    events = jack_midi_get_event_count(inputBuffer);
                    for (uint32_t eventIndex = 0; eventIndex < events; ++eventIndex) {
                        if (jack_midi_event_get(&event, inputBuffer, eventIndex)) {
                            qWarning() << "ZLRouter: jack_midi_event_get failed, received note lost!";
                            continue;
                        }
                        if ((event.buffer[0] & 0xf0) == 0xf0) {
                            continue;
                        }
                        eventChannel = (event.buffer[0] & 0xf);
                        if (eventChannel > -1 && eventChannel < outputs.count()) {
                            // Make sure we're using the correct output
                            // This is done to ensure that if we have any note-on events happen on some
                            // output, then all the following commands associated with that note should
                            // go to the same output (so any further ons, and any matching offs)
                            const unsigned char &byte1 = event.buffer[0];
                            if (0x7F < byte1 && byte1 < 0xA0) {
                                const bool setOn = (byte1 >= 0x90);
                                const int &midiNote = event.buffer[1];
                                int &noteActivation = device->noteActivations[midiNote];
                                if (setOn) {
                                    ++noteActivation;
                                    if (noteActivation == 1) {
                                        device->activeNoteChannel[midiNote] = currentChannel;
                                    }
                                } else if (noteActivation > 0) {
                                    --noteActivation;
                                }
                                adjustedCurrentChannel = device->activeNoteChannel[midiNote];
                                output = outputs[adjustedCurrentChannel];
                                if (currentChannel == adjustedCurrentChannel) {
                                    event.buffer[0] = event.buffer[0] - eventChannel + currentChannel;
                                } else {
                                    event.buffer[0] = event.buffer[0] - eventChannel + adjustedCurrentChannel;
                                }
                            }
                            // Now ensure we have our output buffer
                            if (!output->channelBuffer) {
                                output->channelBuffer = jack_port_get_buffer(output->port, nframes);
                            }

                            switch (output->destination) {
                                case MidiRouter::ZynthianDestination:
                                    writeEventToBuffer(event, passthroughBuffer, adjustedCurrentChannel, output);
                                    writeEventToBuffer(event, zynthianOutputBuffer, eventChannel, output);
                                    for (const int &zynthianChannel : output->zynthianChannels) {
                                        writeEventToBuffer(event, output->channelBuffer, eventChannel, output, zynthianChannel);
                                    }
                                    break;
                                case MidiRouter::SamplerDestination:
                                    writeEventToBuffer(event, passthroughBuffer, adjustedCurrentChannel, output);
                                    writeEventToBuffer(event, samplerOutputBuffer, eventChannel, output);
                                    writeEventToBuffer(event, output->channelBuffer, eventChannel, output);
                                    break;
                                case MidiRouter::ExternalDestination:
                                {
                                    writeEventToBuffer(event, passthroughBuffer, adjustedCurrentChannel, output);
                                    int externalChannel = (output->externalChannel == -1) ? output->inputChannel : output->externalChannel;
                                    writeEventToBuffer(event, externalOutputBuffer, eventChannel, output, externalChannel);
                                    writeEventToBuffer(event, output->channelBuffer, eventChannel, output, externalChannel);
                                }
                                case MidiRouter::NoDestination:
                                default:
                                    // Do nothing here
                                    break;
                            }
                            writeEventToBuffer(event, hardwareInPassthroughBuffer, eventChannel, output);
                        } else {
                            qWarning() << "ZLRouter: Something's badly wrong and we've ended up with a message supposedly on channel" << eventChannel;
                        }
                    }
                }
                jack_midi_clear_buffer(inputBuffer);
            }
        }
        return 0;
    }
    int xrun() {
        return 0;
    }

    QTimer *hardwareInputConnector{nullptr};
    void connectHardwareInputs() {
        const char **ports = jack_get_ports(jackClient, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsPhysical | JackPortIsOutput);
        QList<InputDevice*> newDevices;
        for (const char **p = ports; *p; p++) {
            const QString inputPortName{QString::fromLocal8Bit(*p)};
            InputDevice *device{nullptr};
            for (InputDevice *needle : hardwareInputs) {
                if (needle->jackPortName == inputPortName) {
                    device = needle;
                    break;
                }
            }
            if (!device) {
                device = new InputDevice(inputPortName);
                hardwareInputs << device;
                device->port = jack_port_register(jackClient, QString("input-%1").arg(inputPortName).toUtf8(), JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
                if (device->port) {
                    jack_port_t *hardwarePort = jack_port_by_name(jackClient, *p);
                    if (hardwarePort) {
                        int num_aliases;
                        char *aliases[2];
                        aliases[0] = (char *)malloc(size_t(jack_port_name_size()));
                        aliases[1] = (char *)malloc(size_t(jack_port_name_size()));
                        num_aliases = jack_port_get_aliases(hardwarePort, aliases);
                        if (num_aliases > 0) {
                            int i;
                            for (i = 0; i < num_aliases; i++) {
                                QStringList splitAlias = QString::fromUtf8(aliases[i]).split('-');
                                if (splitAlias.length() > 5) {
                                    for (int i = 0; i < 5; ++i) {
                                        splitAlias.removeFirst();
                                    }
                                    device->humanReadableName = splitAlias.join(" ");
                                }
                            }
                        }
                        if (device->humanReadableName.isEmpty()) {
                            device->humanReadableName = device->jackPortName.split(':').last();
                        }
                        qDebug() << "ZLRouter: Discovered a new bit of hardware, named" << inputPortName << "which we have given the friendly name" << device->humanReadableName;
                    } else {
                        qWarning() << "ZLRouter: Failed to open hardware port for identification:" << inputPortName;
                    }

                    connectPorts(inputPortName, QString("ZLRouter:input-%1").arg(inputPortName));
                    Q_EMIT q->addedHardwareInputDevice(inputPortName, device->humanReadableName);
                } else {
                    qWarning() << "ZLRouter: Failed to register input port for" << inputPortName;
                    device->enabled = false;
                }
            }
            if (device->port) {
                device->enabled = !disabledMidiInPorts.contains(device->jackPortName);
                qDebug() << "ZLRouter: Updated" << device->jackPortName << "enabled state to" << device->enabled;
            }
            newDevices << device;
        }
        // Clean up, in case something's been removed
        QMutableListIterator<InputDevice*> iterator(hardwareInputs);
        while (iterator.hasNext()) {
            InputDevice *device = iterator.next();
            if (!newDevices.contains(device)) {
                iterator.remove();
                qDebug() << "ZLRouter: Detected removal of a hardware device" << device->jackPortName;
                Q_EMIT q->removedHardwareInputDevice(device->jackPortName, device->humanReadableName);
                // TODO When disconnecting a bit of hardware, before deleting it, cycle through its noteActivations and ensure we spit out equivalent off events
                delete device;
            }
        }
        free(ports);
    }

    void disconnectFromOutputs(TrackOutput *output) {
        const QString portName = QString("ZLRouter:%1").arg(output->portName);
        if (output->destination == MidiRouter::ZynthianDestination) {
            disconnectPorts(portName, QLatin1String{"ZynMidiRouter:step_in"});
        } else if (output->destination == MidiRouter::ExternalDestination) {
            for (const QString &externalPort : enabledMidiOutPorts) {
                disconnectPorts(portName, externalPort);
            }
        }
    }

    void connectToOutputs(TrackOutput *output) {
        const QString portName = QString("ZLRouter:%1").arg(output->portName);
        if (output->destination == MidiRouter::ZynthianDestination) {
            connectPorts(portName, QLatin1String{"ZynMidiRouter:step_in"});
        } else if (output->destination == MidiRouter::ExternalDestination) {
            for (const QString &externalPort : enabledMidiOutPorts) {
                connectPorts(portName, externalPort);
            }
        }
    }
};

static int client_process(jack_nframes_t nframes, void* arg) {
    return static_cast<MidiRouterPrivate*>(arg)->process(nframes);
}
static int client_xrun(void* arg) {
    return static_cast<MidiRouterPrivate*>(arg)->xrun();
}
static void client_port_registration(jack_port_id_t /*port*/, int /*registering*/, void *arg) {
    QMetaObject::invokeMethod(static_cast<MidiRouterPrivate*>(arg)->hardwareInputConnector, "start");
}
static void client_registration(const char */*name*/, int /*registering*/, void *arg) {
    QMetaObject::invokeMethod(static_cast<MidiRouterPrivate*>(arg)->hardwareInputConnector, "start");
}

MidiRouter::MidiRouter(QObject *parent)
    : QObject(parent)
    , d(new MidiRouterPrivate(this))
{
    // First, load up our configuration (TODO: also remember to reload it when the config changes)
    reloadConfiguration();
    // Open the client.
    jack_status_t real_jack_status{};
    d->jackClient = jack_client_open(
        "ZLRouter",
        JackNullOption,
        &real_jack_status
    );
    if (d->jackClient) {
        // Register the MIDI output port.
        d->midiInPort = jack_port_register(
            d->jackClient,
            "MidiIn",
            JACK_DEFAULT_MIDI_TYPE,
            JackPortIsInput,
            0
        );
        if (d->midiInPort) {
            // Set the process callback.
            if (
                jack_set_process_callback(
                    d->jackClient,
                    client_process,
                    static_cast<void*>(d)
                ) != 0
            ) {
                qWarning() << "ZLRouter: Failed to set the ZLRouter Jack processing callback";
            } else {
                jack_set_xrun_callback(d->jackClient, client_xrun, static_cast<void*>(d));
                d->hardwareInputConnector = new QTimer(this);
                d->hardwareInputConnector->setSingleShot(true);
                d->hardwareInputConnector->setInterval(300);
                connect(d->hardwareInputConnector, &QTimer::timeout, this, [this](){ d->connectHardwareInputs(); });
                jack_set_port_registration_callback(d->jackClient, client_port_registration, static_cast<void*>(d));
                jack_set_client_registration_callback(d->jackClient, client_registration, static_cast<void*>(d));
                // Activate the client.
                if (jack_activate(d->jackClient) == 0) {
                    qDebug() << "ZLRouter: Successfully created and set up the ZLRouter's Jack client";
                    const QString zmrPort{"ZynMidiRouter:step_in"};
                    // We technically only have ten tracks, but there's no reason we can't handle 16... so, let's do it like so
                    for (int channel = 0; channel < 16; ++channel) {
                        TrackOutput *output = new TrackOutput(channel);
                        output->portName = QString("Channel%2").arg(QString::number(channel));
                        output->port = jack_port_register(d->jackClient, output->portName.toUtf8(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
                        d->outputs << output;
                        d->connectPorts(QString("ZLRouter:%1").arg(output->portName), zmrPort);
                    }
                } else {
                    qWarning() << "ZLRouter: Failed to activate ZLRouter Jack client";
                }
            }
            // Create the passthrough ports
            d->passthroughPort = jack_port_register(d->jackClient, "Passthrough", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
            if (!d->passthroughPort) {
                qWarning() << "ZLRouter: Could not register ZLRouter Jack passthrough port";
            }
            d->internalPassthrough = jack_port_register(d->jackClient, "InternalPassthrough", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
            if (!d->internalPassthrough) {
                qWarning() << "ZLRouter: Could not register ZLRouter Jack Hardware in passthrough output port";
            }
            d->hardwareInPassthrough = jack_port_register(d->jackClient, "HardwareInPassthrough", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
            if (!d->hardwareInPassthrough) {
                qWarning() << "ZLRouter: Could not register ZLRouter Jack Hardware in passthrough output port";
            }
            d->externalOutput = jack_port_register(d->jackClient, "ExternalOut", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
            if (!d->externalOutput) {
                qWarning() << "ZLRouter: Could not register ZLRouter Jack External destination output port";
            }
            d->zynthianOutput = jack_port_register(d->jackClient, "ZynthianOut", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
            if (!d->zynthianOutput) {
                qWarning() << "ZLRouter: Could not register ZLRouter Jack Zynthian destination output port";
            }
            d->samplerOutput = jack_port_register(d->jackClient, "SamplerOut", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
            if (!d->samplerOutput) {
                qWarning() << "ZLRouter: Could not register ZLRouter Jack Sampler destination output port";
            }
            // Now hook up the hardware inputs
            d->hardwareInputConnector->start();
        } else {
            qWarning() << "ZLRouter: Could not register ZLRouter Jack input port for internal messages";
        }
    } else {
        qWarning() << "ZLRouter: Could not create the ZLRouter Jack client.";
    }
    d->constructing = false;
}

MidiRouter::~MidiRouter()
{
    delete d;
}

void MidiRouter::setChannelDestination(int channel, MidiRouter::RoutingDestination destination, int externalChannel)
{
    if (channel > -1 && channel < d->outputs.count()) {
        TrackOutput *output = d->outputs[channel];
        output->externalChannel = externalChannel;
        if (output->destination != destination) {
            d->disconnectFromOutputs(output);
            output->destination = destination;
            d->connectToOutputs(output);
        }
    }
}

void MidiRouter::setCurrentChannel(int currentChannel)
{
    if (d->currentChannel != currentChannel) {
        d->currentChannel = qBound(0, currentChannel, 15);
        Q_EMIT currentChannelChanged();
    }
}

int MidiRouter::currentChannel() const
{
    return d->currentChannel;
}

void MidiRouter::setZynthianChannels(int channel, QList<int> zynthianChannels)
{
    if (channel > -1 && channel < d->outputs.count()) {
        TrackOutput *output = d->outputs[channel];
        bool hasChanged = (output->zynthianChannels.count() != zynthianChannels.count());
        if (!hasChanged) {
            for (int i = 0; i < zynthianChannels.count(); ++i) {
                if (output->zynthianChannels[i] != zynthianChannels[i]) {
                    hasChanged = true;
                    break;
                }
            }
        }
        if (hasChanged) {
            if (DebugZLRouter) { qDebug() << "ZLRouter: Updating zynthian channels for" << output->portName << "from" << output->zynthianChannels << "to" << zynthianChannels; }
            output->zynthianChannels = zynthianChannels;
        }
    }
}

void MidiRouter::reloadConfiguration()
{
    // TODO Make the fb stuff work as well... (also, note to self, work out what that stuff actually is?)
    if (!d->constructing) {
        // First, disconnect our outputs, just in case...
        for (TrackOutput *output : d->outputs) {
            d->disconnectFromOutputs(output);
        }
    }
    // If 0, zynthian expects no midi to be routed externally, and if 1 it expects everything to go out
    // So, in our parlance, that means that 1 means route events external for anything on a Zynthian channel, and for non-Zynthian channels, use our own rules
    QString envVar = qgetenv("ZYNTHIAN_MIDI_FILTER_OUTPUT");
    if (envVar.isEmpty()) {
        if (DebugZLRouter) { qDebug() << "No env var data for output filtering, setting default"; }
        envVar = "0";
    }
    d->filterMidiOut = envVar.toInt();
    envVar = qgetenv("ZYNTHIAN_MIDI_PORTS");
    if (envVar.isEmpty()) {
        if (DebugZLRouter) { qDebug() << "No env var data for midi ports, setting default"; }
        envVar = "DISABLED_IN=\\nENABLED_OUT=ttymidi:MIDI_out\\nENABLED_FB=";
    }
    const QStringList midiPorts = envVar.split("\\n");
    for (const QString &portOptions : midiPorts) {
        const QStringList splitOptions{portOptions.split("=")};
        if (splitOptions.length() == 2) {
            if (splitOptions[0] == "DISABLED_IN") {
                d->disabledMidiInPorts = splitOptions[1].split(",");
            } else if (splitOptions[0] == "ENABLED_OUT") {
                d->enabledMidiOutPorts = splitOptions[1].split(",");
            } else if (splitOptions[0] == "ENABLED_FB") {
                d->enabledMidiFbPorts = splitOptions[1].split(",");
            }
        } else {
            qWarning() << "ZLRouter: Malformed option in the midi ports variable - we expected a single = in the following string, and encountered two:" << portOptions;
        }
    }
    if (DebugZLRouter) {
        qDebug() << "ZLRouter: Loaded settings, which are now:";
        qDebug() << "Filter midi out?" << d->filterMidiOut;
        qDebug() << "Disabled midi input devices:" << d->disabledMidiInPorts;
        qDebug() << "Enabled midi output devices:" << d->enabledMidiOutPorts;
        qDebug() << "Enabled midi fb devices:" << d->enabledMidiFbPorts;
    }
    if (!d->constructing) {
        // Reconnect out outputs after reloading
        for (TrackOutput *output : d->outputs) {
            d->connectToOutputs(output);
        }
        d->connectHardwareInputs();
    }
}

#include "MidiRouter.h"

#include <QDebug>
#include <QProcessEnvironment>
#include <QTimer>

#include <jack/jack.h>
#include <jack/midiport.h>

// Set this to true to emit a bunch more debug output when the router is operating
#define DebugZLRouter false

struct ChannelOutput {
    ChannelOutput(int channel) : channel(channel) {}
    int channel{-1};
    QString portName;
    jack_port_t *port;
    int externalChannel{-1};
    MidiRouter::RoutingDestination destination{MidiRouter::ZynthianDestination};
    void *channelBuffer{nullptr};
};

class MidiRouterPrivate {
public:
    MidiRouterPrivate() {};
    ~MidiRouterPrivate() {
        if (jackClient) {
            jack_client_close(jackClient);
        }
        qDeleteAll(outputs);
    };

    bool constructing{true};
    bool filterMidiOut{false};
    QStringList disabledMidiInPorts;
    QStringList enabledMidiOutPorts;
    QStringList enabledMidiFbPorts;

    int currentChannel{0};
    jack_client_t* jackClient{nullptr};
    jack_port_t *midiInPort{nullptr};
    jack_port_t *externalInput{nullptr};

    jack_port_t *passthroughPort{nullptr};
    jack_port_t *hardwareInPassthrough{nullptr};
    jack_port_t *zynthianOutput{nullptr};
    jack_port_t *samplerOutput{nullptr};
    jack_port_t *externalOutput{nullptr};

    QList<ChannelOutput*> outputs;

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
    static inline void writeEventToBuffer(const jack_midi_event_t &event, void *buffer, int currentChannel, ChannelOutput *output) {
        if (jack_midi_event_write(buffer, event.time, event.buffer, event.size) == ENOBUFS) {
            qWarning() << "ZLRouter: Ran out of space while writing events!";
        } else {
            if (DebugZLRouter) { qDebug() << "ZLRouter: Wrote event to buffer on channel" << currentChannel << "for port" << output->portName; }
        }
    }

    int process(jack_nframes_t nframes) {
        // Get all our output channels' buffers and clear them, if there was one previously set.
        // A little overly protective, given how lightweight the functions are, but might as well
        // be lighter-weight about it.
        for (ChannelOutput *output : outputs) {
            if (output->channelBuffer) {
                jack_midi_clear_buffer(output->channelBuffer);
                output->channelBuffer = nullptr;
            }
        }
        // Get and clear our passthrough buffers
        void *passthroughBuffer = jack_port_get_buffer(passthroughPort, nframes);
        jack_midi_clear_buffer(passthroughBuffer);
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
        ChannelOutput *output{nullptr};
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
                if (output->destination != MidiRouter::NoDestination) {
                    writeEventToBuffer(event, passthroughBuffer, currentChannel, output);
                    if (output->destination == MidiRouter::ExternalDestination && output->externalChannel > -1) {
                        if (DebugZLRouter) { qDebug() << "ZLRouter: We're being redirected to a different channel, let's obey that - going from" << eventChannel << "to" << output->externalChannel; }
                        event.buffer[0] = event.buffer[0] - eventChannel + output->externalChannel;
                    }
                    writeEventToBuffer(event, output->channelBuffer, eventChannel, output);
                    switch (output->destination) {
                        case MidiRouter::ZynthianDestination:
                            writeEventToBuffer(event, zynthianOutputBuffer, eventChannel, output);
                            break;
                        case MidiRouter::SamplerDestination:
                            writeEventToBuffer(event, samplerOutputBuffer, eventChannel, output);
                            break;
                        case MidiRouter::ExternalDestination:
                            writeEventToBuffer(event, externalOutputBuffer, eventChannel, output);
                        case MidiRouter::NoDestination:
                        default:
                            // Do nothing here
                            break;
                    }
                }
            } else {
                qWarning() << "ZLRouter: Something's badly wrong and we've ended up with a message supposedly on channel" << eventChannel;
            }
        }
        jack_midi_clear_buffer(inputBuffer);
        // Now handle all the hardware input - magic for the ones we want to direct to external ports, and straight passthrough for ones aimed at zynthian
        inputBuffer = jack_port_get_buffer(externalInput, nframes);
        if (currentChannel > -1 && currentChannel < outputs.count()) {
            output = outputs[currentChannel];
            bool outputToExternal{currentChannel < outputs.count()};
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
                    if (!output->channelBuffer) {
                        output->channelBuffer = jack_port_get_buffer(output->port, nframes);
                    }
                    // Pass through the note to the channel we're expecting notes on internally...
                    event.buffer[0] = event.buffer[0] - eventChannel + currentChannel;
                    if (output->destination != MidiRouter::NoDestination) {
                        writeEventToBuffer(event, passthroughBuffer, currentChannel, output);
                    }
                    // If the track targets zynthian, pass it directly through to there
                    if (output->destination == MidiRouter::ZynthianDestination) {
                        writeEventToBuffer(event, output->channelBuffer, eventChannel, output);
                        writeEventToBuffer(event, zynthianOutputBuffer, eventChannel, output);
                    }
                    // If the track targets the sampler, send things to the sampeler destination buffer
                    else if (output->destination == MidiRouter::ZynthianDestination) {
                        writeEventToBuffer(event, samplerOutputBuffer, eventChannel, output);
                    }
                    // If we're supposed to target the external thing, do that, with adjustments as expected
                    else if (outputToExternal && (output->destination == MidiRouter::ExternalDestination || filterMidiOut)) {
                        if (output->externalChannel > -1) {
                            // Reset it to the origin before reworking to the new channel
                            event.buffer[0] = event.buffer[0] + eventChannel - currentChannel;
                            if (DebugZLRouter) { qDebug() << "ZLRouter: Hardware Event: We're being redirected to a different channel, let's obey that - going from" << eventChannel << "to" << output->externalChannel; }
                            event.buffer[0] = event.buffer[0] - eventChannel + output->externalChannel;
                        }
                        writeEventToBuffer(event, output->channelBuffer, eventChannel, output);
                        writeEventToBuffer(event, externalOutputBuffer, eventChannel, output);
                    }
                    writeEventToBuffer(event, hardwareInPassthroughBuffer, eventChannel, output);
                } else {
                    qWarning() << "ZLRouter: Something's badly wrong and we've ended up with a message supposedly on channel" << eventChannel;
                }
            }
        }
        jack_midi_clear_buffer(inputBuffer);
        return 0;
    }
    int xrun() {
        return 0;
    }

    QTimer *hardwareInputConnector{nullptr};
    void connectHardwareInputs() {
        const char **ports = jack_get_ports(jackClient, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsPhysical | JackPortIsOutput);
        for (const char **p = ports; *p; p++) {
            const QString inputPortName{QString::fromLocal8Bit(*p)};
            if (disabledMidiInPorts.contains(inputPortName)) {
                disconnectPorts(inputPortName, QLatin1String{"ZLRouter:ExternalInput"});
            } else {
                connectPorts(inputPortName, QLatin1String{"ZLRouter:ExternalInput"});
            }
        }
        free(ports);
    }

    void disconnectFromOutputs(ChannelOutput *output) {
        const QString portName = QString("ZLRouter:%1").arg(output->portName);
        if (output->destination == MidiRouter::ZynthianDestination) {
            disconnectPorts(portName, QLatin1String{"ZynMidiRouter:step_in"});
        } else if (output->destination == MidiRouter::ExternalDestination) {
            for (const QString &externalPort : enabledMidiOutPorts) {
                disconnectPorts(portName, externalPort);
            }
        }
    }

    void connectToOutputs(ChannelOutput *output) {
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
    , d(new MidiRouterPrivate)
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
                    for (int channel = 0; channel < 16; ++channel) {
                        ChannelOutput *output = new ChannelOutput(channel);
                        output->portName = QString("Channel%2").arg(QString::number(channel));
                        output->port = jack_port_register(d->jackClient, output->portName.toUtf8(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
                        d->outputs << output;
                        d->connectPorts(QString("ZLRouter:%1").arg(output->portName), zmrPort);
                    }
                } else {
                    qWarning() << "ZLRouter: Failed to activate ZLRouter Jack client";
                }
            }
            // Create the passthrough port
            d->passthroughPort = jack_port_register(d->jackClient, "Passthrough", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
            if (!d->passthroughPort) {
                qWarning() << "ZLRouter: Could not register ZLRouter Jack passthrough port";
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
            d->externalInput = jack_port_register(
                d->jackClient,
                "ExternalInput",
                JACK_DEFAULT_MIDI_TYPE,
                JackPortIsInput,
                0
            );
            if (d->externalInput) {
                d->hardwareInputConnector->start();
            } else {
                qWarning() << "ZLRouter: Could not register ZLRouter Jack input port for external equipment";
            }
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
        ChannelOutput *output = d->outputs[channel];
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

void MidiRouter::reloadConfiguration()
{
    // TODO Make the fb stuff work as well... (also, note to self, work out what that stuff actually is?)
    if (!d->constructing) {
        // First, disconnect our outputs, just in case...
        for (ChannelOutput *output : d->outputs) {
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
        for (ChannelOutput *output : d->outputs) {
            d->connectToOutputs(output);
        }
        d->connectHardwareInputs();
    }
}

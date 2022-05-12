#include "MidiRouter.h"

#include <QDebug>
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

    int currentChannel{0};
    jack_client_t* jackClient{nullptr};
    jack_port_t *midiInPort{nullptr};
    jack_port_t *passthroughPort{nullptr};
    jack_port_t *externalInput{nullptr};
    QList<ChannelOutput*> outputs;

    void connectPorts(const QString &from, const QString &to) {
        int result = jack_connect(jackClient, from.toUtf8(), to.toUtf8());
        if (result == 0 || result == EEXIST) {
            if (DebugZLRouter) { qDebug() << "ZLRouter:" << (result == EEXIST ? "Retaining existing connection from" : "Successfully created new connection from" ) << from << "to" << to; }
        } else {
            qWarning() << "ZLRouter: Failed to connect" << from << "with" << to << "with error code" << result;
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
        // Get and clear our passthrough buffer
        void *passthroughBuffer = jack_port_get_buffer(passthroughPort, nframes);
        jack_midi_clear_buffer(passthroughBuffer);
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
                jack_midi_event_write(passthroughBuffer, 0, event.buffer, event.size);
                if (output->destination == MidiRouter::ExternalDestination && output->externalChannel > -1) {
                    if (DebugZLRouter) { qDebug() << "ZLRouter: We're being redirected to a different channel, let's obey that - going from" << eventChannel << "to" << output->externalChannel; }
                    event.buffer[0] = event.buffer[0] - eventChannel + output->externalChannel;
                }
                if (jack_midi_event_write(output->channelBuffer, 0, event.buffer, event.size) == ENOBUFS) {
                    qWarning() << "ZLRouter: Ran out of space while writing events!";
                } else {
                    if (DebugZLRouter) { qDebug() << "ZLRouter: Wrote event of size" << event.size << "to channel" << eventChannel << "which is on port" << output->portName; }
                }
            } else {
                qWarning() << "ZLRouter: Something's badly wrong and we've ended up with a message supposedly on channel" << eventChannel;
            }
        }
        jack_midi_clear_buffer(inputBuffer);
        // Now handle all the hardware input - but, importantly, only in case the current channel actually
        // targets external, otherwise just skip those (as zynthian handles that itself)
        output = outputs[currentChannel];
        bool outputToExternal{currentChannel < outputs.count()};
        inputBuffer = jack_port_get_buffer(externalInput, nframes);
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
                if (outputToExternal && !output->channelBuffer) {
                    output->channelBuffer = jack_port_get_buffer(output->port, nframes);
                }
                // First pass through the note to the channel we're expecting notes on internally...
                event.buffer[0] = event.buffer[0] - eventChannel + currentChannel;
                jack_midi_event_write(passthroughBuffer, 0, event.buffer, event.size);
                if (outputToExternal && output->destination == MidiRouter::ExternalDestination) {
                    if (output->externalChannel > -1) {
                        // Reset it to the origin before reworking to the new channel
                        event.buffer[0] = event.buffer[0] + eventChannel - currentChannel;
                        if (DebugZLRouter) { qDebug() << "ZLRouter: Hardware Event: We're being redirected to a different channel, let's obey that - going from" << eventChannel << "to" << output->externalChannel; }
                        event.buffer[0] = event.buffer[0] - eventChannel + output->externalChannel;
                    }
                    if (jack_midi_event_write(output->channelBuffer, 0, event.buffer, event.size) == ENOBUFS) {
                        qWarning() << "ZLRouter: Hardware Event: Ran out of space while writing events!";
                    } else {
                        if (DebugZLRouter) { qDebug() << "ZLRouter: Hardware Event: Wrote event of size" << event.size << "to channel" << eventChannel << "which is on port" << output->portName; }
                    }
                }
            } else {
                qWarning() << "ZLRouter: Something's badly wrong and we've ended up with a message supposedly on channel" << eventChannel;
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
            // TODO Maybe we want to filter this by what is set in webconf, probably?
            connectPorts(QString::fromLocal8Bit(*p), QLatin1String{"ZLRouter:ExternalInput"});
        }
        free(ports);
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
            const QString portName = QString("ZLRouter:%1").arg(output->portName);
            d->disconnectPorts(portName, QLatin1String{output->destination == ZynthianDestination ? "ZynMidiRouter:step_in" : "ttymidi:MIDI_out"});
            output->destination = destination;
            d->connectPorts(portName, QLatin1String{output->destination == ZynthianDestination ? "ZynMidiRouter:step_in" : "ttymidi:MIDI_out"});
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

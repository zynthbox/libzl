#include "MidiRouter.h"

#include <QDebug>

#include <jack/jack.h>
#include <jack/midiport.h>

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
        if (jack_connect(jackClient, from.toUtf8(), to.toUtf8()) == 0) {
            qDebug() << "ZLRouter: Successfully connected" << from << "with" << to;
        } else {
            qWarning() << "ZLRouter: Failed to connect" << from << "with" << to;
        }
    }
    void disconnectPorts(const QString &from, const QString &to) {
        // Don't attempt to connect already connected ports
        if (jack_disconnect(jackClient, from.toUtf8(), to.toUtf8()) == 0) {
            qDebug() << "ZLRouter: Successfully disconnected" << from << "from" << to;
        } else {
            qWarning() << "ZLRouter: Failed to disconnect" << from << "from" << to;
        }
    }

    int process(jack_nframes_t nframes) {
        for (ChannelOutput *output : outputs) {
            if (output->channelBuffer) {
                jack_midi_clear_buffer(output->channelBuffer);
                output->channelBuffer = nullptr;
            }
        }
        void *inputBuffer = jack_port_get_buffer(midiInPort, nframes);
        uint32_t events = jack_midi_get_event_count(inputBuffer);
        void *passthroughBuffer = jack_port_get_buffer(passthroughPort, nframes);
        jack_midi_clear_buffer(passthroughBuffer);
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
                    qDebug() << "ZLRouter: We're being redirected to a different channel, let's obey that - going from" << eventChannel << "to" << output->externalChannel;
                    event.buffer[0] = event.buffer[0] - eventChannel + output->externalChannel;
                }
                if (jack_midi_event_write(output->channelBuffer, 0, event.buffer, event.size) == ENOBUFS) {
                    qWarning() << "ZLRouter: Ran out of space while writing events!";
                } else {
                    qDebug() << "ZLRouter: Wrote event of size" << event.size << "to channel" << eventChannel << "which is on port" << output->portName;
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
                    // Then reset it to the origin
                    event.buffer[0] = event.buffer[0] + eventChannel - currentChannel;
                    if (output->externalChannel > -1) {
                        qDebug() << "ZLRouter: Hardware Event: We're being redirected to a different channel, let's obey that - going from" << eventChannel << "to" << output->externalChannel;
                        event.buffer[0] = event.buffer[0] - eventChannel + output->externalChannel;
                    }
                    if (jack_midi_event_write(output->channelBuffer, 0, event.buffer, event.size) == ENOBUFS) {
                        qWarning() << "ZLRouter: Hardware Event: Ran out of space while writing events!";
                    } else {
                        qDebug() << "ZLRouter: Hardware Event: Wrote event of size" << event.size << "to channel" << eventChannel << "which is on port" << output->portName;
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

    void connectHardwareInputs() {
        const char **ports = jack_get_ports(jackClient, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsPhysical | JackPortIsOutput);
        for (const char **p = ports; *p; p++) {
            qDebug() << "ZLRouter: Found a midi event out-spitter" << *p;
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
                d->connectHardwareInputs();
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

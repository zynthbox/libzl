#include "MidiRouter.h"
#include "JackPassthrough.h"
#include "SyncTimer.h"
#include "libzl.h"
#include "DeviceMessageTranslations.h"
#include "TransportManager.h"

#include <QDebug>
#include <QProcessEnvironment>
#include <QTimer>

#include <jack/jack.h>
#include <jack/midiport.h>

#include <chrono>

// Set this to true to emit a bunch more debug output when the router is operating
#define DebugZLRouter false
// Set this to true to send out more debug information for the listener part of the class
#define DebugMidiListener false
// Set this to true to enable the watchdog
#define ZLROUTER_WATCHDOG false

#define MAX_LISTENER_MESSAGES 1024

class OutputDevice {
public:
    OutputDevice(const QString &jackPortName)
        : jackPortName(jackPortName)
    {}
    ~OutputDevice() {}
    // The string name which identifies this input device in jack
    QString jackPortName;
    // A human-readable name for the port (derived from the port's first alias, if one exists, otherwise it's the port name)
    QString humanReadableName;
    // The device's ID, as understood by Zynthian.
    QString zynthianId;
    // Whether or not we are supposed to send events to this device
    bool enabled{false};
};

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
    // Whether or not we should read events from this device
    alignas(8) bool enabled{false};
    // The jack port we connect to for reading events
    jack_port_t* port;
    // The number of times we have received a note activation on this channel
    // After the first, assume that all other activations will go to the same destination
    int noteActivations[128];
    // The channel which the most recent note activation went to
    // This will be written the first time the associated noteActivations position gets
    // incremented above 0 (all subsequent events read from this hash to determine the
    // destination instead)
    int activeNoteChannel[128];
    // Translations for device messages. If the size of the event at a position is > 0, use that message instead of the received one
    jack_midi_event_t *device_translations_cc{nullptr};
    // The string name which identifies this input device in jack
    QString jackPortName;
    // A human-readable name for the port (derived from the port's first alias, if one exists, otherwise it's the port name)
    QString humanReadableName;
    // The device's ID, as understood by Zynthian.
    QString zynthianId;
};

// This is our translation from midi input channels to destinations. It contains
// information on what external output channel should be used if it's not a straight
// passthrough to the same channel the other side, and what channels should be
// targeted on the zynthian outputs.
struct ChannelOutput {
    ChannelOutput(int inputChannel)
        : inputChannel(inputChannel)
    {
        // By default we assume a straight passthrough for zynthian. This isn't
        // always true, but it's a sensible enough default, and if users need it
        // changed, that will need to happen later (like the external channel)
        for (int i = 0; i < 16; ++i) {
            zynthianChannels[i] = -1;
        }
        zynthianChannels[0] = inputChannel;
    }
    int zynthianChannels[16];
    QString portName;
    jack_port_t *port{nullptr};
    jack_nframes_t mostRecentTime{0};
    int inputChannel{-1};
    int externalChannel{-1};
    MidiRouter::RoutingDestination destination{MidiRouter::ZynthianDestination};
};

struct alignas(64) NoteMessage {
    jack_midi_event_t jackEvent;
    double timeStamp{0};
    NoteMessage *next{nullptr};
    NoteMessage *previous{nullptr};
    bool submitted{true};
};

struct MidiListenerPort {
    MidiListenerPort() {
        NoteMessage *previous{&messages[MAX_LISTENER_MESSAGES - 1]};
        for (int i = 0; i < MAX_LISTENER_MESSAGES; ++i) {
            messages[i].previous = previous;
            previous->next = &messages[i];
            previous = &messages[i];
        }
        readHead = &messages[0];
        writeHead = &messages[0];
    }
    ~MidiListenerPort() { }
    inline NoteMessage &getNextAvailableWriteMessage() {
        NoteMessage &availableMessage = *writeHead;
        writeHead = writeHead->next;
        return availableMessage;
    }
    NoteMessage messages[MAX_LISTENER_MESSAGES];
    NoteMessage* writeHead{nullptr};
    NoteMessage* readHead{nullptr};
    MidiRouter::ListenerPort identifier{MidiRouter::UnknownPort};
    int waitTime{5};
};

// This class will watch what events ZynMidiRouter says it has handled, and just count them.
// The logic is then that we can compare that with what we think we wrote out during the most
// recent run in MidiRouter, and if they don't match, we can reissue the previous run's events
class MidiRouterWatchdog {
public:
    MidiRouterWatchdog();
    ~MidiRouterWatchdog() {
        if (client) {
            jack_client_close(client);
        }
    }
    jack_client_t *client{nullptr};
    jack_port_t *port{nullptr};

    uint32_t mostRecentEventCount{0};
    int process(jack_nframes_t nframes) {
        void *buffer = jack_port_get_buffer(port, nframes);
        mostRecentEventCount = jack_midi_get_event_count(buffer);
        return 0;
    }
};

int watchdog_process(jack_nframes_t nframes, void *arg) {
    return static_cast<MidiRouterWatchdog*>(arg)->process(nframes);
}

MidiRouterWatchdog::MidiRouterWatchdog()
{
#if ZLROUTER_WATCHDOG
    jack_status_t real_jack_status{};
    client = jack_client_open("ZLRouterWatchdog", JackNullOption, &real_jack_status);
    if (client) {
        port = jack_port_register(client, "ZynMidiRouterIn", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput | JackPortIsTerminal, 0);
        if (port) {
            // Set the process callback.
            if (jack_set_process_callback(client, watchdog_process, static_cast<void*>(this)) == 0) {
                if (jack_activate(client) == 0) {
                    int result = jack_connect(client, "ZynMidiRouter:midi_out", "ZLRouterWatchdog:ZynMidiRouterIn");
                    if (result == 0 || result == EEXIST) {
                        qDebug() << "ZLRouter Watchdog: Set up the watchdog for ZynMidiRouter, which lets us keep a track of what events are going through";
                    } else {
                        qWarning() << "ZLRouter Watchdog: Failed to connect to ZynMidiRouter's midi output port";
                    }
                } else {
                    qWarning() << "ZLRouter Watchdog: Failed to activate the Jack client";
                }
            } else {
                qWarning() << "ZLRouter Watchdog: Failed to set Jack processing callback";
            }
        } else {
            qWarning() << "ZLRouter Watchdog: Failed to register watchdog port";
        }
    } else {
        qWarning() << "ZLRouter Watchdog: Failed to create Jack client";
    }
#endif
}

#define OUTPUT_CHANNEL_COUNT 16
#define MAX_INPUT_DEVICES 32
jack_time_t expected_next_usecs{0};
class MidiRouterPrivate {
public:
    MidiRouterPrivate(MidiRouter *q)
        : q(q)
    {
        DeviceMessageTranslations::load();
        for (int i = 0; i < MAX_INPUT_DEVICES; ++i) {
            enabledInputs[i] = nullptr;
        }
        for (int i = 0; i < OUTPUT_CHANNEL_COUNT; ++i) {
            outputs[i] = nullptr;
        }
        passthroughListener.identifier = MidiRouter::PassthroughPort;
        passthroughListener.waitTime = 1;
        listenerPorts[0] = &passthroughListener;
        internalPassthroughListener.identifier = MidiRouter::InternalPassthroughPort;
        internalPassthroughListener.waitTime = 5;
        listenerPorts[1] = &internalPassthroughListener;
        hardwareInListener.identifier = MidiRouter::HardwareInPassthroughPort;
        hardwareInListener.waitTime = 5;
        listenerPorts[2] = &hardwareInListener;
        externalOutListener.identifier = MidiRouter::ExternalOutPort;
        externalOutListener.waitTime = 5;
        listenerPorts[3] = &externalOutListener;
        syncTimer = qobject_cast<SyncTimer*>(SyncTimer_instance());
    };
    ~MidiRouterPrivate() {
        if (jackClient) {
            jack_client_close(jackClient);
        }
        for (ChannelOutput *output : outputs) {
            delete output;
        }
        qDeleteAll(hardwareInputs);
        delete watchdog;
        for (int i = 0; i < 128; ++i) {
            if (device_translations_cc_presonus_atom_sq[i].size > 0) {
                delete[] device_translations_cc_presonus_atom_sq[i].buffer;
            }
        }
    };

    MidiRouter *q;
    MidiRouterWatchdog *watchdog{new MidiRouterWatchdog};
    SyncTimer *syncTimer{nullptr};
    JackPassthrough *globalEffectsPassthrough{nullptr};
    JackPassthrough *globalPlayback{nullptr};
    QList<JackPassthrough*> channelEffectsPassthroughClients;
    bool done{false};
    bool constructing{true};
    bool filterMidiOut{false};
    QStringList disabledMidiInPorts;
    QStringList enabledMidiOutPorts;
    QStringList enabledMidiFbPorts;

    int currentChannel{0};
    jack_client_t* jackClient{nullptr};
    jack_port_t *syncTimerMidiInPort{nullptr};

    QList<InputDevice*> hardwareInputs;
    InputDevice* enabledInputs[MAX_INPUT_DEVICES];
    int enabledInputsCount{0};
    ChannelOutput * outputs[OUTPUT_CHANNEL_COUNT];
    ChannelOutput *zynthianOutputPort{nullptr};
    ChannelOutput *externalOutputPort{nullptr};
    ChannelOutput *passthroughOutputPort{nullptr};

    MidiListenerPort passthroughListener;
    MidiListenerPort internalPassthroughListener;
    MidiListenerPort hardwareInListener;
    MidiListenerPort externalOutListener;
    MidiListenerPort* listenerPorts[4];
    // FIXME The consumers of this currently functionally assume a note message, and... we will need to handle other things as well, but for now we only call this for note messages
    static inline void addMessage(MidiListenerPort &port, const double &timeStamp, const jack_midi_event_t &event)
    {
        NoteMessage &message = port.getNextAvailableWriteMessage();
        message.timeStamp = timeStamp;
        message.jackEvent = event;
        message.submitted = false;
    }

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
    static inline void writeEventToBuffer(const jack_midi_event_t &event, void *buffer, int currentChannel, jack_nframes_t *mostRecentTime, ChannelOutput *output, int outputChannel = -1) {
        Q_UNUSED(currentChannel)
        Q_UNUSED(output)
        const int eventChannel = (event.buffer[0] & 0xf);
        if (outputChannel > -1) {
            event.buffer[0] = event.buffer[0] - eventChannel + outputChannel;
        }
        int errorCode = jack_midi_event_write(buffer, event.time, event.buffer, event.size);
        if (errorCode == -EINVAL) {
            // If the error invalid happens, we should likely assume the event was out of order for whatever reason, and just schedule it at the same time as the most recently scheduled event
            if (DebugZLRouter) { qWarning() << "ZLRouter: Attempted to write out-of-order event for time" << event.time << "so writing to most recent instead:" << *mostRecentTime; }
            errorCode = jack_midi_event_write(buffer, *mostRecentTime, event.buffer, event.size);
        }
        if (errorCode != 0) {
            if (errorCode == -ENOBUFS) {
                qWarning() << "ZLRouter: Ran out of space while writing events!";
            } else {
                qWarning() << "ZLRouter: Error writing midi event:" << -errorCode << strerror(-errorCode) << "for event at time" << event.time << "of size" << event.size;
            }
#if DebugZLRouter
        } else {
            if (DebugZLRouter) { qDebug() << "ZLRouter: Wrote event to buffer at time" << QString::number(event.time).rightJustified(4, ' ') << "on channel" << currentChannel << "for port" << output->portName << "with data" << event.buffer[0] << event.buffer[1]; }
#endif
        }
        if (*mostRecentTime < event.time) {
            *mostRecentTime = event.time;
        }
        if (outputChannel > -1) {
            event.buffer[0] = event.buffer[0] + eventChannel - outputChannel;
        }
    }

    uint32_t mostRecentEventsForZynthian{0};
    QAtomicInt jack_xrun_count{0};
    int process(jack_nframes_t nframes) {
        // auto t1 = std::chrono::high_resolution_clock::now();
        jack_nframes_t current_frames;
        jack_time_t current_usecs;
        jack_time_t next_usecs;
        float period_usecs;
        jack_get_cycle_times(jackClient, &current_frames, &current_usecs, &next_usecs, &period_usecs);
        const quint64 microsecondsPerFrame = (next_usecs - current_usecs) / nframes;

        // TODO Maybe what we should do is reissue all of the previous run's events at immediate time
        // instead, so they all happen /now/ instead of offset by, effectively, nframes samples
        uint32_t unclearedMessages{0};

        void *zynthianOutputBuffer = jack_port_get_buffer(zynthianOutputPort->port, nframes);
        jack_nframes_t zynthianMostRecentTime{0};
        void *externalOutputBuffer = jack_port_get_buffer(externalOutputPort->port, nframes);
        jack_nframes_t externalMostRecentTime{0};
        void *passthroughOutputBuffer = jack_port_get_buffer(passthroughOutputPort->port, nframes);
        jack_nframes_t passthroughOutputMostRecentTime{0};
#if ZLROUTER_WATCHDOG
        bool shouldClearBuffer{true};
        if (watchdog->mostRecentEventCount < mostRecentEventsForZynthian) {
            if (DebugZLRouter) { qWarning() << "ZLRouter: Apparently the last run lost events in Zynthian (received" << watchdog->mostRecentEventCount << "events, we sent out" << mostRecentEventsForZynthian << "events) - let's assume that it broke super badly and not clear our output buffers so things can catch back up"; }
            shouldClearBuffer = false;
            unclearedMessages = watchdog->mostRecentEventCount;
        }
        if (shouldClearBuffer) {
            jack_midi_clear_buffer(zynthianOutputBuffer);
        }
        if (shouldClearBuffer) {
            jack_midi_clear_buffer(externalOutputBuffer);
        }
        if (shouldClearBuffer) {
            jack_midi_clear_buffer(passthroughOutputBuffer);
        }
#else
        jack_midi_clear_buffer(externalOutputBuffer);
        jack_midi_clear_buffer(zynthianOutputBuffer);
        jack_midi_clear_buffer(passthroughOutputBuffer);
#endif
        ChannelOutput *output{nullptr};
        jack_midi_event_t event;
        int eventChannel{-1};
        uint32_t eventIndex = 0;
        void *inputBuffer{nullptr};

        // Handle input coming from our SyncTimer
        // Explicitly process synctimer now, and copy it to a local buffer just for some belts and braces (in case jack decides to swap out the port buffer on us)
        inputBuffer = jack_port_get_buffer(syncTimerMidiInPort, nframes);
        quint64 subbeatLengthInMicroseconds{0};
        quint64 currentJackPlayhead{0};
        syncTimer->process(nframes, inputBuffer, &currentJackPlayhead, &subbeatLengthInMicroseconds);

        // A quick bit of sanity checking - usually everything's fine, but occasionally we might get events while
        // starting up, and we kind of need to settle down before then, and a good indicator something went wrong
        // is that the subbeatLengthInMicroseconds variable is zero, and so we can use that to make sure things are
        // reasonably sane before trying to do anything.
        if (subbeatLengthInMicroseconds > 0) {
            eventIndex = 0;
            const uint32_t eventCount = jack_midi_get_event_count(inputBuffer);
            uint32_t nonEmittedEvents{unclearedMessages};
            while (eventIndex < eventCount) {
                if (int err = jack_midi_event_get(&event, inputBuffer, eventIndex)) {
                    qWarning() << "ZLRouter: jack_midi_event_get, received note lost! We were supposed to have" << eventCount << "events, attempted to fetch at index" << eventIndex << "and the error code is" << err;
                } else {
                    if (event.buffer[0] < 0xf0) {
                        eventChannel = (event.buffer[0] & 0xf);
                        if (eventChannel > -1 && eventChannel < OUTPUT_CHANNEL_COUNT) {
                            const unsigned char &byte1 = event.buffer[0];
                            const bool isNoteMessage = byte1 > 0x7F && byte1 < 0xA0;
                            output = outputs[eventChannel];
                            const double timestamp = currentJackPlayhead + (event.time * microsecondsPerFrame / subbeatLengthInMicroseconds);
                            switch (output->destination) {
                                case MidiRouter::ZynthianDestination:
                                    if (isNoteMessage) {
                                        addMessage(passthroughListener, timestamp, event);
                                        addMessage(internalPassthroughListener, timestamp, event);
                                    }
                                    for (const int &zynthianChannel : output->zynthianChannels) {
                                        if (zynthianChannel == -1) {
                                            break;
                                        }
                                        writeEventToBuffer(event, zynthianOutputBuffer, eventChannel, &zynthianMostRecentTime, zynthianOutputPort, zynthianChannel);
                                    }
                                    writeEventToBuffer(event, passthroughOutputBuffer, eventChannel, &passthroughOutputMostRecentTime, passthroughOutputPort);
                                    break;
                                case MidiRouter::SamplerDestination:
                                    if (isNoteMessage) {
                                        addMessage(passthroughListener, timestamp, event);
                                        addMessage(internalPassthroughListener, timestamp, event);
                                    }
                                    ++nonEmittedEvents; // if we return the above line, remove this
                                    writeEventToBuffer(event, passthroughOutputBuffer, eventChannel, &passthroughOutputMostRecentTime, passthroughOutputPort);
                                    break;
                                case MidiRouter::ExternalDestination:
                                {
                                    int externalChannel = (output->externalChannel == -1) ? output->inputChannel : output->externalChannel;
                                    if (isNoteMessage) {
                                        addMessage(passthroughListener, timestamp, event);
                                        // Not writing to internal passthrough, as this is heading to an external device
                                        addMessage(externalOutListener, timestamp, event);
                                    }
                                    writeEventToBuffer(event, externalOutputBuffer, eventChannel, &externalMostRecentTime, externalOutputPort, externalChannel);
                                    writeEventToBuffer(event, passthroughOutputBuffer, eventChannel, &passthroughOutputMostRecentTime, passthroughOutputPort);
                                }
                                case MidiRouter::NoDestination:
                                default:
                                    if (isNoteMessage) {
                                        addMessage(internalPassthroughListener, timestamp, event);
                                    }
                                    ++nonEmittedEvents;
                                    break;
                            }
                        } else {
                            qWarning() << "ZLRouter: Something's badly wrong and we've ended up with a message supposedly on channel" << eventChannel;
                        }
                    } else if (event.buffer[0] == 0xf0) {
                        // We don't know what to do with sysex messages, so (for now) we're just ignoring them entirely
                        // Likely want to pass them through directly to any connected midi output devices, though
                    } else {
                        writeEventToBuffer(event, externalOutputBuffer, eventChannel, &externalMostRecentTime, externalOutputPort);
                        // Don't pass time code type things through from the SyncTimer input, otherwise we're feeding timecodes back to TransportManager that it likely sent out itself, which would be impractical)
                        if (event.buffer[0] != 0xf2 && event.buffer[0] != 0xf8 && event.buffer[0] != 0xfa && event.buffer[0] != 0xfb && event.buffer[0] != 0xfc && event.buffer[0] != 0xf9) {
                            writeEventToBuffer(event, passthroughOutputBuffer, eventChannel, &passthroughOutputMostRecentTime, passthroughOutputPort);
                        }
                    }
                }
                ++eventIndex;
            }
    #if DebugZLRouter
            if (eventCount > 0) {
                uint32_t totalEvents{nonEmittedEvents};
                const uint32_t zynthianEventCount{jack_midi_get_event_count(zynthianOutputBuffer)};
                totalEvents += zynthianEventCount;
                totalEvents += jack_midi_get_event_count(externalOutputBuffer);
                qDebug() << "ZLRouter: Synctimer event count:" << eventCount << " - Events written:" << totalEvents << "Events targeting zynthian:" << zynthianEventCount;
                if (eventCount != totalEvents) {
                    qWarning() << "ZLRouter: We wrote an incorrect number of events somehow!" << eventCount << "is not the same as" << totalEvents;
                }
            }
    #endif

            // Handle all the hardware input - magic for the ones we want to direct to external ports, and straight passthrough for ones aimed at zynthian
            if (currentChannel > -1 && currentChannel < OUTPUT_CHANNEL_COUNT) {
                int adjustedCurrentChannel{currentChannel};
                int currentEnabledInputIndex = 0;
                for (InputDevice *device : enabledInputs) {
                    if (currentEnabledInputIndex == enabledInputsCount) {
                        break;
                    }
                    inputBuffer = jack_port_get_buffer(device->port, nframes);
                    output = outputs[currentChannel];
                    uint32_t eventIndex{0};
                    ChannelOutput *currentOutput{nullptr};
                    while (true) {
                        currentOutput = output;
                        if (int err = jack_midi_event_get(&event, inputBuffer, eventIndex)) {
                            if (err != -ENOBUFS) {
                                qWarning() << "ZLRouter: jack_midi_event_get failed, received note lost! Attempted to fetch at index" << eventIndex << "and the error code is" << err;
                            }
                            break;
                        } else {
                            if (event.buffer[0] < 0xf0) {
                                // Check whether we've got any message translation to do
                                if (event.buffer[0] > 0xAF && event.buffer[0] < 0xC0) {
                                    // Then it's a CC thing, and maybe we want to do a thing?
                                    const jack_midi_event_t &otherEvent = device->device_translations_cc[event.buffer[1]];
                                    if (otherEvent.size > 0) {
                                        event.size = otherEvent.size;
                                        event.buffer = otherEvent.buffer;
                                        // leave the time code intact
                                    }
                                }
                                eventChannel = (event.buffer[0] & 0xf);
                                if (eventChannel > -1 && eventChannel < OUTPUT_CHANNEL_COUNT) {
                                    // Make sure we're using the correct output
                                    // This is done to ensure that if we have any note-on events happen on some
                                    // output, then all the following commands associated with that note should
                                    // go to the same output (so any further ons, and any matching offs)
                                    const unsigned char &byte1 = event.buffer[0];
                                    bool isNoteMessage{false};
                                    if (0x7F < byte1 && byte1 < 0xA0) {
                                        const int &midiNote = event.buffer[1];
                                        isNoteMessage = true;
                                        int &noteActivation = device->noteActivations[midiNote];
                                        if ((byte1 >= 0x90)) { // this is a note on message
                                            ++noteActivation;
                                            if (noteActivation == 1) {
                                                device->activeNoteChannel[midiNote] = currentChannel;
                                            }
                                        } else {
                                            noteActivation = 0;
                                        }
                                        adjustedCurrentChannel = device->activeNoteChannel[midiNote];
                                        currentOutput = outputs[adjustedCurrentChannel];
                                        if (currentChannel == adjustedCurrentChannel) {
                                            event.buffer[0] = event.buffer[0] - eventChannel + currentChannel;
                                        } else {
                                            event.buffer[0] = event.buffer[0] - eventChannel + adjustedCurrentChannel;
                                        }
                                    }

                                    const double timestamp = currentJackPlayhead + (event.time * microsecondsPerFrame / subbeatLengthInMicroseconds);
                                    switch (currentOutput->destination) {
                                        case MidiRouter::ZynthianDestination:
                                            if (isNoteMessage) {
                                                addMessage(passthroughListener, timestamp, event);
                                            }
                                            for (const int &zynthianChannel : currentOutput->zynthianChannels) {
                                                if (zynthianChannel == -1) {
                                                    break;
                                                }
                                                writeEventToBuffer(event, zynthianOutputBuffer, adjustedCurrentChannel, &zynthianMostRecentTime, zynthianOutputPort, zynthianChannel);
                                            }
                                            writeEventToBuffer(event, passthroughOutputBuffer, adjustedCurrentChannel, &passthroughOutputMostRecentTime, passthroughOutputPort);
                                            break;
                                        case MidiRouter::SamplerDestination:
                                            if (isNoteMessage) {
                                                addMessage(passthroughListener, timestamp, event);
                                            }
                                            writeEventToBuffer(event, passthroughOutputBuffer, adjustedCurrentChannel, &passthroughOutputMostRecentTime, passthroughOutputPort);
                                            break;
                                        case MidiRouter::ExternalDestination:
                                        {
                                            int externalChannel = (currentOutput->externalChannel == -1) ? currentOutput->inputChannel : currentOutput->externalChannel;
                                            if (isNoteMessage) {
                                                addMessage(passthroughListener, timestamp, event);
                                                addMessage(externalOutListener, timestamp, event);
                                            }
                                            writeEventToBuffer(event, externalOutputBuffer, adjustedCurrentChannel, &externalMostRecentTime, externalOutputPort, externalChannel);
                                            writeEventToBuffer(event, passthroughOutputBuffer, adjustedCurrentChannel, &passthroughOutputMostRecentTime, passthroughOutputPort);
                                        }
                                        case MidiRouter::NoDestination:
                                        default:
                                            // Do nothing here
                                            break;
                                    }
                                    if (isNoteMessage) {
                                        addMessage(hardwareInListener, timestamp, event);
                                    }
                                } else if (event.size == 1 || event.size == 2) {
                                    writeEventToBuffer(event, passthroughOutputBuffer, adjustedCurrentChannel, &passthroughOutputMostRecentTime, passthroughOutputPort);
                                } else {
                                    qWarning() << "ZLRouter: Something's badly wrong and we've ended up with a message supposedly on channel" << eventChannel;
                                }
                            } else if (event.buffer[0] == 0xf0) {
                                // We don't know what to do with sysex messages, so (for now) we're just ignoring them entirely
                                // Likely want to pass them through directly to any connected midi output devices, though
                            } else {
                                writeEventToBuffer(event, externalOutputBuffer, eventChannel, &externalMostRecentTime, externalOutputPort);
                                writeEventToBuffer(event, passthroughOutputBuffer, currentChannel, &passthroughOutputMostRecentTime, passthroughOutputPort);
                            }
                        }
                        ++eventIndex;
                    }
                    ++currentEnabledInputIndex;
                }
            }
#if ZLROUTER_WATCHDOG
            mostRecentEventsForZynthian = jack_midi_get_event_count(zynthianOutputBuffer);
#endif
        }

        // std::chrono::duration<double, std::milli> ms_double = std::chrono::high_resolution_clock::now() - t1;
        // if (ms_double.count() > 0.2) {
        //     qDebug() << Q_FUNC_INFO << ms_double.count() << "ms after" << belowThreshold << "runs under 0.2ms";
        //     belowThreshold = 0;
        // } else {
        //     ++belowThreshold;
        // }

        return 0;
    }
    int belowThreshold{0};
    int xrun() {
        ++jack_xrun_count;
        return 0;
    }

    QTimer *hardwareInputConnector{nullptr};
    void connectHardwareInputs() {
        const char **ports = jack_get_ports(jackClient, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsPhysical | JackPortIsOutput);
        QList<InputDevice*> connectedDevices;
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
                                    device->zynthianId = splitAlias.join("_");
                                }
                            }
                        }
                        if (device->humanReadableName.isEmpty()) {
                            device->humanReadableName = device->jackPortName.split(':').last();
                            device->zynthianId = device->jackPortName;
                        }
                        qDebug() << "ZLRouter: Discovered a new bit of hardware, named" << inputPortName << "which we have given the friendly name" << device->humanReadableName;
                        DeviceMessageTranslations::apply(device->humanReadableName, &device->device_translations_cc);
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
                device->enabled = !disabledMidiInPorts.contains(device->zynthianId);
                qDebug() << "ZLRouter: Updated" << device->jackPortName << "enabled state to" << device->enabled;
            }
            connectedDevices << device;
        }
        // Clean up, in case something's been removed
        QMutableListIterator<InputDevice*> iterator(hardwareInputs);
        while (iterator.hasNext()) {
            InputDevice *device = iterator.next();
            if (!connectedDevices.contains(device)) {
                iterator.remove();
                qDebug() << "ZLRouter: Detected removal of a hardware device" << device->jackPortName;
                Q_EMIT q->removedHardwareInputDevice(device->jackPortName, device->humanReadableName);
                // TODO When disconnecting a bit of hardware, before deleting it, cycle through its noteActivations and ensure we spit out equivalent off events
                delete device;
            }
        }
        free(ports);
        enabledInputsCount = 0;
        QListIterator<InputDevice*> allInputs(hardwareInputs);
        for (int i = 0; i < MAX_INPUT_DEVICES; ++i) {
            if (allInputs.hasNext()) {
                InputDevice *device = allInputs.next();
                if (device->enabled) {
                    enabledInputs[i] = device;
                    ++enabledInputsCount;
                }
            } else {
                enabledInputs[i] = nullptr;
            }
        }
    }

    QList<OutputDevice *> hardwareOutputs;
    void refreshOutputsList() {
        const char **ports = jack_get_ports(jackClient, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsPhysical | JackPortIsInput);
        QList<OutputDevice*> connectedDevices;
        for (const char **p = ports; *p; p++) {
            const QString portName{QString::fromLocal8Bit(*p)};
            OutputDevice *device{nullptr};
            for (OutputDevice *needle : hardwareOutputs) {
                if (needle->jackPortName == portName) {
                    device = needle;
                    break;
                }
            }
            if (!device) {
                device = new OutputDevice(portName);
                hardwareOutputs << device;
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
                                device->zynthianId = splitAlias.join("_");
                            }
                        }
                    }
                    if (device->humanReadableName.isEmpty()) {
                        device->humanReadableName = device->jackPortName.split(':').last();
                        device->zynthianId = device->jackPortName;
                    }
                    qDebug() << "ZLRouter: Discovered a new bit of output hardware, named" << portName << "which we have given the friendly name" << device->humanReadableName;
                } else {
                    qWarning() << "ZLRouter: Failed to open hardware port for identification:" << portName;
                }
                Q_EMIT q->addedHardwareOutputDevice(portName, device->humanReadableName);
            }
            device->enabled = enabledMidiOutPorts.contains(device->zynthianId);
            qDebug() << "ZLRouter: Updated" << device->jackPortName << "aka" << device->zynthianId << "enabled state to" << device->enabled;
            connectedDevices << device;
        }
        // Clean up, in case something's been removed
        QMutableListIterator<OutputDevice*> iterator(hardwareOutputs);
        while (iterator.hasNext()) {
            OutputDevice *device = iterator.next();
            if (!connectedDevices.contains(device)) {
                iterator.remove();
                qDebug() << "ZLRouter: Detected removal of a hardware device" << device->jackPortName;
                Q_EMIT q->removedHardwareOutputDevice(device->jackPortName, device->humanReadableName);
                delete device;
            }
        }
        free(ports);
    }

    void disconnectFromOutputs(ChannelOutput *output) {
        const QString portName = QString("ZLRouter:%1").arg(output->portName);
        if (output->destination == MidiRouter::ZynthianDestination) {
//             disconnectPorts(portName, QLatin1String{"ZynMidiRouter:step_in"});
        } else if (output->destination == MidiRouter::ExternalDestination) {
//             for (const QString &externalPort : enabledMidiOutPorts) {
//                 disconnectPorts(portName, externalPort);
//             }
        }
    }

    void connectToOutputs(ChannelOutput *output) {
        const QString portName = QString("ZLRouter:%1").arg(output->portName);
        if (output->destination == MidiRouter::ZynthianDestination) {
//             connectPorts(portName, QLatin1String{"ZynMidiRouter:step_in"});
        } else if (output->destination == MidiRouter::ExternalDestination) {
//             for (const QString &externalPort : enabledMidiOutPorts) {
//                 connectPorts(portName, externalPort);
//             }
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
    : QThread(parent)
    , d(new MidiRouterPrivate(this))
{
    // First, load up our configuration (TODO: also remember to reload it when the config changes)
    reloadConfiguration();
    TransportManager::instance(d->syncTimer)->initialize();
    // Open the client.
    jack_status_t real_jack_status{};
    d->jackClient = jack_client_open("ZLRouter", JackNullOption, &real_jack_status);
    if (d->jackClient) {
        // Register the MIDI output port for SyncTimer - only SyncTimer should connect to this port
        d->syncTimerMidiInPort = jack_port_register(d->jackClient, "SyncTimerIn", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
        if (d->syncTimerMidiInPort) {
            // Set the process callback.
            if (jack_set_process_callback(d->jackClient, client_process, static_cast<void*>(d)) == 0) {
                jack_set_xrun_callback(d->jackClient, client_xrun, static_cast<void*>(d));
                d->hardwareInputConnector = new QTimer(this);
                d->hardwareInputConnector->setSingleShot(true);
                d->hardwareInputConnector->setInterval(300);
                connect(d->hardwareInputConnector, &QTimer::timeout, this, [this](){
                    d->connectHardwareInputs();
                    d->refreshOutputsList();
                    for (OutputDevice *device : d->hardwareOutputs) {
                        if (device->enabled) {
                            d->connectPorts(QString("ZLRouter:%1").arg(d->externalOutputPort->portName), device->jackPortName);
                        }
                    }
                });
                // To ensure we always have the expected channels, ZynMidiRouter wants us to use step_in
                // (or it'll rewrite to whatever it thinks the current channel is)
                const QString zmrPort{"ZynMidiRouter:step_in"};
                // We technically only have ten channels, but there's no reason we can't handle 16... so, let's do it like so
                for (int channel = 0; channel < OUTPUT_CHANNEL_COUNT; ++channel) {
                    ChannelOutput *output = new ChannelOutput(channel);
                    output->portName = QString("Channel%2").arg(QString::number(channel));
//                     output->port = jack_port_register(d->jackClient, output->portName.toUtf8(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
                    d->outputs[channel] = output;
                }
                // Set up the zynthian output port
                d->zynthianOutputPort = new ChannelOutput(0);
                d->zynthianOutputPort->portName = QString("ZynthianOut");
                d->zynthianOutputPort->port = jack_port_register(d->jackClient, d->zynthianOutputPort->portName.toUtf8(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
                // Set up the external output port
                d->externalOutputPort = new ChannelOutput(0);
                d->externalOutputPort->portName = QString("ExternalOut");
                d->externalOutputPort->port = jack_port_register(d->jackClient, d->externalOutputPort->portName.toUtf8(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
                // Set up the passthrough output port
                d->passthroughOutputPort = new ChannelOutput(0);
                d->passthroughOutputPort->portName = QString("PassthroughOut");
                d->passthroughOutputPort->port = jack_port_register(d->jackClient, d->passthroughOutputPort->portName.toUtf8(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

                jack_set_port_registration_callback(d->jackClient, client_port_registration, static_cast<void*>(d));
                jack_set_client_registration_callback(d->jackClient, client_registration, static_cast<void*>(d));
                // Activate the client.
                if (jack_activate(d->jackClient) == 0) {
                    qInfo() << "ZLRouter: Successfully created and set up the ZLRouter's Jack client";
//                     for (ChannelOutput *output : d->outputs) {
//                         d->connectPorts(QString("ZLRouter:%1").arg(output->portName), zmrPort);
//                     }
                    d->connectPorts(QString("ZLRouter:%1").arg(d->zynthianOutputPort->portName), zmrPort);
                    d->connectPorts(QLatin1String{"SyncTimer:midi_out"}, QLatin1String{"ZLRouter:SyncTimerIn"});

                    d->connectPorts(QLatin1String{"ZLRouter:PassthroughOut"}, QLatin1String{"TransportManager:midi_in"});
                    d->connectPorts(QLatin1String{"TransportManager:midi_out"}, QLatin1String{"ZLRouter:SyncTimerIn"});
                    // Now hook up the hardware inputs
                    d->hardwareInputConnector->start();
                } else {
                    qWarning() << "ZLRouter: Failed to activate ZLRouter Jack client";
                }
            } else {
                qWarning() << "ZLRouter: Failed to set the ZLRouter Jack processing callback";
            }
        } else {
            qWarning() << "ZLRouter: Could not register ZLRouter Jack input port for internal messages";
        }
    } else {
        qWarning() << "ZLRouter: Could not create the ZLRouter Jack client.";
    }

    d->globalEffectsPassthrough = new JackPassthrough("GlobalFXPassthrough", this);
    d->globalPlayback = new JackPassthrough("GlobalPlayback", this);
    d->globalPlayback->setWetAmount(0.0f);
    for (int i=0; i<10; ++i) {
        d->channelEffectsPassthroughClients << new JackPassthrough(QString("FXPassthrough-Channel%1").arg(i+1), this);
    }

    d->constructing = false;
    start();
}

MidiRouter::~MidiRouter()
{
    delete d;
}

void MidiRouter::run() {
    while (true) {
        if (d->done) {
            break;
        }
        for (int i = 0; i < 4; ++i) {
            MidiListenerPort *listenerPort = d->listenerPorts[i];
            NoteMessage *message = listenerPort->readHead;
            while (!message->submitted) {
                const unsigned char &byte1 = message->jackEvent.buffer[0];
                const bool setOn = (byte1 >= 0x90);
                const int midiChannel = (byte1 & 0xf);
                const int &midiNote = message->jackEvent.buffer[1];
                const int &velocity = message->jackEvent.buffer[2];
                const int byte2 = (message->jackEvent.size > 1 ? message->jackEvent.buffer[1] : 0);
                const int byte3 = (message->jackEvent.size > 1 ? message->jackEvent.buffer[2] : 0);
                Q_EMIT noteChanged(listenerPort->identifier, midiNote, midiChannel, velocity, setOn, message->timeStamp, message->jackEvent.buffer[0], byte2, byte3);
                message->submitted = true;
                listenerPort->readHead = listenerPort->readHead->next;
                message = listenerPort->readHead;
            }
        }
        msleep(5);
    }
}

void MidiRouter::markAsDone() {
    d->done = true;
}

void MidiRouter::setChannelDestination(int channel, MidiRouter::RoutingDestination destination, int externalChannel)
{
    if (channel > -1 && channel < OUTPUT_CHANNEL_COUNT) {
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

void MidiRouter::setZynthianChannels(int channel, QList<int> zynthianChannels)
{
    if (channel > -1 && channel < OUTPUT_CHANNEL_COUNT) {
        ChannelOutput *output = d->outputs[channel];
        bool hasChanged{false};
        for (int i = 0; i < 16; ++i) {
            int original = output->zynthianChannels[i];
            output->zynthianChannels[i] = zynthianChannels.value(i, -1);
            if (original != output->zynthianChannels[i]) {
                hasChanged = true;
            }
        }
        if (hasChanged) {
            if (DebugZLRouter) { qDebug() << "ZLRouter: Updating zynthian channels for" << output->portName << "from" << output->zynthianChannels << "to" << zynthianChannels; }
        }
    }
}

void MidiRouter::reloadConfiguration()
{
    // TODO Make the fb stuff work as well... (also, note to self, work out what that stuff actually is?)
    if (!d->constructing) {
        // First, disconnect our outputs, just in case...
        for (ChannelOutput *output : d->outputs) {
            d->disconnectFromOutputs(output);
        }
        for (OutputDevice *device : d->hardwareOutputs) {
            if (device->enabled) {
                d->disconnectPorts(QString("ZLRouter:%1").arg(d->externalOutputPort->portName), device->jackPortName);
            }
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
        d->refreshOutputsList();
        for (OutputDevice *device : d->hardwareOutputs) {
            if (device->enabled) {
                d->connectPorts(QString("ZLRouter:%1").arg(d->externalOutputPort->portName), device->jackPortName);
            }
        }
    }
}

QObjectList MidiRouter::channelPassthroughClients() const
{
    QObjectList clients;
    for (JackPassthrough *client : d->channelEffectsPassthroughClients) {
        clients << client;
    }
    return clients;
}

QObject * MidiRouter::globalEffectsPassthroughClient() const
{
    return d->globalEffectsPassthrough;
}

QObject * MidiRouter::globalPlaybackClient() const
{
    return d->globalPlayback;
}

#include "TransportManager.h"
#include "SyncTimer.h"
#include "TimerCommand.h"

#include <QDebug>

#include <jack/jack.h>
#include <jack/midiport.h>

class TransportManagerPrivate {
public:
    TransportManagerPrivate(SyncTimer *syncTimerInstance);
    ~TransportManagerPrivate() {
        if (client) {
            jack_transport_stop(client);
            jack_release_timebase(client);
            jack_client_close(client);
        }
    }
    SyncTimer *syncTimer{nullptr};
    jack_client_t *client{nullptr};
    jack_port_t *inPort{nullptr};
    jack_port_t *outPort{nullptr};
    bool running{false};

    uint32_t mostRecentEventCount{0};
    int process(jack_nframes_t nframes) {
        void *inputBuffer = jack_port_get_buffer(inPort, nframes);
        jack_midi_event_t event;
        uint32_t eventIndex = 0;
        // Sniff for any midi start, stop, continue and so on messages, and react accordingly (unless we're already playing)
        while (true) {
            if (int err = jack_midi_event_get(&event, inputBuffer, eventIndex)) {
                if (err != -ENOBUFS) {
                    qWarning() << Q_FUNC_INFO << "jack_midi_event_get failed, received note lost! Attempted to fetch at index" << eventIndex << "and the error code is" << err;
                }
                break;
            } else {
                // switch (event.size) {
                //     case 1:
                //         qDebug() << Q_FUNC_INFO << "Event data (size 1)" << event.buffer[0];
                //         break;
                //     case 2:
                //         qDebug() << Q_FUNC_INFO << "Event data (size 2)" << event.buffer[0] << event.buffer[1];
                //         break;
                //     case 3:
                //         qDebug() << Q_FUNC_INFO << "Event data (size 3)" << event.buffer[0] << event.buffer[1] << event.buffer[3];
                //         break;
                //     default:
                //         qDebug() << Q_FUNC_INFO << "Weird event received we don't know anything about... apparently size" << event.size;
                // }
                switch(event.buffer[0]) {
                    case 0xf2: // position
                        // (event->buffer[2]<<7) | event->buffer[1];
                        break;
                    case 0xf8: // clock
                        break;
                    case 0xfa: // start
                    case 0xfb: // continue
                        // Spec says to ignore start messages if they arrive while playback is happening
                        qDebug() << Q_FUNC_INFO << "Received MIDI START message";
                        if (!syncTimer->timerRunning()) {
                            TimerCommand *startCommand = syncTimer->getTimerCommand();
                            startCommand->operation = TimerCommand::StartPlaybackOperation;
                            syncTimer->scheduleTimerCommand(0, startCommand);
                        }
                        break;
                    case 0xfc: // stop
                        // Spec says to ignore stop messages if they arrive while playback is already stopped
                        qDebug() << Q_FUNC_INFO << "Received MIDI STOP message";
                        if (syncTimer->timerRunning()) {
                            TimerCommand *stopCommand = syncTimer->getTimerCommand();
                            stopCommand->operation = TimerCommand::StopPlaybackOperation;
                            syncTimer->scheduleTimerCommand(0, stopCommand);
                        }
                        break;
                    default:
                        break;
                }
            }
            ++eventIndex;
        }
        void *outputBuffer = jack_port_get_buffer(outPort, nframes);
        jack_midi_clear_buffer(outputBuffer);
        // TODO These messages want to go onto the control channel (whatever is set in the zynthian settings)
        return 0;
    }
    /**
     * @param state     current transport state.
     * @param nframes   number of frames in current period.
     * @param position  address of the position structure for the next cycle; pos->frame will be its frame number. If new_pos is FALSE, this structure contains extended position information from the current cycle. If TRUE, it contains whatever was set by the requester. The timebase_callback's task is to update the extended information here.
     * @param new_pos   TRUE (non-zero) for a newly requested pos, or for the first cycle after the timebase_callback is defined. 
     */
    void timebase_callback(jack_transport_state_t state, jack_nframes_t nframes, jack_position_t *position, int new_pos)  {
        /**
        * If new_pos is non-zero, calculate position->frame from Bar, Beat, Tick info
        * If new_pos is zero, calculate Bar, Beat, Tick from position->frame (the amount of frames this position is from the start of the playing song)
        */
        if (new_pos) {
            if (position->valid & JackPositionBBT) {
                qDebug() << Q_FUNC_INFO << "New position requested, based on bar/beat/tick" << state << nframes << position->bar << position->beat << position->tick;
            } else {
                // qDebug() << Q_FUNC_INFO << "New position and and bbt is not valid - is this the please-set-us-up-the-thing state?" << position->frame;
                syncTimer->setPosition(position);
            }
            position->valid = JackPositionBBT;
        } else {
            // qDebug() << Q_FUNC_INFO << "Calculate bar/beat/tick from position->frame - but actually we just pass in the values synctimer already calculated" << state << nframes << position->frame;
            syncTimer->setPosition(position);
        }
    }
};

int transport_process(jack_nframes_t nframes, void *arg) {
    return static_cast<TransportManagerPrivate*>(arg)->process(nframes);
}

void transport_timebase_callback(jack_transport_state_t state, jack_nframes_t nframes, jack_position_t *pos, int new_pos, void *arg) {
    return static_cast<TransportManagerPrivate*>(arg)->timebase_callback(state, nframes, pos, new_pos);
}

TransportManagerPrivate::TransportManagerPrivate(SyncTimer *syncTimerInstance)
{
    syncTimer = syncTimerInstance;
    jack_status_t real_jack_status{};
    client = jack_client_open("TransportManager", JackNullOption, &real_jack_status);
    if (client) {
        inPort = jack_port_register(client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput | JackPortIsTerminal, 0);
        outPort = jack_port_register(client, "midi_out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput | JackPortIsTerminal, 0);
        if (inPort && outPort) {
            if (jack_set_timebase_callback(client, 0, transport_timebase_callback, static_cast<void*>(this)) == 0) {
                // Set the process callback.
                if (jack_set_process_callback(client, transport_process, static_cast<void*>(this)) == 0) {
                    if (jack_activate(client) == 0) {
                        qDebug() << Q_FUNC_INFO << "Set up the transport manager, which lets us handle midi sync messages, and function as a Jack timebase master";
                        jack_transport_start(client);
                    } else {
                        qWarning() << Q_FUNC_INFO << "Failed to activate the Jack client";
                    }
                } else {
                    qWarning() << Q_FUNC_INFO << "Failed to set Jack processing callback";
                }
            } else {
                qWarning() << Q_FUNC_INFO << "Failed to register as transport master";
            }
        } else {
            qWarning() << Q_FUNC_INFO << "Failed to register one or both of the transport manager's ports";
        }
    } else {
        qWarning() << Q_FUNC_INFO << "Failed to create Jack client";
    }
}


TransportManager::TransportManager(SyncTimer *parent)
    : QObject(parent)
    , d(new TransportManagerPrivate(parent))
{
}

TransportManager::~TransportManager()
{
    delete d;
}

void TransportManager::restartTransport()
{
    jack_transport_stop(d->client);
    jack_transport_start(d->client);
}

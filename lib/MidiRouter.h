#pragma once

#include <QObject>
#include <QCoreApplication>

class MidiRouterPrivate;
/**
 * \brief System for routing midi messages from one jack input port (ZLRouter:MidiIn) to a set of output ports (ZLRouter::Channel0 through 15) based on their input channel settings
 *
 * By default everything will be routed to ZynMidiRouter without changing the event's channel.
 * To route anywhere else, use  the function setChannelDestination() to set up your redirections.
 * Note that setting the external channel will only affect channels set to ExternalDestination.
 */
class MidiRouter : public QObject
{
    Q_OBJECT
    /**
     * \brief A midi channel (must be 0 through 15) interpreted as the current one
     * Used for routing hardware input if it is supposed to go somewhere external
     * @default 0
     */
    Q_PROPERTY(int currentChannel READ currentChannel WRITE setCurrentChannel NOTIFY currentChannelChanged)
public:
    static MidiRouter* instance() {
        static MidiRouter* instance{nullptr};
        if (!instance) {
            instance = new MidiRouter(qApp);
        }
        return instance;
    };
    explicit MidiRouter(QObject *parent = nullptr);
    virtual ~MidiRouter();

    enum RoutingDestination {
        NoDestination = 0,
        ZynthianDestination = 1,
        ExternalDestination = 2,
    };
    /**
     * \brief Where notes on a specific midi channel should be routed
     * @note Logically, in zynthbox, we really only have ten tracks (corresponding to a channel each), but we might
     *       as well support all 16, because it makes little functional difference, and has near enough to no
     *       performance impact.
     * @param channel The midi channel (0 through 15)
     * @param destination Where the channel's notes should go (the default for all channels is ZynthianDestination)
     * @param externalChannel If set, messages on the given channel will be translated to end up on this channel instead
     */
    void setChannelDestination(int channel, RoutingDestination destination, int externalChannel = -1);

    void setCurrentChannel(int currentChannel);
    int currentChannel() const;
    Q_SIGNAL void currentChannelChanged();
private:
    MidiRouterPrivate *d{nullptr};
};

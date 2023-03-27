#pragma once

#include "SyncTimer.h"

class TransportManagerPrivate;
class TransportManager : public QObject {
    Q_OBJECT
public:
    static TransportManager* instance(SyncTimer *q = nullptr) {
        static TransportManager* instance{nullptr};
        if (!instance) {
            instance = new TransportManager(q);
        }
        return instance;
    };
    explicit TransportManager(SyncTimer *parent = nullptr);
    virtual ~TransportManager();

    // This is called by MidiRouter, to ensure we are ready and able to connect to things
    void initialize();

    void restartTransport();
private:
    TransportManagerPrivate *d{nullptr};
};

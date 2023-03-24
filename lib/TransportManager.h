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

    void restartTransport();
private:
    TransportManagerPrivate *d{nullptr};
};

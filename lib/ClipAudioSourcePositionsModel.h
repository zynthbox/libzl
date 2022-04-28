#pragma once

#include <QAbstractListModel>
#include <memory>

#include "ClipAudioSource.h"

class ClipAudioSourcePositionsModelPrivate;
class ClipAudioSourcePositionsModel : public QAbstractListModel
{
    Q_OBJECT
    /**
     * \brief The highest gain among all positions in the model
     */
    Q_PROPERTY(float peakGain READ peakGain NOTIFY peakGainChanged)
public:
    explicit ClipAudioSourcePositionsModel(ClipAudioSource *clip);
    ~ClipAudioSourcePositionsModel() override;

    enum PositionRoles {
        PositionIDRole = Qt::UserRole + 1,
        PositionProgressRole,
        PositionGainRole,
    };
    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    Q_INVOKABLE qint64 createPositionID (float initialProgress = 0.0f);
    Q_INVOKABLE void setPositionProgress(qint64 positionID, float progress);
    Q_INVOKABLE void setPositionGain(qint64 positionID, float gain);
    Q_INVOKABLE void removePosition(qint64 positionID);
    /**
     * \brief Asynchronously request the creation of a new position. Connect to positionIDCreated to learn what the position is.
     * @param createFor The object (or other pointer) that you wish to use as an identifier for the id (used when positionIDCreated is fired)
     * @param initialProgress The initial progress for the newly created position
     */
    Q_SLOT void requestPositionID(void* createFor, float initialProgress = 0.0f);
    /**
     * \brief Fired when requestPositionID completes (does not fire for calls to createPositionID)
     * @param createdFor The object (or other pointer) used when requesting the new position ID
     * @param newPositionID The new position ID
     */
    Q_SIGNAL void positionIDCreated(void* createdFor, qint64 newPositionID);

    float peakGain() const;
    Q_SIGNAL void peakGainChanged();

protected:
    void cleanUpPositions();
private:
    std::unique_ptr<ClipAudioSourcePositionsModelPrivate> d;
};

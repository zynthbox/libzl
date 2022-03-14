#pragma once

#include <QAbstractListModel>
#include <memory>

#include "ClipAudioSource.h"

class ClipAudioSourcePositionsModelPrivate;
class ClipAudioSourcePositionsModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit ClipAudioSourcePositionsModel(ClipAudioSource *clip);
    ~ClipAudioSourcePositionsModel() override;

    enum PositionRoles {
        PositionIDRole = Qt::UserRole + 1,
        PositionProgressRole,
    };
    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    Q_INVOKABLE qint64 createPositionID (float initialProgress = 0.0f);
    Q_INVOKABLE void setPositionProgress(qint64 positionID, float progress);
    Q_INVOKABLE void removePosition(qint64 positionID);
private:
    std::unique_ptr<ClipAudioSourcePositionsModelPrivate> d;
};

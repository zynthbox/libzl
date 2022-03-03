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

    int createPositionID(float initialProgress = 0.0f);
    void setPositionProgress(int positionID, float progress);
    void removePosition(int positionID);
private:
    std::unique_ptr<ClipAudioSourcePositionsModelPrivate> d;
};

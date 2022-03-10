#include "ClipAudioSourcePositionsModel.h"
#include <QMutex>

struct PositionData {
    int id{-1};
    float progress{0.0f};
};

class ClipAudioSourcePositionsModelPrivate
{
public:
    ClipAudioSourcePositionsModelPrivate() {}
    QList<PositionData*> positions;
    QMutex mutex;
};

ClipAudioSourcePositionsModel::ClipAudioSourcePositionsModel(ClipAudioSource *clip)
    : QAbstractListModel(clip)
    , d(new ClipAudioSourcePositionsModelPrivate)
{
}

ClipAudioSourcePositionsModel::~ClipAudioSourcePositionsModel() = default;

QHash<int, QByteArray> ClipAudioSourcePositionsModel::roleNames() const
{
    static const QHash<int, QByteArray> roleNames{
        {PositionIDRole, "positionID"},
        {PositionProgressRole, "positionProgress"},
    };
    return roleNames;
}

int ClipAudioSourcePositionsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return d->positions.count();
}

QVariant ClipAudioSourcePositionsModel::data(const QModelIndex &index, int role) const
{
    QVariant result;
    if (checkIndex(index)) {
        d->mutex.tryLock();
        PositionData *position = d->positions[index.row()];
        switch (role) {
            case PositionIDRole:
                result.setValue<int>(position->id);
                break;
            case PositionProgressRole:
                result.setValue<float>(position->progress);
                break;
            default:
                break;
        }
        d->mutex.unlock();
    }
    return result;
}

int ClipAudioSourcePositionsModel::createPositionID(float initialProgress)
{
    d->mutex.tryLock();
    PositionData *newPosition = new PositionData();
    newPosition->id = d->positions.count();
    newPosition->progress = initialProgress;
    beginInsertRows(QModelIndex(), newPosition->id, newPosition->id);
    d->positions << newPosition;
    endInsertRows();
    d->mutex.unlock();
    return newPosition->id;
}

void ClipAudioSourcePositionsModel::setPositionProgress(int positionID, float progress)
{
    d->mutex.tryLock();
    int index{0};
    for (PositionData *position : d->positions) {
        if (position->id == positionID) {
            position->progress = progress;
            const QModelIndex idx{createIndex(index, 0)};
            dataChanged(idx, idx, {PositionProgressRole});
            break;
        }
        ++index;
    }
    d->mutex.unlock();
}

void ClipAudioSourcePositionsModel::removePosition(int positionID)
{
    d->mutex.tryLock();
    int index{0};
    for (PositionData *position : d->positions) {
        if (position->id == positionID) {
            beginRemoveRows(QModelIndex(), index, index);
            d->positions.removeAt(index);
            endRemoveRows();
            break;
        }
        ++index;
    }
    d->mutex.unlock();
}

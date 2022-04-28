#include "ClipAudioSourcePositionsModel.h"
#include <QMutex>

struct PositionData {
    qint64 id{-1};
    float progress{0.0f};
    float gain{0.0f};
};

class ClipAudioSourcePositionsModelPrivate
{
public:
    ClipAudioSourcePositionsModelPrivate() {}
    QList<PositionData*> positions;
    bool updatePeakGain{false};
    float peakGain{0.0f};
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
        {PositionGainRole, "positionGain"},
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
                result.setValue<qint64>(position->id);
                break;
            case PositionProgressRole:
                result.setValue<float>(position->progress);
                break;
            case PositionGainRole:
                result.setValue<float>(position->gain);
                break;
            default:
                break;
        }
        d->mutex.unlock();
    }
    return result;
}

qint64 ClipAudioSourcePositionsModel::createPositionID(float initialProgress)
{
    static qint64 nextId{0};
    PositionData *newPosition = new PositionData();
    newPosition->id = nextId++;
    newPosition->progress = initialProgress;
    d->mutex.tryLock();
    beginInsertRows(QModelIndex(), d->positions.count(), d->positions.count());
    d->positions << newPosition;
    d->mutex.unlock();
    endInsertRows();
    d->updatePeakGain = true;
    Q_EMIT peakGainChanged();
    return newPosition->id;
}

void ClipAudioSourcePositionsModel::setPositionProgress(qint64 positionID, float progress)
{
    d->mutex.tryLock();
    int index{0};
    for (PositionData *position : d->positions) {
        if (position->id == positionID) {
            position->progress = progress;
            const QModelIndex idx{createIndex(index, 0)};
            d->mutex.unlock();
            dataChanged(idx, idx, {PositionProgressRole});
            break;
        }
        ++index;
    }
    d->mutex.unlock();
}

void ClipAudioSourcePositionsModel::setPositionGain(qint64 positionID, float gain)
{
    d->mutex.tryLock();
    int index{0};
    for (PositionData *position : d->positions) {
        if (position->id == positionID) {
            position->gain = gain;
            const QModelIndex idx{createIndex(index, 0)};
            d->mutex.unlock();
            dataChanged(idx, idx, {PositionGainRole});
            d->updatePeakGain = true;
            Q_EMIT peakGainChanged();
            break;
        }
        ++index;
    }
    d->mutex.unlock();
}

void ClipAudioSourcePositionsModel::removePosition(qint64 positionID)
{
    d->mutex.tryLock();
    int index{0};
    for (PositionData *position : d->positions) {
        if (position->id == positionID) {
            beginRemoveRows(QModelIndex(), index, index);
            d->positions.removeAt(index);
            d->mutex.unlock();
            endRemoveRows();
            d->updatePeakGain = true;
            Q_EMIT peakGainChanged();
            break;
        }
        ++index;
    }
    d->mutex.unlock();
}

void ClipAudioSourcePositionsModel::requestPositionID(void *createFor, float initialProgress)
{
    Q_EMIT positionIDCreated(createFor, createPositionID(initialProgress));
}

float ClipAudioSourcePositionsModel::peakGain() const
{
    if (d->updatePeakGain) {
        float peak{0.0f};
        for (PositionData *position : d->positions) {
            peak = qMax(peak, position->gain);
        }
        if (abs(d->peakGain - peak) > 0.01) {
            d->peakGain = peak;
        }
    }
    return d->peakGain;
}

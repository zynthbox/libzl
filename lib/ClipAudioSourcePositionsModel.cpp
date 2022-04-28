#include "ClipAudioSourcePositionsModel.h"
#include <QDateTime>
#include <QDebug>
#include <QMutex>

struct PositionData {
    qint64 id{-1};
    float progress{0.0f};
    float gain{0.0f};
    qint64 lastUpdated{0};
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
    newPosition->lastUpdated = QDateTime::currentMSecsSinceEpoch();
    d->mutex.tryLock();
    beginInsertRows(QModelIndex(), d->positions.count(), d->positions.count());
    d->positions << newPosition;
    d->mutex.unlock();
    endInsertRows();
    d->updatePeakGain = true;
    Q_EMIT peakGainChanged();
    cleanUpPositions();
    return newPosition->id;
}

void ClipAudioSourcePositionsModel::setPositionProgress(qint64 positionID, float progress)
{
    d->mutex.tryLock();
    int index{0};
    for (PositionData *position : d->positions) {
        if (position->id == positionID) {
            position->progress = qMin(1.0f, qMax(0.0f, progress));
            position->lastUpdated = QDateTime::currentMSecsSinceEpoch();
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
            position->lastUpdated = QDateTime::currentMSecsSinceEpoch();
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
    cleanUpPositions();
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

// This is an unpleasant hack that i'd like to not have to use
// but without it we occasionally end up with apparently orphaned
// positions in the model, and... less of that is better.
// If someone can work out why we end up with those, though, that'd be lovely
void ClipAudioSourcePositionsModel::cleanUpPositions() {
    d->mutex.tryLock();
    const qint64 allowedTime{QDateTime::currentMSecsSinceEpoch() - 1000};
    QMutableListIterator<PositionData*> i(d->positions);
    int removedAny{0};
    while (i.hasNext()) {
        PositionData *position = i.next();
        if (position->lastUpdated < allowedTime) {
            i.remove();
            ++removedAny;
        }
    }
    if (removedAny > 0) {
        qDebug() << "We had" << removedAny << "orphaned positions, removed those";
        Q_EMIT beginResetModel();
        Q_EMIT endResetModel();
    }
    d->mutex.unlock();
}

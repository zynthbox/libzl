#include "ClipAudioSourcePositionsModel.h"
#include <QDateTime>
#include <QDebug>

#define POSITION_COUNT 32

struct PositionData {
    qint64 id{-1};
    float progress{0.0f};
    float gain{0.0f};
    qint64 lastUpdated{0};
};

class ClipAudioSourcePositionsModelPrivate
{
public:
    ClipAudioSourcePositionsModelPrivate() {
        for (int positionIndex = 0; positionIndex < POSITION_COUNT; ++positionIndex) {
            positions << new PositionData;
        }
    }
    ~ClipAudioSourcePositionsModelPrivate() {
        qDeleteAll(positions);
    }
    QList<PositionData*> positions;
    bool updatePeakGain{false};
    float peakGain{0.0f};
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
    }
    return result;
}

qint64 ClipAudioSourcePositionsModel::createPositionID(float initialProgress)
{
    PositionData *position{nullptr};
    int positionRow{-1};
    for (PositionData *needle : qAsConst(d->positions)) {
        ++positionRow;
        if (needle->id == -1) {
            position = needle;
            break;
        }
    }
    if (position) {
        position->id = positionRow;
        position->progress = initialProgress;
        position->lastUpdated = QDateTime::currentMSecsSinceEpoch();
        QModelIndex modelIndex{createIndex(positionRow, 0)};
        dataChanged(modelIndex, modelIndex);
        d->updatePeakGain = true;
        Q_EMIT peakGainChanged();
        cleanUpPositions();
    }
    return positionRow;
}

void ClipAudioSourcePositionsModel::setPositionProgress(qint64 positionID, float progress)
{
    if (positionID > -1 && positionID < POSITION_COUNT) {
        PositionData *position = d->positions[positionID];
        position->progress = qMin(1.0f, qMax(0.0f, progress));
        position->lastUpdated = QDateTime::currentMSecsSinceEpoch();
        const QModelIndex idx{createIndex(positionID, 0)};
        dataChanged(idx, idx, {PositionProgressRole});
    }
}

void ClipAudioSourcePositionsModel::setPositionGain(qint64 positionID, float gain)
{
    if (positionID > -1 && positionID < POSITION_COUNT) {
        PositionData *position = d->positions[positionID];
        position->gain = gain;
        position->lastUpdated = QDateTime::currentMSecsSinceEpoch();
        const QModelIndex idx{createIndex(positionID, 0)};
        dataChanged(idx, idx, {PositionGainRole});
        d->updatePeakGain = true;
        Q_EMIT peakGainChanged();
    }
}

void ClipAudioSourcePositionsModel::setPositionGainAndProgress(qint64 positionID, float gain, float progress)
{
    if (positionID > -1 && positionID < POSITION_COUNT) {
        PositionData *position = d->positions[positionID];
        position->gain = gain;
        position->progress = progress;
        position->lastUpdated = QDateTime::currentMSecsSinceEpoch();
        const QModelIndex idx{createIndex(positionID, 0)};
        dataChanged(idx, idx, {PositionGainRole, PositionProgressRole});
        d->updatePeakGain = true;
        Q_EMIT peakGainChanged();
    }
}

void ClipAudioSourcePositionsModel::removePosition(qint64 positionID)
{
    if (positionID > -1 && positionID < POSITION_COUNT) {
        PositionData *position = d->positions[positionID];
        position->id = -1;
        position->gain = 0.0f;
        position->progress = 0.0f;
        const QModelIndex idx{createIndex(positionID, 0)};
        dataChanged(idx, idx);
        d->updatePeakGain = true;
        Q_EMIT peakGainChanged();
    }
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
        for (PositionData *position : qAsConst(d->positions)) {
            peak = qMax(peak, position->gain);
        }
        if (abs(d->peakGain - peak) > 0.01) {
            d->peakGain = peak;
        }
        d->updatePeakGain = false;
    }
    return d->peakGain;
}

double ClipAudioSourcePositionsModel::firstProgress() const
{
    double progress{-1.0f};
    for (const PositionData *position : qAsConst(d->positions)) {
        if (position->id > -1) {
            progress = position->progress;
            break;
        }
    }
    return progress;
}

// This is an unpleasant hack that i'd like to not have to use
// but without it we occasionally end up with apparently orphaned
// positions in the model, and... less of that is better.
// If someone can work out why we end up with those, though, that'd be lovely
void ClipAudioSourcePositionsModel::cleanUpPositions() {
    const qint64 allowedTime{QDateTime::currentMSecsSinceEpoch() - 1000};
    QListIterator<PositionData*> i(d->positions);
    int removedAny{0};
    while (i.hasNext()) {
        PositionData *position = i.next();
        if (position->id > -1 && position->lastUpdated < allowedTime) {
            position->id = -1;
            position->gain = 0.0f;
            position->progress = 0.0f;
            ++removedAny;
        }
    }
    if (removedAny > 0) {
        qDebug() << "We had" << removedAny << "orphaned positions, removed those";
        Q_EMIT beginResetModel();
        Q_EMIT endResetModel();
    }
}

/*
  ==============================================================================

    ClipAudioSource.h
    Created: 19/8/2021
    Author:  Marco Martin <mart@kde.org>

  ==============================================================================
*/

#pragma once

#include "JUCEHeaders.h"
#include <QQuickPaintedItem>

class WaveFormItem : public QQuickPaintedItem
{
Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)

public:
    WaveFormItem(QQuickItem *parent = nullptr);
    void paint(QPainter *painter);

    QString source() const;
    void setSource(QString &source);

Q_SIGNALS:
    void sourceChanged();

private:
    QString m_source;

    std::unique_ptr<juce::AudioFormatReaderSource> m_readerSource;
    juce::AudioTransportSource m_transportSource;
    juce::AudioFormatManager m_formatManager;
    //TransportState m_state;
    juce::AudioThumbnailCache m_thumbnailCache;
    juce::AudioThumbnail m_thumbnail;
};


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

public:
    WaveFormItem(QQuickItem *parent = nullptr);
    void paint(QPainter *painter);

private:
    juce::AudioFormatManager m_formatManager;
    //TransportState m_state;
    juce::AudioThumbnailCache m_thumbnailCache;
    juce::AudioThumbnail m_thumbnail;
};


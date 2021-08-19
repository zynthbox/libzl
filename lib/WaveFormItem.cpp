/*
  ==============================================================================

    ClipAudioSource.h
    Created: 19/8/2021
    Author:  Marco Martin <mart@kde.org>

  ==============================================================================
*/

#include "WaveFormItem.h"

WaveFormItem::WaveFormItem(QQuickItem *parent)
    : QQuickPaintedItem(parent),
     // m_state (Stopped),
      m_thumbnailCache (5),
      m_thumbnail (512, m_formatManager, m_thumbnailCache)
{

}

void WaveFormItem::paint(QPainter *painter)
{

}

#include "moc_WaveFormItem.cpp"


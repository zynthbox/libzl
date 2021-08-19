/*
  ==============================================================================

    ClipAudioSource.h
    Created: 19/8/2021
    Author:  Marco Martin <mart@kde.org>

  ==============================================================================
*/

#include "QPainterContext.h"

#include <QPainter>

QPainterContext::QPainterContext(QPainter *painter)
    : juce::LowLevelGraphicsContext(),
      m_painter(painter)
{

}

QPainterContext::~QPainterContext()
{}

#include "moc_QPainterContext.cpp"

/*
  ==============================================================================

    ClipAudioSource.h
    Created: 19/8/2021
    Author:  Marco Martin <mart@kde.org>

  ==============================================================================
*/

#pragma once

#include "JUCEHeaders.h"

class QPainter;

class QPainterContext : public juce::LowLevelGraphicsContext
{
public:
    QPainterContext(QPainter *painter);
    ~QPainterContext();

private:
    QPainter *m_painter;
};

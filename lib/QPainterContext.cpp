/*
  ==============================================================================

    ClipAudioSource.h
    Created: 19/8/2021
    Author:  Marco Martin <mart@kde.org>

  ==============================================================================
*/

#include "QPainterContext.h"

#include <QPainter>
#include <QDebug>

using namespace juce;

QPainterContext::QPainterContext()
    : juce::LowLevelGraphicsContext()
{

}

QPainterContext::~QPainterContext()
{}

void QPainterContext::setPainter(QPainter *painter)
{
    m_painter = painter;
}

QPainter *QPainterContext::painter()
{
    return m_painter;
}

bool QPainterContext::isVectorDevice() const
{
    return false;
}

void QPainterContext::setOrigin(Point<int> jP)
{
    if (!m_painter) {
        return;
    }

    m_painter->setBrushOrigin(jP.getX(), jP.getY());
}

void QPainterContext::addTransform(const AffineTransform&)
{
    return;
}

float QPainterContext::getPhysicalPixelScaleFactor()
{
    return 1.0; //TODO
}

bool QPainterContext::clipToRectangle(const Rectangle<int>& jRect)
{
    if (!m_painter) {
        return false;
    }

    m_painter->setClipRect(jRect.getX(), jRect.getY(), jRect.getWidth(), jRect.getHeight());
    return true;
}

bool QPainterContext::clipToRectangleList(const RectangleList<int>&)
{
    return false;
}

void QPainterContext::excludeClipRectangle(const Rectangle<int>&)
{

}

void QPainterContext::clipToPath(const Path&, const AffineTransform&)
{

}

void QPainterContext::clipToImageAlpha(const Image&, const AffineTransform&)
{

}

bool QPainterContext::clipRegionIntersects(const Rectangle<int>&)
{
    return false;
}

Rectangle<int> QPainterContext::getClipBounds() const
{
    if (!m_painter) {
        return Rectangle<int>();
    }

    if (m_painter->clipBoundingRect().isEmpty()) {
        return Rectangle<int>(0, 0, m_painter->device()->width(), m_painter->device()->height());
    } else {
        const QRectF rect = m_painter->clipBoundingRect();
        return Rectangle<int>(rect.x(), rect.y(), rect.width(), rect.height());
    }
}

bool QPainterContext::isClipEmpty() const
{
    return false;
}

void QPainterContext::saveState()
{
    if (!m_painter) {
        return;
    }
    m_painter->save();
}

void QPainterContext::restoreState()
{
    if (!m_painter) {
        return;
    }
    m_painter->restore();
}

void QPainterContext::beginTransparencyLayer(float opacity)
{

}

void QPainterContext::endTransparencyLayer()
{

}


//==============================================================================
void QPainterContext::setQBrush(const QBrush &brush)
{
    m_brush = brush;
}

QBrush QPainterContext::qBrush() const
{
    return m_brush;
}

void QPainterContext::setFill(const FillType &fillType)
{
    if (!m_painter) {
        return;
    }

    m_brush = QBrush(QColor(fillType.colour.getRed(), fillType.colour.getGreen(), fillType.colour.getBlue(), fillType.colour.getAlpha()));
    m_painter->setBrush(m_brush);
}

void QPainterContext::setOpacity(float opacity)
{
    m_painter->setOpacity(opacity);
}

void QPainterContext::setInterpolationQuality(Graphics::ResamplingQuality)
{

}

//==============================================================================
void QPainterContext::fillRect(const Rectangle<int> &jRect, bool replaceExistingContents)
{
    if (!m_painter) {
        return;
    }
    m_painter->fillRect(jRect.getX(), jRect.getY(), jRect.getWidth(), jRect.getHeight(), m_brush);
}

void QPainterContext::fillRect(const Rectangle<float> &jRect)
{
    if (!m_painter) {
        return;
    }
    m_painter->fillRect(jRect.getX(), jRect.getY(), jRect.getWidth(), jRect.getHeight(), m_brush);
}

void QPainterContext::fillRectList(const RectangleList<float> &jRegion)
{
    if (!m_painter) {
        return;
    }

    for (int i = 0; i < jRegion.getNumRectangles(); ++i) {
        Rectangle<float> jRect = jRegion.getRectangle(i);
        m_painter->fillRect(jRect.getX(), jRect.getY(), jRect.getWidth(), jRect.getHeight(), m_brush);
    }
}

void QPainterContext::fillPath(const Path&, const AffineTransform&)
{

}

void QPainterContext::drawImage(const Image&, const AffineTransform&)
{

}

void QPainterContext::drawLine(const Line<float>&)
{

}

void QPainterContext::setFont(const Font&)
{

}

const Font &QPainterContext::getFont()
{

}

void QPainterContext::drawGlyph(int glyphNumber, const AffineTransform&)
{

}

/*
  ==============================================================================

    ClipAudioSource.h
    Created: 19/8/2021
    Author:  Marco Martin <mart@kde.org>

  ==============================================================================
*/

#include "WaveFormItem.h"

#include <QPainter>
#include <QDebug>

WaveFormItem::WaveFormItem(QQuickItem *parent)
    : QQuickPaintedItem(parent),
     // m_state (Stopped),
      m_juceGraphics(m_painterContext),
      m_thumbnailCache (5),
      m_thumbnail (512, m_formatManager, m_thumbnailCache)

{
    m_formatManager.registerBasicFormats();
}

QString WaveFormItem::source() const
{
    return m_source;
}

void WaveFormItem::setSource(QString &source)
{
    if (source == m_source) {
        return;
    }

    m_source = source;

    juce::File file(source.toUtf8().constData());
    auto *reader = m_formatManager.createReaderFor(file);

    if (reader != nullptr) {
        std::unique_ptr<juce::AudioFormatReaderSource> newSource(new juce::AudioFormatReaderSource(reader, true));
        m_transportSource.setSource (newSource.get(), 0, nullptr, reader->sampleRate);
        m_thumbnail.setSource(new juce::FileInputSource (file));
        m_readerSource.reset (newSource.release());
    }

    emit sourceChanged();
}

QColor WaveFormItem::color() const
{
    return m_color;
}

void WaveFormItem::setColor(const QColor &color)
{
    if (color == m_color) {
        return;
    }

    m_color = color;
    m_painterContext.setQBrush(m_color);
    emit colorChanged();
}

void WaveFormItem::paint(QPainter *painter)
{
    m_painterContext.setPainter(painter);
    juce::Rectangle<int> thumbnailBounds (0, 0, width(), height());
    m_thumbnail.drawChannel(m_juceGraphics,
                            thumbnailBounds,
                            0.0,                                    // start time
                            m_thumbnail.getTotalLength(),             // end time
                            0, // channel num
                            1.0f);
}

#include "moc_WaveFormItem.cpp"


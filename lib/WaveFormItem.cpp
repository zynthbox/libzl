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
#include <QTimer>

WaveFormItem::WaveFormItem(QQuickItem *parent)
    : QQuickPaintedItem(parent),
     // m_state (Stopped),
      m_juceGraphics(m_painterContext),
      m_thumbnailCache (5),
      m_thumbnail (512, m_formatManager, m_thumbnailCache)

{
    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setSingleShot(true);
    m_repaintTimer->setInterval(200);
    connect(m_repaintTimer, &QTimer::timeout, this, [this]() {update();});
    m_formatManager.registerBasicFormats();
    m_thumbnail.addChangeListener(this);
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
}

qreal WaveFormItem::length() const
{
    return m_thumbnail.getTotalLength();
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

qreal WaveFormItem::start() const
{
    return m_start;
}

void WaveFormItem::setStart(qreal start)
{
    if (start == m_start) {
        return;
    }

    m_start = start;
    emit startChanged();
    update();
}

qreal WaveFormItem::end() const
{
    return m_end;
}

void WaveFormItem::setEnd(qreal end)
{
    if (end == m_end) {
        return;
    }

    m_end = end;
    emit endChanged();
    update();
}

void WaveFormItem::changeListenerCallback(juce::ChangeBroadcaster *source)
{
    if (source == &m_thumbnail) {
        // qWarning() << "Thumbnail Source Changed. Repainting.";
        QMetaObject::invokeMethod(this, "thumbnailChanged", Qt::QueuedConnection);
    }
}

void WaveFormItem::thumbnailChanged()
{
    m_start = 0;
    m_end = m_thumbnail.getTotalLength();

    emit startChanged();
    emit endChanged();
    emit sourceChanged();
    emit lengthChanged();
    update();
}

void WaveFormItem::paint(QPainter *painter)
{
    m_painterContext.setPainter(painter);
    juce::Rectangle<int> thumbnailBounds (0, 0, width(), height());
    m_thumbnail.drawChannel(m_juceGraphics,
                            thumbnailBounds,
                            m_start,                                    // start time
                            qMin(m_end, m_thumbnail.getTotalLength()),             // end time
                            0, // channel num
                            1.0f);
    if (!m_thumbnail.isFullyLoaded()) {
        m_repaintTimer->start();
    }
}

#include "moc_WaveFormItem.cpp"


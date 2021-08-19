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

void WaveFormItem::paint(QPainter *painter)
{

}

#include "moc_WaveFormItem.cpp"


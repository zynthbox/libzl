/*
  ==============================================================================

    ClipAudioSource.h
    Created: 19/8/2021
    Author:  Marco Martin <mart@kde.org>

  ==============================================================================
*/

#pragma once

#include "JUCEHeaders.h"
#include "QPainterContext.h"
#include <QQuickPaintedItem>

class WaveFormItem : public QQuickPaintedItem,
                     private juce::ChangeListener
{
Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(qreal length READ length NOTIFY lengthChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(qreal start READ start WRITE setStart NOTIFY startChanged)
    Q_PROPERTY(qreal end READ end WRITE setEnd NOTIFY endChanged)

public:
    WaveFormItem(QQuickItem *parent = nullptr);
    void paint(QPainter *painter);

    QString source() const;
    void setSource(QString &source);

    qreal length() const;

    QColor color() const;
    void setColor(const QColor &color);

    qreal start() const;
    void setStart(qreal start);

    qreal end() const;
    void setEnd(qreal end);

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    Q_SLOT void thumbnailChanged();

Q_SIGNALS:
    void sourceChanged();
    void lengthChanged();
    void colorChanged();

    void startChanged();
    void endChanged();

private:
    QString m_source;

    QTimer *m_repaintTimer;
    juce::Graphics m_juceGraphics;
    QPainterContext m_painterContext;
    QColor m_color;
    std::unique_ptr<juce::AudioFormatReaderSource> m_readerSource;
    juce::AudioTransportSource m_transportSource;
    juce::AudioFormatManager m_formatManager;
    //TransportState m_state;
    juce::AudioThumbnailCache m_thumbnailCache;
    juce::AudioThumbnail m_thumbnail;
    qreal m_start = 0;
    qreal m_end = 0;
};


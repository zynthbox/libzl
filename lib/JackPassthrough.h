/*
  ==============================================================================

    JackPassthrough.h
    Created: 26 Sep 2022
    Author: Dan Leinir Turthra Jensen <admin@leinir.dk>

  ==============================================================================
*/

#pragma once

#include <QObject>

class JackPassthroughPrivate;
/**
 * \brief A splitting passthrough client which has a pair of inputs, and two pairs of outputs (dry and wet) with individual volume for each output
 *
 * The input ports are inputLeft and inpitRight
 * The output ports are dryOutLeft and dryOutRight for the dry pair, and wetOutLeft and wetOutRight for the wet pair
 *
 * Due to the manner in which the client operates, setting the dry and wet amounts to 0 or 1 makes the
 * client operate much faster than any value between the two (the two extremes are direct copies, and
 * the others have to modify the sample values).
 */
class JackPassthrough : public QObject {
    Q_OBJECT
    Q_PROPERTY(float dryAmount READ dryAmount WRITE setDryAmount NOTIFY dryAmountChanged)
    Q_PROPERTY(float wetFx1Amount READ wetFx1Amount WRITE setWetFx1Amount NOTIFY wetFx1AmountChanged)
    Q_PROPERTY(float wetFx2Amount READ wetFx2Amount WRITE setWetFx2Amount NOTIFY wetFx2AmountChanged)
    Q_PROPERTY(float panAmount READ panAmount WRITE setPanAmount NOTIFY panAmountChanged)
public:
    explicit JackPassthrough(const QString &clientName, QObject *parent = nullptr);
    ~JackPassthrough() override;

    float dryAmount() const;
    void setDryAmount(const float& newValue);
    Q_SIGNAL void dryAmountChanged();

    float wetFx1Amount() const;
    void setWetFx1Amount(const float& newValue);
    Q_SIGNAL void wetFx1AmountChanged();

    float wetFx2Amount() const;
    void setWetFx2Amount(const float& newValue);
    Q_SIGNAL void wetFx2AmountChanged();

    float panAmount() const;
    void setPanAmount(const float& newValue);
    Q_SIGNAL void panAmountChanged();
private:
    JackPassthroughPrivate *d{nullptr};
};

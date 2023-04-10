/*
  ==============================================================================

    JackPassthrough.cpp
    Created: 26 Sep 2022
    Author: Dan Leinir Turthra Jensen <admin@leinir.dk>

  ==============================================================================
*/

#include "JackPassthrough.h"

#include <QDebug>

#include <jack/jack.h>
#include <jack/midiport.h>

class JackPassthroughPrivate {
public:
    JackPassthroughPrivate(const QString &clientName);
    ~JackPassthroughPrivate() {
        if (client) {
            jack_client_close(client);
        }
    }
    QString clientName;
    float dryAmount{1.0f};
    float wetFx1Amount{1.0f};
    float wetFx2Amount{1.0f};
    float panAmount{0.0f};
    jack_default_audio_sample_t channelSampleLeft;
    jack_default_audio_sample_t channelSampleRight;

    jack_client_t *client{nullptr};
    jack_port_t *inputLeft{nullptr};
    jack_port_t *inputRight{nullptr};
    jack_port_t *dryOutLeft{nullptr};
    jack_port_t *dryOutRight{nullptr};
    jack_port_t *wetOutFx1Left{nullptr};
    jack_port_t *wetOutFx1Right{nullptr};
    jack_port_t *wetOutFx2Left{nullptr};
    jack_port_t *wetOutFx2Right{nullptr};

    int process(jack_nframes_t nframes) {
        jack_default_audio_sample_t *inputLeftBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(inputLeft, nframes);
        jack_default_audio_sample_t *inputRightBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(inputRight, nframes);
        jack_default_audio_sample_t *dryOutLeftBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(dryOutLeft, nframes);
        jack_default_audio_sample_t *dryOutRightBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(dryOutRight, nframes);
        jack_default_audio_sample_t *wetOutFx1LeftBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(wetOutFx1Left, nframes);
        jack_default_audio_sample_t *wetOutFx1RightBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(wetOutFx1Right, nframes);
        jack_default_audio_sample_t *wetOutFx2LeftBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(wetOutFx2Left, nframes);
        jack_default_audio_sample_t *wetOutFx2RightBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(wetOutFx2Right, nframes);
        bool outputDry{true};
        bool outputWetFx1{true};
        bool outputWetFx2{true};
        if (panAmount == 0 && dryAmount == 0) {
            outputDry = false;
            memset(dryOutLeftBuffer, 0, nframes * sizeof(jack_default_audio_sample_t));
            memset(dryOutRightBuffer, 0, nframes * sizeof(jack_default_audio_sample_t));
        } else if (panAmount == 0 && dryAmount == 1) {
            outputDry = false;
            memcpy(dryOutLeftBuffer, inputLeftBuffer, nframes * sizeof(jack_default_audio_sample_t));
            memcpy(dryOutRightBuffer, inputRightBuffer, nframes * sizeof(jack_default_audio_sample_t));
        }
        if (panAmount == 0 && wetFx1Amount == 0) {
            outputWetFx1 = false;
            memset(wetOutFx1LeftBuffer, 0, nframes * sizeof(jack_default_audio_sample_t));
            memset(wetOutFx1RightBuffer, 0, nframes * sizeof(jack_default_audio_sample_t));
        } else if (panAmount == 0 && wetFx1Amount == 1) {
            outputWetFx1 = false;
            memcpy(wetOutFx1LeftBuffer, inputLeftBuffer, nframes * sizeof(jack_default_audio_sample_t));
            memcpy(wetOutFx1RightBuffer, inputRightBuffer, nframes * sizeof(jack_default_audio_sample_t));
        }
        if (panAmount == 0 && wetFx2Amount == 0) {
            outputWetFx2 = false;
            memset(wetOutFx2LeftBuffer, 0, nframes * sizeof(jack_default_audio_sample_t));
            memset(wetOutFx2RightBuffer, 0, nframes * sizeof(jack_default_audio_sample_t));
        } else if (panAmount == 0 && wetFx2Amount == 1) {
            outputWetFx2 = false;
            memcpy(wetOutFx2LeftBuffer, inputLeftBuffer, nframes * sizeof(jack_default_audio_sample_t));
            memcpy(wetOutFx2RightBuffer, inputRightBuffer, nframes * sizeof(jack_default_audio_sample_t));
        }
        if (panAmount != 0 || outputDry || outputWetFx1 || outputWetFx2) {
            for (jack_nframes_t frame=0; frame<nframes; ++frame) {
                channelSampleLeft = *(inputLeftBuffer + frame);
                channelSampleRight = *(inputRightBuffer + frame);
                // Implement Linear panning : https://forum.juce.com/t/how-do-stereo-panning-knobs-work/25773/9
                // Implementing M/S panning is not producing intended result. For our case Linear(Simple) Panning seems to do the job
                if (panAmount != 0 || outputDry) {
                    *(dryOutLeftBuffer + frame) = dryAmount * channelSampleLeft * std::min(1 - panAmount, 1.0f);
                    *(dryOutRightBuffer + frame) = dryAmount * channelSampleRight * std::min(1 + panAmount, 1.0f);
                }
                if (panAmount != 0 || outputWetFx1) {
                    *(wetOutFx1LeftBuffer + frame) = wetFx1Amount * channelSampleLeft * std::min(1 - panAmount, 1.0f);
                    *(wetOutFx1RightBuffer + frame) = wetFx1Amount * channelSampleRight * std::min(1 + panAmount, 1.0f);
                }
                if (panAmount != 0 || outputWetFx2) {
                    *(wetOutFx2LeftBuffer + frame) = wetFx2Amount * channelSampleLeft * std::min(1 - panAmount, 1.0f);
                    *(wetOutFx2RightBuffer + frame) = wetFx2Amount * channelSampleRight * std::min(1 + panAmount, 1.0f);
                }
            }
        }
        return 0;
    }
};

static int jackPassthroughProcess(jack_nframes_t nframes, void* arg) {
  return static_cast<JackPassthroughPrivate*>(arg)->process(nframes);
}

JackPassthroughPrivate::JackPassthroughPrivate(const QString &clientName)
{
    jack_status_t real_jack_status{};
    client = jack_client_open(clientName.toUtf8(), JackNullOption, &real_jack_status);
    if (client) {
        inputLeft = jack_port_register(client, "inputLeft", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        inputRight = jack_port_register(client, "inputRight", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        dryOutLeft = jack_port_register(client, "dryOutLeft", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        dryOutRight = jack_port_register(client, "dryOutRight", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        wetOutFx1Left = jack_port_register(client, "wetOutFx1Left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        wetOutFx1Right = jack_port_register(client, "wetOutFx1Right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        wetOutFx2Left = jack_port_register(client, "wetOutFx2Left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        wetOutFx2Right = jack_port_register(client, "wetOutFx2Right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (inputLeft && inputRight && dryOutLeft && dryOutRight && wetOutFx1Left && wetOutFx1Right && wetOutFx2Left && wetOutFx2Right) {
            // Set the process callback.
            if (jack_set_process_callback(client, jackPassthroughProcess, static_cast<void*>(this)) == 0) {
                if (jack_activate(client) == 0) {
                    // Success! Now we just kind of sit here and do the thing until we're done or whatever
                } else {
                    qWarning() << "JackPasstrough Client: Failed to activate the Jack client for" << clientName;
                }
            } else {
                qWarning() << "JackPasstrough Client: Failed to set Jack processing callback for" << clientName;
            }
        } else {
            qWarning() << "JackPasstrough Client: Failed to register ports for" << clientName;
        }
    } else {
        qWarning() << "JackPasstrough Client: Failed to create Jack client for" << clientName;
    }
}

JackPassthrough::JackPassthrough(const QString &clientName, QObject *parent)
    : QObject(parent)
    , d(new JackPassthroughPrivate(clientName))
{
}

JackPassthrough::~JackPassthrough()
{
    delete d;
}

float JackPassthrough::dryAmount() const
{
    return d->dryAmount;
}

void JackPassthrough::setDryAmount(const float &newValue)
{
    if (d->dryAmount != newValue) {
        d->dryAmount = newValue;
        Q_EMIT dryAmountChanged();
    }
}

float JackPassthrough::wetFx1Amount() const
{
    return d->wetFx1Amount;
}

void JackPassthrough::setWetFx1Amount(const float &newValue)
{
    if (d->wetFx1Amount != newValue) {
        d->wetFx1Amount = newValue;
        Q_EMIT wetFx1AmountChanged();
    }
}

float JackPassthrough::wetFx2Amount() const
{
    return d->wetFx2Amount;
}

void JackPassthrough::setWetFx2Amount(const float &newValue)
{
    if (d->wetFx2Amount != newValue) {
        d->wetFx2Amount = newValue;
        Q_EMIT wetFx2AmountChanged();
    }
}

float JackPassthrough::panAmount() const
{
    return d->panAmount;
}

void JackPassthrough::setPanAmount(const float &newValue)
{
    if (d->panAmount != newValue) {
        d->panAmount = newValue;
        Q_EMIT panAmountChanged();
    }
}

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
    float wetAmount{1.0f};

    jack_client_t *client{nullptr};
    jack_port_t *inputLeft{nullptr};
    jack_port_t *inputRight{nullptr};
    jack_port_t *dryOutLeft{nullptr};
    jack_port_t *dryOutRight{nullptr};
    jack_port_t *wetOutLeft{nullptr};
    jack_port_t *wetOutRight{nullptr};

    int process(jack_nframes_t nframes) {
        jack_default_audio_sample_t *inputLeftBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(inputLeft, nframes);
        jack_default_audio_sample_t *inputRightBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(inputRight, nframes);
        jack_default_audio_sample_t *dryOutLeftBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(dryOutLeft, nframes);
        jack_default_audio_sample_t *dryOutRightBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(dryOutRight, nframes);
        jack_default_audio_sample_t *wetOutLeftBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(wetOutLeft, nframes);
        jack_default_audio_sample_t *wetOutRightBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(wetOutRight, nframes);
        bool outputDry{true};
        bool outputWet{true};
        if (dryAmount == 0) {
            outputDry = false;
            memset(dryOutLeftBuffer, 0, nframes * sizeof(jack_default_audio_sample_t));
            memset(dryOutRightBuffer, 0, nframes * sizeof(jack_default_audio_sample_t));
        } else if (dryAmount == 1) {
            outputDry = false;
            memcpy(dryOutLeftBuffer, inputLeftBuffer, nframes * sizeof(jack_default_audio_sample_t));
            memcpy(dryOutRightBuffer, inputRightBuffer, nframes * sizeof(jack_default_audio_sample_t));
        }
        if (wetAmount == 0) {
            outputWet = false;
            memset(wetOutLeftBuffer, 0, nframes * sizeof(jack_default_audio_sample_t));
            memset(wetOutRightBuffer, 0, nframes * sizeof(jack_default_audio_sample_t));
        } else if (wetAmount == 1) {
            outputWet = false;
            memcpy(wetOutLeftBuffer, inputLeftBuffer, nframes * sizeof(jack_default_audio_sample_t));
            memcpy(wetOutRightBuffer, inputRightBuffer, nframes * sizeof(jack_default_audio_sample_t));
        }
        if (outputDry || outputWet) {
            const jack_default_audio_sample_t *portBuffer{nullptr};
            const jack_default_audio_sample_t *portBufferEnd{nullptr};

            portBuffer = inputLeftBuffer;
            portBufferEnd = inputLeftBuffer + nframes;
            for (const float* channelSample = portBuffer; channelSample < portBufferEnd; ++channelSample) {
                if (outputDry) {
                    *dryOutLeftBuffer = dryAmount * (*channelSample);
                    ++dryOutLeftBuffer;
                }
                if (outputWet) {
                    *wetOutLeftBuffer = wetAmount * (*channelSample);
                    ++wetOutLeftBuffer;
                }
            }
            portBuffer = inputRightBuffer;
            portBufferEnd = inputRightBuffer + nframes;
            for (const float* channelSample = portBuffer; channelSample < portBufferEnd; ++channelSample) {
                if (outputDry) {
                    *dryOutRightBuffer = dryAmount * (*channelSample);
                    ++dryOutRightBuffer;
                }
                if (outputWet) {
                    *wetOutRightBuffer = wetAmount * (*channelSample);
                    ++wetOutRightBuffer;
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
        wetOutLeft = jack_port_register(client, "wetOutLeft", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        wetOutRight = jack_port_register(client, "wetOutRight", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (inputLeft && inputRight && dryOutLeft && dryOutRight && wetOutLeft && wetOutRight) {
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

float JackPassthrough::wetAmount() const
{
    return d->wetAmount;
}

void JackPassthrough::setWetAmount(const float &newValue)
{
    if (d->wetAmount != newValue) {
        d->wetAmount = newValue;
        Q_EMIT wetAmountChanged();
    }
}

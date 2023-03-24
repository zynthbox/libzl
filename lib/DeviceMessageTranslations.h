#pragma once

#include <QDebug>
#include <QString>

#include <jack/midiport.h>

const QLatin1String device_identifier_presonus_atom_sq{"ATM SQ ATM SQ"};
jack_midi_event_t device_translations_cc_presonus_atom_sq[128];
jack_midi_event_t device_translations_cc_none[128];

namespace DeviceMessageTranslations {
    void load() {
        for (int i = 0; i < 128; ++i) {
            switch(i) {
                case 85:
                    device_translations_cc_presonus_atom_sq[85].size = 1;
                    device_translations_cc_presonus_atom_sq[85].buffer = new jack_midi_data_t[1]();
                    device_translations_cc_presonus_atom_sq[85].buffer[0] = 0xFC;
                    break;
                case 86:
                    device_translations_cc_presonus_atom_sq[86].size = 1;
                    device_translations_cc_presonus_atom_sq[86].buffer = new jack_midi_data_t[1]();
                    device_translations_cc_presonus_atom_sq[86].buffer[0] = 0xFA;
                    break;
                default:
                    device_translations_cc_none[i].size = 0;
                    device_translations_cc_presonus_atom_sq[i].size = 0;
                    break;
            }
        }
    }
    void apply(const QString &identifier, jack_midi_event_t **translations_cc) {
        if (identifier.endsWith(device_identifier_presonus_atom_sq)) {
            qDebug() << "ZLRouter: Identified device as Presonus Atom SQ main device, applying CC translations";
            *translations_cc = device_translations_cc_presonus_atom_sq;
        } else {
            *translations_cc = device_translations_cc_none;
        }
    }
}

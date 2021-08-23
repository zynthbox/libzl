
import QtQuick 2.0
import QtQuick.Layouts 1.3
import QtQuick.Controls 2.3 as Controls
import org.kde.kirigami 2.4 as Kirigami

import JuceGraphics 1.0

Controls.ApplicationWindow {
    width: 300
    height: 400
    visible: true

    WaveFormItem {
        anchors.fill: parent
        color: Kirigami.Theme.textColor
        source: "/home/diau/test.wav"
    }
}

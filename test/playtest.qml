
import QtQuick 2.0
import QtQuick.Layouts 1.3
import QtQuick.Controls 2.3 as Controls
import org.kde.kirigami 2.4 as Kirigami


Controls.ApplicationWindow {
    width: 300
    height: 400
    visible: true

    ColumnLayout {
        Controls.Button {
            text: "Play"
            onClicked: bridge.play()
        }
        Controls.Button {
            text: "Stop"
            onClicked: bridge.stop()
        }
    }
}

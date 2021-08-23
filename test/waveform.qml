
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
        id: wav
        anchors.fill: parent
        color: Kirigami.Theme.textColor
        source: "/home/diau/test.wav"
        PinchArea {
            anchors.fill: parent
            property real point1X
            property real point2x
            property real scale: 1
            onPinchStarted: {
                point1X = pinch.point1.x;
                point2x = pinch.point2.x;
            }
            onPinchUpdated: {
                let ratio = pinch.center.x / width;
                let newLength = wav.length / Math.max(1, scale + pinch.scale - 1);
                let remaining = wav.length - newLength;
                wav.start = remaining/(1-ratio);
                wav.end = newLength - remaining/(ratio);
            }
            onPinchFinished: {
                scale = pinch.scale
                print ("scale"+scale)
            }
            MouseArea {
                anchors.fill: parent
                property int lastX
                onPressed: {
                    lastX = mouse.x
                }
                onPositionChanged: {
                    let pixelToSecs = (wav.end - wav.start) / width
                    let delta = pixelToSecs * (mouse.x - lastX)
                    wav.start -= delta
                    wav.end -= delta
                    lastX = mouse.x
                }
            }
        }
    }
}

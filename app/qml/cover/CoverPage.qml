import QtQuick 2.2
import Sailfish.Silica 1.0
import de.ygriega.lauscher 1.0

CoverBackground {
    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingMedium

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            truncationMode: TruncationMode.Fade
            text: mdr.state === Mdr.Ready && mdr.deviceName.length > 0
                  ? mdr.deviceName : qsTr("Lauscher")
        }

        Repeater {
            model: mdr.batteries

            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: qsTr("%1 %2 %").arg(modelData.name).arg(modelData.level)
            }
        }
    }
}

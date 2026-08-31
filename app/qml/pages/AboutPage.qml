import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("About Lauscher") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Lauscher controls Sony headphones over the MDR protocol. "
                           + "The protocol implementation is libmdr from the SonyHeadphonesClient "
                           + "project; the Bluetooth transport is BlueZ's own Profile1 D-Bus API.")
            }

            SectionHeader { text: qsTr("Lauscher") }

            DetailItem { label: qsTr("Licence"); value: "GPLv3" }

            SectionHeader { text: qsTr("Credits") }

            DetailItem { label: qsTr("Protocol"); value: "libmdr / SonyHeadphonesClient" }
            DetailItem { label: qsTr("Licence"); value: "MIT" }
        }

        VerticalScrollDecorator {}
    }
}

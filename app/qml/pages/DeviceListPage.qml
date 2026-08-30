import QtQuick 2.2
import Sailfish.Silica 1.0
import de.ygriega.lauscher 1.0

Page {
    id: page

    // Coming back from the device page means we disconnected; make sure the
    // list reflects whatever BlueZ thinks is paired right now.
    onStatusChanged: {
        if (status === PageStatus.Activating)
            mdr.refreshPairedDevices()
    }

    SilicaListView {
        id: listView
        anchors.fill: parent

        PullDownMenu {
            MenuItem {
                text: qsTr("About")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
            MenuItem {
                text: qsTr("Refresh")
                onClicked: mdr.refreshPairedDevices()
            }
        }

        header: Column {
            width: listView.width

            PageHeader { title: qsTr("Lauscher") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Pick a paired Sony headset. Devices that do not speak the Sony protocol will simply fail to connect.")
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        model: mdr.pairedDevices

        delegate: ListItem {
            id: delegate
            contentHeight: Theme.itemSizeMedium

            Column {
                anchors {
                    left: parent.left
                    right: parent.right
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    text: modelData.name
                    color: delegate.highlighted ? Theme.highlightColor : Theme.primaryColor
                }

                Label {
                    width: parent.width
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: delegate.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: modelData.connected
                          ? qsTr("%1 · connected").arg(modelData.address)
                          : modelData.address
                }
            }

            onClicked: {
                mdr.connectToDevice(modelData.address)
                pageStack.push(Qt.resolvedUrl("DevicePage.qml"))
            }
        }

        ViewPlaceholder {
            enabled: listView.count === 0
            text: qsTr("No paired devices")
            hintText: qsTr("Pair your headphones in the Bluetooth settings first.")
        }

        VerticalScrollDecorator {}
    }
}

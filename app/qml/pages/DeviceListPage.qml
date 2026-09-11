/*
    Copyright (C) 2026 Sebastian J. Wolf and other contributors

    This file is part of Lauscher.

    Lauscher is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Lauscher is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Lauscher. If not, see <http://www.gnu.org/licenses/>.
*/

import QtQuick 2.2
import Sailfish.Silica 1.0
import de.ygriega.lauscher 1.0

Page {
    id: page

    // Silica defaults every page to Orientation.Portrait - _defaultPageOrientations
    // on ApplicationWindow - no matter what the window allows, so the window's
    // defaultAllowedOrientations alone rotates nothing. Page.allowedOrientations is
    // intersected with the window's, so All here means "whatever the device permits".
    allowedOrientations: Orientation.All

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
                text: qsTr("Pick a paired headset. Only headphones that speak the Sony protocol are listed, so anything else you have paired is missing on purpose.")
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
                    // A device that never told BlueZ a name is still worth
                    // listing; its address would say nothing to the reader.
                    text: modelData.name.length > 0
                          ? modelData.name : qsTr("Unnamed device")
                    color: delegate.highlighted ? Theme.highlightColor : Theme.primaryColor
                }

                Label {
                    width: parent.width
                    // The address identifies the device to BlueZ, not to the
                    // reader; only its state is worth a second line.
                    visible: modelData.connected
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: delegate.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: qsTr("Connected")
                }
            }

            onClicked: {
                mdr.connectToDevice(modelData.address)
                pageStack.push(Qt.resolvedUrl("DevicePage.qml"))
            }
        }

        ViewPlaceholder {
            enabled: listView.count === 0
            text: qsTr("No headphones")
            hintText: qsTr("Pair your Sony headphones in the Bluetooth settings first. If they are paired but missing here, connect them there once so the phone reads their services again.")
        }

        VerticalScrollDecorator {}
    }
}

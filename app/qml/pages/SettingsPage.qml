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

// The settings the headset keeps about itself rather than about the sound: the
// booleans it defines and names on its own - multipoint is one of them - and what
// its Bluetooth link is tuned for.
//
// Pushed from DevicePage, which stays connected while it is on the stack: the
// session is dropped on that page's destruction, not on it being deactivated.
Page {
    id: page

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge

        Column {
            id: content
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Settings")
                description: mdr.deviceName
            }

            // Everything here is read from the headset and written back to it, so
            // without a session there is nothing to show and nothing to change. The
            // page stays open - it was opened from a page that reconnects by itself.
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: mdr.state !== Mdr.Ready
                wrapMode: Text.WordWrap
                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("These are the headset's own settings. They are readable again once it is connected.")
            }

            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                enabled: mdr.state === Mdr.Ready
                opacity: enabled ? 1.0 : Theme.opacityLow

                /* ------------------------------------------ device settings */

                // No section header over these: each one is named by the device and
                // says what it is, and a heading above them on a page that already
                // carries the headset's name would only repeat it.
                //
                // The device decides what is in here, how many there are and what
                // they are called; all this page knows is that each one is a switch.
                // A headset that offers none - or only settings in a shape the
                // protocol library cannot read - simply has no section.
                Repeater {
                    id: settings
                    model: mdr.generalSettings

                    TextSwitch {
                        width: page.width
                        text: modelData.title
                        description: modelData.description
                        // Without this the first tap replaces the binding with a plain
                        // value and the switch stops following the headset.
                        automaticCheck: false
                        checked: modelData.value
                        onClicked: mdr.setGeneralSetting(modelData.index, !checked)
                    }
                }

                /* --------------------------------------- connection quality */

                SectionHeader {
                    text: qsTr("Bluetooth connection quality")
                    visible: mdr.connectionModeAvailable
                }

                ComboBox {
                    id: priorityCombo
                    visible: mdr.connectionModeAvailable
                    width: parent.width
                    label: qsTr("Priority")

                    // -1 for a device that has not said which it is on, the way the
                    // equalizer's preset picker handles a preset it cannot show:
                    // nothing selected says that better than pointing at an item.
                    function indexOfPriority(priority) {
                        return priority === Mdr.PriorityQuality ? 0
                             : priority === Mdr.PriorityStability ? 1 : -1
                    }

                    // Assigned, never bound: Silica writes currentIndex itself when the
                    // user picks an item, and a binding would not survive that first tap.
                    function syncCurrent() {
                        var idx = indexOfPriority(mdr.audioPriority)
                        if (currentIndex !== idx)
                            currentIndex = idx
                    }

                    menu: ContextMenu {
                        MenuItem { text: qsTr("Sound quality") }
                        MenuItem { text: qsTr("Stable connection") }
                    }

                    Component.onCompleted: syncCurrent()

                    Connections {
                        target: mdr
                        onConnectionModeChanged: priorityCombo.syncCurrent()
                    }

                    onCurrentIndexChanged: {
                        // Also the assignment above coming back around, and the case of
                        // a device that has said nothing: neither is a request.
                        if (currentIndex < 0)
                            return
                        var priority = currentIndex === 1 ? Mdr.PriorityStability
                                                          : Mdr.PriorityQuality
                        if (priority !== mdr.audioPriority)
                            mdr.setAudioPriority(priority)
                    }
                }

                // Both of these are settings the headset applies by dropping its
                // Bluetooth links and bringing them back up - the protocol has a
                // disconnection reason for each - so the app is about to lose the
                // control channel and say so. Worth warning about before the screen
                // does it, rather than leaving it to look like a fault.
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    visible: settings.count > 0 || mdr.connectionModeAvailable
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("Changing one of these makes the headset drop its Bluetooth connections for a moment, music included. Lauscher connects again by itself.")
                }
            }
        }

        VerticalScrollDecorator {}
    }
}

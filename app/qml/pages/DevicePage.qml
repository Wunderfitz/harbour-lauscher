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

    // Track names reach the headset over AVRCP from whatever is playing, so all
    // three are empty unless the phone is pushing metadata. Nothing to show then.
    readonly property bool hasTrackInfo: mdr.trackTitle.length > 0
                                         || mdr.trackArtist.length > 0
                                         || mdr.trackAlbum.length > 0

    // Leaving the page tears the RFCOMM channel down; the headset only allows
    // one control session at a time and Sound Connect on another phone would
    // otherwise be locked out.
    onStatusChanged: {
        if (status === PageStatus.Deactivating)
            mdr.disconnectDevice()
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Disconnect")
                onClicked: pageStack.pop()
            }
        }

        Column {
            id: content
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: mdr.deviceName.length > 0 ? mdr.deviceName : qsTr("Headphones")
                description: mdr.state === Mdr.Ready && mdr.firmwareVersion.length > 0
                             ? qsTr("Firmware %1").arg(mdr.firmwareVersion)
                             : mdr.statusMessage
            }

            /* ------------------------------------------------ busy / error */

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                size: BusyIndicatorSize.Large
                running: mdr.state === Mdr.Connecting || mdr.state === Mdr.Initializing
                visible: running
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: mdr.state === Mdr.Error
                wrapMode: Text.WordWrap
                color: Theme.errorColor
                text: mdr.statusMessage
            }

            /* ---------------------------------------------------- battery */

            SectionHeader {
                text: qsTr("Battery")
                visible: mdr.batteries.length > 0
            }

            Repeater {
                model: mdr.batteries

                Item {
                    width: page.width
                    height: Theme.itemSizeSmall
                    visible: mdr.batteries.length > 0

                    // A bud that is in the case reads 0 %, and so does one that is
                    // genuinely empty - the protocol carries nothing that tells the two
                    // apart. Rather than pick a reading, the row goes disabled: still
                    // there, plainly not a measurement. enabled propagates to both
                    // labels, so name and level dim together.
                    enabled: modelData.level > 0

                    Label {
                        anchors {
                            left: parent.left
                            leftMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        text: modelData.name
                        color: parent.enabled ? Theme.highlightColor
                                              : Theme.secondaryHighlightColor
                    }

                    Label {
                        anchors {
                            right: parent.right
                            rightMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        text: modelData.chargingComplete
                              ? qsTr("%1 % · charged").arg(modelData.level)
                              : modelData.charging
                                ? qsTr("%1 % · charging").arg(modelData.level)
                                : qsTr("%1 %").arg(modelData.level)
                        color: parent.enabled ? Theme.primaryColor
                                              : Theme.secondaryColor
                    }
                }
            }

            /* ---------------------------------------------------- playback */

            SectionHeader {
                text: qsTr("Playback")
                visible: mdr.volumeAvailable || mdr.playbackControlAvailable
            }

            Column {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: page.hasTrackInfo

                Label {
                    width: parent.width
                    visible: mdr.trackTitle.length > 0
                    text: mdr.trackTitle
                    color: Theme.highlightColor
                    truncationMode: TruncationMode.Fade
                }

                Label {
                    width: parent.width
                    visible: text.length > 0
                    text: mdr.trackArtist.length > 0 && mdr.trackAlbum.length > 0
                          ? qsTr("%1 · %2").arg(mdr.trackArtist).arg(mdr.trackAlbum)
                          : mdr.trackArtist.length > 0 ? mdr.trackArtist : mdr.trackAlbum
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    truncationMode: TruncationMode.Fade
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: mdr.playbackControlAvailable
                spacing: Theme.paddingLarge

                IconButton {
                    icon.source: "image://theme/icon-m-previous"
                    onClicked: mdr.previousTrack()
                }

                IconButton {
                    // The status comes from the source device, so the button shows
                    // what the phone is doing rather than what was last tapped.
                    icon.source: mdr.playbackStatus === Mdr.PlaybackPlaying
                                 ? "image://theme/icon-m-pause"
                                 : "image://theme/icon-m-play"
                    onClicked: {
                        if (mdr.playbackStatus === Mdr.PlaybackPlaying)
                            mdr.pause()
                        else
                            mdr.play()
                    }
                }

                IconButton {
                    icon.source: "image://theme/icon-m-next"
                    onClicked: mdr.nextTrack()
                }
            }

            Slider {
                id: volumeSlider
                visible: mdr.volumeAvailable
                width: parent.width
                minimumValue: 0
                maximumValue: 30
                stepSize: 1
                label: qsTr("Volume")
                valueText: value.toFixed(0)

                // 0..30 is the headset's own scale, not a percentage. Same reason as
                // the ComboBox for assigning rather than binding: dragging writes to
                // value, and the headset reports the volume the phone sets, so the
                // two would fight over the binding.
                Component.onCompleted: value = mdr.volume

                Connections {
                    target: mdr
                    onPlaybackChanged: {
                        if (!volumeSlider.pressed && volumeSlider.value !== mdr.volume)
                            volumeSlider.value = mdr.volume
                    }
                }

                // Sending on release rather than on every step: each change is a
                // command the device has to acknowledge before the next one goes out.
                onReleased: mdr.setVolume(value)
            }

            /* ------------------------------------------ ambient / NC modes */

            SectionHeader {
                text: qsTr("Ambient sound control")
                visible: mdr.noiseControlAvailable
            }

            ComboBox {
                id: noiseCombo
                visible: mdr.noiseControlAvailable
                width: parent.width
                label: qsTr("Mode")

                // currentIndex is assigned, never bound: Silica writes to it when
                // the user picks an item, which would destroy a binding and leave
                // the control deaf to later changes made on the headset itself.
                function indexOfMode(mode) {
                    return mode === Mdr.NoiseCancelling ? 1
                         : mode === Mdr.AmbientSound ? 2 : 0
                }

                menu: ContextMenu {
                    MenuItem { text: qsTr("Off") }
                    MenuItem { text: qsTr("Noise cancelling") }
                    MenuItem { text: qsTr("Ambient sound") }
                }

                Component.onCompleted: currentIndex = indexOfMode(mdr.noiseMode)

                Connections {
                    target: mdr
                    onNoiseControlChanged: {
                        var idx = noiseCombo.indexOfMode(mdr.noiseMode)
                        if (noiseCombo.currentIndex !== idx)
                            noiseCombo.currentIndex = idx
                    }
                }

                onCurrentIndexChanged: {
                    var mode = currentIndex === 1 ? Mdr.NoiseCancelling
                             : currentIndex === 2 ? Mdr.AmbientSound
                             : Mdr.NoiseOff
                    if (mode !== mdr.noiseMode)
                        mdr.setNoiseMode(mode)
                }
            }

            Slider {
                id: ambientSlider
                visible: mdr.noiseControlAvailable && mdr.ambientLevelAvailable
                         && mdr.noiseMode === Mdr.AmbientSound
                width: parent.width
                minimumValue: 0
                maximumValue: 20
                stepSize: 1
                label: qsTr("Ambient sound level")
                valueText: value.toFixed(0)

                // Same reason as the ComboBox: dragging writes to value.
                Component.onCompleted: value = mdr.ambientLevel

                Connections {
                    target: mdr
                    onNoiseControlChanged: {
                        if (!ambientSlider.pressed && ambientSlider.value !== mdr.ambientLevel)
                            ambientSlider.value = mdr.ambientLevel
                    }
                }

                onReleased: mdr.setAmbientLevel(value)
            }

            TextSwitch {
                visible: mdr.noiseControlAvailable && mdr.noiseMode === Mdr.AmbientSound
                text: qsTr("Focus on voice")
                checked: mdr.focusOnVoice
                onClicked: mdr.setFocusOnVoice(checked)
            }

            /* ------------------------------------------------ listening mode */

            SectionHeader {
                text: qsTr("Listening mode")
                visible: mdr.listeningModeAvailable
            }

            ComboBox {
                id: listeningCombo
                visible: mdr.listeningModeAvailable
                width: parent.width
                label: qsTr("Sound field")

                // A device advertises each mode on its own -- LinkBuds Clip has
                // background music, voice boost and sound leakage reduction but no
                // cinema -- so the ones it lacks are hidden rather than left out.
                // Silica numbers menu items whether or not they are visible, in both
                // ContextMenu._foreachMenuItem and ComboBoxController._updateCurrent,
                // so hiding one does not shift the indices of the rest. Building the
                // menu with a Repeater instead would: its items only appear once the
                // capabilities arrive, and the controller does not look them up again.
                property var modes: [Mdr.Standard, Mdr.BackgroundMusic, Mdr.Cinema,
                                     Mdr.VoiceBoost, Mdr.SoundLeakageReduction]

                function offers(mode) {
                    return mdr.listeningModes.indexOf(mode) >= 0
                }

                function indexOfMode(mode) {
                    return modes.indexOf(mode)
                }

                menu: ContextMenu {
                    MenuItem { text: qsTr("Standard") }
                    MenuItem {
                        text: qsTr("Ambient background music")
                        visible: listeningCombo.offers(Mdr.BackgroundMusic)
                    }
                    MenuItem {
                        text: qsTr("Cinema")
                        visible: listeningCombo.offers(Mdr.Cinema)
                    }
                    MenuItem {
                        text: qsTr("Voice boost")
                        visible: listeningCombo.offers(Mdr.VoiceBoost)
                    }
                    MenuItem {
                        text: qsTr("Sound leakage reduction")
                        visible: listeningCombo.offers(Mdr.SoundLeakageReduction)
                    }
                }

                Component.onCompleted: currentIndex = indexOfMode(mdr.listeningMode)

                Connections {
                    target: mdr
                    onListeningChanged: {
                        var idx = listeningCombo.indexOfMode(mdr.listeningMode)
                        if (listeningCombo.currentIndex !== idx)
                            listeningCombo.currentIndex = idx
                    }
                }

                onCurrentIndexChanged: {
                    if (currentIndex < 0 || currentIndex >= modes.length)
                        return
                    var mode = modes[currentIndex]
                    if (mode !== mdr.listeningMode)
                        mdr.setListeningMode(mode)
                }
            }

            ComboBox {
                id: roomCombo
                visible: mdr.backgroundRoomAvailable
                         && mdr.listeningMode === Mdr.BackgroundMusic
                width: parent.width
                label: qsTr("Distance")

                // Unlike the modes, MDR_ROOM_UNKNOWN has no item to sit on, so
                // syncing falls back to the first one. Writing that back out would
                // pick a distance the user never asked for, hence the guard.
                property bool syncing: false

                function indexOfRoom(room) {
                    return room === Mdr.RoomMedium ? 1
                         : room === Mdr.RoomLarge ? 2 : 0
                }

                menu: ContextMenu {
                    MenuItem { text: qsTr("My room") }
                    MenuItem { text: qsTr("Living room") }
                    MenuItem { text: qsTr("Cafe") }
                }

                Component.onCompleted: currentIndex = indexOfRoom(mdr.backgroundRoom)

                Connections {
                    target: mdr
                    onListeningChanged: {
                        var idx = roomCombo.indexOfRoom(mdr.backgroundRoom)
                        if (roomCombo.currentIndex === idx)
                            return
                        roomCombo.syncing = true
                        roomCombo.currentIndex = idx
                        roomCombo.syncing = false
                    }
                }

                onCurrentIndexChanged: {
                    if (syncing)
                        return
                    var room = currentIndex === 1 ? Mdr.RoomMedium
                             : currentIndex === 2 ? Mdr.RoomLarge
                             : Mdr.RoomSmall
                    if (room !== mdr.backgroundRoom)
                        mdr.setBackgroundRoom(room)
                }
            }

            /* ------------------------------------------------ device info */

            SectionHeader {
                text: qsTr("Device")
                visible: mdr.state === Mdr.Ready
            }

            DetailItem {
                visible: mdr.state === Mdr.Ready && mdr.codec.length > 0
                label: qsTr("Codec")
                value: mdr.codec
            }

            DetailItem {
                visible: mdr.state === Mdr.Ready && mdr.serialNumber.length > 0
                label: qsTr("Serial")
                value: mdr.serialNumber
            }

            DetailItem {
                visible: mdr.state === Mdr.Ready
                label: qsTr("MDR protocol")
                value: mdr.protocolVersion
            }
        }

        VerticalScrollDecorator {}
    }
}

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
import "../components"

CoverBackground {
    id: cover

    readonly property bool ready: mdr.state === Mdr.Ready

    // The distance only means anything while background music is the active mode,
    // which is also the only time the device accepts a new one.
    readonly property bool roomApplies: mdr.backgroundRoomAvailable
                                        && mdr.listeningMode === Mdr.BackgroundMusic

    // One mode is nothing to cycle through, and the picker is pointless before the
    // capability list has arrived.
    readonly property bool canPickMode: ready && mdr.listeningModeAvailable
                                        && mdr.listeningModes.length > 1
    readonly property bool canPickRoom: canPickMode && roomApplies

    // The headset counts 0..30, which says nothing at a glance. These are even
    // steps over that range, named rather than numbered.
    function volumeText(volume) {
        if (volume <= 0)
            return qsTr("off")
        if (volume <= 6)
            return qsTr("very quiet")
        if (volume <= 12)
            return qsTr("quiet")
        if (volume <= 18)
            return qsTr("moderate")
        if (volume <= 24)
            return qsTr("loud")
        if (volume <= 29)
            return qsTr("very loud")
        return qsTr("maximum")
    }

    function modeName(mode) {
        switch (mode) {
        case Mdr.BackgroundMusic: return qsTr("Ambient background music")
        case Mdr.Cinema: return qsTr("Cinema")
        case Mdr.VoiceBoost: return qsTr("Voice boost")
        case Mdr.SoundLeakageReduction: return qsTr("Sound leakage reduction")
        default: return qsTr("Standard")
        }
    }

    // Nothing tints a cover action's icon, so each one ships in both colours and
    // is picked the way the backdrop is.
    readonly property string inkSuffix: (Theme.colorScheme ? "black" : "white") + ".svg"

    function modeIcon(mode) {
        switch (mode) {
        case Mdr.BackgroundMusic: return "../../images/icon-cover-mode-background-music-" + inkSuffix
        case Mdr.Cinema: return "../../images/icon-cover-mode-cinema-" + inkSuffix
        case Mdr.VoiceBoost: return "../../images/icon-cover-mode-voice-boost-" + inkSuffix
        case Mdr.SoundLeakageReduction: return "../../images/icon-cover-mode-leakage-" + inkSuffix
        default: return "../../images/icon-cover-mode-standard-" + inkSuffix
        }
    }

    // MDR_ROOM_UNKNOWN has no name and no icon of its own; it falls in with the
    // nearest distance, the same way DevicePage's picker does.
    function roomName(room) {
        switch (room) {
        case Mdr.RoomMedium: return qsTr("Living room")
        case Mdr.RoomLarge: return qsTr("Cafe")
        default: return qsTr("My room")
        }
    }

    function roomIcon(room) {
        switch (room) {
        case Mdr.RoomMedium: return "../../images/icon-cover-room-medium-" + inkSuffix
        case Mdr.RoomLarge: return "../../images/icon-cover-room-large-" + inkSuffix
        default: return "../../images/icon-cover-room-small-" + inkSuffix
        }
    }

    // The cover has no room for a menu, so each action steps to the next option and
    // the icon shows where that landed. Only the modes this device advertises are in
    // the rotation.
    function nextMode() {
        var modes = mdr.listeningModes
        if (!modes || modes.length === 0)
            return
        var next = modes[(modes.indexOf(mdr.listeningMode) + 1) % modes.length]
        if (next !== mdr.listeningMode)
            mdr.setListeningMode(next)
    }

    function nextRoom() {
        var rooms = [Mdr.RoomSmall, Mdr.RoomMedium, Mdr.RoomLarge]
        // An unknown distance is not in the list, so this starts at the first one.
        mdr.setBackgroundRoom(rooms[(rooms.indexOf(mdr.backgroundRoom) + 1) % rooms.length])
    }

    BackgroundImage {
        id: backgroundImage
        width: parent.height - Theme.paddingLarge
        height: width
        sourceDimension: width
        anchors {
            verticalCenter: parent.verticalCenter
            centerIn: undefined
            bottom: parent.bottom
            bottomMargin: Theme.paddingMedium
            right: parent.right
            rightMargin: Theme.paddingMedium
        }
    }

    Column {
        id: content

        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: Theme.paddingLarge
        }
        spacing: Theme.paddingSmall

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeLarge
            color: Theme.highlightColor
            text: cover.ready && mdr.deviceName.length > 0 ? mdr.deviceName
                                                           : qsTr("Lauscher")
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.primaryColor
            visible: !cover.ready && text.length > 0
            text: mdr.statusMessage
        }

        Repeater {
            model: cover.ready ? mdr.batteries : 0

            Label {
                width: content.width
                horizontalAlignment: Text.AlignLeft
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.primaryColor
                text: qsTr("%1: %2 %").arg(modelData.name).arg(modelData.level)
            }
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignLeft
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.primaryColor
            visible: cover.ready && mdr.volumeAvailable
            text: qsTr("Volume: %1").arg(cover.volumeText(mdr.volume))
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.primaryColor
            visible: cover.ready && mdr.listeningModeAvailable
            text: cover.modeName(mdr.listeningMode)
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignLeft
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.primaryColor
            visible: cover.ready && cover.roomApplies
            text: qsTr("Distance: %1").arg(cover.roomName(mdr.backgroundRoom))
        }
    }

    // Two lists rather than one with a hidden action: lipstick takes the first
    // enabled list wholesale, so the distance appears only when it applies.
    CoverActionList {
        enabled: cover.canPickMode && !cover.canPickRoom

        CoverAction {
            iconSource: cover.modeIcon(mdr.listeningMode)
            onTriggered: cover.nextMode()
        }
    }

    CoverActionList {
        enabled: cover.canPickRoom

        CoverAction {
            iconSource: cover.modeIcon(mdr.listeningMode)
            onTriggered: cover.nextMode()
        }

        CoverAction {
            iconSource: cover.roomIcon(mdr.backgroundRoom)
            onTriggered: cover.nextRoom()
        }
    }
}

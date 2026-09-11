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

    // The same test for the ambient sound control, which is the whole of what a
    // closed-back headset offers here: it has no listening modes at all, so gating
    // the cover on those alone left it with no actions whatsoever.
    readonly property bool canPickNoise: ready && mdr.noiseControlAvailable
                                         && mdr.noiseModes.length > 1

    function modeName(mode) {
        switch (mode) {
        case Mdr.BackgroundMusic: return qsTr("Ambient background music")
        case Mdr.Cinema: return qsTr("Cinema")
        case Mdr.VoiceBoost: return qsTr("Voice boost")
        case Mdr.SoundLeakageReduction: return qsTr("Sound leakage reduction")
        default: return qsTr("Standard")
        }
    }

    // Off is spelled out rather than left as "Off": the cover shows the line with no
    // section header over it, so the state has to name the feature it belongs to.
    function noiseName(mode) {
        switch (mode) {
        case Mdr.NoiseCancelling: return qsTr("Noise cancelling")
        case Mdr.AmbientSound: return qsTr("Ambient sound")
        default: return qsTr("Ambient sound control off")
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

    function noiseIcon(mode) {
        switch (mode) {
        case Mdr.NoiseCancelling: return "../../images/icon-cover-noise-cancelling-" + inkSuffix
        case Mdr.AmbientSound: return "../../images/icon-cover-noise-ambient-" + inkSuffix
        default: return "../../images/icon-cover-noise-off-" + inkSuffix
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

    // Off, noise cancelling and ambient sound in the order the device advertised
    // them, so a headset with only one of the two halves still steps cleanly.
    function nextNoiseMode() {
        var modes = mdr.noiseModes
        if (!modes || modes.length === 0)
            return
        var next = modes[(modes.indexOf(mdr.noiseMode) + 1) % modes.length]
        if (next !== mdr.noiseMode)
            mdr.setNoiseMode(next)
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
                // 0 % is either a bud in the case or an empty one; nothing in the
                // protocol separates them, so the whole row goes disabled the way
                // DevicePage's does.
                enabled: modelData.level > 0
                color: enabled ? Theme.primaryColor : Theme.secondaryColor
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
            text: qsTr("Volume: %1 %").arg(mdr.volumePercent)
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.primaryColor
            visible: cover.ready && mdr.noiseControlAvailable
            text: cover.noiseName(mdr.noiseMode)
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

    /* One list per combination rather than one list with hidden actions: lipstick
     * takes an enabled list wholesale, so what a device does not have has to be
     * absent from the list it is offered rather than hidden inside it. The
     * conditions are mutually exclusive for the same reason - a device with both
     * halves is in exactly one of them.
     *
     * Two actions is the ceiling, which is what the priority below is about: a
     * headset with the listening modes, the ambient sound control and background
     * music playing has three things to step through and room for two. The
     * distance wins that, because it only applies while that one mode is active
     * and the other two are reachable for the rest of the session. */
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

    CoverActionList {
        enabled: cover.canPickMode && cover.canPickNoise && !cover.canPickRoom

        CoverAction {
            iconSource: cover.modeIcon(mdr.listeningMode)
            onTriggered: cover.nextMode()
        }

        CoverAction {
            iconSource: cover.noiseIcon(mdr.noiseMode)
            onTriggered: cover.nextNoiseMode()
        }
    }

    CoverActionList {
        enabled: cover.canPickMode && !cover.canPickNoise && !cover.canPickRoom

        CoverAction {
            iconSource: cover.modeIcon(mdr.listeningMode)
            onTriggered: cover.nextMode()
        }
    }

    // A closed-back headset lands here: noise cancelling and ambient sound, and no
    // listening modes to put in front of them.
    CoverActionList {
        enabled: cover.canPickNoise && !cover.canPickMode

        CoverAction {
            iconSource: cover.noiseIcon(mdr.noiseMode)
            onTriggered: cover.nextNoiseMode()
        }
    }
}

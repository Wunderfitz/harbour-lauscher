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

// One band of a graphic equalizer: a vertical track with a handle on it, which is
// what an equalizer has looked like since they had faders.
//
// Silica has no vertical slider and its horizontal one cannot be turned on its side:
// SliderBase lays the groove out along the item's width, drags on the X axis, and
// reserves a row of its own for a label and another for the value. Ten of those, one
// per band, would be a page and a half of scrolling and would still not show the
// curve. So this is built from Theme colours and geometry instead of wrapped around
// one, and it borrows the stock slider's colour language: primary at rest, highlight
// while the finger is on it.
Item {
    id: band

    property int value
    property int minimumValue: -6
    property int maximumValue: 6
    // How tall the track is. The page picks it, since every band has to agree.
    property real trackHeight: Theme.itemSizeHuge
    property alias label: frequencyLabel.text

    readonly property bool pressed: dragArea.pressed && !dragArea.rejected

    // The device takes the whole set of band steps in one frame, so the page sends on
    // release rather than on every step - as the volume slider does on DevicePage.
    signal released()

    readonly property int _range: Math.max(1, maximumValue - minimumValue)
    readonly property real _stepHeight: trackHeight / _range
    // Value to position: the top of the track is the maximum, as a fader reads.
    function _positionOf(step) {
        return trackHeight - (step - minimumValue) * _stepHeight
    }

    implicitWidth: Theme.itemSizeSmall
    implicitHeight: trackHeight + Theme.paddingSmall + frequencyLabel.height

    Item {
        id: track

        width: parent.width
        height: band.trackHeight

        // The groove the handle travels in.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.dp(4)
            height: parent.height
            radius: width / 2
            color: Theme.rgba(Theme.primaryColor, Theme.opacityLow)
        }

        // 0 dB. Without it a curve of small cuts looks like a curve of small boosts.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - Theme.paddingSmall
            height: Theme.dp(2)
            y: band._positionOf(0) - height / 2
            color: Theme.rgba(Theme.primaryColor, Theme.opacityFaint)
        }

        // What this band does to the sound, drawn from 0 dB rather than from the
        // bottom: the length is the boost or the cut, and which side it is on says
        // which. Zero draws nothing, which is the honest picture of a flat band.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.dp(4)
            radius: width / 2
            y: Math.min(band._positionOf(0), handle.y + handle.height / 2)
            height: Math.abs(band._positionOf(0) - (handle.y + handle.height / 2))
            color: band.pressed ? Theme.highlightColor : Theme.primaryColor
        }

        Rectangle {
            id: handle

            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.max(Theme.dp(24), parent.width * 0.6)
            height: Theme.paddingMedium
            radius: height / 2
            y: band._positionOf(band.value) - height / 2
            color: band.pressed ? Theme.highlightColor : Theme.primaryColor

            // Only when the device is the one moving it. Following a finger has to be
            // immediate, and an animation there reads as lag.
            Behavior on y {
                enabled: !band.pressed
                NumberAnimation { duration: 100; easing.type: Easing.InOutQuad }
            }
        }
    }

    Label {
        id: frequencyLabel

        anchors {
            top: track.bottom
            topMargin: Theme.paddingSmall
            horizontalCenter: parent.horizontalCenter
        }
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        truncationMode: TruncationMode.Fade
        font.pixelSize: Theme.fontSizeTiny
        color: band.pressed ? Theme.highlightColor : Theme.secondaryColor
    }

    MouseArea {
        id: dragArea

        anchors.fill: parent

        // The page scrolls the same way this drags, and whoever grabs first keeps the
        // gesture - so the band has to claim it up front or lose every drag to the
        // flickable. The cost is that the page cannot be scrolled by starting on the
        // strip, which is the usual bargain for a direct-manipulation control; the
        // rejection below keeps a sideways gesture from being charged for it.
        preventStealing: true

        property int pressValue
        property real pressX
        property real pressY
        // Set once a gesture has declared itself horizontal. Nothing this band does is
        // horizontal, so it is left alone for the rest of the press.
        property bool rejected

        onPressed: {
            pressValue = band.value
            pressX = mouse.x
            pressY = mouse.y
            rejected = false
        }

        // Relative, not absolute: the value moves by how far the finger moved rather
        // than to where it landed, so a tap changes nothing and a grab anywhere on the
        // band works. With ten of these side by side, a control that snapped to the
        // touch would rewrite a band every time the strip was brushed.
        onPositionChanged: {
            if (rejected)
                return
            var dx = mouse.x - pressX
            var dy = pressY - mouse.y
            if (Math.abs(dx) > Math.abs(dy) && Math.abs(dx) > Theme.startDragDistance) {
                rejected = true
                band.value = pressValue
                return
            }
            var step = pressValue + Math.round(dy / band._stepHeight)
            band.value = Math.max(band.minimumValue, Math.min(band.maximumValue, step))
        }

        onReleased: {
            if (!rejected && band.value !== pressValue)
                band.released()
        }

        // The grab going elsewhere leaves the value half-dragged and nothing to send it,
        // so put it back where the press found it.
        onCanceled: {
            if (!rejected)
                band.value = pressValue
            rejected = true
        }
    }
}

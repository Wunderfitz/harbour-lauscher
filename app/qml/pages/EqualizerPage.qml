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

// Everything the headset says its equalizer has: the preset it is on, the band
// steps it reports - five of them or ten, which is the device's choice, not ours -
// and clear bass, which only the five-band layout carries.
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
                title: qsTr("Equalizer")
                description: mdr.deviceName
            }

            // The button that opens this page is disabled while the device says the
            // equalizer is off, so this is the case of it being switched off while
            // the page is already open - a listening mode picked on the cover, or on
            // the headset itself.
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: !mdr.equalizerUsable
                wrapMode: Text.WordWrap
                color: Theme.errorColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("The headset has turned the equalizer off. It does that while a listening mode other than Standard is active.")
            }

            // Dimmed and deaf as a whole in that case: the device would ignore every
            // one of these, and a control that looks live but is not is worse than
            // one that plainly is not.
            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                enabled: mdr.equalizerUsable
                opacity: enabled ? 1.0 : Theme.opacityLow

                // The presets are the ones the device advertised, so the menu is built from
                // that list rather than from a fixed set of items with the absent ones hidden,
                // which is what DevicePage's pickers do.
                //
                // That is the shape the QML gotchas warn about, and this is why it holds here:
                // ComboBoxController resolves currentIndex against the menu items that exist at
                // the moment it is assigned - _updateCurrent() walks _contentColumn.children
                // every time - so a Repeater is only a problem when its items arrive after the
                // last assignment. On DevicePage they do: that page is built while the device
                // is still being read. This page cannot be opened before the equalizer is
                // known, and the assignment is repeated whenever the list changes underneath
                // it, so the controller always resolves against the items on screen.
                //
                // Silica opens the menu as a full page of its own once there are more than five
                // items, which is what makes a list of thirty - the fallback for a device that
                // advertised none - a reasonable thing to put in a ComboBox at all.
                ComboBox {
                    id: presetCombo
                    width: parent.width
                    label: qsTr("Preset")

                    function indexOfPreset(preset) {
                        for (var i = 0; i < mdr.equalizerPresets.length; ++i) {
                            if (mdr.equalizerPresets[i].preset === preset)
                                return i
                        }
                        // Nothing to select: the device is on a preset it did not advertise.
                        // -1 clears the selection rather than pointing at the wrong item.
                        return -1
                    }

                    // Assigned, never bound: Silica writes currentIndex itself when the user
                    // picks an item, and a binding would not survive that first tap.
                    function syncCurrent() {
                        var idx = indexOfPreset(mdr.equalizerPreset)
                        if (currentIndex !== idx)
                            currentIndex = idx
                    }

                    menu: ContextMenu {
                        Repeater {
                            model: mdr.equalizerPresets

                            MenuItem {
                                text: modelData.name
                            }

                            // The items the controller resolves against are these, so a list
                            // that changes under it has to be answered by assigning again.
                            onCountChanged: presetCombo.syncCurrent()
                        }
                    }

                    Component.onCompleted: syncCurrent()

                    Connections {
                        target: mdr
                        onEqualizerChanged: presetCombo.syncCurrent()
                        onEqualizerPresetsChanged: presetCombo.syncCurrent()
                    }

                    onCurrentIndexChanged: {
                        if (currentIndex < 0 || currentIndex >= mdr.equalizerPresets.length)
                            return
                        var preset = mdr.equalizerPresets[currentIndex].preset
                        if (preset !== mdr.equalizerPreset)
                            mdr.setEqualizerPreset(preset)
                    }
                }

                SectionHeader {
                    text: qsTr("Bands")
                    visible: mdr.equalizerBandCount > 0
                }

                // What the band in hand is doing, spelled out. Ten readouts standing
                // permanently under ten bands is noise; the curve is the display, and
                // this is for the one being changed. The height is held whether or not
                // there is anything to say, so the strip does not jump on a touch.
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    height: Theme.itemSizeExtraSmall
                    visible: mdr.equalizerBandCount > 0
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: Theme.highlightColor
                    text: bandStrip.readout
                }

                // The bands side by side, as an equalizer draws them. The Repeater is
                // modelled on the count rather than on the values: the values change on
                // every step, and a list model would take the bands down and build them
                // again underneath the finger dragging one.
                //
                // A band claims the vertical drag it needs (see EqualizerBand), so the
                // page cannot be scrolled from one. The page margins either side of the
                // strip are outside the bands and still scroll, which is what keeps a
                // tall strip from trapping the page.
                Row {
                    id: bandStrip

                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    visible: mdr.equalizerBandCount > 0

                    property string readout: ""

                    function describe(index, value) {
                        return qsTr("%1 · %2 dB").arg(mdr.equalizerBandFrequency(index))
                                                 .arg(value > 0 ? "+" + value : String(value))
                    }

                    Repeater {
                        model: mdr.equalizerBandCount

                        EqualizerBand {
                            id: bandItem

                            width: bandStrip.width / Math.max(1, mdr.equalizerBandCount)
                            // Room for a finger to travel: about twenty pixels a step on
                            // the ten-band layout, which is the one this device has.
                            trackHeight: Theme.itemSizeHuge * 1.5
                            minimumValue: mdr.equalizerBandMinimum
                            maximumValue: mdr.equalizerBandMaximum
                            label: mdr.equalizerBandLabel(index)

                            // Assigned, never bound: dragging writes to value, and a
                            // binding would not survive the first one.
                            Component.onCompleted: value = mdr.equalizerBands[index]

                            onPressedChanged: bandStrip.readout =
                                pressed ? bandStrip.describe(index, value) : ""
                            onValueChanged: if (pressed)
                                bandStrip.readout = bandStrip.describe(index, value)

                            Connections {
                                target: mdr
                                onEqualizerChanged: {
                                    if (bandItem.pressed
                                            || index >= mdr.equalizerBands.length
                                            || bandItem.value === mdr.equalizerBands[index])
                                        return
                                    bandItem.value = mdr.equalizerBands[index]
                                }
                            }

                            // On release rather than on every step: each change is a
                            // frame the device has to acknowledge before the next.
                            onReleased: mdr.setEqualizerBand(index, value)
                        }
                    }
                }

                // No section of its own: it is one more band in everything but name,
                // and only the five-band layout has room for it in its frames.
                Slider {
                    id: clearBassSlider
                    visible: mdr.clearBassAvailable
                    width: parent.width
                    minimumValue: mdr.clearBassMinimum
                    maximumValue: mdr.clearBassMaximum
                    stepSize: 1
                    label: qsTr("Clear bass")
                    valueText: qsTr("%1 dB").arg(value > 0 ? "+" + value.toFixed(0)
                                                           : value.toFixed(0))

                    Component.onCompleted: value = mdr.clearBass

                    Connections {
                        target: mdr
                        onEqualizerChanged: {
                            if (!clearBassSlider.pressed && clearBassSlider.value !== mdr.clearBass)
                                clearBassSlider.value = mdr.clearBass
                        }
                    }

                    onReleased: mdr.setClearBass(value)
                }
            }
        }

        VerticalScrollDecorator {}
    }
}

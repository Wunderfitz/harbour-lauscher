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
import "../components"

Page {
    id: aboutPage

    readonly property string sourcesUrl: "https://github.com/Wunderfitz/harbour-lauscher"
    readonly property string clientUrl: "https://github.com/mos9527/SonyHeadphonesClient"

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: aboutPage.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("About Lauscher") }

            // The app icon's own master, which is installed with the cover
            // artwork; drawn from the SVG so it stays sharp at this size.
            Image {
                id: logo
                anchors.horizontalCenter: parent.horizontalCenter
                source: "../../images/harbour-lauscher.svg"
                width: Math.min(2 * Theme.itemSizeHuge,
                                Math.min(aboutPage.width, aboutPage.height) / 2)
                height: width
                // Named rather than a sourceSize group block: inside one, a
                // bare width reads ambiguously, and rasterising the SVG at
                // zero would leave the page looking simply empty.
                sourceSize.width: logo.width
                sourceSize.height: logo.width
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                // Bump together with rpm/harbour-lauscher.spec.
                text: qsTr("Lauscher %1").arg("0.1")
                font.pixelSize: Theme.fontSizeExtraLarge
            }

            AboutParagraph {
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Sony headphone control for Sailfish OS")
            }

            Item { width: 1; height: Theme.paddingMedium }

            AboutParagraph {
                horizontalAlignment: Text.AlignHCenter
                color: Theme.primaryColor
                text: qsTr("By Sebastian J. Wolf and Claude")
            }

            AboutLink {
                url: "mailto:sebastian@ygriega.de"
                label: qsTr("Send e-mail")
            }

            Separator {
                width: parent.width
                color: Theme.primaryColor
                horizontalAlignment: Qt.AlignHCenter
            }

            AboutParagraph {
                horizontalAlignment: Text.AlignHCenter
                color: Theme.primaryColor
                text: qsTr("Licensed under GNU GPLv3 or later")
            }

            AboutLink {
                url: aboutPage.sourcesUrl
                label: qsTr("Sources on GitHub")
            }

            SectionHeader { text: qsTr("Good to know") }

            AboutParagraph {
                text: qsTr("Pair the headset in the Bluetooth settings first. Lauscher lists what is paired and connects on tap — it is a control channel, not an audio one, so music keeps playing over the usual route.")
            }

            AboutParagraph {
                text: qsTr("Only one app can hold that channel. Leaving the device page closes it on purpose, so Sony's own app — or another phone — can take over right away.")
            }

            AboutParagraph {
                text: qsTr("What you see is what the headset advertises. Two Sony models rarely offer the same set of features, and a section that is missing is one this device did not offer.")
            }

            AboutParagraph {
                text: qsTr("A battery level of 0 % is shown dimmed. The headset reports it both for an earbud sitting in its charging case and for an empty one, and says nothing that would tell the two apart.")
            }

            SectionHeader { text: qsTr("Devices") }

            AboutParagraph {
                text: qsTr("Developed against the Sony LinkBuds Clip. Other Sony headsets speaking the same protocol are expected to work, and the app shows whichever features they report. The older protocol of the WH-1000XM4 and its generation is built in but has not been tried.")
            }

            AboutParagraph {
                text: qsTr("Lauscher is an independent project. It is not affiliated with, endorsed or certified by Sony.")
            }

            SectionHeader { text: qsTr("Credits") }

            AboutParagraph {
                text: qsTr("The protocol is spoken by libmdr from the SonyHeadphonesClient project. Thanks for making it available under the conditions of the MIT License!")
            }

            AboutLink {
                url: aboutPage.clientUrl
                label: qsTr("Open SonyHeadphonesClient on GitHub")
            }

            AboutParagraph {
                text: qsTr("Formatting inside libmdr uses fmt. Copyright 2012 to the present day, Victor Zverovich and the fmt contributors. Thanks for making it available under the conditions of the MIT License!")
            }

            AboutLink {
                url: "https://github.com/fmtlib/fmt"
                label: qsTr("Open fmt on GitHub")
            }

            AboutParagraph {
                text: qsTr("The Bluetooth connection is set up by BlueZ, over the profile API bluetoothd already offers every app on the phone.")
            }

            AboutLink {
                url: "http://www.bluez.org"
                label: qsTr("Open the BlueZ website")
            }

            AboutParagraph {
                text: qsTr("Lauscher was written by its author together with Claude, Anthropic's assistant, in Claude Code. The commits it worked on name it as a co-author.")
            }

            AboutLink {
                url: "https://claude.com/claude-code"
                label: qsTr("Open the Claude Code website")
            }
        }

        VerticalScrollDecorator {}
    }
}

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

// A tappable link. Silica has no such element, so the markup, the highlight
// colour and the handler would otherwise be repeated at every link on
// AboutPage. Centred by default; set horizontalAlignment to change that.
Label {
    property string url
    property string label

    x: Theme.horizontalPageMargin
    width: parent.width - 2 * Theme.horizontalPageMargin
    horizontalAlignment: Text.AlignHCenter
    wrapMode: Text.WordWrap
    font.pixelSize: Theme.fontSizeSmall
    textFormat: Text.StyledText
    linkColor: Theme.highlightColor
    text: "<a href=\"" + url + "\">" + label + "</a>"
    onLinkActivated: Qt.openUrlExternally(link)
}

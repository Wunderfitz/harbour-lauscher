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

/*
 * The app's own artwork, faint, as a backdrop. Same component as
 * harbour-fernschreiber's: the caller sets sourceDimension and overrides the
 * anchors it wants.
 *
 * Theme.colorScheme is Theme.LightOnDark (0) on a dark ambience, so the pale
 * artwork belongs to the falsy case.
 */
Image {
    id: backgroundImage

    property int sourceDimension: Math.min(parent.width, parent.height) - Theme.paddingMedium

    asynchronous: true
    fillMode: Image.PreserveAspectFit
    width: sourceDimension
    height: sourceDimension
    opacity: 0.15
    source: "../../images/background-" + (Theme.colorScheme ? "black" : "white") + ".svg"

    sourceSize {
        width: sourceDimension
        height: sourceDimension
    }

    anchors {
        centerIn: parent
    }
}

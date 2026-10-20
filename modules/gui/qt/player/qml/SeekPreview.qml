/*****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
import QtQuick
import QtQuick.Window

import VLC.Style
import VLC.Util

/**
 * Preview of the frame under the pointer, shown above the seek bar.
 *
 * The card is reparented to the content item of the window so it is never
 * clipped by the control bar, and is placed above the time tool tip.
 * Nothing is shown for media without a frame to show (audio, network streams),
 * the seek bar then behaves exactly as before.
 */
Item {
    id: root

    // media to take the frames from, empty to disable the preview
    property url mediaUrl

    // position to preview, 0..1
    property real position: 0.0

    // whether the pointer is currently over (or dragging) the seek bar
    property bool active: false

    // the seek bar, and the x coordinate to point at, in its coordinates
    property Item anchorItem: null
    property real anchorX: 0.0

    // extra room to leave between the seek bar and the card, for the time tool tip
    property real clearance: VLCStyle.dp(64, VLCStyle.scale)

    readonly property bool hasFrame: (thumbnailer.source.toString() !== "")

    readonly property real _aspect: (thumbnailer.aspectRatio > 0) ? thumbnailer.aspectRatio : (16 / 9)
    readonly property real _imageHeight: (_aspect >= 1) ? VLCStyle.dp(120, VLCStyle.scale)
                                                         : VLCStyle.dp(168, VLCStyle.scale)
    readonly property real _imageWidth: Math.min(_imageHeight * _aspect, VLCStyle.dp(260, VLCStyle.scale))
    readonly property real _margin: VLCStyle.margin_xsmall
    readonly property real _edge: VLCStyle.margin_small

    // where the seek bar is, in the coordinates of the window
    readonly property point _barPoint: {
        // not tracked by the binding, but the pointer moves all the time while we are shown
        anchorX

        if (anchorItem && root.parent)
            return anchorItem.mapToItem(root.parent, anchorX, 0)
        return Qt.point(0, 0)
    }

    parent: Window.contentItem
    z: 100

    implicitWidth: _imageWidth + 2 * _margin
    implicitHeight: _imageHeight + 2 * _margin
    width: implicitWidth
    height: implicitHeight

    x: {
        const available = root.parent ? root.parent.width : 0
        return Helpers.clamp(_barPoint.x - width / 2, _edge, Math.max(_edge, available - width - _edge))
    }
    y: _barPoint.y - clearance - height

    opacity: (active && hasFrame) ? 1.0 : 0.0
    visible: opacity > 0

    Behavior on opacity {
        NumberAnimation { duration: VLCStyle.duration_short }
    }

    // the card must never take the input meant for what is under it
    enabled: false

    SeekThumbnailer {
        id: thumbnailer

        mediaUrl: root.mediaUrl

        // the frame is generated at the size it is displayed at
        thumbnailHeight: Math.round(root._imageHeight * Math.max(1, Screen.devicePixelRatio))
    }

    onPositionChanged: {
        if (active)
            thumbnailer.requestPosition(position)
    }

    onActiveChanged: {
        if (active)
            thumbnailer.requestPosition(position)
        else
            thumbnailer.cancel()
    }

    readonly property ColorContext colorContext: ColorContext {
        id: theme
        colorSet: ColorContext.Tooltip
    }

    Rectangle {
        anchors.fill: parent

        radius: VLCStyle.dp(10, VLCStyle.scale)
        color: theme.bg.primary
        border.color: theme.border
        border.width: VLCStyle.dp(1, VLCStyle.scale)
    }

    Image {
        anchors.fill: parent
        anchors.margins: root._margin

        source: thumbnailer.source

        // frames are one-shot images served by a provider, the url changes for each one
        cache: false
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
}

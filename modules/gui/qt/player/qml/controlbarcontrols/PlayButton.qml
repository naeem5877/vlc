/*****************************************************************************
 * Copyright (C) 2021 VLC authors and VideoLAN
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
import QtQuick.Templates as T


import VLC.MainInterface
import VLC.Widgets as Widgets
import VLC.Style
import VLC.Player
import VLC.Playlist
import VLC.Util

T.Control {
    id: root

    // Properties

    property bool paintOnly: false

    readonly property bool _showsPause: !paintOnly
                                        && Player.playingState !== Player.PLAYING_STATE_PAUSED
                                        && Player.playingState !== Player.PLAYING_STATE_STOPPED

    readonly property ColorContext colorContext: ColorContext {
        id: theme

        colorSet: ColorContext.ToolButton

        enabled: root.enabled || root.paintOnly
        focused: root.activeFocus
        hovered: root.cursorInside
        pressed: mouseArea.containsPress
    }

    property bool _keyOkPressed: false

    // Aliases

    property alias cursorInside: mouseArea.cursorInside

    // Settings

    implicitWidth: implicitBackgroundWidth + leftInset + rightInset
    implicitHeight: implicitBackgroundHeight + topInset + bottomInset

    scale: (_keyOkPressed || (mouseArea.pressed && cursorInside)) ? 0.95
                                                                  : 1.00

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Play/Pause")
    AccessibleCompat.id: "playButton"
    Accessible.checkable: true
    Accessible.checked: Player.playingState !== Player.PLAYING_STATE_PAUSED
                        && Player.playingState !== Player.PLAYING_STATE_STOPPED
    Accessible.onPressAction: MainPlaylistController.togglePlayPause()
    Accessible.onToggleAction: MainPlaylistController.togglePlayPause()

    // Tooltip

    T.ToolTip.visible: (hovered || visualFocus)
    T.ToolTip.delay: VLCStyle.delayToolTipAppear
    T.ToolTip.text: root.Accessible.name

    // Keys

    Keys.onPressed: (event) => {
        if (KeyHelper.matchOk(event) ) {
            if (!event.isAutoRepeat) {
                _keyOkPressed = true
                keyHoldTimer.restart()
            }
            event.accepted = true
        }
        Navigation.defaultKeyAction(event)
    }

    Keys.onReleased: (event) => {
        if (KeyHelper.matchOk(event)) {
            if (!event.isAutoRepeat) {
                _keyOkPressed = false
                keyHoldTimer.stop()
                if (Player.playingState !== Player.PLAYING_STATE_STOPPED)
                    MainPlaylistController.togglePlayPause()
            }
            event.accepted = true
        }
    }

    // Functions

    function _pressAndHoldAction() {
        _keyOkPressed = false
        MainPlaylistController.stop()
    }

    // Children

    Timer {
        id: keyHoldTimer

        interval: mouseArea.pressAndHoldInterval
        repeat: false

        Component.onCompleted: {
            triggered.connect(_pressAndHoldAction)
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent

        hoverEnabled: true

        readonly property bool cursorInside: containsMouse

        onPressed: (mouse) => {
            root.forceActiveFocus(Qt.MouseFocusReason)
        }

        onClicked: (mouse) => {
            MainPlaylistController.togglePlayPause()
            mouse.accepted = true
        }

        onPressAndHold: (mouse) => {
            _pressAndHoldAction()
            mouse.accepted = true
        }
    }

    // Design: 54x28 rounded rectangle, orange while paused, grey while playing

    contentItem: Image {
        readonly property real iconSize: VLCStyle.dp(24, VLCStyle.scale)

        fillMode: Image.PreserveAspectFit
        smooth: true

        sourceSize: Qt.size(iconSize * Screen.devicePixelRatio,
                            iconSize * Screen.devicePixelRatio)

        source: SVGColorImage.colorize("qrc:///figma/" + (root._showsPause ? "pause" : "play_filled") + ".svg")
                    .any({ "#8F8F8F": "#161616" })
                    .uri()

        Accessible.ignored: true
    }

    background: Item {
        implicitWidth: VLCStyle.dp(54, VLCStyle.scale)
        implicitHeight: VLCStyle.dp(28, VLCStyle.scale)

        Widgets.AnimatedBackground {
            anchors.fill: parent
            anchors.margins: -border.width * 4

            radius: VLCStyle.dp(8, VLCStyle.scale)
            border.pixelAligned: false

            enabled: theme.initialized

            border.color: root.visualFocus ? theme.visualFocus : Qt.alpha(theme.visualFocus, 0.0)
        }

        Rectangle {
            anchors.fill: parent

            radius: VLCStyle.dp(6, VLCStyle.scale)

            color: {
                const base = root._showsPause ? "#6B6B6B" : theme.accent
                return root.cursorInside ? Qt.lighter(base, 1.12) : base
            }

            Behavior on color {
                ColorAnimation {
                    duration: VLCStyle.duration_veryShort
                    easing.type: Easing.InOutSine
                }
            }
        }
    }
}


/*****************************************************************************
 * Copyright (C) 2023 VLC authors and VideoLAN
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

import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import QtQml.Models


import VLC.MainInterface
import VLC.Style
import VLC.Player
import VLC.Widgets as Widgets
import VLC.Util
import VLC.Dialogs

// Figma design: floating panel with Video / Audio / Subtitles tabs
ColumnLayout {
    id: root

    readonly property ColorContext colorContext: ColorContext {
        id: theme

        colorSet: ColorContext.View
    }

    //FIXME make TrackMenuController a proper type (see TrackMenu)
    required property QtObject trackMenuController

    readonly property real preferredWidth: VLCStyle.dp(400, VLCStyle.scale)

    // 0: video, 1: audio, 2: subtitles
    property int currentTab: 2

    readonly property color headerColor: theme.palette.isDark ? "#090909" : theme.bg.secondary

    spacing: 0

    focus: true

    // ---- tabs
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: VLCStyle.dp(50, VLCStyle.scale)

        Rectangle {
            anchors.fill: parent
            radius: VLCStyle.dp(10, VLCStyle.scale)
            color: root.headerColor
        }

        // square the bottom corners of the header
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: VLCStyle.dp(10, VLCStyle.scale)
            color: root.headerColor
        }

        Row {
            anchors.fill: parent

            Repeater {
                model: [
                    { "label": qsTr("Video"),     "icon": "tab_video" },
                    { "label": qsTr("Audio"),     "icon": "audio" },
                    { "label": qsTr("Subtitles"), "icon": "subtitles" }
                ]

                delegate: Item {
                    id: tab

                    required property int index
                    required property var modelData

                    readonly property bool active: root.currentTab === index

                    width: parent.width / 3
                    height: parent.height

                    Accessible.role: Accessible.PageTab
                    Accessible.name: modelData.label

                    Row {
                        anchors.centerIn: parent
                        spacing: VLCStyle.dp(8, VLCStyle.scale)

                        Image {
                            anchors.verticalCenter: parent.verticalCenter

                            width: VLCStyle.dp(16, VLCStyle.scale)
                            height: width

                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            sourceSize: Qt.size(width * Screen.devicePixelRatio,
                                                height * Screen.devicePixelRatio)

                            source: SVGColorImage.colorize("qrc:///figma/" + tab.modelData.icon + (tab.active ? "_active" : "") + ".svg")
                                        .any({
                                            "#8F8F8F": tabHover.hovered ? Qt.lighter(theme.fg.primary, 1.2) : theme.fg.primary,
                                            "#D37918": theme.accent,
                                            "#AB6314": Qt.alpha(theme.accent, 0.7)
                                        }).uri()
                        }

                        T.Label {
                            anchors.verticalCenter: parent.verticalCenter

                            text: tab.modelData.label
                            color: tab.active ? theme.fg.secondary : theme.fg.primary
                            font.pixelSize: VLCStyle.fontSize_small
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: VLCStyle.dp(8, VLCStyle.scale)
                        anchors.rightMargin: VLCStyle.dp(8, VLCStyle.scale)
                        height: VLCStyle.dp(2, VLCStyle.scale)
                        color: theme.accent
                        visible: tab.active
                    }

                    HoverHandler {
                        id: tabHover
                    }

                    TapHandler {
                        onTapped: root.currentTab = tab.index
                    }
                }
            }
        }
    }

    // ---- panes
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

        TrackPane {
            anchors.fill: parent
            visible: root.currentTab === 0

            title: qsTr("Video Tracks")
            tracksModel: Player.videoTracks

            Widgets.IconToolButton {
                text: VLCIcons.add
                figmaIcon: "plus"
                description: qsTr("Add")
                onClicked: DialogsProvider.loadVideoFile()
            }
        }

        TrackPane {
            anchors.fill: parent
            visible: root.currentTab === 1

            title: qsTr("Embedded Tracks")
            tracksModel: Player.audioTracks

            Widgets.IconToolButton {
                text: VLCIcons.expand
                figmaIcon: "more"
                description: qsTr("Menu")
                onClicked: menuAudio.popup(mapToGlobal(0, height))
            }
        }

        TrackPane {
            anchors.fill: parent
            visible: root.currentTab === 2

            title: qsTr("Embedded Tracks")
            tracksModel: Player.subtitleTracks

            Widgets.IconToolButton {
                text: VLCIcons.add
                figmaIcon: "plus"
                description: qsTr("Add")
                onClicked: DialogsProvider.showAddSubtitlesDialog()
            }

            Widgets.IconToolButton {
                text: VLCIcons.expand
                figmaIcon: "more"
                description: qsTr("Menu")
                onClicked: menuSubtitle.popup(mapToGlobal(0, height))
            }
        }
    }

    // ---- footer
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: VLCStyle.dp(12, VLCStyle.scale)
        spacing: VLCStyle.dp(8, VLCStyle.scale)

        Widgets.ButtonExt {
            id: playbackBtn

            text: qsTr("%1x").arg(+Player.rate.toFixed(2))

            onClicked: root.trackMenuController.requestPlaybackSpeedPage()

            T.ToolTip.visible: (hovered || visualFocus)
            T.ToolTip.text: qsTr("Playback Speed")
            T.ToolTip.delay: VLCStyle.delayToolTipAppear
        }

        Widgets.ButtonExt {
            Layout.fillWidth: true

            text: qsTr("Show Preferences")

            onClicked: DialogsProvider.prefsDialog()
        }
    }

    // ---- one tab: title + header buttons + checkable track list
    component TrackPane : ColumnLayout {
        id: pane

        required property string title
        required property var tracksModel

        default property alias headerButtons: buttonRow.data

        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: VLCStyle.dp(36, VLCStyle.scale)
            Layout.leftMargin: VLCStyle.dp(16, VLCStyle.scale)
            Layout.rightMargin: VLCStyle.dp(8, VLCStyle.scale)
            Layout.topMargin: VLCStyle.dp(8, VLCStyle.scale)

            T.Label {
                Layout.fillWidth: true

                text: pane.title
                color: Qt.alpha(theme.fg.primary, 0.6)
                font.pixelSize: VLCStyle.fontSize_small
                elide: Text.ElideRight
            }

            Row {
                id: buttonRow

                spacing: VLCStyle.dp(4, VLCStyle.scale)
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            clip: true
            model: pane.tracksModel
            boundsBehavior: Flickable.StopAtBounds

            Accessible.role: Accessible.List
            Accessible.name: qsTr("Track list")

            delegate: T.ItemDelegate {
                id: row

                width: ListView.view ? ListView.view.width : 0
                height: VLCStyle.dp(34, VLCStyle.scale)

                leftPadding: VLCStyle.dp(16, VLCStyle.scale)
                rightPadding: VLCStyle.dp(16, VLCStyle.scale)

                hoverEnabled: true

                background: Rectangle {
                    color: row.hovered ? Qt.alpha("white", 0.06) : "transparent"
                }

                contentItem: RowLayout {
                    spacing: VLCStyle.dp(12, VLCStyle.scale)

                    Item {
                        Layout.preferredWidth: VLCStyle.dp(18, VLCStyle.scale)
                        Layout.preferredHeight: VLCStyle.dp(18, VLCStyle.scale)

                        Image {
                            anchors.centerIn: parent

                            width: VLCStyle.dp(16, VLCStyle.scale)
                            height: width

                            visible: model.checked

                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            sourceSize: Qt.size(width * Screen.devicePixelRatio,
                                                height * Screen.devicePixelRatio)

                            source: "qrc:///figma/m_check.svg"
                        }
                    }

                    T.Label {
                        Layout.fillWidth: true

                        text: model.display
                        color: model.checked ? theme.fg.secondary : theme.fg.primary
                        font.pixelSize: VLCStyle.fontSize_normal
                        elide: Text.ElideRight
                    }
                }

                onClicked: model.checked = !model.checked
            }
        }
    }

    QmlSubtitleMenu {
        id: menuSubtitle

        player: Player
        ctx: MainCtx

        onTriggered: {
            if (action === QmlSubtitleMenu.Open) {
                DialogsProvider.loadSubtitlesFile()
            }
            else if (action === QmlSubtitleMenu.Synchronize) {
                trackMenuController.requestSubtitlePage()
            }
            else if (action === QmlSubtitleMenu.Download) {
                // OpenSubtitles without an account / API key
                DialogsProvider.showAddSubtitlesDialog()
            }
        }
    }

    QmlAudioMenu {
        id: menuAudio

        ctx: MainCtx

        onTriggered: {
            if (action === QmlSubtitleMenu.Open) {
                DialogsProvider.loadAudioFile()
            }
            else if (action === QmlSubtitleMenu.Synchronize) {
                trackMenuController.requestAudioPage()
            }
        }
    }
}

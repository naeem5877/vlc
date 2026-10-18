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
import QtQuick.Templates as T
import QtQuick.Layouts

import VLC.MainInterface
import VLC.Player
import VLC.Style
import VLC.Util
import VLC.Widgets as Widgets
import VLC.Dialogs

// Figma design: "Add Subtitles to Playlist/Folder"
//
//   To This File | To This Folder      From Computer | From OpenSubtitles
//
//   - folder + computer : every video is paired with the subtitle of the same episode
//   - file   + computer : pick one or several subtitle files of a folder
//   - file   + online   : search OpenSubtitles (no API key needed) and pick the results
ModalDialog {
    id: root

    // ---- state

    // false: "To This File", true: "To This Folder"
    property bool toFolder: false
    // false: "From Computer", true: "From OpenSubtitles"
    property bool online: false

    // online search is only available for a single file
    readonly property bool useOnline: online && !toFolder

    property string folder: ""
    property string query: ""
    property string languageCode: "eng"

    // [{ video, videoPath, subtitle, subtitlePath }]
    property var matches: []

    readonly property string mediaName: {
        const u = Player.url.toString()
        if (u.startsWith("file:"))
            return decodeURIComponent(u.substring(u.lastIndexOf("/") + 1))
        return Player.name
    }

    readonly property var languages: [
        { "label": qsTr("All languages"), "code": "" },
        { "label": qsTr("English"), "code": "eng" },
        { "label": qsTr("Spanish"), "code": "spa" },
        { "label": qsTr("French"), "code": "fre" },
        { "label": qsTr("German"), "code": "ger" },
        { "label": qsTr("Italian"), "code": "ita" },
        { "label": qsTr("Portuguese"), "code": "por" },
        { "label": qsTr("Portuguese (Brazil)"), "code": "pob" },
        { "label": qsTr("Dutch"), "code": "dut" },
        { "label": qsTr("Russian"), "code": "rus" },
        { "label": qsTr("Arabic"), "code": "ara" },
        { "label": qsTr("Bengali"), "code": "ben" },
        { "label": qsTr("Hindi"), "code": "hin" },
        { "label": qsTr("Urdu"), "code": "urd" },
        { "label": qsTr("Turkish"), "code": "tur" },
        { "label": qsTr("Persian"), "code": "per" },
        { "label": qsTr("Hebrew"), "code": "heb" },
        { "label": qsTr("Greek"), "code": "gre" },
        { "label": qsTr("Polish"), "code": "pol" },
        { "label": qsTr("Czech"), "code": "cze" },
        { "label": qsTr("Romanian"), "code": "rum" },
        { "label": qsTr("Bulgarian"), "code": "bul" },
        { "label": qsTr("Serbian"), "code": "srp" },
        { "label": qsTr("Croatian"), "code": "hrv" },
        { "label": qsTr("Hungarian"), "code": "hun" },
        { "label": qsTr("Swedish"), "code": "swe" },
        { "label": qsTr("Norwegian"), "code": "nor" },
        { "label": qsTr("Danish"), "code": "dan" },
        { "label": qsTr("Finnish"), "code": "fin" },
        { "label": qsTr("Indonesian"), "code": "ind" },
        { "label": qsTr("Thai"), "code": "tha" },
        { "label": qsTr("Vietnamese"), "code": "vie" },
        { "label": qsTr("Japanese"), "code": "jpn" },
        { "label": qsTr("Korean"), "code": "kor" },
        { "label": qsTr("Chinese"), "code": "chi" },
        { "label": qsTr("Tamil"), "code": "tam" },
        { "label": qsTr("Malayalam"), "code": "mal" }
    ]

    readonly property real rowHeight: VLCStyle.dp(30, VLCStyle.scale)
    readonly property real controlHeight: VLCStyle.dp(30, VLCStyle.scale)
    readonly property color fieldColor: Qt.alpha(colorContext.fg.primary, 0.07)

    readonly property bool canSave: {
        if (toFolder) {
            for (const m of matches) {
                if (m.subtitlePath)
                    return true
            }
            return false
        }
        if (useOnline)
            return finder.selectedCount > 0 && !finder.loading
        return localSelectedCount > 0
    }

    property int localSelectedCount: 0

    // ---- functions

    function reload() {
        localModel.clear()
        matches = []
        localSelectedCount = 0

        if (!folder)
            return

        if (toFolder) {
            matches = finder.matchFolder(folder)
        } else {
            const subs = finder.subtitleFilesIn(folder)
            for (const s of subs)
                localModel.append({ "fileName": s.name, "path": s.path, "selected": false })
        }
    }

    function search() {
        finder.search(query, languageCode)
    }

    function chooseFolder() {
        const f = finder.chooseFolder(folder)
        if (f) {
            folder = f
            reload()
        }
    }

    function countLocalSelected() {
        let n = 0
        for (let i = 0; i < localModel.count; ++i) {
            if (localModel.get(i).selected)
                ++n
        }
        localSelectedCount = n
    }

    function save() {
        if (toFolder) {
            // copies the subtitles next to their video, the playing video gets its subtitle now
            const current = finder.applyMatches(matches, Player.url)
            if (current.toString() !== "")
                Player.associateSubtitleFile(current.toString())
            close()
        } else if (!useOnline) {
            for (let i = 0; i < localModel.count; ++i) {
                const item = localModel.get(i)
                if (!item.selected)
                    continue
                const url = finder.prepareLocalSubtitle(item.path, Player.url, "")
                Player.associateSubtitleFile(url.toString())
            }
            close()
        } else {
            // the dialog is closed by onDownloaded
            finder.downloadSelected(Player.url)
        }
    }

    // ---- setup

    width: VLCStyle.dp(640, VLCStyle.scale)

    padding: 0
    margins: VLCStyle.margin_large

    closePolicy: Popup.CloseOnEscape

    onAboutToShow: {
        folder = finder.folderOf(Player.url)
        query = finder.suggestedQuery(mediaName)
        // a stream has no folder: online is the only useful source
        online = (folder === "")
        reload()
        if (online)
            search()
    }

    onClosed: finder.clear()

    SubtitleFinder {
        id: finder

        onDownloaded: (fileUrls) => {
            for (const url of fileUrls)
                Player.associateSubtitleFile(url)
            root.close()
        }
    }

    ListModel {
        id: localModel
    }

    // ---- look

    background: Rectangle {
        radius: VLCStyle.dp(6, VLCStyle.scale)
        color: root.colorContext.bg.primary
        border.width: 1
        border.color: Qt.alpha(root.colorContext.fg.primary, 0.12)
    }

    header: Item {
        implicitHeight: VLCStyle.dp(40, VLCStyle.scale)

        T.Label {
            anchors.left: parent.left
            anchors.leftMargin: VLCStyle.dp(16, VLCStyle.scale)
            anchors.verticalCenter: parent.verticalCenter

            text: qsTr("Add Subtitles")
            color: root.colorContext.fg.primary
            font.pixelSize: VLCStyle.fontSize_small
        }

        Item {
            id: closeButton

            anchors.right: parent.right
            anchors.rightMargin: VLCStyle.dp(10, VLCStyle.scale)
            anchors.verticalCenter: parent.verticalCenter

            width: VLCStyle.dp(24, VLCStyle.scale)
            height: width

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Close")

            Image {
                anchors.centerIn: parent

                width: VLCStyle.dp(16, VLCStyle.scale)
                height: width

                opacity: closeHover.hovered ? 1.0 : 0.7
                fillMode: Image.PreserveAspectFit
                sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                source: "qrc:///figma/close.svg"
            }

            HoverHandler {
                id: closeHover
            }

            TapHandler {
                onTapped: root.close()
            }
        }
    }

    contentItem: ColumnLayout {
        implicitWidth: VLCStyle.dp(640, VLCStyle.scale)
        implicitHeight: VLCStyle.dp(380, VLCStyle.scale)

        spacing: VLCStyle.dp(12, VLCStyle.scale)

        // ---- filters
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: VLCStyle.dp(16, VLCStyle.scale)
            Layout.rightMargin: VLCStyle.dp(16, VLCStyle.scale)

            spacing: VLCStyle.dp(8, VLCStyle.scale)

            Widgets.ComboBoxExt {
                Layout.preferredWidth: VLCStyle.dp(130, VLCStyle.scale)
                Layout.preferredHeight: root.controlHeight

                font.pixelSize: VLCStyle.fontSize_small
                bgColor: root.fieldColor

                model: [qsTr("To This File"), qsTr("To This Folder")]
                currentIndex: root.toFolder ? 1 : 0

                Accessible.name: qsTr("Target")

                onActivated: (index) => {
                    root.toFolder = (index === 1)
                    root.reload()
                    if (root.useOnline && finder.count === 0)
                        root.search()
                }
            }

            Widgets.ComboBoxExt {
                Layout.preferredWidth: VLCStyle.dp(150, VLCStyle.scale)
                Layout.preferredHeight: root.controlHeight

                font.pixelSize: VLCStyle.fontSize_small
                bgColor: root.fieldColor

                // a whole folder can only be filled from local files
                enabled: !root.toFolder

                model: [qsTr("From Computer"), qsTr("From OpenSubtitles")]
                currentIndex: root.useOnline ? 1 : 0

                Accessible.name: qsTr("Source")

                onActivated: (index) => {
                    root.online = (index === 1)
                    if (root.online) {
                        if (finder.count === 0)
                            root.search()
                    } else {
                        root.reload()
                    }
                }
            }

            // folder (computer)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.controlHeight

                visible: !root.useOnline

                radius: VLCStyle.dp(2, VLCStyle.scale)
                color: root.fieldColor

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Choose a folder")

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: VLCStyle.dp(8, VLCStyle.scale)
                    anchors.rightMargin: VLCStyle.dp(8, VLCStyle.scale)

                    spacing: VLCStyle.dp(8, VLCStyle.scale)

                    Image {
                        Layout.preferredWidth: VLCStyle.dp(16, VLCStyle.scale)
                        Layout.preferredHeight: VLCStyle.dp(16, VLCStyle.scale)

                        fillMode: Image.PreserveAspectFit
                        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                        source: "qrc:///figma/folder.svg"
                    }

                    T.Label {
                        Layout.fillWidth: true

                        text: root.folder ? finder.prettyPath(root.folder) : qsTr("Choose a folder…")
                        color: root.colorContext.fg.primary
                        font.pixelSize: VLCStyle.fontSize_small
                        elide: Text.ElideLeft
                    }
                }

                HoverHandler {
                    id: folderHover

                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    onTapped: root.chooseFolder()
                }
            }

            // search (online)
            Widgets.TextFieldExt {
                id: searchField

                Layout.fillWidth: true
                Layout.preferredHeight: root.controlHeight

                visible: root.useOnline

                radius: VLCStyle.dp(2, VLCStyle.scale)
                font.pixelSize: VLCStyle.fontSize_small
                leftPadding: VLCStyle.dp(30, VLCStyle.scale)

                placeholderText: qsTr("Title, e.g. The Wand S01E01")
                text: root.query

                background: Rectangle {
                    radius: VLCStyle.dp(2, VLCStyle.scale)
                    color: root.fieldColor
                    border.width: searchField.activeFocus ? 1 : 0
                    border.color: root.colorContext.accent
                }

                Widgets.IconLabel {
                    anchors.left: parent.left
                    anchors.leftMargin: VLCStyle.dp(8, VLCStyle.scale)
                    anchors.verticalCenter: parent.verticalCenter

                    text: VLCIcons.search
                    color: root.colorContext.fg.secondary
                    font.pixelSize: VLCStyle.dp(14, VLCStyle.scale)
                }

                Accessible.name: qsTr("Search subtitles")

                onTextEdited: root.query = text
                onAccepted: {
                    root.query = text
                    root.search()
                }
            }

            Widgets.ComboBoxExt {
                Layout.preferredWidth: VLCStyle.dp(130, VLCStyle.scale)
                Layout.preferredHeight: root.controlHeight

                visible: root.useOnline

                font.pixelSize: VLCStyle.fontSize_small
                bgColor: root.fieldColor

                model: root.languages
                textRole: "label"
                currentIndex: 1 // English

                Accessible.name: qsTr("Language")

                onActivated: (index) => {
                    root.languageCode = root.languages[index].code
                    root.search()
                }
            }
        }

        // ---- list
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: VLCStyle.dp(16, VLCStyle.scale)
            Layout.rightMargin: VLCStyle.dp(16, VLCStyle.scale)

            radius: VLCStyle.dp(4, VLCStyle.scale)
            color: Qt.alpha("black", root.colorContext.palette.isDark ? 0.25 : 0.04)

            // folder: video <-> subtitle pairs
            ListView {
                anchors.fill: parent
                anchors.topMargin: VLCStyle.dp(6, VLCStyle.scale)
                anchors.bottomMargin: VLCStyle.dp(6, VLCStyle.scale)

                visible: root.toFolder

                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.matches

                Accessible.role: Accessible.List
                Accessible.name: qsTr("Videos and their subtitles")

                delegate: Item {
                    id: pairRow

                    required property var modelData

                    width: ListView.view ? ListView.view.width : 0
                    height: root.rowHeight

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: VLCStyle.dp(16, VLCStyle.scale)
                        anchors.rightMargin: VLCStyle.dp(16, VLCStyle.scale)

                        spacing: VLCStyle.dp(8, VLCStyle.scale)

                        FileIcon {
                            source: "qrc:///figma/m_play.svg"
                        }

                        T.Label {
                            Layout.preferredWidth: parent.width * 0.36

                            text: pairRow.modelData.video
                            color: root.colorContext.fg.primary
                            font.pixelSize: VLCStyle.fontSize_small
                            elide: Text.ElideMiddle
                        }

                        // dashed connector
                        Item {
                            id: connector

                            Layout.fillWidth: true
                            Layout.preferredHeight: 1

                            visible: pairRow.modelData.subtitle !== ""

                            Repeater {
                                model: Math.max(0, Math.floor(connector.width / VLCStyle.dp(6, VLCStyle.scale)))

                                Rectangle {
                                    required property int index

                                    x: index * VLCStyle.dp(6, VLCStyle.scale)
                                    width: VLCStyle.dp(3, VLCStyle.scale)
                                    height: 1
                                    color: Qt.alpha(root.colorContext.fg.primary, 0.3)
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: pairRow.modelData.subtitle === ""
                            visible: pairRow.modelData.subtitle === ""
                        }

                        FileIcon {
                            visible: pairRow.modelData.subtitle !== ""
                            source: "qrc:///figma/m_subtitles.svg"
                        }

                        T.Label {
                            Layout.preferredWidth: parent.width * 0.36

                            visible: pairRow.modelData.subtitle !== ""
                            text: pairRow.modelData.subtitle
                            color: root.colorContext.fg.primary
                            font.pixelSize: VLCStyle.fontSize_small
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            // file: local subtitle files
            ListView {
                anchors.fill: parent
                anchors.topMargin: VLCStyle.dp(6, VLCStyle.scale)
                anchors.bottomMargin: VLCStyle.dp(6, VLCStyle.scale)

                visible: !root.toFolder && !root.useOnline

                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: localModel

                Accessible.role: Accessible.List
                Accessible.name: qsTr("Subtitle files")

                delegate: SubtitleRow {
                    onToggled: {
                        model.selected = !model.selected
                        root.countLocalSelected()
                    }
                }
            }

            // file: online results
            ListView {
                anchors.fill: parent
                anchors.topMargin: VLCStyle.dp(6, VLCStyle.scale)
                anchors.bottomMargin: VLCStyle.dp(6, VLCStyle.scale)

                visible: root.useOnline

                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: finder

                Accessible.role: Accessible.List
                Accessible.name: qsTr("Subtitle results")

                delegate: SubtitleRow {
                    onToggled: model.selected = !model.selected
                }
            }

            // empty / loading / error states
            T.Label {
                anchors.centerIn: parent
                width: parent.width - VLCStyle.dp(32, VLCStyle.scale)

                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: root.colorContext.fg.secondary
                font.pixelSize: VLCStyle.fontSize_small

                visible: text !== ""

                text: {
                    if (root.useOnline) {
                        if (finder.loading)
                            return qsTr("Searching…")
                        if (finder.count === 0)
                            return finder.error
                        return ""
                    }
                    if (!root.folder)
                        return qsTr("Choose a folder to look for subtitles")
                    if (root.toFolder)
                        return root.matches.length === 0 ? qsTr("No videos in this folder") : ""
                    return localModel.count === 0 ? qsTr("No subtitle files in this folder") : ""
                }
            }

            Widgets.BusyIndicatorExt {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -VLCStyle.dp(24, VLCStyle.scale)

                runningDelayed: root.useOnline && finder.loading
                visible: running
            }
        }

        // error while downloading, the list is not empty then
        T.Label {
            Layout.fillWidth: true
            Layout.leftMargin: VLCStyle.dp(16, VLCStyle.scale)
            Layout.rightMargin: VLCStyle.dp(16, VLCStyle.scale)

            visible: root.useOnline && finder.count > 0 && finder.error !== ""

            text: finder.error
            color: root.colorContext.fg.secondary
            font.pixelSize: VLCStyle.fontSize_small
            wrapMode: Text.Wrap
        }
    }

    footer: Item {
        implicitHeight: VLCStyle.dp(56, VLCStyle.scale)

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: VLCStyle.dp(16, VLCStyle.scale)
            anchors.rightMargin: VLCStyle.dp(16, VLCStyle.scale)

            Widgets.ButtonExt {
                text: qsTr("Cancel")
                font.pixelSize: VLCStyle.fontSize_small

                onClicked: root.close()
            }

            Item {
                Layout.fillWidth: true
            }

            Widgets.ActionButtonPrimary {
                text: qsTr("Save")
                font.pixelSize: VLCStyle.fontSize_small

                enabled: root.canSave
                busy: finder.loading && root.useOnline && finder.selectedCount > 0

                onClicked: root.save()
            }
        }
    }

    // ---- components

    component FileIcon : Image {
        Layout.preferredWidth: VLCStyle.dp(14, VLCStyle.scale)
        Layout.preferredHeight: VLCStyle.dp(14, VLCStyle.scale)

        opacity: 0.7
        fillMode: Image.PreserveAspectFit
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
    }

    // a checkable subtitle entry: icon, name, orange check when selected
    component SubtitleRow : T.ItemDelegate {
        id: row

        signal toggled()

        width: ListView.view ? ListView.view.width : 0
        height: root.rowHeight

        leftPadding: VLCStyle.dp(16, VLCStyle.scale)
        rightPadding: VLCStyle.dp(16, VLCStyle.scale)

        hoverEnabled: true

        background: Rectangle {
            color: row.hovered ? Qt.alpha(root.colorContext.fg.primary, 0.06) : "transparent"
        }

        contentItem: RowLayout {
            spacing: VLCStyle.dp(8, VLCStyle.scale)

            FileIcon {
                source: "qrc:///figma/m_subtitles.svg"
            }

            T.Label {
                Layout.fillWidth: true

                text: model.fileName
                color: model.selected ? root.colorContext.fg.secondary : root.colorContext.fg.primary
                font.pixelSize: VLCStyle.fontSize_small
                elide: Text.ElideMiddle
            }

            Image {
                Layout.preferredWidth: VLCStyle.dp(16, VLCStyle.scale)
                Layout.preferredHeight: VLCStyle.dp(16, VLCStyle.scale)

                visible: model.selected

                fillMode: Image.PreserveAspectFit
                sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                source: "qrc:///figma/m_check.svg"
            }
        }

        Accessible.role: Accessible.CheckBox
        Accessible.checked: model.selected
        Accessible.name: model.fileName

        onClicked: row.toggled()
    }
}

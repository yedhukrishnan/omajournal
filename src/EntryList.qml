import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The timeline: every entry, newest first, under the day it was written.
FocusScope {
    id: root

    signal newEntryRequested()
    signal editRequested()

    function focusList() { list.forceActiveFocus() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            Layout.leftMargin: 20
            Layout.rightMargin: 8

            Label {
                Layout.fillWidth: true
                text: qsTr("Journal")
                font.pixelSize: 18
                font.bold: true
                elide: Text.ElideRight
            }
            Button {
                objectName: "newEntryButton"
                text: qsTr("New entry")
                flat: true
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                Material.foreground: theme.accent
                onClicked: root.newEntryRequested()
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: "Ctrl+N"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: theme.colors.selection
        }

        // Entries that couldn't be decrypted are named, never skipped quietly.
        Label {
            objectName: "loadErrors"
            Layout.fillWidth: true
            Layout.margins: 12
            visible: backend.loadErrors.length > 0
            wrapMode: Text.Wrap
            color: theme.colors.red
            text: qsTr("%Ln entries couldn't be read:", "", backend.loadErrors.length) + "\n" + backend.loadErrors.join("\n")
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 24
            visible: backend.loading
            running: backend.loading
        }

        ListView {
            id: list
            objectName: "timeline"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            model: backend.entries
            currentIndex: -1
            keyNavigationEnabled: true
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Component.onCompleted: currentIndex = backend.currentIndex
            // Moving through the timeline opens each entry as it is reached.
            onCurrentIndexChanged: {
                const id = backend.entries.idAt(currentIndex)
                if (id !== "" && id !== backend.currentId)
                    backend.openEntry(id)
            }
            Keys.onReturnPressed: root.editRequested()
            Keys.onEnterPressed: root.editRequested()

            Connections {
                target: backend
                function onCurrentIndexChanged() {
                    list.currentIndex = backend.currentIndex
                    if (list.currentIndex >= 0)
                        list.positionViewAtIndex(list.currentIndex, ListView.Contain)
                }
            }

            section.property: "day"
            section.criteria: ViewSection.FullString
            section.delegate: Label {
                required property string section
                width: ListView.view.width
                topPadding: 18
                bottomPadding: 6
                leftPadding: 20
                rightPadding: 20
                color: theme.colors.muted
                font.bold: true
                font.pixelSize: 12
                font.capitalization: Font.AllUppercase
                elide: Text.ElideRight
                text: {
                    translator.language
                    const date = new Date(section + "T12:00:00")
                    const thisYear = date.getFullYear() === new Date().getFullYear()
                    return date.toLocaleDateString(Qt.locale(), thisYear ? "dddd, d MMMM" : "dddd, d MMMM yyyy")
                }
            }

            delegate: ItemDelegate {
                id: row
                required property int index
                required property string entryId
                required property string title
                required property string excerpt
                required property var created

                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                focusPolicy: Qt.NoFocus
                leftPadding: 20
                rightPadding: 20
                topPadding: 10
                bottomPadding: 10

                background: Rectangle {
                    color: row.highlighted ? theme.colors.selection
                         : row.hovered ? Qt.rgba(theme.colors.selection.r, theme.colors.selection.g,
                                                 theme.colors.selection.b, 0.45)
                         : "transparent"
                    Rectangle {
                        anchors.left: parent.left
                        width: 3
                        height: parent.height
                        color: theme.accent
                        visible: row.highlighted
                    }
                }

                contentItem: ColumnLayout {
                    spacing: 3

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Label {
                            Layout.fillWidth: true
                            text: row.title !== "" ? row.title : qsTr("Untitled")
                            font.bold: row.title !== ""
                            font.italic: row.title === ""
                            color: row.title !== "" ? theme.foreground : theme.colors.muted
                            elide: Text.ElideRight
                        }
                        Label {
                            color: theme.colors.muted
                            font.pixelSize: 12
                            text: {
                                translator.language
                                return row.created.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: row.excerpt !== ""
                        text: row.excerpt
                        color: theme.colors.muted
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }

                onClicked: {
                    list.currentIndex = row.index
                    list.forceActiveFocus()
                }
                onDoubleClicked: root.editRequested()
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !backend.loading && list.count === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignTop
            topPadding: 32
            wrapMode: Text.Wrap
            color: theme.colors.muted
            text: qsTr("No entries yet.")
        }
    }
}

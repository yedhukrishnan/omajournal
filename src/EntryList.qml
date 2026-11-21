import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The timeline: every entry, newest first, under the day it was written.
FocusScope {
    id: root

    signal newEntryRequested()
    signal editRequested()

    // In a narrow window the calendar sits above the timeline.
    property bool showCalendar: false

    function focusList() { list.forceActiveFocus() }
    function focusCalendar() { calendar.forceActiveFocus() }
    function focusSearch() {
        searchOpen = true
        searchField.forceActiveFocus()
        searchField.selectAll()
    }
    function closeSearch() {
        searchField.clear()
        backend.search.query = ""
        backend.search.tag = ""
        searchOpen = false
        focusList()
    }

    property bool searchOpen: false

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

        // Ctrl+F: narrows the timeline as you type. Escape closes it.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 10
            visible: root.searchOpen || backend.search.tag !== ""
            spacing: 4

            // The tag the timeline is narrowed to, if any.
            TagChip {
                objectName: "tagFilter"
                visible: backend.search.tag !== ""
                tag: backend.search.tag
                active: true
                removable: true
                onRemoved: backend.search.tag = ""
            }

            TextField {
                id: searchField
                visible: root.searchOpen
                objectName: "searchField"
                Layout.fillWidth: true
                placeholderText: qsTr("Search entries")
                Material.roundedScale: theme.radius
                Material.containerStyle: Material.Outlined
                // A short pause before searching, so each key isn't a search.
                onTextEdited: searchDelay.restart()
                onAccepted: {
                    searchDelay.stop()
                    backend.search.query = text
                    if (list.count > 0) {
                        if (list.currentIndex < 0)
                            list.openRow(0)
                        root.focusList()
                    }
                }
                Keys.onEscapePressed: root.closeSearch()
                Keys.onDownPressed: {
                    searchDelay.stop()
                    backend.search.query = text
                    if (list.currentIndex < 0)
                        list.openRow(0)
                    root.focusList()
                }
                Timer {
                    id: searchDelay
                    interval: 150
                    onTriggered: backend.search.query = searchField.text
                }
            }
            Label {
                objectName: "searchCount"
                visible: backend.search.active
                color: theme.secondaryText
                font.pixelSize: 12
                text: backend.search.count === 0 ? qsTr("No entries match.")
                    : qsTr("%Ln entries match", "", backend.search.count)
            }
        }

        CalendarPanel {
            id: calendar
            objectName: "narrowCalendar"
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            visible: root.showCalendar
            Keys.onTabPressed: root.focusList()
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: root.showCalendar
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
            model: backend.search.active ? backend.search : backend.entries
            onModelChanged: follow()
            currentIndex: -1
            // The selection only ever follows the open entry. Moving opens an
            // entry explicitly, since a ListView shifts its current index on
            // its own as rows come and go, and that must not open anything.
            keyNavigationEnabled: false
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            function follow() {
                currentIndex = model.indexOf(backend.currentId)
                if (currentIndex >= 0)
                    positionViewAtIndex(currentIndex, ListView.Contain)
            }
            function openRow(row) {
                const id = model.idAt(row)
                if (id !== "")
                    backend.openEntry(id)
            }

            Component.onCompleted: follow()
            Keys.onUpPressed: openRow(currentIndex - 1)
            Keys.onDownPressed: openRow(currentIndex + 1)
            Keys.onReturnPressed: root.editRequested()
            Keys.onEnterPressed: root.editRequested()
            // Escape leaves a search, back to the whole timeline.
            Keys.onEscapePressed: (event) => {
                if (root.searchOpen || backend.search.tag !== "")
                    root.closeSearch()
                else
                    event.accepted = false
            }

            Connections {
                target: backend
                function onCurrentIndexChanged() { list.follow() }
                function onCurrentChanged() { list.follow() }
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
                color: theme.secondaryText
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
                required property string snippet
                required property var created
                required property var tags

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
                            color: row.title !== "" ? theme.foreground : theme.secondaryText
                            elide: Text.ElideRight
                        }
                        Label {
                            color: theme.secondaryText
                            font.pixelSize: 12
                            text: {
                                translator.language
                                return row.created.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: row.tags.length > 0
                        text: row.tags.map((t) => "#" + t).join("  ")
                        color: theme.accent
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: row.snippet !== ""
                        // Escaped in C++; a search's matches come in bold.
                        textFormat: Text.StyledText
                        text: row.snippet
                        color: theme.secondaryText
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }

                onClicked: {
                    list.openRow(row.index)
                    list.forceActiveFocus()
                }
                onDoubleClicked: root.editRequested()
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !backend.loading && list.count === 0 && !backend.search.active
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignTop
            topPadding: 32
            wrapMode: Text.Wrap
            color: theme.secondaryText
            text: qsTr("No entries yet.")
        }
    }
}

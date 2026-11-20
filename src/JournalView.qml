import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The open journal: the calendar, the timeline, and the editor. In a narrow
// window (tiled at half a screen) the calendar folds into the top of the
// timeline's pane.
FocusScope {
    id: view
    objectName: "journalView"
    // The Loader passes focus on only to an item that asks for it.
    focus: true

    signal lockRequested()
    signal newEntryRequested()

    readonly property bool narrow: width < 900

    function focusCalendar() {
        if (narrow)
            timeline.focusCalendar()
        else
            calendar.forceActiveFocus()
    }
    function focusList() { timeline.focusList() }
    function focusEditor() { editor.focusEditor() }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        // A line between the panes, as wide as it looks plus room to grab.
        handle: Item {
            implicitWidth: 5
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 1
                height: parent.height
                color: parent.SplitHandle.hovered || parent.SplitHandle.pressed ? theme.accent : theme.colors.selection
            }
        }

        Item {
            id: calendarPane
            visible: !view.narrow
            SplitView.preferredWidth: 280
            SplitView.minimumWidth: 250
            SplitView.maximumWidth: 360

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                CalendarPanel {
                    id: calendar
                    Layout.fillWidth: true
                    paneHeader: true
                    Keys.onTabPressed: view.focusList()
                }
                Item { Layout.fillHeight: true }
            }
        }

        EntryList {
            id: timeline
            SplitView.preferredWidth: 320
            SplitView.minimumWidth: 240
            showCalendar: view.narrow
            onNewEntryRequested: view.newEntryRequested()
            onEditRequested: editor.focusEditor()
        }

        EntryEditor {
            id: editor
            SplitView.fillWidth: true
            SplitView.minimumWidth: 320
            focus: true
            onLockRequested: view.lockRequested()
            onTimelineRequested: timeline.focusList()
        }
    }
}

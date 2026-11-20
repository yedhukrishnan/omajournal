import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The open journal: the timeline beside the editor.
FocusScope {
    id: view
    objectName: "journalView"

    signal lockRequested()
    signal newEntryRequested()

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

        EntryList {
            id: timeline
            SplitView.preferredWidth: 320
            SplitView.minimumWidth: 240
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

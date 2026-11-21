import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: help
    property bool rightToLeft: false

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    padding: 24
    background: Rectangle {
        color: theme.background
        border.color: theme.colors.selection
        radius: theme.radius
    }

    ColumnLayout {
        spacing: 8
        // A popup is not an Item and doesn't inherit the window's mirroring,
        // so its content opts in.
        LayoutMirroring.enabled: help.rightToLeft
        LayoutMirroring.childrenInherit: true

        Label {
            text: qsTr("Keyboard shortcuts")
            font.bold: true
            font.pixelSize: 16
            Layout.bottomMargin: 8
        }

        Repeater {
            model: [
                { keys: "Ctrl+N", action: qsTr("New entry") },
                { keys: "Ctrl+S", action: qsTr("Save now") },
                { keys: "Ctrl+F", action: qsTr("Search entries") },
                { keys: "Ctrl+T", action: qsTr("Add a tag") },
                { keys: "Ctrl+G", action: qsTr("Add where you are") },
                { keys: "Ctrl+D", action: qsTr("Change the date and time") },
                { keys: "Esc", action: qsTr("Close the search") },
                { keys: "Ctrl+1", action: qsTr("Go to the calendar") },
                { keys: "Ctrl+2", action: qsTr("Go to the timeline") },
                { keys: "Ctrl+3", action: qsTr("Go to the editor") },
                { keys: "← → ↑ ↓", action: qsTr("Move through the calendar's days") },
                { keys: "PgUp PgDn", action: qsTr("Previous or next month") },
                { keys: "↑ ↓", action: qsTr("Move through the timeline") },
                { keys: "Enter", action: qsTr("Edit the selected entry") },
                { keys: "Ctrl+B", action: qsTr("Bold") },
                { keys: "Ctrl+I", action: qsTr("Italic") },
                { keys: "Ctrl+E", action: qsTr("Code") },
                { keys: "Ctrl+,", action: qsTr("Settings: the writing font") },
                { keys: "Ctrl++ Ctrl+−", action: qsTr("Larger or smaller text") },
                { keys: "Ctrl+0", action: qsTr("Text back to its usual size") },
                { keys: "Ctrl+Z", action: qsTr("Undo") },
                { keys: "Ctrl+Shift+Z", action: qsTr("Redo") },
                { keys: "Ctrl+Shift+L", action: qsTr("Lock the journal") },
                { keys: "F1", action: qsTr("Show or hide the shortcuts") },
                { keys: "Ctrl+W", action: qsTr("Close and lock") },
                { keys: "Ctrl+Q", action: qsTr("Quit and lock") }
            ]
            delegate: RowLayout {
                required property var modelData
                spacing: 24

                Label {
                    Layout.preferredWidth: 110
                    text: modelData.keys
                    color: theme.accent
                    font.bold: true
                }
                Label { text: modelData.action }
            }
        }
    }
}

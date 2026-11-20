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
                { keys: "Ctrl+1", action: qsTr("Go to the timeline") },
                { keys: "Ctrl+2", action: qsTr("Go to the editor") },
                { keys: "↑ ↓", action: qsTr("Move through the timeline") },
                { keys: "Enter", action: qsTr("Edit the selected entry") },
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

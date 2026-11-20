import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Omajournal

// The open entry: its date, its text, and whether it's saved.
FocusScope {
    id: root

    signal lockRequested()
    signal timelineRequested()

    function focusEditor() { area.forceActiveFocus() }

    // Set while a different entry's text goes in, so that isn't typing.
    property bool loadingText: false
    function loadText() {
        loadingText = true
        area.text = backend.currentText
        area.cursorPosition = area.length
        loadingText = false
    }

    Connections {
        target: backend
        function onCurrentChanged() { root.loadText() }
    }
    Component.onCompleted: loadText()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            Layout.leftMargin: 24
            Layout.rightMargin: 8

            Label {
                objectName: "entryDate"
                Layout.fillWidth: true
                color: theme.colors.muted
                elide: Text.ElideRight
                text: {
                    translator.language
                    if (backend.currentId === "")
                        return ""
                    const date = backend.currentCreated
                    return date.toLocaleDateString(Qt.locale(), "dddd, d MMMM yyyy") + "  ·  "
                         + date.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                }
            }
            Button {
                objectName: "lockButton"
                text: qsTr("Lock")
                flat: true
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                onClicked: root.lockRequested()
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: "Ctrl+Shift+L"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: theme.colors.selection
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ScrollView {
                id: scroll
                anchors.fill: parent
                visible: backend.currentId !== ""

                TextArea {
                    id: area
                    objectName: "editor"
                    focus: true
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    background: null
                    // A comfortable line length, centered, however wide the pane.
                    leftPadding: Math.max(32, (scroll.width - 720) / 2)
                    rightPadding: leftPadding
                    topPadding: 28
                    bottomPadding: 120
                    font.pixelSize: 16
                    color: theme.foreground
                    selectionColor: theme.colors.selection
                    selectedTextColor: theme.foreground
                    persistentSelection: true
                    onTextChanged: if (!root.loadingText) backend.updateFromDocument(area.textDocument)
                    Keys.onEscapePressed: root.timelineRequested()

                    Label {
                        x: area.leftPadding
                        y: area.topPadding
                        visible: area.length === 0
                        text: qsTr("Write something…")
                        color: theme.colors.muted
                        font: area.font
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: backend.currentId === "" && !backend.loading
                horizontalAlignment: Text.AlignHCenter
                color: theme.colors.muted
                text: qsTr("Press Ctrl+N to write an entry.")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            Layout.leftMargin: 24
            Layout.rightMargin: 16
            visible: backend.currentId !== ""

            Label {
                objectName: "saveState"
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.pixelSize: 12
                color: backend.saveState === Backend.Failed ? theme.colors.red : theme.colors.muted
                text: backend.saveState === Backend.Failed ? qsTr("Not saved: %1").arg(backend.saveError)
                    : backend.saveState === Backend.Unsaved ? qsTr("Saving…")
                    : qsTr("Saved, encrypted")
            }
        }
    }
}

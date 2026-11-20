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

    // Wraps the selection in a Markdown mark, or takes the mark away if it is
    // already there. With nothing selected, the cursor lands between a pair.
    function toggleMark(mark) {
        const m = mark.length
        const start = area.selectionStart, end = area.selectionEnd
        const before = area.getText(Math.max(0, start - m), start)
        const after = area.getText(end, end + m)
        const inner = area.getText(start, end)
        // A single star beside a double one belongs to bold, not italic.
        const bolded = mark === "*" && area.getText(Math.max(0, start - 2), start) === "**"
                       && area.getText(end, end + 2) === "**"
        if (before === mark && after === mark && !bolded) {
            area.remove(end, end + m)
            area.remove(start - m, start)
            area.select(start - m, end - m)
        } else if (inner.length >= 2 * m && inner.startsWith(mark) && inner.endsWith(mark)
                   && !(mark === "*" && inner.startsWith("**"))) {
            area.remove(end - m, end)
            area.remove(start, start + m)
            area.select(start, end - 2 * m)
        } else {
            area.insert(end, mark)
            area.insert(start, mark)
            area.select(start + m, end + m)
        }
    }

    Shortcut {
        sequences: [StandardKey.Bold]
        enabled: area.activeFocus
        onActivated: root.toggleMark("**")
    }
    Shortcut {
        sequences: [StandardKey.Italic]
        enabled: area.activeFocus
        onActivated: root.toggleMark("*")
    }
    Shortcut {
        sequence: "Ctrl+E"
        enabled: area.activeFocus
        onActivated: root.toggleMark("`")
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

                    // Markdown styled as it's typed; the text itself stays as typed.
                    MarkdownHighlighter {
                        id: highlighter
                        textDocument: area.textDocument
                        baseSize: area.font.pixelSize
                        foreground: theme.foreground
                        muted: theme.colors.muted
                        accent: theme.accent
                        codeBackground: theme.colors.selection
                        matchBackground: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.35)
                        search: backend.search.query
                    }

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

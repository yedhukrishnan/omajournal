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
    signal fontRequested()

    function focusEditor() { area.forceActiveFocus() }
    function startTag() {
        if (backend.currentId === "")
            return
        tagField.text = ""
        adding = true
        tagField.forceActiveFocus()
    }
    function editDate() {
        if (backend.currentId !== "")
            dateTime.open()
    }
    function locate() {
        if (backend.currentId !== "")
            backend.locate()
    }

    property bool adding: false
    // Tags in use that start with what's typed, and that the entry lacks.
    readonly property var suggestions: {
        const typed = tagField.text.replace(/^#/, "").toLowerCase()
        return backend.tagCounts
            .filter((t) => t.tag.startsWith(typed) && backend.currentTags.indexOf(t.tag) < 0)
            .slice(0, 8)
    }
    function finishTag(tag) {
        if (tag !== "" && !backend.addTag(tag)) {
            tagProblem.visible = true
            return
        }
        adding = false
        tagProblem.visible = false
        focusEditor()
    }

    // Degrees with a hemisphere, in the reader's number format.
    function coordinate(value, positive, negative) {
        translator.language
        return Math.abs(value).toLocaleString(Qt.locale(), "f", 4) + "° " + (value >= 0 ? positive : negative)
    }

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

            // The date and time; a click (or Ctrl+D) changes them.
            Label {
                objectName: "entryDate"
                Layout.fillWidth: true
                color: dateHover.hovered && backend.currentId !== "" ? theme.foreground : theme.secondaryText
                elide: Text.ElideRight
                text: {
                    translator.language
                    if (backend.currentId === "")
                        return ""
                    const date = backend.currentCreated
                    return date.toLocaleDateString(Qt.locale(), "dddd, d MMMM yyyy") + "  ·  "
                         + date.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                }
                HoverHandler { id: dateHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.editDate() }
                ToolTip.visible: dateHover.hovered && backend.currentId !== ""
                ToolTip.delay: 600
                ToolTip.text: qsTr("Change the date and time (Ctrl+D)")
            }
            SmallAction {
                objectName: "fontButton"
                text: "Aa"
                shortcut: qsTr("Writing font (Ctrl+Shift+F)")
                onClicked: root.fontRequested()
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

        // Tags and where it was written.
        Flow {
            id: meta
            objectName: "entryMeta"
            Layout.fillWidth: true
            // In line with the text below, whatever the pane's width.
            Layout.leftMargin: Math.max(32, (scroll.width - 720) / 2)
            Layout.rightMargin: Layout.leftMargin
            Layout.topMargin: 16
            visible: backend.currentId !== ""
            spacing: 6

            Repeater {
                model: backend.currentTags
                delegate: TagChip {
                    required property string modelData
                    tag: modelData
                    removable: true
                    onRemoved: backend.removeTag(modelData)
                }
            }
            // #tags written in the text; they change with the text.
            Repeater {
                model: backend.currentInlineTags.filter((t) => backend.currentTags.indexOf(t) < 0)
                delegate: TagChip {
                    required property string modelData
                    tag: modelData
                    opacity: 0.75
                }
            }

            TextField {
                id: tagField
                objectName: "tagField"
                visible: root.adding
                width: 180
                height: 32
                topPadding: 4
                bottomPadding: 4
                font.pixelSize: 13
                placeholderText: qsTr("Tag")
                Material.containerStyle: Material.Outlined
                Material.roundedScale: theme.radius
                property int picked: -1
                onTextEdited: {
                    picked = -1
                    tagProblem.visible = false
                }
                onAccepted: root.finishTag(picked >= 0 && picked < root.suggestions.length
                                           ? root.suggestions[picked].tag : text)
                Keys.onEscapePressed: {
                    root.adding = false
                    tagProblem.visible = false
                    root.focusEditor()
                }
                Keys.onDownPressed: picked = Math.min(picked + 1, root.suggestions.length - 1)
                Keys.onUpPressed: picked = Math.max(picked - 1, -1)
                onActiveFocusChanged: if (!activeFocus && root.adding && text === "") root.adding = false

                Popup {
                    id: suggestionList
                    objectName: "tagSuggestions"
                    y: tagField.height + 2
                    width: tagField.width
                    padding: 4
                    visible: root.adding && tagField.activeFocus && root.suggestions.length > 0
                    closePolicy: Popup.NoAutoClose
                    background: Rectangle {
                        color: theme.background
                        border.color: theme.colors.selection
                        radius: Math.min(4, theme.radius)
                    }
                    contentItem: Column {
                        Repeater {
                            model: root.suggestions
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                width: suggestionList.availableWidth
                                height: 26
                                radius: Math.min(4, theme.radius)
                                color: index === tagField.picked ? theme.colors.selection : "transparent"
                                Label {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "#" + modelData.tag
                                    font.pixelSize: 13
                                }
                                Label {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.count
                                    font.pixelSize: 11
                                    color: theme.secondaryText
                                }
                                TapHandler { onTapped: root.finishTag(modelData.tag) }
                            }
                        }
                    }
                }
            }
            Label {
                id: tagProblem
                visible: false
                height: 24
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 12
                color: theme.colors.red
                text: qsTr("A tag is letters, digits, - and _, with no spaces.")
            }

            // Where it was written, once known.
            Rectangle {
                objectName: "locationChip"
                visible: backend.currentLocation.latitude !== undefined
                implicitHeight: 24
                implicitWidth: locationText.implicitWidth + 16
                height: implicitHeight
                width: implicitWidth
                radius: Math.min(4, theme.radius)
                color: locationHover.hovered ? Qt.lighter(theme.colors.selection, 1.15) : theme.colors.selection
                Label {
                    id: locationText
                    anchors.centerIn: parent
                    font.pixelSize: 12
                    text: backend.currentLocation.latitude === undefined ? ""
                        : "⌖ " + root.coordinate(backend.currentLocation.latitude, qsTr("N"), qsTr("S")) + ", "
                          + root.coordinate(backend.currentLocation.longitude, qsTr("E"), qsTr("W"))
                }
                HoverHandler { id: locationHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: locationMenu.popup() }
                ToolTip.visible: locationHover.hovered && backend.currentLocation.accuracy > 0
                ToolTip.delay: 600
                ToolTip.text: qsTr("Within about %1 m").arg(Math.round(backend.currentLocation.accuracy || 0))

                Menu {
                    id: locationMenu
                    MenuItem {
                        text: qsTr("Open in OpenStreetMap")
                        // Only on request: the map site then sees these coordinates.
                        onTriggered: {
                            const l = backend.currentLocation
                            Qt.openUrlExternally("https://www.openstreetmap.org/?mlat=" + l.latitude + "&mlon=" + l.longitude
                                                 + "#map=15/" + l.latitude + "/" + l.longitude)
                        }
                    }
                    MenuItem {
                        text: qsTr("Remove the location")
                        onTriggered: backend.clearLocation()
                    }
                }
            }
            Row {
                visible: backend.locating
                height: 24
                spacing: 6
                BusyIndicator {
                    width: 20
                    height: 20
                    running: backend.locating
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: 12
                    color: theme.secondaryText
                    text: qsTr("Finding where you are…")
                }
            }
            Label {
                objectName: "locationError"
                visible: backend.locationError !== ""
                width: Math.min(implicitWidth, meta.width)
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: theme.colors.red
                text: backend.locationError
            }

            SmallAction {
                objectName: "addTagButton"
                text: qsTr("+ Tag")
                shortcut: "Ctrl+T"
                visible: !root.adding
                onClicked: root.startTag()
            }
            SmallAction {
                objectName: "addLocationButton"
                text: qsTr("+ Location")
                shortcut: "Ctrl+G"
                visible: backend.currentLocation.latitude === undefined && !backend.locating
                onClicked: root.locate()
            }
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
                    topPadding: 16
                    bottomPadding: 120
                    // The writing font; empty means the desktop's own.
                    font.family: preferences.fontFamily !== "" ? preferences.fontFamily : Qt.application.font.family
                    font.pixelSize: preferences.fontSize
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
                        color: theme.secondaryText
                        font: area.font
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: backend.currentId === "" && !backend.loading
                horizontalAlignment: Text.AlignHCenter
                color: theme.secondaryText
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
                color: backend.saveState === Backend.Failed ? theme.colors.red : theme.secondaryText
                text: backend.saveState === Backend.Failed ? qsTr("Not saved: %1").arg(backend.saveError)
                    : backend.saveState === Backend.Unsaved ? qsTr("Saving…")
                    : qsTr("Saved, encrypted")
            }
        }
    }

    DateTimePopup {
        id: dateTime
        rightToLeft: Qt.application.layoutDirection === Qt.RightToLeft
        onClosed: root.focusEditor()
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Picks the font entries are written in, and its size. Every choice shows in
// the editor at once, so the popup doesn't dim it.
Popup {
    id: popup
    objectName: "fontPopup"

    property bool rightToLeft: false
    // The installed families holding what's typed in the search field.
    readonly property var matches: {
        const typed = filter.text.trim().toLowerCase()
        return typed === "" ? families : families.filter((f) => f.toLowerCase().indexOf(typed) >= 0)
    }
    property var families: []

    function choose(family) {
        preferences.fontFamily = family
    }

    // Over the calendar and timeline, so the editor stays in view while a
    // font is tried; mirrored for right-to-left languages.
    parent: Overlay.overlay
    x: rightToLeft ? parent.width - width - 24 : 24
    y: 72
    width: Math.min(440, parent.width - 48)
    modal: true
    dim: false
    focus: true
    padding: 20
    background: Rectangle {
        color: theme.background
        border.color: theme.colors.selection
        radius: theme.radius
    }

    onAboutToShow: {
        // Read only now: listing every installed font takes a moment.
        families = preferences.fontFamilies
        filter.text = ""
        list.currentIndex = Math.max(0, matches.indexOf(preferences.fontFamily))
        list.positionViewAtIndex(list.currentIndex, ListView.Center)
        filter.forceActiveFocus()
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 10
        // A popup doesn't inherit the window's mirroring.
        LayoutMirroring.enabled: popup.rightToLeft
        LayoutMirroring.childrenInherit: true

        Label {
            text: qsTr("Writing font")
            font.bold: true
            font.pixelSize: 16
        }

        // The desktop's own choices for each kind of face, whatever is installed.
        RowLayout {
            spacing: 6
            Repeater {
                model: [
                    { family: "", label: qsTr("Default") },
                    { family: "serif", label: qsTr("Serif") },
                    { family: "monospace", label: qsTr("Monospace") }
                ]
                delegate: Rectangle {
                    required property var modelData
                    objectName: "fontPreset-" + (modelData.family || "default")
                    readonly property bool chosen: preferences.fontFamily === modelData.family
                    implicitWidth: presetLabel.implicitWidth + 24
                    implicitHeight: 32
                    radius: Math.min(4, theme.radius)
                    color: chosen ? theme.accent : presetHover.hovered ? theme.colors.selection : "transparent"
                    border.color: theme.colors.selection
                    Label {
                        id: presetLabel
                        anchors.centerIn: parent
                        text: modelData.label
                        font.family: modelData.family === "" ? Qt.application.font.family : modelData.family
                        font.pixelSize: 14
                        color: parent.chosen ? theme.accentForeground : theme.foreground
                    }
                    HoverHandler { id: presetHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: popup.choose(modelData.family) }
                }
            }
        }

        TextField {
            id: filter
            objectName: "fontFilter"
            Layout.fillWidth: true
            placeholderText: qsTr("Search installed fonts")
            Material.roundedScale: theme.radius
            onTextChanged: list.currentIndex = 0
            Keys.onDownPressed: list.incrementCurrentIndex()
            Keys.onUpPressed: list.decrementCurrentIndex()
            function chooseCurrent() {
                if (list.currentIndex >= 0 && list.count > 0)
                    popup.choose(popup.matches[list.currentIndex])
            }
            Keys.onReturnPressed: chooseCurrent()
            Keys.onEnterPressed: chooseCurrent()
        }

        ListView {
            id: list
            objectName: "fontList"
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            clip: true
            model: popup.matches
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: row
                required property string modelData
                required property int index
                readonly property bool chosen: preferences.fontFamily === modelData
                width: ListView.view.width
                height: 34
                radius: Math.min(4, theme.radius)
                color: index === list.currentIndex ? theme.colors.selection
                     : rowHover.hovered ? Qt.rgba(theme.colors.selection.r, theme.colors.selection.g,
                                                  theme.colors.selection.b, 0.5)
                     : "transparent"

                // The name in the window's font, since a symbol font can't
                // spell its own; a sample beside it in the font itself.
                Label {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: sample.left
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData
                    elide: Text.ElideRight
                    font.bold: row.chosen
                    color: row.chosen ? theme.accent : theme.foreground
                }
                Label {
                    id: sample
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Aa Bb 123"
                    font.family: row.modelData
                    font.pixelSize: 16
                    color: theme.secondaryText
                }
                HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        list.currentIndex = row.index
                        popup.choose(row.modelData)
                    }
                }
            }
        }

        Label {
            visible: list.count === 0
            text: qsTr("No installed font has that name.")
            color: theme.secondaryText
            font.pixelSize: 12
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label { text: qsTr("Size") }
            SmallAction {
                objectName: "fontSmaller"
                text: "−"
                shortcut: "Ctrl+−"
                onClicked: preferences.shrinkFont()
            }
            Label {
                objectName: "fontSizeLabel"
                Layout.minimumWidth: 28
                horizontalAlignment: Text.AlignHCenter
                text: { translator.language; return preferences.fontSize.toLocaleString(Qt.locale(), "f", 0) }
            }
            SmallAction {
                objectName: "fontLarger"
                text: "+"
                shortcut: "Ctrl++"
                onClicked: preferences.enlargeFont()
            }
            Item { Layout.fillWidth: true }
            SmallAction {
                text: qsTr("Reset size")
                shortcut: "Ctrl+0"
                onClicked: preferences.resetFontSize()
            }
        }

        Label {
            objectName: "fontMissing"
            visible: !preferences.fontInstalled
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: theme.colors.yellow
            text: qsTr("%1 isn't installed, so another font is shown in its place.").arg(preferences.fontFamily)
        }
        Label {
            visible: preferences.saveError !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: theme.colors.red
            text: preferences.saveError
        }
    }
}

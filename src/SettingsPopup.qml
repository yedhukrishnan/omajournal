import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Settings, on Ctrl+, as in most desktop apps. For now that's the writing
// font and its size, from a short list: moving through it shows each font in the editor;
// Enter or a click keeps it, and Escape or closing goes back to the font
// from before. The editor stays in view, so the popup doesn't dim it.
Popup {
    id: popup
    objectName: "settingsPopup"

    property bool rightToLeft: false
    // The fonts on offer, read when the popup opens: a preview changes the
    // font, and a live binding would reset the list under the highlight.
    property var rows: []

    // Shows the highlighted font in the editor without choosing it.
    function browse() {
        if (list.currentIndex >= 0 && list.currentIndex < rows.length)
            preferences.previewFont(rows[list.currentIndex].family)
    }
    function choose(index) {
        if (index < 0 || index >= rows.length)
            return
        preferences.fontFamily = rows[index].family
        // Ending the preview also takes the highlight to the chosen row.
        preferences.endPreview()
        // The chosen font may drop a row it had added before.
        rows = preferences.fontChoices
        list.currentIndex = rows.findIndex((r) => r.family === preferences.fontFamily)
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
        rows = preferences.fontChoices
        list.currentIndex = rows.findIndex((r) => r.family === preferences.fontFamily)
        list.forceActiveFocus()
    }
    // Whatever closed it, only a chosen font stays; a font being tried goes.
    onClosed: preferences.endPreview()

    // The window's own shortcuts wait while the popup is open, so the key
    // that opened it closes it from here.
    Shortcut {
        sequence: "Ctrl+,"
        enabled: popup.opened
        onActivated: popup.close()
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 10
        // A popup doesn't inherit the window's mirroring.
        LayoutMirroring.enabled: popup.rightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("Settings")
                font.bold: true
                font.pixelSize: 16
            }
            ToolButton {
                objectName: "settingsClose"
                text: "✕"
                font.pixelSize: 18
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                Accessible.name: qsTr("Close")
                onClicked: popup.close()
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Close (Esc)")
            }
        }
        Label {
            Layout.topMargin: 4
            text: qsTr("Writing font")
            font.bold: true
        }

        ListView {
            id: list
            objectName: "fontList"
            Layout.fillWidth: true
            Layout.preferredHeight: contentHeight
            interactive: false
            model: popup.rows
            focus: true
            keyNavigationEnabled: false
            Keys.onDownPressed: {
                incrementCurrentIndex()
                popup.browse()
            }
            Keys.onUpPressed: {
                decrementCurrentIndex()
                popup.browse()
            }
            Keys.onReturnPressed: popup.choose(currentIndex)
            Keys.onEnterPressed: popup.choose(currentIndex)

            delegate: Column {
                id: row
                required property var modelData
                required property int index
                readonly property bool chosen: preferences.fontFamily === modelData.family
                width: ListView.view.width

                Rectangle {
                    width: parent.width
                    height: 34
                    radius: Math.min(4, theme.radius)
                    color: row.index === list.currentIndex ? theme.colors.selection
                         : rowHover.hovered ? Qt.rgba(theme.colors.selection.r, theme.colors.selection.g,
                                                      theme.colors.selection.b, 0.5)
                         : "transparent"

                    // The name in the window's font, since a symbol font can't
                    // spell its own; a sample beside it in the font itself.
                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.right: sample.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        clip: true
                        Label {
                            text: row.modelData.name
                            font.bold: row.chosen
                            color: row.chosen ? theme.accent : theme.foreground
                        }
                        Label {
                            visible: row.modelData.detail !== ""
                            text: "· " + row.modelData.detail
                            color: theme.secondaryText
                        }
                    }
                    Label {
                        id: sample
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Aa Bb 123"
                        font.family: row.modelData.family !== "" ? row.modelData.family : Qt.application.font.family
                        font.styleName: preferences.regularStyle(row.modelData.family)
                        font.pixelSize: 16
                        color: theme.secondaryText
                    }
                    HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: popup.choose(row.index) }
                }
            }
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

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A quiet text action the height of a chip: "+ Tag". A Material button can't
// be made that small without its padding squeezing the caption away.
Rectangle {
    id: action

    property string text: ""
    property string shortcut: ""
    signal clicked()

    implicitHeight: 24
    implicitWidth: label.implicitWidth + 16
    radius: Math.min(4, theme.radius)
    color: hover.hovered ? theme.colors.selection : "transparent"

    Label {
        id: label
        anchors.centerIn: parent
        text: action.text
        font.pixelSize: 12
        color: hover.hovered ? theme.foreground : theme.colors.muted
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: action.clicked() }
    ToolTip.visible: hover.hovered && shortcut !== ""
    ToolTip.delay: 600
    ToolTip.text: shortcut
}

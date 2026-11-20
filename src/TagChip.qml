import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A tag as a small rounded label. A tag set on the entry can be removed; a
// #tag written in the text is changed by editing the text.
Rectangle {
    id: chip

    property string tag: ""
    property bool removable: false
    property bool active: false
    signal clicked()
    signal removed()

    implicitHeight: 24
    implicitWidth: row.implicitWidth + 16
    radius: Math.min(4, theme.radius)
    color: active ? theme.accent : hover.hovered ? Qt.lighter(theme.colors.selection, 1.15) : theme.colors.selection

    HoverHandler { id: hover }
    TapHandler { onTapped: chip.clicked() }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Label {
            text: "#" + chip.tag
            font.pixelSize: 12
            color: chip.active ? theme.accentForeground : theme.foreground
        }
        Label {
            visible: chip.removable
            text: "×"
            font.pixelSize: 14
            color: chip.active ? theme.accentForeground : theme.colors.muted
            TapHandler { onTapped: chip.removed() }
        }
    }
}

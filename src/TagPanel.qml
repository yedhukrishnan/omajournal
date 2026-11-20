import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every tag and how many entries carry it. Picking one narrows the timeline
// to it; picking it again shows everything.
ColumnLayout {
    id: panel
    spacing: 2

    Label {
        Layout.leftMargin: 20
        Layout.bottomMargin: 4
        visible: backend.tagCounts.length > 0
        text: qsTr("Tags")
        color: theme.colors.muted
        font.bold: true
        font.pixelSize: 12
        font.capitalization: Font.AllUppercase
    }

    ListView {
        id: tags
        objectName: "tagList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: backend.tagCounts
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        delegate: ItemDelegate {
            id: row
            required property var modelData
            readonly property bool active: backend.search.tag === modelData.tag
            width: ListView.view.width
            height: 30
            leftPadding: 20
            rightPadding: 20
            focusPolicy: Qt.NoFocus
            background: Rectangle {
                color: row.active ? theme.colors.selection
                     : row.hovered ? Qt.rgba(theme.colors.selection.r, theme.colors.selection.g,
                                             theme.colors.selection.b, 0.45)
                     : "transparent"
                Rectangle {
                    anchors.left: parent.left
                    width: 3
                    height: parent.height
                    color: theme.accent
                    visible: row.active
                }
            }
            contentItem: RowLayout {
                Label {
                    Layout.fillWidth: true
                    text: "#" + row.modelData.tag
                    elide: Text.ElideRight
                    font.bold: row.active
                }
                Label {
                    text: row.modelData.count.toLocaleString(Qt.locale(), "f", 0)
                    color: theme.colors.muted
                    font.pixelSize: 12
                }
            }
            onClicked: backend.search.tag = active ? "" : modelData.tag
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Moves the open entry to another day and time. The time stays in the UTC
// offset the entry was written with.
Popup {
    id: popup
    objectName: "dateTimePopup"

    property bool rightToLeft: false
    property date chosen: new Date()
    property date shown: new Date()
    readonly property var dateLocale: { translator.language; return Qt.locale() }

    function iso(date) {
        const pad = (n) => (n < 10 ? "0" : "") + n
        return date.getFullYear() + "-" + pad(date.getMonth() + 1) + "-" + pad(date.getDate())
    }
    function save() {
        if (!timeField.acceptableInput)
            return
        if (backend.setCreated(iso(chosen), timeField.text))
            close()
    }

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    padding: 20
    background: Rectangle {
        color: theme.background
        border.color: theme.colors.selection
        radius: theme.radius
    }

    onAboutToShow: {
        const parts = backend.currentDay.split("-")
        chosen = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]))
        shown = chosen
        timeField.text = backend.currentTime
        timeField.forceActiveFocus()
        timeField.selectAll()
    }

    ColumnLayout {
        spacing: 8
        // A popup doesn't inherit the window's mirroring.
        LayoutMirroring.enabled: popup.rightToLeft
        LayoutMirroring.childrenInherit: true

        Label {
            text: qsTr("Date and time")
            font.bold: true
            font.pixelSize: 16
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                font.bold: true
                text: popup.shown.toLocaleDateString(popup.dateLocale, "MMMM yyyy")
            }
            ToolButton {
                text: popup.rightToLeft ? "›" : "‹"
                focusPolicy: Qt.NoFocus
                onClicked: popup.shown = new Date(popup.shown.getFullYear(), popup.shown.getMonth() - 1, 1)
            }
            ToolButton {
                text: popup.rightToLeft ? "‹" : "›"
                focusPolicy: Qt.NoFocus
                onClicked: popup.shown = new Date(popup.shown.getFullYear(), popup.shown.getMonth() + 1, 1)
            }
        }

        DayOfWeekRow {
            Layout.fillWidth: true
            locale: popup.dateLocale
            delegate: Label {
                required property string shortName
                text: shortName
                font.pixelSize: 11
                color: theme.secondaryText
                horizontalAlignment: Text.AlignHCenter
            }
        }

        MonthGrid {
            id: grid
            objectName: "dateGrid"
            Layout.fillWidth: true
            month: popup.shown.getMonth()
            year: popup.shown.getFullYear()
            locale: popup.dateLocale
            delegate: Item {
                id: day
                required property var model
                readonly property bool picked: popup.iso(model.date) === popup.iso(popup.chosen)
                implicitWidth: 34
                implicitHeight: 30
                opacity: model.month === grid.month ? 1 : 0.4
                Rectangle {
                    anchors.centerIn: parent
                    width: 30
                    height: 28
                    radius: Math.min(4, theme.radius)
                    color: day.picked ? theme.accent : "transparent"
                }
                Label {
                    anchors.centerIn: parent
                    text: day.model.day
                    font.pixelSize: 13
                    color: day.picked ? theme.accentForeground : day.model.today ? theme.accent : theme.foreground
                }
            }
            onClicked: (date) => popup.chosen = date
        }

        TextField {
            id: timeField
            objectName: "timeField"
            Layout.fillWidth: true
            placeholderText: qsTr("Time (HH:MM)")
            inputMethodHints: Qt.ImhTime
            validator: RegularExpressionValidator { regularExpression: /^([01]?\d|2[0-3]):[0-5]\d$/ }
            Material.roundedScale: theme.radius
            onAccepted: popup.save()
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Cancel")
                flat: true
                Material.roundedScale: theme.radius
                onClicked: popup.close()
            }
            AccentButton {
                objectName: "dateSaveButton"
                text: qsTr("Change")
                enabled: timeField.acceptableInput
                onClicked: popup.save()
            }
        }
    }
}

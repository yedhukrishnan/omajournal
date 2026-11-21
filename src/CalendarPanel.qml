import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A month at a time, with a dot under every day that has entries. The arrow
// keys move a cursor day, Page Up and Page Down change month, Home goes to
// today, and Enter opens the cursor day's newest entry.
FocusScope {
    id: cal
    objectName: "calendar"

    // The day the keyboard is on, and the month shown, which follows it.
    property date cursor: new Date()
    readonly property int year: cursor.getFullYear()
    readonly property int month: cursor.getMonth()
    // Locale-aware formatting re-renders when the language changes.
    readonly property var locale: { translator.language; return Qt.locale() }

    // As a pane of its own, its month heading lines up with the other panes'
    // headers and the line under them.
    property bool paneHeader: false

    // Ctrl+Enter, or the action under the month: a new entry on the cursor day.
    signal newEntryOnRequested(string day)

    implicitHeight: column.implicitHeight

    function iso(date) {
        const pad = (n) => (n < 10 ? "0" : "") + n
        return date.getFullYear() + "-" + pad(date.getMonth() + 1) + "-" + pad(date.getDate())
    }
    function fromIso(day) {
        const parts = day.split("-")
        return new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]))
    }
    function moveDays(days) {
        cursor = new Date(cursor.getFullYear(), cursor.getMonth(), cursor.getDate() + days)
    }
    function moveMonths(months) {
        // The same day of the month where it exists, else the month's last.
        const target = new Date(cursor.getFullYear(), cursor.getMonth() + months, 1)
        const last = new Date(target.getFullYear(), target.getMonth() + 1, 0).getDate()
        cursor = new Date(target.getFullYear(), target.getMonth(), Math.min(cursor.getDate(), last))
    }
    function openCursor() { backend.openDay(iso(cursor)) }

    // The open entry's day comes into view as entries are opened elsewhere.
    Connections {
        target: backend
        function onCurrentChanged() {
            if (backend.currentDay !== "")
                cal.cursor = cal.fromIso(backend.currentDay)
        }
    }
    Component.onCompleted: if (backend.currentDay !== "") cursor = fromIso(backend.currentDay)

    Keys.onLeftPressed: moveDays(cal.locale.textDirection === Qt.RightToLeft ? 1 : -1)
    Keys.onRightPressed: moveDays(cal.locale.textDirection === Qt.RightToLeft ? -1 : 1)
    Keys.onUpPressed: moveDays(-7)
    Keys.onDownPressed: moveDays(7)
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_PageUp) {
            moveMonths(-1)
        } else if (event.key === Qt.Key_PageDown) {
            moveMonths(1)
        } else if (event.key === Qt.Key_Home) {
            cursor = new Date()
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                   && (event.modifiers & Qt.ControlModifier)) {
            newEntryOnRequested(iso(cursor))
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            openCursor()
        } else {
            return
        }
        event.accepted = true
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: cal.paneHeader ? 56 : -1
            Layout.leftMargin: cal.paneHeader ? 20 : 12
            Layout.rightMargin: 4

            Label {
                objectName: "calendarMonth"
                Layout.fillWidth: true
                font.bold: true
                font.pixelSize: cal.paneHeader ? 16 : 14
                elide: Text.ElideRight
                text: cal.cursor.toLocaleDateString(cal.locale, "MMMM yyyy")
            }
            ToolButton {
                text: cal.locale.textDirection === Qt.RightToLeft ? "›" : "‹"
                focusPolicy: Qt.NoFocus
                font.pixelSize: 18
                onClicked: cal.moveMonths(-1)
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Previous month")
            }
            ToolButton {
                text: cal.locale.textDirection === Qt.RightToLeft ? "‹" : "›"
                focusPolicy: Qt.NoFocus
                font.pixelSize: 18
                onClicked: cal.moveMonths(1)
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Next month")
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            // Cancel the column's spacing so the line sits at 56px, level
            // with the lines under the other panes' headers.
            Layout.topMargin: -column.spacing
            Layout.bottomMargin: 8
            visible: cal.paneHeader
            color: theme.colors.selection
        }

        DayOfWeekRow {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            locale: cal.locale
            delegate: Label {
                required property string shortName
                text: shortName
                font.pixelSize: 11
                color: theme.secondaryText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        MonthGrid {
            id: grid
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            month: cal.month
            year: cal.year
            locale: cal.locale
            spacing: 2

            delegate: Item {
                id: day
                required property var model
                readonly property string key: cal.iso(model.date)
                readonly property bool inMonth: model.month === grid.month
                readonly property int entries: backend.entryDays[key] || 0
                readonly property bool open: key === backend.currentDay
                readonly property bool atCursor: cal.activeFocus && key === cal.iso(cal.cursor)

                implicitWidth: 32
                implicitHeight: 34
                opacity: inMonth ? 1 : 0.4

                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, 30)
                    height: 30
                    radius: Math.min(4, theme.radius)
                    color: day.open ? theme.colors.selection : "transparent"
                    border.width: day.atCursor ? 2 : 0
                    border.color: theme.accent
                }
                Label {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: -2
                    text: day.model.day
                    font.pixelSize: 13
                    font.bold: day.model.today || day.entries > 0
                    color: day.model.today ? theme.accent
                         : day.entries > 0 ? theme.foreground
                         : theme.secondaryText
                }
                // A dot for a day with entries; true circles keep their radius.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    width: 4
                    height: 4
                    radius: 2
                    visible: day.entries > 0
                    color: theme.accent
                }
            }

            onClicked: (date) => {
                cal.cursor = date
                cal.forceActiveFocus()
                cal.openCursor()
            }
        }

        SmallAction {
            objectName: "newEntryOnDay"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4
            text: qsTr("+ Entry on %1").arg(cal.cursor.toLocaleDateString(cal.locale, "d MMM yyyy"))
            shortcut: "Ctrl+Enter"
            onClicked: cal.newEntryOnRequested(cal.iso(cal.cursor))
        }
    }
}

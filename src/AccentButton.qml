import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The main action on a screen, in the theme's accent.
Button {
    id: button
    highlighted: true
    // Material.roundedScale isn't inherited, and takes a binding, not a
    // literal number.
    Material.roundedScale: theme.radius
    // A highlighted button colors its caption by Material's own rule, not
    // Material.foreground, so it can't read on every accent.
    contentItem: Label {
        text: button.text
        font: button.font
        color: button.enabled ? theme.accentForeground : theme.colors.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    // Material picks a highlighted button's fill from Material.background,
    // which the window sets to the theme's, so the accent is drawn here.
    background: Rectangle {
        implicitWidth: 64
        implicitHeight: 40
        radius: theme.radius
        color: !button.enabled ? theme.colors.selection
             : button.down ? Qt.darker(theme.accent, 1.15)
             : button.hovered ? Qt.lighter(theme.accent, 1.08)
             : theme.accent
    }
}

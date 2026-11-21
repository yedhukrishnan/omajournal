import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// How strong a new passphrase looks: four bars and a word.
RowLayout {
    id: meter
    property string passphrase: ""
    readonly property int strength: vault.passphraseStrength(passphrase)

    spacing: 6

    Repeater {
        model: 3
        delegate: Rectangle {
            required property int index
            Layout.preferredWidth: 36
            Layout.preferredHeight: 4
            radius: Math.min(2, theme.radius)
            color: meter.strength > index
                   ? (meter.strength === 1 ? theme.colors.red : meter.strength === 2 ? theme.colors.yellow : theme.colors.green)
                   : theme.colors.selection
        }
    }

    Label {
        Layout.leftMargin: 6
        color: theme.secondaryText
        text: meter.passphrase.length === 0 ? ""
              : meter.strength === 0 ? qsTr("At least 8 characters")
              : meter.strength === 1 ? qsTr("Weak")
              : meter.strength === 2 ? qsTr("Fair")
              : qsTr("Strong")
    }
}

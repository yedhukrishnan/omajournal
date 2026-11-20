import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The locked journal: a passphrase, or the recovery key and a new passphrase.
FocusScope {
    id: screen
    objectName: "unlockScreen"

    property bool recovering: false
    property string error: ""

    Connections {
        target: vault
        function onFailed(message) { screen.error = message }
    }

    Rectangle { anchors.fill: parent; color: theme.background }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 420)
        spacing: 14

        Label {
            Layout.fillWidth: true
            text: screen.recovering ? qsTr("Use your recovery key") : qsTr("Unlock your journal")
            font.pixelSize: 26
            font.bold: true
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            text: vault.path
            color: theme.colors.muted
            elide: Text.ElideMiddle
        }

        // The passphrase.
        ColumnLayout {
            visible: !screen.recovering
            Layout.fillWidth: true
            spacing: 12

            TextField {
                id: passphraseField
                objectName: "unlockField"
                Layout.fillWidth: true
                placeholderText: qsTr("Passphrase")
                echoMode: TextInput.Password
                enabled: !vault.busy
                focus: !screen.recovering
                Material.roundedScale: theme.radius
                onAccepted: unlockButton.clicked()
                // A field loses focus while it's disabled; after a wrong try
                // it's ready for the next one.
                onEnabledChanged: if (enabled && !screen.recovering) forceActiveFocus()
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    objectName: "forgotButton"
                    text: qsTr("Forgot your passphrase?")
                    flat: true
                    enabled: !vault.busy
                    Material.roundedScale: theme.radius
                    onClicked: {
                        screen.error = ""
                        screen.recovering = true
                        recoveryField.forceActiveFocus()
                    }
                }
                Item { Layout.fillWidth: true }
                BusyIndicator {
                    visible: vault.busy
                    running: vault.busy
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                }
                AccentButton {
                    id: unlockButton
                    objectName: "unlockButton"
                    text: qsTr("Unlock")
                    enabled: !vault.busy && passphraseField.text.length > 0
                    onClicked: {
                        if (!enabled)
                            return
                        screen.error = ""
                        vault.unlock(passphraseField.text)
                        passphraseField.clear()
                    }
                }
            }
        }

        // The recovery key and a new passphrase.
        ColumnLayout {
            visible: screen.recovering
            Layout.fillWidth: true
            spacing: 12

            TextField {
                id: recoveryField
                objectName: "recoveryField"
                Layout.fillWidth: true
                placeholderText: qsTr("Recovery key")
                font.family: "monospace"
                enabled: !vault.busy
                Material.roundedScale: theme.radius
                onAccepted: newField.forceActiveFocus()
                onEnabledChanged: if (enabled && screen.recovering) forceActiveFocus()
            }
            TextField {
                id: newField
                objectName: "newPassphraseField"
                Layout.fillWidth: true
                placeholderText: qsTr("New passphrase")
                echoMode: TextInput.Password
                enabled: !vault.busy
                Material.roundedScale: theme.radius
                onAccepted: newRepeatField.forceActiveFocus()
            }
            StrengthMeter { passphrase: newField.text }
            TextField {
                id: newRepeatField
                objectName: "newRepeatField"
                Layout.fillWidth: true
                placeholderText: qsTr("New passphrase again")
                echoMode: TextInput.Password
                enabled: !vault.busy
                Material.roundedScale: theme.radius
                onAccepted: recoverButton.clicked()
            }
            Label {
                Layout.fillWidth: true
                visible: newRepeatField.text.length > 0 && newRepeatField.text !== newField.text
                color: theme.colors.red
                text: qsTr("The passphrases don't match.")
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("Back")
                    flat: true
                    enabled: !vault.busy
                    Material.roundedScale: theme.radius
                    onClicked: {
                        screen.error = ""
                        screen.recovering = false
                        passphraseField.forceActiveFocus()
                    }
                }
                Item { Layout.fillWidth: true }
                BusyIndicator {
                    visible: vault.busy
                    running: vault.busy
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                }
                AccentButton {
                    id: recoverButton
                    objectName: "recoverButton"
                    text: qsTr("Unlock and set passphrase")
                    enabled: !vault.busy && recoveryField.text.length > 0
                             && vault.passphraseStrength(newField.text) > 0 && newField.text === newRepeatField.text
                    onClicked: {
                        if (!enabled)
                            return
                        screen.error = ""
                        vault.recover(recoveryField.text, newField.text)
                        newField.clear()
                        newRepeatField.clear()
                    }
                }
            }
        }

        Label {
            objectName: "unlockError"
            Layout.fillWidth: true
            visible: screen.error !== ""
            wrapMode: Text.Wrap
            color: theme.colors.red
            text: screen.error
        }
    }
}

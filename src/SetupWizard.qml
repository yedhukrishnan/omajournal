import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// First run: where the journal lives, its passphrase, and its recovery key.
FocusScope {
    id: wizard
    objectName: "setupWizard"
    // The Loader passes focus on only to an item that asks for it.
    focus: true

    // From the moment the keys are being made until the recovery key is confirmed.
    signal creating()
    signal finished()

    property int step: 0
    property string recoveryKey: ""
    property string error: ""

    Connections {
        target: vault
        function onCreated(key) {
            wizard.recoveryKey = key
            wizard.step = 2
            confirmField.forceActiveFocus()
        }
        function onFailed(message) { wizard.error = message }
    }

    Rectangle { anchors.fill: parent; color: theme.background }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 480)
        spacing: 16

        Label {
            Layout.fillWidth: true
            text: wizard.step === 2 ? qsTr("Save your recovery key") : qsTr("Welcome to Omajournal")
            font.pixelSize: 26
            font.bold: true
            wrapMode: Text.Wrap
        }

        // Step 0: the folder.
        ColumnLayout {
            visible: wizard.step === 0
            Layout.fillWidth: true
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.colors.muted
                text: qsTr("Every entry is encrypted with a passphrase only you know. Choose a folder for the journal; syncing or backing it up only ever copies encrypted files.")
            }
            TextField {
                id: folderField
                objectName: "folderField"
                Layout.fillWidth: true
                placeholderText: qsTr("Folder")
                text: vault.path
                focus: wizard.step === 0
                Material.roundedScale: theme.radius
                onTextEdited: vault.path = text
                onAccepted: if (vault.pathProblem === "") wizard.step = 1
            }
            Label {
                Layout.fillWidth: true
                visible: vault.pathProblem !== ""
                wrapMode: Text.Wrap
                color: theme.colors.red
                text: vault.pathProblem
            }
            AccentButton {
                objectName: "continueButton"
                Layout.alignment: Qt.AlignRight
                text: qsTr("Continue")
                enabled: vault.pathProblem === ""
                onClicked: {
                    wizard.step = 1
                    passphraseField.forceActiveFocus()
                }
            }
        }

        // Step 1: the passphrase.
        ColumnLayout {
            visible: wizard.step === 1
            Layout.fillWidth: true
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.colors.muted
                text: qsTr("Pick a passphrase you'll remember. A few unrelated words make a strong one. It can't be recovered without the recovery key you'll see next.")
            }
            TextField {
                id: passphraseField
                objectName: "passphraseField"
                Layout.fillWidth: true
                placeholderText: qsTr("Passphrase")
                echoMode: TextInput.Password
                enabled: !vault.busy
                Material.roundedScale: theme.radius
                onAccepted: repeatField.forceActiveFocus()
            }
            StrengthMeter { passphrase: passphraseField.text }
            TextField {
                id: repeatField
                objectName: "repeatField"
                Layout.fillWidth: true
                placeholderText: qsTr("Passphrase again")
                echoMode: TextInput.Password
                enabled: !vault.busy
                Material.roundedScale: theme.radius
                onAccepted: if (createButton.enabled) createButton.clicked()
            }
            Label {
                Layout.fillWidth: true
                visible: repeatField.text.length > 0 && repeatField.text !== passphraseField.text
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
                    onClicked: wizard.step = 0
                }
                Item { Layout.fillWidth: true }
                BusyIndicator {
                    visible: vault.busy
                    running: vault.busy
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                }
                AccentButton {
                    id: createButton
                    objectName: "createButton"
                    text: vault.busy ? qsTr("Making your keys…") : qsTr("Create journal")
                    enabled: !vault.busy && vault.passphraseStrength(passphraseField.text) > 0
                             && passphraseField.text === repeatField.text
                    onClicked: {
                        wizard.error = ""
                        wizard.creating()
                        vault.create(passphraseField.text)
                        passphraseField.clear()
                        repeatField.clear()
                    }
                }
            }
        }

        // Step 2: the recovery key, shown once.
        ColumnLayout {
            visible: wizard.step === 2
            Layout.fillWidth: true
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.colors.muted
                text: qsTr("If you forget your passphrase, this key is the only way back in. Write it down or keep it in a password manager. It won't be shown again.")
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: keyText.implicitHeight + 32
                color: "transparent"
                border.color: theme.colors.selection
                radius: theme.radius

                TextEdit {
                    id: keyText
                    objectName: "recoveryKey"
                    anchors.fill: parent
                    anchors.margins: 16
                    text: wizard.recoveryKey
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    font.family: "monospace"
                    font.pixelSize: 17
                    color: theme.accent
                    selectionColor: theme.colors.selection
                    selectedTextColor: theme.foreground
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            Button {
                text: qsTr("Copy")
                flat: true
                Material.roundedScale: theme.radius
                onClicked: {
                    keyText.selectAll()
                    keyText.copy()
                    keyText.deselect()
                }
            }
            TextField {
                id: confirmField
                objectName: "confirmField"
                Layout.fillWidth: true
                placeholderText: qsTr("Type its last two groups to confirm")
                Material.roundedScale: theme.radius
                onAccepted: if (doneButton.enabled) doneButton.clicked()
            }
            AccentButton {
                id: doneButton
                objectName: "doneButton"
                Layout.alignment: Qt.AlignRight
                text: qsTr("Open my journal")
                enabled: vault.confirmsRecoveryKey(wizard.recoveryKey, confirmField.text)
                onClicked: {
                    wizard.recoveryKey = ""
                    wizard.finished()
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: wizard.error !== ""
            wrapMode: Text.Wrap
            color: theme.colors.red
            text: wizard.error
        }
    }
}

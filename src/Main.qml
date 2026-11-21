import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1100
    height: 720
    minimumWidth: 640
    minimumHeight: 460
    visible: true
    title: "Omajournal"
    color: theme.background

    Material.theme: theme.light ? Material.Light : Material.Dark
    Material.accent: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground

    readonly property bool rightToLeft: Qt.application.layoutDirection === Qt.RightToLeft
    // From the moment a journal is made until its recovery key is confirmed.
    property bool settingUp: false
    readonly property bool journalOpen: !vault.locked && !settingUp
    // What to do once a failed save has been answered.
    property string pendingAction: ""

    function lockJournal() {
        if (!backend.lock()) {
            pendingAction = "lock"
            saveFailed.open()
        }
    }

    // Closing locks: the open entry is saved first, and a save that fails
    // asks rather than throwing the text away.
    onClosing: (close) => {
        if (vault.locked || pendingAction === "quit")
            return
        if (!backend.lock()) {
            close.accepted = false
            pendingAction = "quit"
            saveFailed.open()
        }
    }

    Shortcut {
        sequences: ["F1", "Ctrl+?"]
        onActivated: help.opened ? help.close() : help.open()
    }
    Shortcut { sequence: "Ctrl+Q"; onActivated: win.close() }
    Shortcut { sequence: "Ctrl+W"; onActivated: win.close() }
    Shortcut {
        sequence: "Ctrl+N"
        enabled: win.journalOpen
        onActivated: {
            backend.newEntry()
            content.item.focusEditor()
        }
    }
    Shortcut {
        sequence: "Ctrl+T"
        enabled: win.journalOpen
        onActivated: content.item.startTag()
    }
    Shortcut {
        sequence: "Ctrl+G"
        enabled: win.journalOpen
        onActivated: content.item.locate()
    }
    Shortcut {
        sequence: "Ctrl+D"
        enabled: win.journalOpen
        onActivated: content.item.editDate()
    }
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: win.journalOpen
        onActivated: content.item.focusSearch()
    }
    Shortcut {
        sequences: [StandardKey.Save]
        enabled: win.journalOpen
        onActivated: backend.save()
    }
    Shortcut {
        sequence: "Ctrl+Shift+L"
        enabled: win.journalOpen
        onActivated: win.lockJournal()
    }
    Shortcut {
        sequence: "Ctrl+1"
        enabled: win.journalOpen
        onActivated: content.item.focusCalendar()
    }
    Shortcut {
        sequence: "Ctrl+2"
        enabled: win.journalOpen
        onActivated: content.item.focusList()
    }
    // The writing font: a picker, and the size from the keyboard.
    Shortcut {
        sequence: "Ctrl+Shift+F"
        enabled: win.journalOpen
        onActivated: fontPopup.opened ? fontPopup.close() : fontPopup.open()
    }
    Shortcut {
        sequences: [StandardKey.ZoomIn, "Ctrl+="]
        enabled: win.journalOpen
        onActivated: preferences.enlargeFont()
    }
    Shortcut {
        sequences: [StandardKey.ZoomOut]
        enabled: win.journalOpen
        onActivated: preferences.shrinkFont()
    }
    Shortcut {
        sequence: "Ctrl+0"
        enabled: win.journalOpen
        onActivated: preferences.resetFontSize()
    }
    Shortcut {
        sequence: "Ctrl+3"
        enabled: win.journalOpen
        onActivated: content.item.focusEditor()
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: win.rightToLeft
        LayoutMirroring.childrenInherit: true

        // Locking destroys the journal view, and every decrypted text with it.
        Loader {
            id: content
            anchors.fill: parent
            focus: true
            source: !vault.exists || win.settingUp ? "qrc:/SetupWizard.qml"
                  : vault.locked ? "qrc:/UnlockScreen.qml"
                  : "qrc:/JournalView.qml"
        }

        Connections {
            target: content.item
            ignoreUnknownSignals: true
            function onCreating() { win.settingUp = true }
            function onFinished() { win.settingUp = false }
            function onLockRequested() { win.lockJournal() }
            function onFontRequested() { fontPopup.open() }
            function onNewEntryRequested() {
                backend.newEntry()
                content.item.focusEditor()
            }
        }
    }

    Dialog {
        id: saveFailed
        objectName: "saveFailed"
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        title: qsTr("The entry couldn't be saved")
        // Escape and closing keep the entry open: the answer that loses nothing.
        onRejected: win.pendingAction = ""

        ColumnLayout {
            spacing: 12
            LayoutMirroring.enabled: win.rightToLeft
            LayoutMirroring.childrenInherit: true

            Label {
                Layout.maximumWidth: 420
                wrapMode: Text.Wrap
                color: theme.colors.red
                text: backend.saveError
            }
            Label {
                Layout.maximumWidth: 420
                wrapMode: Text.Wrap
                text: qsTr("Keep editing to try again, or discard the changes since the last save.")
            }
        }

        footer: DialogButtonBox {
            LayoutMirroring.enabled: win.rightToLeft
            LayoutMirroring.childrenInherit: true
            Button {
                text: qsTr("Keep editing")
                flat: true
                Material.roundedScale: theme.radius
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "discardButton"
                text: win.pendingAction === "quit" ? qsTr("Quit without saving") : qsTr("Lock without saving")
                flat: true
                Material.roundedScale: theme.radius
                Material.foreground: theme.colors.red
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                onClicked: {
                    const quitting = win.pendingAction === "quit"
                    saveFailed.close()
                    backend.lock(true)
                    win.pendingAction = quitting ? "quit" : ""
                    if (quitting)
                        win.close()
                }
            }
        }
    }

    FontPopup {
        id: fontPopup
        rightToLeft: win.rightToLeft
        // The font stays chosen; typing goes back to the entry.
        onClosed: if (win.journalOpen) content.item.focusEditor()
    }

    ShortcutsOverlay {
        id: help
        objectName: "help"
        rightToLeft: win.rightToLeft
    }
}

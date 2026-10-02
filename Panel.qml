import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons
import qs.Ui
import "Model.js" as Model

// hlsovpp settings (a fork of yayuuu/hyprland-scroll-overview). Every
// change is applied live with `hyprctl eval`, then written to
// ~/.config/hypr/hlsovpp.lua so it survives a restart. The Lua file is the
// only store: hand edits show up here.
//
// Open with: omarchy-shell shell toggle jgarza.hlsovpp
Item {
  id: root

  property var shell: null
  property var manifest: null

  readonly property string luaPath: Quickshell.env("HOME") + "/.config/hypr/hlsovpp.lua"

  property bool opened: false
  property var settings: Model.defaults()
  property bool pluginLoaded: false
  property int sectionIndex: 0
  property int cursorIndex: 0
  property string errorText: ""
  property string statusText: ""
  property bool selfWrite: false

  readonly property string section: Model.SECTIONS[sectionIndex]
  readonly property var rows: Model.rowsFor(section, settings)
  onRowsChanged: if (cursorIndex >= rows.length) cursorIndex = Math.max(0, rows.length - 1)

  property color background: Color.menu.background
  property color foreground: Color.menu.text
  property color accent: Color.accent
  property color scrim: Color.menu.scrim
  property string fontFamily: Style.font.menuFamily

  // -------------------------------------------------------------- lifecycle

  function open(payloadJson) {
    root.opened = true
    root.errorText = ""
    luaFile.reload()
    statusProc.running = true
    Qt.callLater(function() { keyCatcher.forceActiveFocus() })
  }

  function close() {
    if (persistTimer.running) persistNow()
    root.opened = false
  }

  function dismiss() {
    if (root.shell && typeof root.shell.hide === "function")
      root.shell.hide((root.manifest && root.manifest.id) || "jgarza.hlsovpp")
    else close()
  }

  function toggle() {
    if (root.opened) dismiss()
    else open("{}")
  }

  // ----------------------------------------------------------------- values

  function resetValue(key) {
    var item = Model.itemFor(key)
    if (item) setValue(key, item.fallback, true)
  }

  function setValue(key, raw, commit) {
    var c = Model.coerce(key, raw)
    if (!c.ok) { root.errorText = c.error; return }
    root.errorText = ""

    var next = {}
    for (var k in root.settings) next[k] = root.settings[k]
    next[key] = c.value
    root.settings = next

    push()
    if (commit) persistTimer.restart()
  }

  // One eval in flight; if more changes land meanwhile, send the newest
  // settings once it finishes, so a slider drag can't outrun hyprctl.
  property bool dirty: false
  function push() {
    if (applyProc.running) { root.dirty = true; return }
    root.dirty = false
    applyProc.command = Model.evalCommand(root.settings)
    applyProc.running = true
  }

  function persistNow() {
    persistTimer.stop()
    var next = Model.renderLua(root.settings)
    if (next === luaFile.text()) return
    root.selfWrite = true
    root.statusText = "Saving…"
    luaFile.setText(next)
  }

  function resetAll() {
    root.settings = Model.defaults()
    push()
    persistNow()
  }

  // Hide the panel, then open the overview so the motion can be seen.
  function test() {
    if (persistTimer.running) persistNow()
    dismiss()
    previewTimer.restart()
  }

  // ------------------------------------------------------------- keyboard

  function moveSection(delta) {
    var n = Model.SECTIONS.length
    root.sectionIndex = (root.sectionIndex + delta + n) % n
    root.cursorIndex = 0
  }

  function moveCursor(delta) {
    var n = root.rows.length
    if (n === 0) return
    root.cursorIndex = (root.cursorIndex + delta + n) % n
    rowList.positionViewAtIndex(root.cursorIndex, ListView.Contain)
  }

  function cursorItem() {
    return root.cursorIndex >= 0 && root.cursorIndex < root.rows.length ? root.rows[root.cursorIndex] : null
  }

  function nudgeCursor(direction) {
    var item = cursorItem()
    if (!item) return
    var next = Model.nudge(item, root.settings[item.key], direction)
    if (String(next) !== String(root.settings[item.key])) setValue(item.key, next, true)
  }

  function activateCursor() {
    var item = cursorItem()
    if (!item) return
    if (item.type === "bool") setValue(item.key, root.settings[item.key] !== true, true)
    else if (item.type === "enum") nudgeCursor(1)
  }

  // -------------------------------------------------------------- processes

  Process {
    id: applyProc
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        var out = String(text || "").trim()
        if (out !== "" && out !== "ok") root.errorText = out
      }
    }
    onExited: if (root.dirty) Qt.callLater(root.push)
  }

  Process {
    id: statusProc
    command: ["hyprctl", "plugin", "list"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: root.pluginLoaded = Model.pluginLoaded(text)
    }
  }

  Timer {
    id: previewTimer
    interval: 250
    onTriggered: Quickshell.execDetached(Model.PREVIEW_COMMAND)
  }

  Timer {
    id: persistTimer
    interval: 450
    onTriggered: root.persistNow()
  }

  Timer {
    interval: 1600
    running: root.statusText === "Saved"
    onTriggered: root.statusText = ""
  }

  FileView {
    id: luaFile
    path: root.luaPath
    atomicWrites: true
    printErrors: false
    watchChanges: true
    onLoaded: root.settings = Model.parseLua(text())
    onLoadFailed: root.settings = Model.defaults()
    onSaved: { root.selfWrite = false; root.statusText = "Saved" }
    onSaveFailed: { root.selfWrite = false; root.statusText = ""; root.errorText = "Could not write ~/.config/hypr/hlsovpp.lua" }
    // Adopt a hand edit, but never mid-drag.
    onFileChanged: {
      if (root.selfWrite || persistTimer.running) return
      reload()
    }
  }

  // --------------------------------------------------------------------- UI

  PanelWindow {
    id: window
    visible: root.opened
    anchors { top: true; bottom: true; left: true; right: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "hlsovpp"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive

    Rectangle {
      anchors.fill: parent
      color: root.scrim
      MouseArea { anchors.fill: parent; onClicked: root.dismiss() }
    }

    BorderSurface {
      id: card
      anchors.centerIn: parent
      width: Math.min(Style.space(720), window.width - Style.gapsOut * 4)
      height: Math.min(Style.space(600), window.height - Style.gapsOut * 4)
      radius: Style.cornerRadius
      color: root.background
      borderSpec: Border.surfaceSpec("menu", "border", Color.menu.border, Math.max(1, Style.space(2)))
      padding: Style.spacing.panelPadding

      MouseArea { anchors.fill: parent; onClicked: {} }

      Item {
        id: keyCatcher
        anchors.fill: parent
        focus: true
        // Arrows or hjkl (unmodified, so Ctrl+L etc. stay with the
        // compositor). Text fields take their own keys while focused; Esc or
        // Enter hands focus back here.
        Keys.onPressed: function(event) {
          var vim = !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
          if (event.key === Qt.Key_Escape) root.dismiss()
          else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
            root.moveSection(event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier) ? -1 : 1)
          else if (event.key === Qt.Key_Down || (vim && event.key === Qt.Key_J)) root.moveCursor(1)
          else if (event.key === Qt.Key_Up || (vim && event.key === Qt.Key_K)) root.moveCursor(-1)
          else if (event.key === Qt.Key_Right || (vim && event.key === Qt.Key_L)) root.nudgeCursor(1)
          else if (event.key === Qt.Key_Left || (vim && event.key === Qt.Key_H)) root.nudgeCursor(-1)
          else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) root.activateCursor()
          else if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
            var item = root.cursorItem()
            if (item) root.resetValue(item.key)
          }
          else if (vim && event.key === Qt.Key_T) root.test()
          else return
          event.accepted = true
        }
      }

      ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: card.contentTopInset
        anchors.rightMargin: card.contentRightInset
        anchors.bottomMargin: card.contentBottomInset
        anchors.leftMargin: card.contentLeftInset
        spacing: Style.spacing.panelGap

        // Header
        Item {
          Layout.fillWidth: true
          Layout.preferredHeight: Math.max(titleBlock.implicitHeight, actions.implicitHeight)

          Column {
            id: titleBlock
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Style.spacing.xxs

            Text {
              text: "Scroll Overview"
              color: root.foreground
              font.family: root.fontFamily
              font.pixelSize: Style.font.heading
              font.bold: true
            }

            Text {
              text: root.pluginLoaded
                ? "~/.config/hypr/hlsovpp.lua · fork of yayuuu/hyprland-scroll-overview"
                : "Native plugin not loaded. Run ./install in the plugin folder (or: hyprpm update)"
              color: root.pluginLoaded ? Qt.darker(root.foreground, 1.6) : Color.urgent
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
            }
          }

          Row {
            id: actions
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Style.spacing.lg

            Text {
              text: root.statusText
              visible: text !== ""
              color: Qt.darker(root.foreground, 1.4)
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              anchors.verticalCenter: parent.verticalCenter
            }

            Button {
              text: "Preview"
              iconText: "󰕰"
              tooltipText: "Close this and open the overview (T)"
              foreground: root.foreground
              accent: root.accent
              bordered: true
              enabled: root.pluginLoaded
              anchors.verticalCenter: parent.verticalCenter
              onClicked: root.test()
            }

            PanelActionButton {
              iconText: "󰕌"
              tooltipText: "Reset everything to defaults"
              foreground: root.foreground
              anchors.verticalCenter: parent.verticalCenter
              onClicked: root.resetAll()
            }

            PanelActionButton {
              iconText: "󰅖"
              tooltipText: "Close (Esc)"
              foreground: root.foreground
              anchors.verticalCenter: parent.verticalCenter
              onClicked: root.dismiss()
            }
          }
        }

        ButtonGroup {
          options: Model.SECTIONS
          value: root.section
          foreground: root.foreground
          accent: root.accent
          focusable: false
          onChanged: function(v) { root.sectionIndex = Math.max(0, Model.SECTIONS.indexOf(v)); root.cursorIndex = 0 }
        }

        PanelSeparator { Layout.fillWidth: true }

        ListView {
          id: rowList
          Layout.fillWidth: true
          Layout.fillHeight: true
          clip: true
          spacing: Style.spacing.xs
          model: root.rows
          boundsBehavior: Flickable.StopAtBounds

          delegate: SettingRow {
            required property var modelData
            required property int index
            width: ListView.view.width
            item: modelData
            value: root.settings[modelData.key]
            foreground: root.foreground
            accent: root.accent
            fontFamily: root.fontFamily
            defaultValue: modelData.fallback
            onEdited: function(v) { root.setValue(modelData.key, v, false) }
            onCommitted: function(v) { root.setValue(modelData.key, v, true) }
            onReset: root.resetValue(modelData.key)
            hasCursor: index === root.cursorIndex
            onFocusRequested: root.cursorIndex = index
            onDoneEditing: keyCatcher.forceActiveFocus()
          }
        }

        Text {
          Layout.fillWidth: true
          text: root.errorText !== "" ? root.errorText : "Tab: section · ↑↓/jk: row · ←→/hl: change · Enter: toggle · ⌫: reset row · T: preview · Esc: close"
          color: root.errorText !== "" ? Color.urgent : Qt.darker(root.foreground, 1.6)
          font.family: root.fontFamily
          font.pixelSize: Style.font.caption
          wrapMode: Text.WordWrap
        }
      }
    }
  }
}

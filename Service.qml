import QtQuick
import Quickshell
import Quickshell.Io
import "Model.js" as Model

// Keeps the native plugin in step with ~/.config/hypr/hlsovpp.lua:
//
//   - creates the file with defaults the first time, so hyprland.lua's
//     require has something to load
//   - pushes the file to the plugin (hyprctl eval) at login and on every
//     edit, so a hand edit applies live without `hyprctl reload`
//   - says so once if hyprpm has the plugin enabled but it isn't loading
//     (usually: Hyprland updated, run `hyprpm update`)
//
// IPC: omarchy-shell ipc call hlsovpp apply | preview
Item {
  id: root

  property var shell: null
  property var manifest: null

  readonly property string luaPath: Quickshell.env("HOME") + "/.config/hypr/hlsovpp.lua"

  function apply(text) {
    applyProc.command = Model.evalCommand(Model.parseLua(text))
    if (!applyProc.running) applyProc.running = true
  }

  Process { id: applyProc }

  FileView {
    id: luaFile
    path: root.luaPath
    printErrors: false
    watchChanges: true
    atomicWrites: true
    onLoaded: root.apply(text())
    onLoadFailed: setText(Model.renderLua(Model.defaults()))
    onFileChanged: reload()
  }

  // The plugin may load (hyprpm reload -n) after the shell started; re-apply
  // once it's had a moment, then check it actually loaded.
  Timer {
    interval: 4000
    running: true
    onTriggered: { luaFile.reload(); healthProc.running = true }
  }

  property bool warned: false

  Process {
    id: healthProc
    command: ["hyprctl", "plugin", "list"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: if (!Model.pluginLoaded(text)) hyprpmProc.running = true
    }
  }

  // Not loaded: only worth a word if hyprpm has it enabled (a stale build
  // after a Hyprland update) - not if the user simply never installed it.
  Process {
    id: hyprpmProc
    command: ["hyprpm", "list"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        var out = String(text || "").replace(/\x1b\[[0-9;]*m/g, "")
        if (root.warned || !/Plugin scrolloverview[\s\S]*?enabled:\s*true/.test(out)) return
        root.warned = true
        Quickshell.execDetached(["notify-send", "-a", "hlsovpp", "Scroll overview isn't loading",
                                 "Hyprland was probably updated. Rebuild the plugin with: hyprpm update"])
      }
    }
  }

  IpcHandler {
    target: "hlsovpp"

    function apply(): string {
      luaFile.reload()
      return "ok"
    }

    function preview(): string {
      Quickshell.execDetached(Model.PREVIEW_COMMAND)
      return "ok"
    }
  }
}

// Pure helpers for hlsovpp. Imported from QML as `import "Model.js" as Model`
// and ES5-only so the same file runs under node (tests/model.test.js).
//
// ~/.config/hypr/hlsovpp.lua is the one place settings live: the panel
// renders it, Hyprland reads it on every reload, and a hand edit is read back
// by parseLua. Live changes go through `hyprctl eval` with the same
// hl.config() table, so nothing waits on a reload.
//
// Keys are Hyprland option paths below plugin:scrolloverview, so
// "motion:style" is plugin.scrolloverview.motion.style in Lua.

var LUA_HEADER = "-- hlsovpp: ScrollOverview with per-window motion (jgarza.hlsovpp)."
var PLUGIN = "scrolloverview"

var STYLE_OPTIONS = [
  { value: "none", label: "None" },
  { value: "ripple", label: "Ripple" },
  { value: "converge", label: "Converge" },
  { value: "sweep", label: "Sweep" },
  { value: "random", label: "Random" },
  { value: "jitter", label: "Jitter" },
  { value: "scatter", label: "Scatter" },
  { value: "spring", label: "Spring" }
]

// Order here is the order rows appear in the panel and keys in the file.
var SCHEMA = [
  { section: "Motion", key: "motion:style", type: "enum", label: "Motion style",
    description: "How windows move when the overview opens and closes. None: all together (upstream). Ripple: nearest to the origin first. Converge: farthest first. Sweep: one after another in layout order. Random: scattered delays. Jitter: same start, different curves. Scatter: random + jitter. Spring: ripple with an overshoot.",
    options: STYLE_OPTIONS, fallback: "ripple" },
  { section: "Motion", key: "motion:spread", type: "float", label: "Stagger",
    description: "How much of the animation is spent staggering windows. 0 = together.",
    min: 0, max: 0.9, step: 0.05, decimals: 2, fallback: 0.35,
    needs: "motion:style", needsValue: ["ripple", "converge", "sweep", "random", "scatter", "spring"] },
  { section: "Motion", key: "motion:origin", type: "enum", label: "Wave origin",
    description: "Where the ripple starts.",
    options: [
      { value: "focus", label: "Focused window" },
      { value: "cursor", label: "Cursor" }
    ], fallback: "focus", needs: "motion:style", needsValue: ["ripple", "converge", "spring"] },
  { section: "Motion", key: "motion:direction", type: "enum", label: "Sweep direction",
    description: "Forward: first workspace, left to right. Reverse: the other way.",
    options: [
      { value: "forward", label: "Forward" },
      { value: "reverse", label: "Reverse" }
    ], fallback: "forward", needs: "motion:style", needsValue: "sweep" },
  { section: "Motion", key: "motion:jitter", type: "float", label: "Jitter",
    description: "How different each window's easing curve is.",
    min: 0, max: 1, step: 0.05, decimals: 2, fallback: 0.5,
    needs: "motion:style", needsValue: ["jitter", "scatter"] },
  { section: "Motion", key: "motion:overshoot", type: "float", label: "Overshoot",
    description: "How far windows travel past their spot before settling.",
    min: 0, max: 1, step: 0.05, decimals: 2, fallback: 0.4,
    needs: "motion:style", needsValue: "spring" },
  { section: "Motion", key: "motion:rewind_on_close", type: "bool", label: "Rewind on close",
    description: "Closing plays the opening order backwards: the first window out is the last one home.",
    fallback: true, needs: "motion:style", needsValue: ["ripple", "converge", "sweep", "random", "scatter", "spring"] },
  { section: "Motion", key: "motion:on_gesture", type: "bool", label: "During gestures",
    description: "Also stagger windows while a touchpad swipe drives the overview.", fallback: false,
    needs: "motion:style", needsValue: ["ripple", "converge", "sweep", "random", "jitter", "scatter", "spring"] },

  { section: "Layout", key: "scale", type: "float", label: "Scale",
    description: "How small workspaces get in the overview.",
    min: 0.1, max: 0.9, step: 0.05, decimals: 2, fallback: 0.5 },
  { section: "Layout", key: "workspace_gap", type: "int", label: "Workspace gap",
    description: "Space between workspaces in the overview.", unit: "px",
    min: 0, max: 400, step: 5, fallback: 0 },
  { section: "Layout", key: "layout", type: "enum", label: "Direction",
    description: "Stack workspaces vertically or horizontally. Auto: horizontal on portrait monitors.",
    options: [
      { value: "vertical", label: "Vertical" },
      { value: "horizontal", label: "Horizontal" },
      { value: "auto", label: "Auto" }
    ], fallback: "vertical" },
  { section: "Layout", key: "gesture_distance", type: "int", label: "Gesture distance",
    description: "How far a swipe travels to fully open the overview.", unit: "px",
    min: 50, max: 1000, step: 10, fallback: 200 },
  { section: "Layout", key: "cross_monitor_drag", type: "bool", label: "Cross-monitor drag",
    description: "Drag windows between monitors through the overview.", fallback: false },

  { section: "Look", key: "wallpaper", type: "enum", label: "Wallpaper",
    description: "Global: one wallpaper behind everything. Per workspace: each card gets its own. Both: both.",
    options: [
      { value: 0, label: "Global" },
      { value: 1, label: "Per workspace" },
      { value: 2, label: "Both" }
    ], fallback: 0 },
  { section: "Look", key: "blur", type: "bool", label: "Blur wallpaper",
    description: "Blur the main overview wallpaper (not the workspace ones).", fallback: false },
  { section: "Look", key: "shadow:enabled", type: "bool", label: "Card shadow",
    description: "Draw a shadow around each workspace card.", fallback: false },
  { section: "Look", key: "shadow:range", type: "int", label: "Shadow range",
    description: "Size of the card shadow.", unit: "px",
    min: 0, max: 200, step: 2, fallback: 50, needs: "shadow:enabled", needsValue: "true" },

  { section: "Input", key: "input:scrolling_mode", type: "enum", label: "Mouse wheel",
    description: "Layout aware, inverted, or split between workspaces and columns.",
    options: [
      { value: 0, label: "Auto" },
      { value: 1, label: "Inverted" },
      { value: 2, label: "V: ws · H: col" },
      { value: 3, label: "V: col · H: ws" }
    ], fallback: 0 },
  { section: "Input", key: "input:drag_mode", type: "enum", label: "Drag",
    description: "Which button drags windows and which pans scrolling workspaces.",
    options: [
      { value: 0, label: "Left drags" },
      { value: 1, label: "Left pans" }
    ], fallback: 0 },
  { section: "Input", key: "input:drag_threshold", type: "int", label: "Drag threshold",
    description: "Movement before a press becomes a drag. 0 = immediately.", unit: "px",
    min: 0, max: 60, step: 1, fallback: 10 },
  { section: "Input", key: "input:touchpad_scroll_factor", type: "float", label: "Touchpad scroll",
    description: "Multiplier for touchpad scrolling inside the overview.",
    min: 0.1, max: 5, step: 0.1, decimals: 1, fallback: 1 },
  { section: "Input", key: "input:scroll_event_delay", type: "int", label: "Scroll delay",
    description: "Minimum time between wheel steps.", unit: "ms",
    min: 0, max: 1000, step: 10, fallback: 200 }
]

var SECTIONS = ["Motion", "Layout", "Look", "Input"]

function itemFor(key) {
  for (var i = 0; i < SCHEMA.length; i++)
    if (SCHEMA[i].key === key) return SCHEMA[i]
  return null
}

function defaults() {
  var out = {}
  for (var i = 0; i < SCHEMA.length; i++) out[SCHEMA[i].key] = SCHEMA[i].fallback
  return out
}

// A dependent row shows when its `needs` key has `needsValue` - one value or
// any of an array of them.
function needsMet(item, settings) {
  var have = String(settings[item.needs])
  var want = item.needsValue
  if (Array.isArray(want)) {
    for (var i = 0; i < want.length; i++)
      if (String(want[i]) === have) return true
    return false
  }
  return String(want) === have
}

function rowsFor(section, settings) {
  var out = []
  for (var i = 0; i < SCHEMA.length; i++) {
    var item = SCHEMA[i]
    if (item.section !== section) continue
    if (item.needs && settings && !needsMet(item, settings)) continue
    out.push(item)
  }
  return out
}

// Validate/normalise one value. Returns { ok, value } or { ok: false, error }.
function coerce(key, raw) {
  var item = itemFor(key)
  if (!item) return { ok: false, error: "unknown setting " + key }

  if (item.type === "bool") {
    if (raw === true || raw === "true" || raw === 1 || raw === "1") return { ok: true, value: true }
    if (raw === false || raw === "false" || raw === 0 || raw === "0") return { ok: true, value: false }
    return { ok: false, error: key + " must be true or false" }
  }

  if (item.type === "int" || item.type === "float") {
    var n = Number(raw)
    if (raw === "" || raw === null || typeof raw === "boolean" || !isFinite(n)) return { ok: false, error: key + " must be a number" }
    n = Math.max(item.min, Math.min(item.max, n))
    if (item.type === "int") n = Math.round(n)
    else n = parseFloat(n.toFixed(item.decimals === undefined ? 2 : item.decimals))
    return { ok: true, value: n }
  }

  if (item.type === "enum") {
    // options keep their type (numbers stay numbers in the Lua file)
    for (var i = 0; i < item.options.length; i++)
      if (String(item.options[i].value) === String(raw)) return { ok: true, value: item.options[i].value }
    return { ok: false, error: key + " must be one of " + item.options.map(function(o) { return o.value }).join(", ") }
  }

  return { ok: false, error: "unsupported setting " + key }
}

// Fill gaps with defaults and drop anything invalid.
function normalize(settings) {
  var out = defaults()
  if (!settings) return out
  for (var key in settings) {
    var c = coerce(key, settings[key])
    if (c.ok) out[key] = c.value
  }
  return out
}

// Keyboard: what Left (-1) / Right (+1) does to a row's value.
function nudge(item, value, direction) {
  if (!item) return value
  if (item.type === "bool") return direction > 0
  if (item.type === "enum") {
    var options = item.options || []
    if (options.length === 0) return value
    var at = -1
    for (var i = 0; i < options.length; i++)
      if (String(options[i].value) === String(value)) at = i
    if (at < 0) return options[direction > 0 ? 0 : options.length - 1].value
    return options[(at + direction + options.length) % options.length].value
  }
  if (item.type === "int" || item.type === "float") {
    var step = item.step === undefined ? 1 : item.step
    var n = Number(value) + step * (direction > 0 ? 1 : -1)
    n = Math.max(item.min, Math.min(item.max, n))
    return parseFloat(n.toFixed(item.type === "int" ? 0 : (item.decimals === undefined ? 2 : item.decimals)))
  }
  return value
}

// ---------------------------------------------------------------- Lua

function luaValue(v) {
  if (typeof v === "boolean") return v ? "true" : "false"
  if (typeof v === "number") return String(v)
  // enum values only (validated), but escape anyway
  return '"' + String(v).replace(/\\/g, "\\\\").replace(/"/g, '\\"').replace(/[\x00-\x1f]/g, "") + '"'
}

// settings -> [{ name, entries: [[leaf, value]] }] in schema order; the
// top-level table has name "".
function groups(settings) {
  var s = normalize(settings)
  var order = []
  var byName = {}
  for (var i = 0; i < SCHEMA.length; i++) {
    var parts = SCHEMA[i].key.split(":")
    var name = parts.length > 1 ? parts[0] : ""
    var leaf = parts[parts.length - 1]
    if (!byName[name]) { byName[name] = { name: name, entries: [] }; order.push(byName[name]) }
    byName[name].entries.push([leaf, s[SCHEMA[i].key]])
  }
  // top-level keys first, then sub-tables
  order.sort(function(a, b) { return (a.name === "" ? 0 : 1) - (b.name === "" ? 0 : 1) })
  return order
}

// The `scrolloverview = { ... }` table body, one key per line.
function tableLines(settings, indent) {
  var lines = []
  var gs = groups(settings)
  for (var g = 0; g < gs.length; g++) {
    var group = gs[g]
    var pad = indent
    if (group.name !== "") {
      lines.push(indent + group.name + " = {")
      pad = indent + "  "
    }
    for (var e = 0; e < group.entries.length; e++)
      lines.push(pad + group.entries[e][0] + " = " + luaValue(group.entries[e][1]) + ",")
    if (group.name !== "") lines.push(indent + "},")
  }
  return lines
}

function renderLua(settings) {
  var lines = [
    LUA_HEADER,
    "-- Fork of https://github.com/yayuuu/hyprland-scroll-overview",
    "-- Written by the panel: omarchy-shell shell toggle jgarza.hlsovpp",
    "-- Hand edits are fine - the panel reads this file back. Keep one key per line.",
    "",
    "if hl.plugin." + PLUGIN + " then",
    "  hl.config({ plugin = { " + PLUGIN + " = {"
  ]
  lines = lines.concat(tableLines(settings, "    "))
  lines.push("  } } })")
  lines.push("end")
  lines.push("")
  return lines.join("\n")
}

// Read `key = value` lines back out of hlsovpp.lua, tracking `name = {` /
// `}` so sub-table keys come back as "name:key". Unknown keys or invalid
// values are ignored, so a broken hand edit can't take the panel down.
function parseLua(text) {
  var out = {}
  var stack = []
  var lines = String(text || "").split("\n")
  for (var i = 0; i < lines.length; i++) {
    var line = lines[i].replace(/--.*$/, "")
    // Value lines have no braces; any line with braces only moves the table
    // stack (`hl.config({ plugin = { scrolloverview = {` opens three). A
    // one-line table like `shadow = { enabled = true },` opens and closes,
    // so its values are ignored rather than misread.
    if (/[{}]/.test(line)) {
      var tokens = line.match(/[a-z_]+\s*=\s*\{|\{|\}/g) || []
      for (var t = 0; t < tokens.length; t++) {
        if (tokens[t] === "}") stack.pop()
        else if (tokens[t] === "{") stack.push("")
        else stack.push(tokens[t].replace(/\s*=\s*\{$/, ""))
      }
      continue
    }
    var m = line.match(/^\s*([a-z_]+)\s*=\s*("(?:[^"\\]|\\.)*"|'[^']*'|true|false|-?[0-9.]+)\s*,?\s*$/)
    if (!m) continue
    // keys sit under ... scrolloverview [= { sub = {] }
    var at = stack.lastIndexOf(PLUGIN)
    if (at === -1) continue
    var path = stack.slice(at + 1)
    var key = path.concat([m[1]]).join(":")
    if (!itemFor(key)) continue
    var raw = m[2]
    var value
    if (raw === "true") value = true
    else if (raw === "false") value = false
    else if (raw.charAt(0) === '"') value = raw.slice(1, -1).replace(/\\(.)/g, "$1")
    else if (raw.charAt(0) === "'") value = raw.slice(1, -1)
    else value = Number(raw)
    var c = coerce(key, value)
    if (c.ok) out[key] = c.value
  }
  return normalize(out)
}

// ------------------------------------------------------------- hyprctl

// Apply everything live: the same table hl.config() gets from the file.
function evalCommand(settings) {
  var body = tableLines(settings, "").map(function(l) { return l.trim() }).join(" ")
  return ["hyprctl", "eval",
          "if hl.plugin." + PLUGIN + " then hl.config({ plugin = { " + PLUGIN + " = { " + body + " } } }) end"]
}

var PREVIEW_COMMAND = ["hyprctl", "eval", "if hl.plugin." + PLUGIN + " then hl.plugin." + PLUGIN + ".overview(\"open all\") end"]

// `hyprctl plugin list` -> is the overview plugin loaded?
function pluginLoaded(text) {
  return /Plugin scrolloverview\b/.test(String(text || ""))
}

if (typeof module !== "undefined") {
  module.exports = {
    SCHEMA: SCHEMA,
    SECTIONS: SECTIONS,
    STYLE_OPTIONS: STYLE_OPTIONS,
    PREVIEW_COMMAND: PREVIEW_COMMAND,
    itemFor: itemFor,
    defaults: defaults,
    needsMet: needsMet,
    rowsFor: rowsFor,
    coerce: coerce,
    normalize: normalize,
    nudge: nudge,
    renderLua: renderLua,
    parseLua: parseLua,
    evalCommand: evalCommand,
    pluginLoaded: pluginLoaded
  }
}

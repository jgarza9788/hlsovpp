// Run: node tests/model.test.js   (also part of `make test`)
"use strict"
var M = require("../Model.js")
var fs = require("fs")
var path = require("path")

var failed = 0
function ok(name, cond) {
  if (cond) console.log("PASS " + name)
  else { console.log("FAIL " + name); failed++ }
}

// ── schema ───────────────────────────────────────────────────────────────────
var keys = M.SCHEMA.map(function(i) { return i.key })
ok("schema keys unique", keys.length === new Set(keys).size)
ok("every row is in a known section", M.SCHEMA.every(function(i) { return M.SECTIONS.indexOf(i.section) !== -1 }))
ok("every fallback is valid", M.SCHEMA.every(function(i) {
  var c = M.coerce(i.key, i.fallback); return c.ok && c.value === i.fallback
}))
ok("every needs points at a real key", M.SCHEMA.every(function(i) { return !i.needs || M.itemFor(i.needs) }))

// Every schema key is an option the native plugin registers, and every motion
// style the panel offers is one Motion.cpp parses.
var config = fs.readFileSync(path.join(__dirname, "..", "Config.cpp"), "utf8")
ok("every key is registered in Config.cpp", keys.every(function(k) {
  return config.indexOf('"plugin:scrolloverview:' + k + '"') !== -1
}))
var motionSrc = fs.readFileSync(path.join(__dirname, "..", "Motion.cpp"), "utf8")
ok("every style is parsed by Motion.cpp", M.STYLE_OPTIONS.every(function(o) {
  return motionSrc.indexOf('{"' + o.value + '", EStyle::') !== -1
}))
ok("every easing (but follow) is a curve in Motion.cpp", M.EASING_OPTIONS.every(function(o) {
  return o.value === "follow" || motionSrc.indexOf('{"' + o.value + '", ') !== -1
}))

// ── coerce ───────────────────────────────────────────────────────────────────
ok("bool from string", M.coerce("blur", "true").value === true)
ok("bool rejects junk", !M.coerce("blur", "yes").ok)
ok("float clamped", M.coerce("scale", 5).value === 0.9)
ok("float rounded to decimals", M.coerce("motion:spread", 0.123456).value === 0.12)
ok("int rounded", M.coerce("workspace_gap", 10.6).value === 11)
ok("number rejects bool", !M.coerce("scale", true).ok)
ok("number rejects empty", !M.coerce("scale", "").ok)
ok("enum accepts known", M.coerce("motion:style", "sweep").value === "sweep")
ok("removed styles rejected (file falls back to default)", !M.coerce("motion:style", "spring").ok && !M.coerce("motion:style", "jitter").ok)
ok("removed keys ignored", !M.coerce("motion:jitter", 0.5).ok && !M.coerce("motion:overshoot", 0.5).ok)
ok("easing accepts named curves", M.coerce("motion:easing", "smooth").value === "smooth")
ok("removed easing names rejected", !M.coerce("motion:easing", "expo-out").ok)
ok("tilt defaults to off", M.defaults()["motion:tilt"] === 0)
ok("tilt accepts negative (lean away) and clamps to +/-45", M.coerce("motion:tilt", -12).value === -12 && M.coerce("motion:tilt", -90).value === -45)
ok("speed defaults to follow windowsMove", M.defaults()["motion:speed"] === 0)
ok("speed accepts slower (negative) and clamps to +/-4", M.coerce("motion:speed", -2).value === -2 && M.coerce("motion:speed", 9).value === 4 && M.coerce("motion:speed", -9).value === -4)
ok("enum rejects unknown", !M.coerce("motion:style", "explode").ok)
ok("numeric enum keeps number type", M.coerce("wallpaper", "2").value === 2)
ok("unknown key rejected", !M.coerce("nope", 1).ok)

// ── rows ─────────────────────────────────────────────────────────────────────
var d = M.defaults()
var motionRows = function(s) { return M.rowsFor("Motion", s).map(function(i) { return i.key }) }
ok("ripple shows origin, hides direction", motionRows(d).indexOf("motion:origin") !== -1 && motionRows(d).indexOf("motion:direction") === -1)
ok("sweep shows direction", motionRows(Object.assign({}, d, { "motion:style": "sweep" })).indexOf("motion:direction") !== -1)
ok("none hides stagger and its options", motionRows(Object.assign({}, d, { "motion:style": "none" })).join() === "motion:style,motion:easing,motion:speed,motion:tilt,motion:on_gesture")
ok("shadow range needs shadow", M.rowsFor("Look", d).every(function(i) { return i.key !== "shadow:range" }))

// ── nudge ────────────────────────────────────────────────────────────────────
ok("nudge enum wraps", M.nudge(M.itemFor("motion:style"), "random", 1) === "none")
ok("nudge float steps cleanly", M.nudge(M.itemFor("scale"), 0.2, 1) === 0.25)
ok("nudge clamps", M.nudge(M.itemFor("scale"), 0.9, 1) === 0.9)

// ── Lua round trip ───────────────────────────────────────────────────────────
var lua = M.renderLua(d)
ok("render guards on the plugin", lua.indexOf("if hl.plugin.scrolloverview then") !== -1)
ok("render nests sub-tables", /motion = \{\n\s+style = "ripple",/.test(lua))
ok("round trip defaults", JSON.stringify(M.parseLua(lua)) === JSON.stringify(d))
var custom = Object.assign({}, d, { scale: 0.2, "motion:style": "sweep", "motion:tilt": 12, "motion:easing": "bouncy", "motion:speed": -1.5, "shadow:enabled": true, wallpaper: 2, "input:drag_mode": 1 })
ok("round trip custom", JSON.stringify(M.parseLua(M.renderLua(custom))) === JSON.stringify(custom))

var template = fs.readFileSync(path.join(__dirname, "..", "lua", "hlsovpp.lua"), "utf8")
ok("lua/hlsovpp.lua matches renderLua(defaults)", template === lua)

// Hand edits: comments, junk lines, bad values, keys outside the plugin table.
var hand = [
  "if hl.plugin.scrolloverview then",
  "  hl.config({ plugin = { scrolloverview = {",
  "    scale = 0.3, -- smaller",
  "    layout = 'horizontal',",
  "    blur = maybe,",
  "    bogus = 1,",
  "    motion = {",
  "      style = \"sweep\",",
  "      spread = 9,",
  "    },",
  "    shadow = { enabled = true },", // one-liner: ignored, not a crash
  "  } } })",
  "end"
].join("\n")
var parsed = M.parseLua(hand)
ok("hand edit: plain value", parsed.scale === 0.3)
ok("hand edit: single quotes", parsed.layout === "horizontal")
ok("hand edit: bad value -> default", parsed.blur === false)
ok("hand edit: sub-table value", parsed["motion:style"] === "sweep")
ok("hand edit: out of range clamped", parsed["motion:spread"] === 0.9)
ok("hand edit: unknown key ignored", parsed.bogus === undefined)
ok("garbage in, defaults out", JSON.stringify(M.parseLua("}}} {{{ ;;;")) === JSON.stringify(d))
ok("keys outside scrolloverview ignored", M.parseLua("decoration = {\n  blur = true,\n}\n").blur === false)

// ── hyprctl ──────────────────────────────────────────────────────────────────
var cmd = M.evalCommand(custom)
ok("eval is a single argv", cmd.length === 3 && cmd[0] === "hyprctl" && cmd[1] === "eval")
ok("eval guards on the plugin", cmd[2].indexOf("if hl.plugin.scrolloverview then hl.config(") === 0)
ok("eval carries nested values", cmd[2].indexOf('motion = { style = "sweep",') !== -1 && cmd[2].indexOf("tilt = 12,") !== -1 && cmd[2].indexOf('easing = "bouncy",') !== -1)
ok("eval is one line", cmd[2].indexOf("\n") === -1)
ok("preview runs the dispatcher (not a bind factory)", M.PREVIEW_COMMAND[2].indexOf('._dispatch("overview", "open all")') !== -1)
ok("pluginLoaded yes", M.pluginLoaded("Plugin scrolloverview by yayuuu:\n\tHandle: 1"))
ok("pluginLoaded no", !M.pluginLoaded("Plugin hyprripple by jgarza:\n") && !M.pluginLoaded(""))

console.log()
if (failed) { console.log(failed + " failed"); process.exit(1) }
console.log("all passed")

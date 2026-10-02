# hlsovpp: ScrollOverview with per-window motion

> **This is a fork.** The overview itself is
> [yayuuu/hyprland-scroll-overview](https://github.com/yayuuu/hyprland-scroll-overview)
> by [@yayuuu](https://github.com/yayuuu) (BSD-3-Clause, see [LICENSE](LICENSE)).
> Please star and report overview bugs upstream. This fork adds:
>
> - **Per-window motion**: windows move independently when the overview opens and closes, instead of all zooming together
> - **Omarchy packaging**: one-command install, a settings panel (Omarchy menu > Style > Scroll Overview) and a Lua settings file

The internal plugin name is still `scrolloverview`, so existing configs, binds
(`hl.plugin.scrolloverview.overview("toggle all")`) and upstream docs all apply
unchanged. hyprpm knows this fork as the repository `hlsovpp`.

## Install (Omarchy)

```bash
omarchy plugin add https://github.com/jgarza9788/hlsovpp --enable
cd ~/.config/omarchy/plugins/jgarza.hlsovpp && ./install
```

`./install` (safe to re-run, `--dry-run` to preview):

- builds and enables the plugin with hyprpm (removing upstream's `hyprland-scroll-overview` repo if present, since both provide `scrolloverview`)
- creates `~/.config/hypr/hlsovpp.lua` and adds a fenced `require("hypr.hlsovpp")` to `hyprland.lua`
- adds Omarchy menu entries, and warns if another file also configures the plugin

Bind the overview yourself, for example in `~/.config/hypr/bindings.lua`:

```lua
hl.bind("SUPER + TAB", function() hl.plugin.scrolloverview.overview("toggle all") end)
```

`./uninstall` removes everything it added (it asks first; `--keep-settings`, `--keep-folder`, `--dry-run`).

Without Omarchy: `hyprpm add https://github.com/jgarza9788/hlsovpp && hyprpm enable scrolloverview`, then configure as below.

## Motion

```lua
hl.config({ plugin = { scrolloverview = {
  motion = {
    style = "ripple",       -- none ripple converge sweep random jitter scatter spring
    spread = 0.35,          -- share of the animation spent staggering, 0 - 0.9
    origin = "focus",       -- ripple/converge/spring: "focus" or "cursor"
    direction = "forward",  -- sweep: "forward" or "reverse"
    jitter = 0.5,           -- jitter/scatter: per-window curve variation, 0 - 1
    overshoot = 0.4,        -- spring: how far past the spot windows travel, 0 - 1
    rewind_on_close = true, -- closing plays the opening order backwards
    on_gesture = false,     -- also stagger while a touchpad swipe drives it
  },
} } })
```

| style | what moves when |
| --- | --- |
| `none` | everything together (upstream behaviour) |
| `ripple` | nearest to the focused window (or cursor) first, a wave outward |
| `converge` | farthest first, a wave inward |
| `sweep` | one after another in layout order |
| `random` | each window gets its own stable random delay |
| `jitter` | all start together, each with a different easing curve |
| `scatter` | random delays and different curves |
| `spring` | ripple, and windows overshoot their spot and settle back |

Each window replays the overview animation (the `windowsMove` animation's
curve and speed) on its own timeline, shifted by its delay. Every window still
starts where it is and ends exactly where upstream would put it, so the final
layout, clicks and drags are unchanged. Reversing mid-animation stays smooth.
Motion options also work per monitor via `hl.plugin.scrolloverview.configure`.

## Development

```bash
make           # build scrolloverview.so
make test      # motion unit tests (ASan/UBSan), panel model, installer
hyprctl plugin load "$PWD/scrolloverview.so"   # try a dev build
```

Per-window motion lives in `Motion.cpp` (pure math, unit tested) and
`CScrollOverview::updateMotion` in `scrollOverview.cpp`. Upstream is tracked
as the `upstream` remote; merge it as usual.

## Upstream README


ScrollOverview is an overview plugin like niri.

https://github.com/user-attachments/assets/e5eb1ad2-79bc-492a-82cd-02cd8b960d3e

### Installation

### Using Hyprpm (recommended)

1. Add the plugin repository:
   ```bash
   hyprpm add https://github.com/yayuuu/hyprland-scroll-overview.git
   ```
   If you use a Git build of Hyprland, add the plugin from the `new-release` branch instead:
   ```bash
   hyprpm add https://github.com/yayuuu/hyprland-scroll-overview origin/new-release
   ```
2. Build and fetch dependencies:
   ```bash
   hyprpm update
   ```
3. Enable the plugin:
   ```bash
   hyprpm enable scrolloverview
   ```
4. Configure and Enjoy.

For more installation methods, including building from source and loading the plugin automatically, see the [Installation guide](https://github.com/yayuuu/hyprland-scroll-overview/wiki/Installation).

### Configuration

### Hyprlang

```ini
# .config/hypr/hyprland.conf
plugin {
    scrolloverview {
        gesture_distance = 300 # how far is the "max" for the gesture
        scale = 0.5 # preferred overview scale
        workspace_gap = 100
        layout = vertical # vertical, horizontal, or auto (per-monitor orientation)
        wallpaper = 2 # 0: global only, 1: per-workspace only, 2: both
        blur = true # blur only the main overview wallpaper

        shadow {
            enabled = true
            range = 50
        }
    }
}

# Toggle ScrollOverview with SUPER+g
bind = SUPER, g, scrolloverview:overview, "toggle all"
```

### Lua

```lua
-- .config/hypr/hyprland.lua
hl.config({
    plugin = {
        scrolloverview = {
            gesture_distance = 300, -- how far is the "max" for the gesture
            scale = 0.5, -- preferred overview scale
            workspace_gap = 100,
            layout = "vertical", -- vertical, horizontal, or auto (per-monitor orientation)
            wallpaper = 2, -- 0: global only, 1: per-workspace only, 2: both
            blur = true, -- blur only the main overview wallpaper

            shadow = {
                enabled = true,
                range = 50,
            },
        },
    },
})

-- Toggle ScrollOverview with SUPER+g
hl.bind("SUPER + g", function()
    hl.plugin.scrolloverview.overview("toggle all")
end)
```

Set `cross_monitor_drag = true` to drag windows between monitors through ScrollOverview and adopt Hyprland move drags when another overview is open. It defaults to `false`. Native adoption uses Hyprland internals; if unavailable, overview-origin cross-monitor dragging still works.

This setting does not affect overview-origin drags with `toggle all` because every overview is already open. Existing multi-monitor commands remain available when it is disabled.

### Documentation

The complete documentation is available in the [ScrollOverview wiki](https://github.com/yayuuu/hyprland-scroll-overview/wiki). It covers all configuration options, keybinds, submaps, gestures, dispatchers, and advanced Lua examples.

### Supported plugins

- `hyprbars`

<br>
<br>

### Star History

<a href="https://www.star-history.com/?repos=yayuuu%2Fhyprland-scroll-overview&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=yayuuu/hyprland-scroll-overview&type=date&theme=dark&legend=top-left&sealed_token=dnoTQqdWontVtW1U-Qv5yJAQVYpafoKJ0ytKAwVNsCxYO8lor7ZbsD4oVIRHBW_9LjjRPMV-wHAXTw70cn4SbI_2OIrdFEMbOyjv9GVHi9EOBFIgwzKwvMTGWBhkYg5q1f9oV6mQqQZFgec__fA790Nj_OS5WOyVjeHFwceIJRFBlgSvBWyGMn3WUqym" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=yayuuu/hyprland-scroll-overview&type=date&legend=top-left&sealed_token=dnoTQqdWontVtW1U-Qv5yJAQVYpafoKJ0ytKAwVNsCxYO8lor7ZbsD4oVIRHBW_9LjjRPMV-wHAXTw70cn4SbI_2OIrdFEMbOyjv9GVHi9EOBFIgwzKwvMTGWBhkYg5q1f9oV6mQqQZFgec__fA790Nj_OS5WOyVjeHFwceIJRFBlgSvBWyGMn3WUqym" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=yayuuu/hyprland-scroll-overview&type=date&legend=top-left&sealed_token=dnoTQqdWontVtW1U-Qv5yJAQVYpafoKJ0ytKAwVNsCxYO8lor7ZbsD4oVIRHBW_9LjjRPMV-wHAXTw70cn4SbI_2OIrdFEMbOyjv9GVHi9EOBFIgwzKwvMTGWBhkYg5q1f9oV6mQqQZFgec__fA790Nj_OS5WOyVjeHFwceIJRFBlgSvBWyGMn3WUqym" />
 </picture>
</a>

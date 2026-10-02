#pragma once

// Per-window motion for the overview open/close animation (hlsovpp).
//
// Upstream drives every window with one shared scale animation, so they all
// move in lockstep. Here each window replays the same animation curve, but
// time-shifted by its own delay, and can lean (3D tilt) while it travels.
//
// Pure logic with no Hyprland dependencies so it can be unit tested.

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Motion {

enum class EStyle : uint8_t {
    NONE,     // upstream behaviour: everything moves together
    RIPPLE,   // nearest to the origin (focus/cursor) moves first, outward wave
    CONVERGE, // farthest from the origin moves first, inward wave
    SWEEP,    // one after another in layout order
    RANDOM,   // stable random delay per window
};

enum class EDelaySource : uint8_t {
    NONE,
    DISTANCE,
    INVERSE_DISTANCE,
    ORDER,
    RANDOM,
};

std::optional<EStyle> parseStyle(std::string_view name);
std::string_view      styleName(EStyle style);
EDelaySource          delaySource(EStyle style);

struct SParams {
    EStyle style         = EStyle::NONE;
    float  spread        = 0.35F; // share of the animation used to stagger, [0, 0.9]
    bool   reverse       = false; // sweep: reverse layout order
    bool   rewindOnClose = true;  // closing replays the opening order backwards
    float  tilt          = 0.F;   // peak in-flight 3D tilt in degrees, [-45, 45]; negative leans away
};

struct SPoint {
    float x = 0.F;
    float y = 0.F;
};

struct SWindowSample {
    SPoint   center; // window centre in the overview layout (any consistent space)
    double   order = 0.0; // sweep order key, ascending
    uint64_t key   = 0;   // stable per-window identity (seeds random choices)
};

// ── easing ──────────────────────────────────────────────────────────────────
// Named cubic beziers (CSS-style control points). "follow" is not in the
// table: it means "use the windowsMove animation's own bezier".
struct SEasing {
    std::string_view name;
    float            x1, y1, x2, y2;
};

std::span<const SEasing> easings();
std::optional<SEasing>   findEasing(std::string_view name);
bool                     isEasingName(std::string_view name); // table entry or "follow"
// y for x on the curve (x solved numerically). For tests and fallbacks.
float easingY(const SEasing& easing, float x);

// ── delays and progress ─────────────────────────────────────────────────────

// Uniform value in [0, 1) derived from key and salt. Deterministic.
float hashUnit(uint64_t key, uint32_t salt);

// One delay in [0, 1] per sample.
std::vector<float> computeDelays(const SParams& params, std::span<const SWindowSample> samples, SPoint origin, bool closing);

// Effective stagger: no stagger when the style has no delays.
float effectiveSpread(const SParams& params);

// Window-local time in [0, 1] from the global animation time.
float localTime(float percent, float delay, float spread);

// Fraction of the way from the window's start to the goal, given the global
// animation time `percent` in [0, 1] and the animation's easing. Always 0 at
// percent 0 and 1 at percent 1.
float windowProgress(const SParams& params, float percent, float delay, const std::function<float(float)>& ease);

// ── speed ───────────────────────────────────────────────────────────────────

// motion:speed range: 0 keeps the base speed, +1 is twice as fast, -1 half.
inline constexpr float MAX_SPEED = 4.F;

// Animation duration (Hyprland speed units, 1 = 100 ms) for `speed` applied
// to `baseDuration`. Clamped to +/-MAX_SPEED; NaN means unchanged.
float scaledDuration(float baseDuration, float speed);

// ── tilt ────────────────────────────────────────────────────────────────────

// Tilt in radians `progress` of the way along a window's own motion: 0 at
// both ends, peaking half-way. Positive leans into the move, negative away
// from it; capped at +/-45. NaN means no tilt.
float tiltAngle(float progress, float tiltDegrees);

// Rotation axis (unit, in the screen plane) for a window travelling by
// (dx, dy): perpendicular to the travel so the window leans into the move.
// A window that barely moves sideways leans back around the X axis.
SPoint tiltAxis(float dx, float dy, float minTravel = 4.F);

}

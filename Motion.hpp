#pragma once

// Per-window motion for the overview open/close animation (hlsovpp).
//
// Upstream drives every window with one shared scale animation, so they all
// move in lockstep. Here each window replays the same animation curve, but
// time-shifted by its own delay and optionally reshaped (jitter, overshoot).
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
    JITTER,   // same start, per-window easing curve
    SCATTER,  // random delay + per-window easing curve
    SPRING,   // ripple + overshoot that settles back
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
bool                  usesJitter(EStyle style);
bool                  usesOvershoot(EStyle style);

struct SParams {
    EStyle style         = EStyle::NONE;
    float  spread        = 0.35F; // share of the animation used to stagger, [0, 0.9]
    float  jitter        = 0.5F;  // per-window curve variation, [0, 1]
    float  overshoot     = 0.4F;  // how far past the target a window travels, [0, 1]
    bool   reverse       = false; // sweep: reverse layout order
    bool   rewindOnClose = true;  // closing replays the opening order backwards
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

// Uniform value in [0, 1) derived from key and salt. Deterministic.
float hashUnit(uint64_t key, uint32_t salt);

// One delay in [0, 1] per sample.
std::vector<float> computeDelays(const SParams& params, std::span<const SWindowSample> samples, SPoint origin, bool closing);

// Effective stagger: no stagger when the style has no delays.
float effectiveSpread(const SParams& params);

// Window-local time in [0, 1] from the global animation time.
float localTime(float percent, float delay, float spread);

// Exponent applied to local time for jitter; 1 when jitter is off.
float jitterExponent(uint64_t key, float jitter);

// Back-out overshoot applied on top of an eased value; f(0)=0, f(1)=1.
float overshootCurve(float value, float overshoot);

// Fraction of the way from the window's start to the goal, given the global
// animation time `percent` in [0, 1] and the animation's easing.
// Always 0 at percent 0 and 1 at percent 1 (may exceed 1 mid-way with overshoot).
float windowProgress(const SParams& params, float percent, float delay, uint64_t key, const std::function<float(float)>& ease);

}

#include "Motion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

namespace Motion {

namespace {

constexpr std::array<std::pair<std::string_view, EStyle>, 5> STYLES = {{
    {"none", EStyle::NONE},
    {"ripple", EStyle::RIPPLE},
    {"converge", EStyle::CONVERGE},
    {"sweep", EStyle::SWEEP},
    {"random", EStyle::RANDOM},
}};

constexpr std::array<SEasing, 3> EASINGS = {{
    {"smooth", 0.42F, 0.F, 0.58F, 1.F},  // gentle start and stop (ease-in-out)
    {"snappy", 0.16F, 1.F, 0.3F, 1.F},   // fast start, soft landing (expo-out)
    {"bouncy", 0.34F, 1.56F, 0.64F, 1.F}, // small overshoot that settles (back-out)
}};

constexpr uint32_t SALT_DELAY = 0x9E3779B9U;
constexpr float    PI         = 3.14159265358979F;
constexpr float    MAX_TILT   = 45.F;

float clamp01(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.F, 1.F) : 0.F;
}

uint64_t mix64(uint64_t x) { // splitmix64 finalizer
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

float bezier1D(float a, float b, float t) { // control points 0, a, b, 1
    const float U = 1.F - t;
    return 3.F * U * U * t * a + 3.F * U * t * t * b + t * t * t;
}

}

std::optional<EStyle> parseStyle(std::string_view name) {
    for (const auto& [styleNameValue, style] : STYLES) {
        if (styleNameValue == name)
            return style;
    }
    return std::nullopt;
}

std::string_view styleName(EStyle style) {
    for (const auto& [styleNameValue, value] : STYLES) {
        if (value == style)
            return styleNameValue;
    }
    return "none";
}

EDelaySource delaySource(EStyle style) {
    switch (style) {
        case EStyle::RIPPLE: return EDelaySource::DISTANCE;
        case EStyle::CONVERGE: return EDelaySource::INVERSE_DISTANCE;
        case EStyle::SWEEP: return EDelaySource::ORDER;
        case EStyle::RANDOM: return EDelaySource::RANDOM;
        case EStyle::NONE: return EDelaySource::NONE;
    }
    return EDelaySource::NONE;
}

std::span<const SEasing> easings() {
    return EASINGS;
}

std::optional<SEasing> findEasing(std::string_view name) {
    for (const auto& easing : EASINGS) {
        if (easing.name == name)
            return easing;
    }
    return std::nullopt;
}

bool isEasingName(std::string_view name) {
    return name == "follow" || findEasing(name).has_value();
}

float easingY(const SEasing& easing, float x) {
    x = clamp01(x);
    // x(t) is monotonic for x1, x2 in [0, 1]: bisect for t.
    float lo = 0.F, hi = 1.F, t = x;
    for (int i = 0; i < 40; ++i) {
        t = (lo + hi) * 0.5F;
        if (bezier1D(easing.x1, easing.x2, t) < x)
            lo = t;
        else
            hi = t;
    }
    return bezier1D(easing.y1, easing.y2, t);
}

float hashUnit(uint64_t key, uint32_t salt) {
    const auto HASH = mix64(mix64(key) ^ ((static_cast<uint64_t>(salt) << 32) | salt));
    return static_cast<float>(HASH >> 40) / static_cast<float>(1ULL << 24);
}

std::vector<float> computeDelays(const SParams& params, std::span<const SWindowSample> samples, SPoint origin, bool closing) {
    std::vector<float> delays(samples.size(), 0.F);
    if (samples.empty())
        return delays;

    switch (delaySource(params.style)) {
        case EDelaySource::NONE: return delays;
        case EDelaySource::DISTANCE:
        case EDelaySource::INVERSE_DISTANCE: {
            float maxDistance = 0.F;
            for (size_t i = 0; i < samples.size(); ++i) {
                const float DX = samples[i].center.x - origin.x;
                const float DY = samples[i].center.y - origin.y;
                delays[i]      = std::sqrt(DX * DX + DY * DY);
                if (!std::isfinite(delays[i]))
                    delays[i] = 0.F;
                maxDistance = std::max(maxDistance, delays[i]);
            }
            for (auto& delay : delays)
                delay = maxDistance > 1e-3F ? delay / maxDistance : 0.F;
            if (delaySource(params.style) == EDelaySource::INVERSE_DISTANCE) {
                for (auto& delay : delays)
                    delay = 1.F - delay;
            }
            break;
        }
        case EDelaySource::ORDER: {
            std::vector<size_t> ranks(samples.size());
            std::iota(ranks.begin(), ranks.end(), 0);
            std::stable_sort(ranks.begin(), ranks.end(), [&](size_t a, size_t b) { return samples[a].order < samples[b].order; });
            const float LAST = static_cast<float>(samples.size() - 1);
            for (size_t rank = 0; rank < ranks.size(); ++rank)
                delays[ranks[rank]] = LAST > 0.F ? static_cast<float>(rank) / LAST : 0.F;
            if (params.reverse) {
                for (auto& delay : delays)
                    delay = 1.F - delay;
            }
            break;
        }
        case EDelaySource::RANDOM:
            for (size_t i = 0; i < samples.size(); ++i)
                delays[i] = hashUnit(samples[i].key, SALT_DELAY);
            break;
    }

    if (closing && params.rewindOnClose) {
        for (auto& delay : delays)
            delay = 1.F - delay;
    }

    for (auto& delay : delays)
        delay = clamp01(delay);

    return delays;
}

float effectiveSpread(const SParams& params) {
    if (delaySource(params.style) == EDelaySource::NONE)
        return 0.F;
    return std::isfinite(params.spread) ? std::clamp(params.spread, 0.F, 0.9F) : 0.F;
}

float localTime(float percent, float delay, float spread) {
    percent = clamp01(percent);
    if (spread <= 0.F)
        return percent;

    return clamp01((percent - clamp01(delay) * spread) / (1.F - spread));
}

float windowProgress(const SParams& params, float percent, float delay, const std::function<float(float)>& ease) {
    const float T = localTime(percent, delay, effectiveSpread(params));

    // Pin the endpoints exactly so the final layout never drifts.
    if (T <= 0.F)
        return 0.F;
    if (T >= 1.F)
        return 1.F;

    const float VALUE = ease ? ease(T) : T;
    return std::isfinite(VALUE) ? VALUE : T;
}

float scaledDuration(float baseDuration, float speed) {
    if (!std::isfinite(speed))
        return baseDuration;

    return baseDuration * std::exp2(-std::clamp(speed, -MAX_SPEED, MAX_SPEED));
}

float tiltAngle(float progress, float tiltDegrees) {
    if (!std::isfinite(tiltDegrees) || tiltDegrees == 0.F)
        return 0.F;

    return std::sin(PI * clamp01(progress)) * std::clamp(tiltDegrees, -MAX_TILT, MAX_TILT) * (PI / 180.F);
}

SPoint tiltAxis(float dx, float dy, float minTravel) {
    const float LENGTH = std::sqrt(dx * dx + dy * dy);
    if (!std::isfinite(LENGTH) || LENGTH < minTravel)
        return {1.F, 0.F};

    // perpendicular to the travel, in the screen plane
    return {-dy / LENGTH, dx / LENGTH};
}

}

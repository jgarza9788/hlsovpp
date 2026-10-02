#include "Motion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

namespace Motion {

namespace {

constexpr std::array<std::pair<std::string_view, EStyle>, 8> STYLES = {{
    {"none", EStyle::NONE},
    {"ripple", EStyle::RIPPLE},
    {"converge", EStyle::CONVERGE},
    {"sweep", EStyle::SWEEP},
    {"random", EStyle::RANDOM},
    {"jitter", EStyle::JITTER},
    {"scatter", EStyle::SCATTER},
    {"spring", EStyle::SPRING},
}};

constexpr uint32_t SALT_DELAY  = 0x9E3779B9U;
constexpr uint32_t SALT_JITTER = 0x85EBCA6BU;

// Largest jitter exponent is 2^MAX_JITTER_OCTAVES (and smallest its inverse).
constexpr float MAX_JITTER_OCTAVES = 1.25F;
// Overshoot 1 maps to a back-ease constant of this (about 25% past target).
constexpr float MAX_BACK_CONSTANT = 2.5F;

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
        case EStyle::RIPPLE:
        case EStyle::SPRING: return EDelaySource::DISTANCE;
        case EStyle::CONVERGE: return EDelaySource::INVERSE_DISTANCE;
        case EStyle::SWEEP: return EDelaySource::ORDER;
        case EStyle::RANDOM:
        case EStyle::SCATTER: return EDelaySource::RANDOM;
        case EStyle::NONE:
        case EStyle::JITTER: return EDelaySource::NONE;
    }
    return EDelaySource::NONE;
}

bool usesJitter(EStyle style) {
    return style == EStyle::JITTER || style == EStyle::SCATTER;
}

bool usesOvershoot(EStyle style) {
    return style == EStyle::SPRING;
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

float jitterExponent(uint64_t key, float jitter) {
    jitter = clamp01(jitter);
    if (jitter <= 0.F)
        return 1.F;

    const float SIGNED = 2.F * hashUnit(key, SALT_JITTER) - 1.F; // [-1, 1)
    return std::exp2(SIGNED * jitter * MAX_JITTER_OCTAVES);
}

float overshootCurve(float value, float overshoot) {
    overshoot = clamp01(overshoot);
    if (overshoot <= 0.F)
        return value;

    const float C1 = overshoot * MAX_BACK_CONSTANT;
    const float C3 = C1 + 1.F;
    const float X  = value - 1.F;
    return 1.F + C3 * X * X * X + C1 * X * X;
}

float windowProgress(const SParams& params, float percent, float delay, uint64_t key, const std::function<float(float)>& ease) {
    if (params.style == EStyle::NONE)
        return ease ? ease(clamp01(percent)) : clamp01(percent);

    float t = localTime(percent, delay, effectiveSpread(params));
    if (usesJitter(params.style))
        t = std::pow(t, jitterExponent(key, params.jitter));

    float value = ease ? ease(t) : t;
    if (usesOvershoot(params.style))
        value = overshootCurve(value, params.overshoot);

    // Pin the endpoints exactly so the final layout never drifts.
    if (t <= 0.F)
        return 0.F;
    if (t >= 1.F)
        return 1.F;
    return std::isfinite(value) ? value : t;
}

}

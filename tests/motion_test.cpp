// Unit tests for Motion.cpp (no Hyprland needed). Run with: make test
#include "../Motion.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace Motion;

static int g_failures = 0;

#define CHECK(cond)                                                                                                                                                                \
    do {                                                                                                                                                                           \
        if (cond)                                                                                                                                                                  \
            std::printf("PASS %s\n", #cond);                                                                                                                                       \
        else {                                                                                                                                                                     \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                                                             \
            ++g_failures;                                                                                                                                                          \
        }                                                                                                                                                                          \
    } while (0)

static bool near(float a, float b, float eps = 1e-5F) {
    return std::fabs(a - b) <= eps;
}

static const std::vector<EStyle> ALL = {EStyle::NONE, EStyle::RIPPLE, EStyle::CONVERGE, EStyle::SWEEP, EStyle::RANDOM, EStyle::JITTER, EStyle::SCATTER, EStyle::SPRING};

static std::vector<SWindowSample> grid() {
    std::vector<SWindowSample> samples;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x)
            samples.push_back({.center = {x * 100.F, y * 100.F}, .order = y * 10.0 + x, .key = 0x1000ULL + static_cast<uint64_t>(y * 4 + x) * 0x40});
    return samples;
}

static float easeInOut(float t) { // smoothstep, a stand-in for a bezier
    return t * t * (3.F - 2.F * t);
}

static void testParse() {
    for (const auto style : ALL)
        CHECK(parseStyle(styleName(style)) == style);
    CHECK(!parseStyle("bogus").has_value());
    CHECK(!parseStyle("").has_value());
    CHECK(styleName(EStyle::RIPPLE) == "ripple");
}

static void testHash() {
    bool inRange = true, stable = true;
    float sum    = 0.F;
    for (uint64_t k = 0; k < 2000; ++k) {
        const float h = hashUnit(k * 0x1F0 + 0x55aa00, 7);
        inRange       = inRange && h >= 0.F && h < 1.F;
        stable        = stable && h == hashUnit(k * 0x1F0 + 0x55aa00, 7);
        sum += h;
    }
    CHECK(inRange);
    CHECK(stable);
    CHECK(std::fabs(sum / 2000.F - 0.5F) < 0.05F); // roughly uniform
    CHECK(hashUnit(42, 1) != hashUnit(42, 2));      // salts differ
}

static void testEndpoints() {
    // Every style: progress 0 at the start and exactly 1 at the end, for any delay.
    for (const auto style : ALL) {
        SParams params{.style = style, .spread = 0.5F, .jitter = 1.F, .overshoot = 1.F};
        bool    ok = true;
        for (float delay : {0.F, 0.3F, 1.F}) {
            for (uint64_t key : {1ULL, 99ULL, 123456789ULL}) {
                ok = ok && near(windowProgress(params, 0.F, delay, key, easeInOut), 0.F);
                ok = ok && near(windowProgress(params, 1.F, delay, key, easeInOut), 1.F);
            }
        }
        CHECK(ok);
    }
}

static void testMonotonicWithoutOvershoot() {
    for (const auto style : ALL) {
        if (usesOvershoot(style))
            continue;
        SParams params{.style = style, .spread = 0.6F, .jitter = 1.F};
        bool    ok = true;
        for (float delay : {0.F, 0.5F, 1.F}) {
            float prev = 0.F;
            for (int i = 0; i <= 200; ++i) {
                const float v = windowProgress(params, i / 200.F, delay, 777, easeInOut);
                ok            = ok && v >= prev - 1e-6F && v >= 0.F && v <= 1.F;
                prev          = v;
            }
        }
        CHECK(ok);
    }
}

static void testStagger() {
    SParams params{.style = EStyle::RIPPLE, .spread = 0.5F};
    // A delay-1 window hasn't moved when the delay-0 window is well underway.
    CHECK(windowProgress(params, 0.25F, 1.F, 1, nullptr) == 0.F);
    CHECK(windowProgress(params, 0.25F, 0.F, 1, nullptr) > 0.4F);
    // With spread 0 everything moves together.
    params.spread = 0.F;
    CHECK(near(windowProgress(params, 0.3F, 0.F, 1, nullptr), windowProgress(params, 0.3F, 1.F, 1, nullptr)));
    // Out-of-range spread is clamped, never divides by zero.
    params.spread = 5.F;
    CHECK(std::isfinite(windowProgress(params, 0.95F, 1.F, 1, nullptr)));
    params.spread = NAN;
    CHECK(std::isfinite(windowProgress(params, 0.5F, 1.F, 1, nullptr)));
}

static void testJitterHasNoDeadTime() {
    // Jitter has no delays, so windows must use the whole animation (no
    // early finish caused by spread).
    SParams params{.style = EStyle::JITTER, .spread = 0.8F, .jitter = 0.F};
    CHECK(near(windowProgress(params, 0.5F, 0.F, 3, nullptr), 0.5F));
    CHECK(effectiveSpread(params) == 0.F);
    params.jitter = 1.F;
    bool differs  = false;
    for (uint64_t k = 0; k < 20; ++k)
        differs = differs || !near(windowProgress(params, 0.5F, 0.F, k * 977, nullptr), windowProgress(params, 0.5F, 0.F, 1, nullptr), 1e-3F);
    CHECK(differs);
    CHECK(jitterExponent(5, 0.F) == 1.F);
    const float e = jitterExponent(5, 1.F);
    CHECK(e > 0.4F && e < 2.4F);
}

static void testOvershoot() {
    CHECK(overshootCurve(0.F, 1.F) == 0.F);
    CHECK(near(overshootCurve(1.F, 1.F), 1.F));
    CHECK(overshootCurve(0.5F, 0.F) == 0.5F);
    float peak = 0.F;
    for (int i = 0; i <= 100; ++i)
        peak = std::max(peak, overshootCurve(i / 100.F, 1.F));
    CHECK(peak > 1.05F && peak < 1.5F);
    SParams params{.style = EStyle::SPRING, .spread = 0.F, .overshoot = 0.8F};
    CHECK(windowProgress(params, 0.8F, 0.F, 1, nullptr) > 1.F);
}

static void testTilt() {
    CHECK(tiltAngle(0.F, 10.F, 1.F) == 0.F);
    CHECK(std::fabs(tiltAngle(1.F, 10.F, 1.F)) < 1e-5F);
    CHECK(near(tiltAngle(0.5F, 10.F, 1.F), 10.F * 3.14159265F / 180.F, 1e-4F));
    CHECK(near(tiltAngle(0.5F, 10.F, -1.F), -tiltAngle(0.5F, 10.F, 1.F)));
    CHECK(tiltAngle(0.5F, 0.F, 1.F) == 0.F);
    CHECK(near(tiltAngle(0.5F, 90.F, 1.F), 30.F * 3.14159265F / 180.F, 1e-4F)); // capped
    CHECK(tiltAngle(0.5F, NAN, 1.F) == 0.F);
    CHECK(std::fabs(tiltAngle(1.3F, 10.F, 1.F)) < 1e-5F); // overshoot clamps to straight
}

static void testDelays() {
    const auto samples = grid();
    SParams    params{.style = EStyle::RIPPLE};

    auto delays = computeDelays(params, samples, {0.F, 0.F}, false);
    CHECK(delays.size() == samples.size());
    CHECK(delays[0] == 0.F);               // at the origin
    CHECK(near(delays.back(), 1.F));        // farthest
    CHECK(delays[1] < delays[2]);           // farther = later

    params.style = EStyle::CONVERGE;
    delays       = computeDelays(params, samples, {0.F, 0.F}, false);
    CHECK(near(delays[0], 1.F));
    CHECK(near(delays.back(), 0.F));

    params.style = EStyle::SWEEP;
    delays       = computeDelays(params, samples, {}, false);
    CHECK(delays[0] == 0.F && near(delays.back(), 1.F));
    bool ordered = true;
    for (size_t i = 1; i < delays.size(); ++i)
        ordered = ordered && delays[i] > delays[i - 1];
    CHECK(ordered);
    params.reverse = true;
    delays         = computeDelays(params, samples, {}, false);
    CHECK(near(delays[0], 1.F) && near(delays.back(), 0.F));
    params.reverse = false;

    params.style = EStyle::RANDOM;
    const auto r1 = computeDelays(params, samples, {}, false);
    const auto r2 = computeDelays(params, samples, {}, false);
    CHECK(r1 == r2);
    bool inRange = true;
    for (auto d : r1)
        inRange = inRange && d >= 0.F && d <= 1.F;
    CHECK(inRange);

    params.style = EStyle::JITTER;
    delays       = computeDelays(params, samples, {}, false);
    bool zeros   = true;
    for (auto d : delays)
        zeros = zeros && d == 0.F;
    CHECK(zeros);
}

static void testRewindOnClose() {
    const auto samples = grid();
    SParams    params{.style = EStyle::RIPPLE, .rewindOnClose = true};
    auto       open  = computeDelays(params, samples, {0.F, 0.F}, false);
    auto       close = computeDelays(params, samples, {0.F, 0.F}, true);
    bool       ok    = true;
    for (size_t i = 0; i < open.size(); ++i)
        ok = ok && near(open[i] + close[i], 1.F);
    CHECK(ok);

    params.rewindOnClose = false;
    close                = computeDelays(params, samples, {0.F, 0.F}, true);
    CHECK(open == close);

    // Styles without delays stay delay-free when closing.
    params = {.style = EStyle::JITTER, .rewindOnClose = true};
    close  = computeDelays(params, samples, {}, true);
    bool zeros = true;
    for (auto d : close)
        zeros = zeros && d == 0.F;
    CHECK(zeros);
}

static void testDegenerate() {
    SParams params{.style = EStyle::RIPPLE};
    CHECK(computeDelays(params, {}, {}, false).empty());
    // One window / all at the origin: no NaNs.
    std::vector<SWindowSample> one = {{.center = {5.F, 5.F}, .key = 1}};
    CHECK(computeDelays(params, one, {5.F, 5.F}, false)[0] == 0.F);
    params.style = EStyle::SWEEP;
    CHECK(computeDelays(params, one, {}, false)[0] == 0.F);
    std::vector<SWindowSample> bad = {{.center = {NAN, INFINITY}, .key = 1}, {.center = {1.F, 1.F}, .key = 2}};
    params.style                   = EStyle::RIPPLE;
    const auto d                   = computeDelays(params, bad, {}, false);
    CHECK(d[0] >= 0.F && d[0] <= 1.F && d[1] >= 0.F && d[1] <= 1.F);
}

int main() {
    testParse();
    testHash();
    testEndpoints();
    testMonotonicWithoutOvershoot();
    testStagger();
    testJitterHasNoDeadTime();
    testOvershoot();
    testTilt();
    testDelays();
    testRewindOnClose();
    testDegenerate();

    if (g_failures) {
        std::printf("%d motion test(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("all motion tests passed\n");
    return EXIT_SUCCESS;
}

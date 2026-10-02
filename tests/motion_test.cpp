// Unit tests for Motion.cpp (no Hyprland needed). Run with: make test
#include "../Motion.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
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

static const std::vector<EStyle> ALL = {EStyle::NONE, EStyle::RIPPLE, EStyle::CONVERGE, EStyle::SWEEP, EStyle::RANDOM};

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
    // removed styles fall back (the plugin treats unknown as none)
    CHECK(!parseStyle("jitter").has_value());
    CHECK(!parseStyle("spring").has_value());
    CHECK(!parseStyle("").has_value());
}

static void testEasings() {
    std::set<std::string> names;
    bool                  endpoints = true, monotonicX = true;
    for (const auto& e : easings()) {
        names.insert(std::string{e.name});
        endpoints  = endpoints && near(easingY(e, 0.F), 0.F, 1e-3F) && near(easingY(e, 1.F), 1.F, 1e-3F);
        monotonicX = monotonicX && e.x1 >= 0.F && e.x1 <= 1.F && e.x2 >= 0.F && e.x2 <= 1.F;
        CHECK(findEasing(e.name).has_value());
    }
    CHECK(names.size() == easings().size()); // unique
    CHECK(endpoints);
    CHECK(monotonicX); // valid for Hyprland's bezier solver
    CHECK(isEasingName("follow"));
    CHECK(!findEasing("follow").has_value()); // follow = windowsMove's own bezier
    CHECK(!isEasingName("bounce"));
    CHECK(easings().size() == 3);
    CHECK(!isEasingName("linear") && !isEasingName("expo-out")); // trimmed
    CHECK(near(easingY(*findEasing("smooth"), 0.5F), 0.5F, 1e-2F)); // symmetric
    CHECK(easingY(*findEasing("snappy"), 0.3F) > 0.6F);              // front-loaded
    float peak = 0.F;
    for (int i = 0; i <= 100; ++i)
        peak = std::max(peak, easingY(*findEasing("bouncy"), i / 100.F));
    CHECK(peak > 1.02F); // a little overshoot
}

static void testHash() {
    bool  inRange = true, stable = true;
    float sum     = 0.F;
    for (uint64_t k = 0; k < 2000; ++k) {
        const float h = hashUnit(k * 0x1F0 + 0x55aa00, 7);
        inRange       = inRange && h >= 0.F && h < 1.F;
        stable        = stable && h == hashUnit(k * 0x1F0 + 0x55aa00, 7);
        sum += h;
    }
    CHECK(inRange);
    CHECK(stable);
    CHECK(std::fabs(sum / 2000.F - 0.5F) < 0.05F);
}

static void testEndpointsAndMonotonic() {
    for (const auto style : ALL) {
        SParams params{.style = style, .spread = 0.6F};
        bool    ok = true;
        for (float delay : {0.F, 0.5F, 1.F}) {
            ok         = ok && near(windowProgress(params, 0.F, delay, easeInOut), 0.F) && near(windowProgress(params, 1.F, delay, easeInOut), 1.F);
            float prev = 0.F;
            for (int i = 0; i <= 200; ++i) {
                const float v = windowProgress(params, i / 200.F, delay, easeInOut);
                ok            = ok && v >= prev - 1e-6F && v >= 0.F && v <= 1.F;
                prev          = v;
            }
        }
        CHECK(ok);
    }
}

static void testStagger() {
    SParams params{.style = EStyle::RIPPLE, .spread = 0.5F};
    CHECK(windowProgress(params, 0.25F, 1.F, nullptr) == 0.F);
    CHECK(windowProgress(params, 0.25F, 0.F, nullptr) > 0.4F);
    params.spread = 0.F;
    CHECK(near(windowProgress(params, 0.3F, 0.F, nullptr), windowProgress(params, 0.3F, 1.F, nullptr)));
    params.spread = 5.F;
    CHECK(std::isfinite(windowProgress(params, 0.95F, 1.F, nullptr)));
    params.spread = NAN;
    CHECK(std::isfinite(windowProgress(params, 0.5F, 1.F, nullptr)));
    // none: no stagger, whatever the spread
    params = {.style = EStyle::NONE, .spread = 0.8F};
    CHECK(effectiveSpread(params) == 0.F);
    CHECK(near(windowProgress(params, 0.5F, 0.F, nullptr), 0.5F));
}

static void testDelays() {
    const auto samples = grid();
    SParams    params{.style = EStyle::RIPPLE};

    auto delays = computeDelays(params, samples, {0.F, 0.F}, false);
    CHECK(delays.size() == samples.size());
    CHECK(delays[0] == 0.F);
    CHECK(near(delays.back(), 1.F));
    CHECK(delays[1] < delays[2]);

    params.style = EStyle::CONVERGE;
    delays       = computeDelays(params, samples, {0.F, 0.F}, false);
    CHECK(near(delays[0], 1.F) && near(delays.back(), 0.F));

    params.style = EStyle::SWEEP;
    delays       = computeDelays(params, samples, {}, false);
    bool ordered = true;
    for (size_t i = 1; i < delays.size(); ++i)
        ordered = ordered && delays[i] > delays[i - 1];
    CHECK(ordered && delays[0] == 0.F && near(delays.back(), 1.F));
    params.reverse = true;
    delays         = computeDelays(params, samples, {}, false);
    CHECK(near(delays[0], 1.F) && near(delays.back(), 0.F));
    params.reverse = false;

    params.style  = EStyle::RANDOM;
    const auto r1 = computeDelays(params, samples, {}, false);
    CHECK(r1 == computeDelays(params, samples, {}, false));
    bool inRange = true;
    for (auto d : r1)
        inRange = inRange && d >= 0.F && d <= 1.F;
    CHECK(inRange);

    params.style = EStyle::NONE;
    bool zeros   = true;
    for (auto d : computeDelays(params, samples, {}, true))
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
    CHECK(open == computeDelays(params, samples, {0.F, 0.F}, true));
}

static void testDegenerate() {
    SParams params{.style = EStyle::RIPPLE};
    CHECK(computeDelays(params, {}, {}, false).empty());
    std::vector<SWindowSample> one = {{.center = {5.F, 5.F}, .key = 1}};
    CHECK(computeDelays(params, one, {5.F, 5.F}, false)[0] == 0.F);
    params.style = EStyle::SWEEP;
    CHECK(computeDelays(params, one, {}, false)[0] == 0.F);
    std::vector<SWindowSample> bad = {{.center = {NAN, INFINITY}, .key = 1}, {.center = {1.F, 1.F}, .key = 2}};
    params.style                   = EStyle::RIPPLE;
    const auto d                   = computeDelays(params, bad, {}, false);
    CHECK(d[0] >= 0.F && d[0] <= 1.F && d[1] >= 0.F && d[1] <= 1.F);
}

static void testTilt() {
    constexpr float RAD = 3.14159265F / 180.F;
    CHECK(tiltAngle(0.F, 20.F) == 0.F);
    CHECK(std::fabs(tiltAngle(1.F, 20.F)) < 1e-5F);
    CHECK(near(tiltAngle(0.5F, 20.F), 20.F * RAD, 1e-4F));
    CHECK(tiltAngle(0.5F, 0.F) == 0.F); // default: off
    CHECK(near(tiltAngle(0.5F, -20.F), -20.F * RAD, 1e-4F)); // negative leans the other way
    CHECK(near(tiltAngle(0.5F, -90.F), -45.F * RAD, 1e-4F)); // capped both ways
    CHECK(tiltAngle(0.5F, NAN) == 0.F);
    CHECK(near(tiltAngle(0.5F, 90.F), 45.F * RAD, 1e-4F)); // capped
    CHECK(std::fabs(tiltAngle(1.3F, 20.F)) < 1e-5F);      // overshoot clamps to flat

    // axis is a unit vector perpendicular to the travel
    const auto A = tiltAxis(30.F, 40.F);
    CHECK(near(A.x * A.x + A.y * A.y, 1.F, 1e-4F));
    CHECK(near(A.x * 30.F + A.y * 40.F, 0.F, 1e-3F));
    const auto R = tiltAxis(100.F, 0.F); // moving right: rotate about the vertical axis
    CHECK(near(R.x, 0.F) && near(std::fabs(R.y), 1.F));
    const auto F = tiltAxis(1.F, 1.F); // barely moving: lean back about X
    CHECK(F.x == 1.F && F.y == 0.F);
    const auto N = tiltAxis(NAN, 1.F);
    CHECK(N.x == 1.F && N.y == 0.F);
}

static void testSpeed() {
    CHECK(std::fabs(scaledDuration(5.F, 0.F) - 5.F) < 1e-5F);    // 0 = same as windowsMove
    CHECK(std::fabs(scaledDuration(5.F, 1.F) - 2.5F) < 1e-5F);   // faster = shorter
    CHECK(std::fabs(scaledDuration(5.F, -1.F) - 10.F) < 1e-5F);  // slower = longer
    CHECK(std::fabs(scaledDuration(5.F, 99.F) - scaledDuration(5.F, MAX_SPEED)) < 1e-5F);
    CHECK(std::fabs(scaledDuration(5.F, -99.F) - scaledDuration(5.F, -MAX_SPEED)) < 1e-5F);
    CHECK(std::fabs(scaledDuration(5.F, NAN) - 5.F) < 1e-5F);
}

int main() {
    testParse();
    testEasings();
    testHash();
    testEndpointsAndMonotonic();
    testStagger();
    testDelays();
    testRewindOnClose();
    testDegenerate();
    testTilt();
    testSpeed();

    if (g_failures) {
        std::printf("%d motion test(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("all motion tests passed\n");
    return EXIT_SUCCESS;
}

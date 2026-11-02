// SPDX-License-Identifier: MIT
// Conformance tests for the shared motion contract v1. Byte-identical in the
// FSR / frame generation mod and Native AA. Portable: g++ -std=c++17 -I<dir>.
#include "motion_contract.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>

static int failures = 0;
static void expect(bool condition, const char *what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}
static bool approx(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }

int main() {
    using namespace xpmotion;
    expect(contractVersion == 1, "contract version");
    expect(depthFormat == 100 && motionFormat == 83 && reactiveFormat == 9, "image formats");

    const float W = 1920, H = 1080;
    // Pixel (959,539) centre, unjittered.
    const float u = (959.f + .5f) / W, v = (539.f + .5f) / H;
    // Static scene, static camera: previous clip equals current clip.
    float same[4] = {u * 2 - 1, -(v * 2 - 1), .5f, 1};
    auto m = fromPreviousClip(same, u, v, W, H, -1);
    expect(m.valid && approx(m.x, 0) && approx(m.y, 0), "static scene gives zero motion");

    // Content was 0.1 NDC to the right last frame -> +96 px (previous minus current).
    float right[4] = {same[0] + .1f, same[1], .5f, 1};
    m = fromPreviousClip(right, u, v, W, H, -1);
    expect(m.valid && approx(m.x, 96.f, 1e-2f) && approx(m.y, 0, 1e-2f), "pan right is +X pixels");

    // Homogeneous W must not change the result.
    float scaled[4] = {right[0] * 3, right[1] * 3, 1.5f, 3};
    auto ms = fromPreviousClip(scaled, u, v, W, H, -1);
    expect(ms.valid && approx(ms.x, m.x, 1e-2f) && approx(ms.y, m.y, 1e-2f), "perspective divide");

    // +Y down in pixels: with the flipped viewport (ySign -1), content that was
    // higher on screen last frame (larger clip Y) has negative Y motion.
    float up[4] = {same[0], same[1] + .1f, .5f, 1};
    m = fromPreviousClip(up, u, v, W, H, -1);
    expect(m.valid && approx(m.y, -54.f, 1e-2f), "Y down convention with flipped viewport");
    m = fromPreviousClip(up, u, v, W, H, 1);
    expect(m.valid, "unflipped viewport accepted");

    // Invalid inputs give (0,0) and invalid.
    float behind[4] = {0, 0, .5f, -1};
    m = fromPreviousClip(behind, u, v, W, H, -1);
    expect(!m.valid && m.x == 0 && m.y == 0, "point behind previous camera rejected");
    float nan[4] = {std::numeric_limits<float>::quiet_NaN(), 0, .5f, 1};
    expect(!fromPreviousClip(nan, u, v, W, H, -1).valid, "NaN rejected");
    float far[4] = {10, 0, .5f, 1}; // 5 widths away
    expect(!fromPreviousClip(far, u, v, W, H, -1).valid, "motion beyond 2x extent rejected");

    expect(stored(1e6f) == motionStoreLimit && stored(-1e6f) == -motionStoreLimit &&
               stored(12.5f) == 12.5f,
           "stored motion clamps to half-float-safe range");

    // Jitter: current pixel centre minus this jitter, plus motion, plus last jitter.
    float px, py;
    Motion pan{4, -2, true};
    previousSamplePosition(10, 20, pan, .25f, -.25f, -.125f, .375f, px, py);
    expect(approx(px, 10.5f - .25f + 4 - .125f) && approx(py, 20.5f + .25f - 2 + .375f),
           "previous sample position");

    HistoryKey a{100, 1920, 1080, Source::CameraDerived, false, false, true};
    HistoryKey b = a;
    b.frame = 101;
    expect(historyContinues(a, b), "next frame continues");
    b.frame = 100 + maxFrameGap + 1;
    expect(!historyContinues(a, b), "long gap resets");
    b.frame = 100;
    expect(!historyContinues(a, b), "same frame does not continue");
    b.frame = 101;
    b.width = 1280;
    expect(!historyContinues(a, b), "resize resets");
    b.width = 1920;
    b.source = Source::NativeSsr;
    expect(!historyContinues(a, b), "changing motion source resets");
    b.source = Source::CameraDerived;
    b.cameraCut = true;
    expect(!historyContinues(a, b), "camera cut resets");
    b.cameraCut = false;
    b.projectionChanged = true;
    expect(!historyContinues(a, b), "projection change resets");
    b.projectionChanged = false;
    a.valid = false;
    expect(!historyContinues(a, b), "invalid previous resets");

    expect(std::string_view(sourceToken(Source::NativeSsr)) == "native-renderer" &&
               std::string_view(sourceToken(Source::CameraDerived)) == "camera",
           "settings tokens");
    expect(parseSource("native-renderer") == Source::NativeSsr &&
               parseSource("native-renderer2") == Source::CameraDerived &&
               parseSource("") == Source::CameraDerived && parseSource(nullptr) == Source::CameraDerived,
           "unknown saved tokens stay camera-derived");
    expect(reactiveInvalid == 1.f && reactiveRigidCabin < reactiveCabinGhosting &&
               reactiveSkyGhosting < reactiveCabinGhosting,
           "reactive ordering");

    if (failures)
        return 1;
    std::printf("PASS: motion contract v1 conformance\n");
    return 0;
}

// SPDX-License-Identifier: MIT
// Portable pacing-policy checks. Build: c++ -std=c++17 src/frame_pacing_tests.cpp
#include "frame_pacing.hpp"
#include <cstdio>
#include <cstdlib>
using namespace xpfsr::pacing;
static void check(bool ok, const char *what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
int main() {
    const auto smooth = tuning(Profile::Smooth);
    check(smooth.safetyMarginInMs == 0.1f && smooth.varianceFactor == 0.1f && !smooth.allowHybridSpin &&
              smooth.hybridSpinTime == 2 && !smooth.allowWaitForSingleObjectOnFence,
          "Smooth must equal AMD's documented defaults");
    const auto recover = tuning(Profile::Recover);
    check(recover.safetyMarginInMs == 0.01f && recover.varianceFactor == 0.3f && !recover.allowHybridSpin,
          "Recover must equal AMD's tuning set B");
    const auto power = tuning(Profile::PowerSaver);
    check(power.allowHybridSpin && power.hybridSpinTime >= 2 && power.allowWaitForSingleObjectOnFence,
          "Power saver must sleep, never spin below 2 units");
    check(clampProfile(-4) == Profile::Smooth && clampProfile(99) == Profile::PowerSaver, "Profile clamp");

    check(recommendedBaseCap(60) == 29 && recommendedBaseCap(120) == 59 && recommendedBaseCap(144) == 71 &&
              recommendedBaseCap(165) == 81 && recommendedBaseCap(240) == 119,
          "Base cap must stay slightly below half refresh");
    check(recommendedBaseCap(0) == 0 && recommendedBaseCap(-1) == 0 && recommendedBaseCap(5000) == 0,
          "Unknown refresh gives no cap");

    DisplayState ok{true, 26, 10, true, false, true, true};
    check(driverPacingBlockers(ok).empty(), "Documented driver-pacing requirements accepted");
    auto vsync = ok; vsync.vsyncOn = true;
    check(driverPacingBlockers(vsync).size() == 1, "VSync blocks driver pacing");
    auto old = ok; old.driverMajor = 26; old.driverMinor = 9;
    check(driverPacingBlockers(old).size() == 1, "Old driver blocks driver pacing");
    auto patch0 = ok; patch0.driverPatch = 0;
    check(driverPacingBlockers(patch0).size() == 1, "26.10.0 is below the documented 26.10.1 minimum");
    auto newer = ok; newer.driverMinor = 11; newer.driverPatch = 0;
    check(driverPacingBlockers(newer).empty(), "26.11.0 meets the minimum");
    DisplayState none{};
    check(driverPacingBlockers(none).size() == 5, "Every unmet requirement is explained");
    auto unknown = ok; unknown.driverKnown = false; unknown.variableRefreshKnown = false;
    check(driverPacingBlockers(unknown).size() == 2, "Unknown driver/VRR are reported, not assumed");

    int major = 0, minor = 0;
    check(parseDriverVersion("26.10.1 (AMD proprietary shader compiler)", major, minor) && major == 26 && minor == 10,
          "Adrenalin version parsed");
    check(parseDriverVersion("Driver 25.3.1", major, minor) && major == 25 && minor == 3, "Prefixed version parsed");
    int patch = -1;
    check(parseDriverVersion("26.10.1 (LLPC)", major, minor, patch) && patch == 1, "Patch parsed");
    check(parseDriverVersion("26.10", major, minor, patch) && patch == 0, "Missing patch reads as 0");
    check(!parseDriverVersion("Mesa 24.2.0", major, minor) || major == 24, "Any year.month form is read as-is");
    check(!parseDriverVersion("version 1.4.303", major, minor), "Vulkan API versions are not driver versions");
    check(looksLikeRdna4OrLater(0x1002, "AMD Radeon AI PRO R9700") && looksLikeRdna4OrLater(0x1002, "AMD Radeon RX 9070 XT") &&
              !looksLikeRdna4OrLater(0x1002, "AMD Radeon RX 7900 XTX") && !looksLikeRdna4OrLater(0x10de, "RX 9 imitation"),
          "RDNA 4 hint");

    check(resolveBaseCap(0, 144) == 0 && resolveBaseCap(-1, 144) == 71 && resolveBaseCap(60, 144) == 60 &&
              resolveBaseCap(5, 144) == 20 && resolveBaseCap(-1, 0) == 0,
          "Base cap resolution");
    check(capIndex(-1) == 1 && capIndex(72) == 8 && capIndex(77) == 0 && capCount == 12, "Cap choices");

    FrameLimiter limiter;
    check(!limiter.enabled() && limiter.delay(123) == 0, "Off limiter never waits");
    limiter.setFps(50);  // 20 ms
    const std::int64_t ms = 1000000;
    check(limiter.delay(0) == 0, "First frame is not delayed");
    check(limiter.delay(5 * ms) == 15 * ms, "Early frame waits for its slot");
    check(limiter.delay(40 * ms + ms / 2) == 0, "Slightly late frame keeps cadence without waiting");
    check(limiter.delay(41 * ms) == 19 * ms, "Cadence kept after a small delay");
    check(limiter.delay(500 * ms) == 0, "Hitch never waits");
    check(limiter.delay(501 * ms) == 19 * ms, "After a hitch the cadence restarts instead of bursting");
    std::puts("Frame pacing profiles, base-FPS cap and driver-pacing requirement checks passed");
}

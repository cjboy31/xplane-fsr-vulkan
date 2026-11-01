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
    DisplayState none{};
    check(driverPacingBlockers(none).size() == 5, "Every unmet requirement is explained");
    std::puts("Frame pacing profiles, base-FPS cap and driver-pacing requirement checks passed");
}

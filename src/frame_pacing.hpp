// SPDX-License-Identifier: MIT
#pragma once
// Frame-generation pacing policy (v2). Portable: no Windows, Vulkan or AMD
// headers, so the policy is unit-tested on every CI platform.
//
// The AMD FSR SDK frame-generation swapchain paces presents itself. Since
// SDK 2.1 its pacer accepts FfxApiSwapchainFramePacingTuning through the
// FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING key (DX12 and Vulkan):
//   target delta = average frame time - variance * varianceFactor - safetyMargin
// AMD's own header documents that the default tuning can stay "stuck" at a
// slow target after a heavy scene, and that a smaller margin with a larger
// variance factor recovers sooner at a small cost in variance. X-Plane
// alternates between very different loads (scenery paging, clouds, airports),
// which is exactly that case, so it is offered as a choice.
//
// SDK 2.4.0 adds Driver-Assisted Frame Pacing on RDNA 4 with Adrenalin
// 26.10.1+. It is selected automatically by the swapchain; this file only
// explains whether the documented requirements look satisfied.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace xpfsr::pacing {

enum class Profile : int { Smooth = 0, Recover = 1, PowerSaver = 2 };
inline constexpr int profileCount = 3;
inline constexpr const char *profileNames[profileCount] = {
    "Smooth (AMD default)", "Recover after heavy scenes", "Power saver"};

// Same field order and meaning as FfxApiSwapchainFramePacingTuning.
struct Tuning {
    float safetyMarginInMs{0.1f};
    float varianceFactor{0.1f};
    bool allowHybridSpin{false};
    std::uint32_t hybridSpinTime{2};
    bool allowWaitForSingleObjectOnFence{false};
};

inline Profile clampProfile(int value) {
    return Profile(std::clamp(value, 0, profileCount - 1));
}

inline Tuning tuning(Profile profile) {
    Tuning t;  // AMD defaults: 0.1 ms margin, 0.1 variance, busy spin.
    switch (profile) {
    case Profile::Smooth:
        break;
    case Profile::Recover:
        // AMD's documented "Tuning set B": recovers frame rate after a
        // heavy-to-light transition with only slightly larger variance.
        t.safetyMarginInMs = 0.01f;
        t.varianceFactor = 0.3f;
        break;
    case Profile::PowerSaver:
        // Sleep part of the wait instead of spinning a CPU core. Hybrid spin
        // time stays at the recommended minimum of 2 timer units.
        t.allowHybridSpin = true;
        t.hybridSpinTime = 2;
        t.allowWaitForSingleObjectOnFence = true;
        break;
    }
    return t;
}

// AMD: "The application should ensure that the rendered frame rate is
// slightly below half the desired output frame rate." Returns the largest
// whole base FPS cap strictly below half of the refresh rate, or 0 when the
// refresh rate is unknown/implausible.
inline int recommendedBaseCap(double refreshHz) {
    if (!(refreshHz >= 40.0 && refreshHz <= 1000.0))
        return 0;
    const double half = refreshHz / 2.0;
    int cap = int(half);
    if (double(cap) >= half - 0.5)
        cap -= 1;  // Keep at least half a frame of headroom.
    return std::max(cap, 1);
}

struct DisplayState {
    bool amdRdna4OrLater{};    // RX 9000 / Radeon AI PRO R9700 class.
    int driverMajor{}, driverMinor{};  // Adrenalin version, e.g. 26.10.
    bool fullscreenCoversDisplay{};    // Exclusive or borderless covering the display.
    bool vsyncOn{};
    bool variableRefresh{};            // VRR/FreeSync active for this display.
    bool presenterIs4OrLater{};        // FG swapchain 4.0.0+, not the 3.1.7 compatibility presenter.
};

// Plain-language reasons why driver-assisted pacing will not be used. An
// empty list means the documented requirements look met; the driver still
// decides, so callers must not claim it is active from this alone.
inline std::vector<std::string> driverPacingBlockers(const DisplayState &s) {
    std::vector<std::string> out;
    if (!s.amdRdna4OrLater)
        out.push_back("needs an AMD Radeon RX 9000-series (RDNA 4) or newer GPU");
    if (s.driverMajor < 26 || (s.driverMajor == 26 && s.driverMinor < 10))
        out.push_back("needs AMD Software: Adrenalin 26.10.1 or newer");
    if (!s.presenterIs4OrLater)
        out.push_back("the compatibility presenter (3.1.7) is selected");
    if (!s.fullscreenCoversDisplay)
        out.push_back("X-Plane must be fullscreen or a borderless window covering the display");
    if (s.vsyncOn)
        out.push_back("turn VSync off");
    if (!s.variableRefresh)
        out.push_back("turn on FreeSync / variable refresh for this display");
    return out;
}

}  // namespace xpfsr::pacing

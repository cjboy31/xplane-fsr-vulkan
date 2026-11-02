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
#include <cctype>
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
    bool driverKnown{true};            // false: the driver version string could not be read.
    bool variableRefreshKnown{true};   // false: Windows does not report VRR to the layer.
    int driverPatch{1};                // third part of 26.10.1; the minimum is 26.10.1.
};

// Plain-language reasons why driver-assisted pacing will not be used. An
// empty list means the documented requirements look met; the driver still
// decides, so callers must not claim it is active from this alone.
inline std::vector<std::string> driverPacingBlockers(const DisplayState &s) {
    std::vector<std::string> out;
    if (!s.amdRdna4OrLater)
        out.push_back("needs an AMD Radeon RX 9000-series (RDNA 4) or newer GPU");
    if (!s.driverKnown)
        out.push_back("could not read the driver version; Adrenalin 26.10.1 or newer is needed");
    else if (s.driverMajor < 26 || (s.driverMajor == 26 && (s.driverMinor < 10 || (s.driverMinor == 10 && s.driverPatch < 1))))
        out.push_back("needs AMD Software: Adrenalin 26.10.1 or newer");
    if (!s.presenterIs4OrLater)
        out.push_back("the compatibility presenter (3.1.7) is selected");
    if (!s.fullscreenCoversDisplay)
        out.push_back("X-Plane must be fullscreen or a borderless window covering the display");
    if (s.vsyncOn)
        out.push_back("turn VSync off");
    if (!s.variableRefreshKnown)
        out.push_back("make sure FreeSync / variable refresh is on (Windows does not report it to the mod)");
    else if (!s.variableRefresh)
        out.push_back("turn on FreeSync / variable refresh for this display");
    return out;
}

// Reads "26.10.1" style versions from a Vulkan driverInfo string. Returns
// false when no plausible Adrenalin-style version (year >= 20) is present.
inline bool parseDriverVersion(const std::string &info, int &major, int &minor, int &patch) {
    for (std::size_t i = 0; i + 3 < info.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(info[i])) || (i && std::isdigit(static_cast<unsigned char>(info[i - 1]))))
            continue;
        std::size_t j = i;
        int a = 0, b = 0, digits = 0;
        while (j < info.size() && std::isdigit(static_cast<unsigned char>(info[j])) && digits < 3) { a = a * 10 + (info[j] - '0'); ++j; ++digits; }
        if (j >= info.size() || info[j] != '.' || a < 20 || a > 99)
            continue;
        ++j; digits = 0;
        while (j < info.size() && std::isdigit(static_cast<unsigned char>(info[j])) && digits < 3) { b = b * 10 + (info[j] - '0'); ++j; ++digits; }
        if (!digits || b > 12)
            continue;
        int c = 0;
        if (j + 1 < info.size() && info[j] == '.' && std::isdigit(static_cast<unsigned char>(info[j + 1]))) {
            ++j; digits = 0;
            while (j < info.size() && std::isdigit(static_cast<unsigned char>(info[j])) && digits < 4) { c = c * 10 + (info[j] - '0'); ++j; ++digits; }
        }
        major = a; minor = b; patch = c;
        return true;
    }
    return false;
}
inline bool parseDriverVersion(const std::string &info, int &major, int &minor) {
    int patch = 0;
    return parseDriverVersion(info, major, minor, patch);
}

// AMD lists driver-assisted pacing for RX 9000 (RDNA 4) and newer. The
// device name is the only portable hint a layer has; treat it as a hint.
inline bool looksLikeRdna4OrLater(unsigned vendorId, const std::string &name) {
    if (vendorId != 0x1002)
        return false;
    for (const char *marker : {"RX 9", "R9700", "R9600", "AI PRO R9"})
        if (name.find(marker) != std::string::npos)
            return true;
    return false;
}

// ---- Base frame-rate cap -------------------------------------------------
// AMD: keep the real frame rate slightly below half the output rate. The cap
// is applied to real frames only, before the SDK presents them.
inline constexpr int capSettings[] = {0, -1, 30, 36, 40, 45, 48, 60, 72, 82, 90, 120};
inline constexpr const char *capNames[] = {
    "Off", "Auto (just under half refresh)", "30 FPS", "36 FPS", "40 FPS", "45 FPS",
    "48 FPS", "60 FPS", "72 FPS", "82 FPS", "90 FPS", "120 FPS"};
inline constexpr int capCount = int(sizeof(capSettings) / sizeof(capSettings[0]));
inline int capIndex(int setting) {
    for (int i = 0; i < capCount; ++i)
        if (capSettings[i] == setting)
            return i;
    return 0;
}
// 0 = Off, -1 = Auto from the display refresh rate, otherwise frames per second.
inline int resolveBaseCap(int setting, double refreshHz) {
    if (setting == 0)
        return 0;
    if (setting < 0)
        return recommendedBaseCap(refreshHz);
    return std::clamp(setting, 20, 240);
}

// Deadline scheduler in nanoseconds. The caller sleeps for the returned delay.
// Keeps an even cadence after small delays and never bursts to catch up after
// a long hitch (scenery load, pause): the schedule restarts instead.
class FrameLimiter {
    std::int64_t period_{}, next_{};
    bool started_{};
  public:
    void setFps(int fps) {
        period_ = fps > 0 ? 1000000000LL / fps : 0;
        started_ = false;
    }
    bool enabled() const { return period_ > 0; }
    std::int64_t period() const { return period_; }
    void reset() { started_ = false; }
    std::int64_t delay(std::int64_t now) {
        if (!period_)
            return 0;
        if (!started_) {
            started_ = true;
            next_ = now + period_;
            return 0;
        }
        std::int64_t wait = next_ - now;
        if (wait < 0) {
            if (-wait > period_)
                next_ = now;  // Hitch: restart the cadence, no catch-up burst.
            wait = 0;
        }
        next_ += period_;
        return wait;
    }
};

}  // namespace xpfsr::pacing

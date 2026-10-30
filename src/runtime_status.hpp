// SPDX-License-Identifier: MIT
#pragma once
#include "shared.hpp"
namespace launcher {
enum class RuntimeState {
    stopped,
    native,
    starting,
    loaded,
    active,
    paused,
    stalled,
    groundSafety
};
struct RuntimeMonitor {
    uint64_t presents{}, recordings{}, lastProgress{}, lastDispatch{};
    bool confirmed{};
    void reset() { *this = {}; }
    RuntimeState update(bool running, bool native, bool available, const app::Telemetry &s,
                        bool enabled, uint64_t now) {
        if (!running) {
            reset();
            return RuntimeState::stopped;
        }
        if (native)
            return RuntimeState::native;
        // A camera mapping alone does not establish that the Vulkan layer loaded.
        if (!available || (!s.reason[0] && !s.presents && !s.recordings))
            return confirmed ? RuntimeState::stalled : RuntimeState::starting;
        confirmed=true;
        // A different intercepted pass can publish a bypass after a successful
        // FSR dispatch. Advancing recordings remain proof of actual FSR work.
        bool progress = s.presents != presents || s.recordings != recordings;
        if (progress)
            lastProgress = now;
        if (s.recordings != recordings && s.recordings)
            lastDispatch = now;
        presents = s.presents;
        recordings = s.recordings;
        if (!enabled)
            return RuntimeState::paused;
        if (lastProgress && now - lastProgress > 6000)
            return RuntimeState::stalled;
        if (!s.active && strncmp(s.reason, "Ground camera safety:", 21) == 0)
            return RuntimeState::groundSafety;
        if (s.presents && lastDispatch && now - lastDispatch <= 1500)
            return RuntimeState::active;
        return RuntimeState::loaded;
    }
};
} // namespace launcher

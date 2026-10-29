// SPDX-License-Identifier: MIT
#pragma once
#include "shared.hpp"
namespace launcher {
enum class RuntimeState { stopped, native, starting, loaded, active, paused, stalled };
struct RuntimeMonitor {
    uint64_t presents{}, recordings{}, lastProgress{}, lastDispatch{};
    void reset() { *this = {}; }
    RuntimeState update(bool running, bool native, bool available, const app::Telemetry &s,
                        bool enabled, uint64_t now) {
        if (!running)
            return RuntimeState::stopped;
        if (native)
            return RuntimeState::native;
        // A camera mapping alone does not establish that the Vulkan layer loaded.
        if (!available || (!s.reason[0] && !s.presents && !s.recordings))
            return RuntimeState::starting;
        bool progress = s.presents > presents || s.recordings > recordings;
        if (progress)
            lastProgress = now;
        if (s.recordings > recordings && s.active)
            lastDispatch = now;
        presents = s.presents;
        recordings = s.recordings;
        if (!enabled)
            return RuntimeState::paused;
        if (lastProgress && now - lastProgress > 6000)
            return RuntimeState::stalled;
        if (s.active && s.presents && lastDispatch && now - lastDispatch <= 6000)
            return RuntimeState::active;
        return RuntimeState::loaded;
    }
};
} // namespace launcher

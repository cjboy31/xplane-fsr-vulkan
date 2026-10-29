// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace app {
// The camera that supplied a command buffer's scene jitter is immutable evidence.
// Executing that buffer later must never relabel it as a newer camera frame.
struct SceneCameraStamp {
    uint64_t frame{};
    bool conflict{};
    void observe(uint64_t value) {
        if (!value || (frame && frame != value))
            conflict = true;
        if (!frame)
            frame = value;
    }
    bool matches(uint64_t expected) const { return !conflict && frame && frame == expected; }
};
} // namespace app

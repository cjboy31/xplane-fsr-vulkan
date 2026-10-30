// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace xpfsr {
// Single pending SDK prepare. Source IDs come from actual scene recordings,
// rather than menu/presentation counts. An unmatched scene is presented natively.
struct FrameSequence {
    uint64_t prepared{}, submitted{}, presented{}, latestScene{};
    bool canPrepare() const { return prepared <= presented; }
    bool ready() const {
        return prepared > presented && prepared == submitted && prepared == latestScene;
    }
    void submit(uint64_t scene, uint64_t generation) {
        if (scene)
            latestScene = scene;
        if (generation)
            submitted = generation;
    }
    void finishPresent() {
        if (latestScene >= prepared)
            presented = prepared;
    }
};
} // namespace xpfsr

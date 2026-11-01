// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace xpfsr {
// Diagnostics must not turn a prolonged history-reset condition into a file
// open/write/close on every rendered frame. This never changes render resets.
struct FrameDiagnostics {
    uint64_t lastTick{}, resets{}, suppressed{};
    uint64_t reportedResets{}, reportedSuppressed{};
    bool reported{};
    bool record(uint64_t frame, bool reset, uint64_t now) {
        if (reset) ++resets;
        const bool periodic = frame && frame % 1000 == 0;
        const bool resetDue = reset && (!reported || now < lastTick || now - lastTick >= 2000);
        if (frame <= 4 || periodic || resetDue) {
            reportedResets = resets;
            reportedSuppressed = suppressed;
            resets = suppressed = 0;
            lastTick = now;
            reported = true;
            return true;
        }
        if (reset) ++suppressed;
        return false;
    }
};
}

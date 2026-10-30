// SPDX-License-Identifier: MIT
#pragma once
#include "shared.hpp"
namespace app {
inline bool cameraCut(const Camera &before, const Camera &after) {
    if (!before.valid || !after.valid || before.internal != after.internal ||
        before.width != after.width || before.height != after.height ||
        before.reverseZ != after.reverseZ || before.infiniteZ != after.infiniteZ)
        return true;
    double distance = 0, dot = 0, a = 0, b = 0;
    for (int i = 0; i < 3; ++i) {
        auto step = double(after.position[i]) - before.position[i];
        distance += step * step;
        dot += double(before.forward[i]) * after.forward[i];
        a += double(before.forward[i]) * before.forward[i];
        b += double(after.forward[i]) * after.forward[i];
    }
    // Retain history during ordinary camera motion. Reject teleports, a >20
    // degree single-frame turn, invalid bases and abrupt zoom/projection cuts.
    return !std::isfinite(distance + dot + a + b) || a < .5 || b < .5 || distance > 10000 ||
           dot / std::sqrt(a * b) < .93969262 || !std::isfinite(after.verticalFov) ||
           std::abs(after.verticalFov - before.verticalFov) > .1f;
}
} // namespace app

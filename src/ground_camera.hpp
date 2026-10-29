// SPDX-License-Identifier: MIT
#pragma once
#include "shared.hpp"
namespace app {
// ABI 4's obsolete own-aircraft sphere is no longer used by reconstruction.
// Reuse its four slots only with an explicit marker; old publishers remain
// distinguishable and no shared-memory offsets or sizes change.
constexpr float groundCameraMarker = 59009.f;
inline void groundMetadata(Camera &camera, bool onGround, float clearance) {
    camera.ownSphere[0] = std::isfinite(clearance) ? clearance : 0.f;
    camera.ownSphere[1] = onGround ? 1.f : 0.f;
    camera.ownSphere[2] = std::isfinite(clearance) ? 1.f : 0.f;
    camera.ownSphere[3] = groundCameraMarker;
}
struct GroundCameraSafety {
    bool active{};
    bool update(const Camera &camera) {
        if (camera.internal || camera.ownSphere[3] != groundCameraMarker ||
            camera.ownSphere[1] < .5f) {
            active = false;
            return active;
        }
        if (!camera.valid || camera.ownSphere[2] < .5f || !std::isfinite(camera.ownSphere[0]))
            return active;
        // Hysteresis avoids alternating temporal/native frames at the collision
        // floor. Once airborne or inside the cockpit, the restriction ends.
        const float clearance = camera.ownSphere[0];
        active = active ? clearance < 2.5f : clearance <= 1.5f;
        return active;
    }
};
} // namespace app

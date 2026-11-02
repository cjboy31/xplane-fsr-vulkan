#pragma once
// v2: opt-in texture LOD bias for temporal upscaling (original code).
// AMD's FSR 2/3/4 documentation recommends
//     mipBias = log2(renderResolution / displayResolution) - 1.0
// X-Plane 12.4.4 applies "AMD's recommended" bias for its own FSR, which is
// FSR 1 (whose guidance has no "- 1.0" term). The extra choices add -0.5 or the
// full -1.0 on top of whatever X-Plane set; the log records X-Plane's original
// values so the assumption can be checked on a real flight.
// Only samplers that look like material-texture samplers are changed: they use
// anisotropic filtering, have mip levels and are not depth-compare,
// unnormalized or nearest-filter samplers. Render targets and data lookups
// (bloom chains, shadow maps, LUTs) keep X-Plane's exact settings.
#include "texture_bias_choices.hpp"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cmath>
namespace xpfsr::lod {
inline constexpr float extraBias[] = {0.f, -0.5f, -1.f};
inline bool eligible(const VkSamplerCreateInfo &s) {
    return s.anisotropyEnable == VK_TRUE && s.compareEnable == VK_FALSE && s.unnormalizedCoordinates == VK_FALSE &&
           s.minFilter == VK_FILTER_LINEAR && s.magFilter == VK_FILTER_LINEAR && s.maxLod > 0.f;
}
// Adds `extra` (<= 0) to the application's bias, within the device limit.
inline float biased(float existing, float extra, float deviceLimit) {
    const float limit = std::isfinite(deviceLimit) && deviceLimit > 0 ? deviceLimit : 2.f;
    if (!std::isfinite(existing)) existing = 0.f;
    return std::clamp(existing + std::min(0.f, extra), -limit, limit);
}
inline float parse(const wchar_t *text) {
    if (!text) return 0.f;
    if (text[0] == L'1' && text[1] == 0) return -0.5f;
    if (text[0] == L'2' && text[1] == 0) return -1.f;
    return 0.f;
}
}  // namespace xpfsr::lod

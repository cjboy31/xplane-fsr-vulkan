// SPDX-License-Identifier: MIT
// Shared motion contract v1 for the X-Plane 12 FSR / frame generation mod and
// Native AA. This file is kept byte-identical in both repositories; each repo's
// source check pins its SHA-256 so neither side can drift alone.
// Original code. No simulator, vendor or GPL motion-mod code.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace xpmotion {

constexpr std::uint32_t contractVersion = 1;

// Images every producer writes and every consumer (FSR upscaling, frame
// generation, neural rendering, Native AA TAA) reads. Numbers are VkFormat
// values so this header needs no Vulkan include.
constexpr std::uint32_t depthFormat = 100;    // VK_FORMAT_R32_SFLOAT, reversed Z, 0 = sky
constexpr std::uint32_t motionFormat = 83;    // VK_FORMAT_R16G16_SFLOAT
constexpr std::uint32_t reactiveFormat = 9;   // VK_FORMAT_R8_UNORM

// Motion convention (identical to Native AA's canonical MotionConvention):
// previous position minus current position, in render pixels, +Y down,
// jitter removed, stored on the current frame's grid. A static scene under
// a static camera is exactly (0, 0).
// Jitter is reported by the producer every frame in pixels; consumers use the
// reported value and never assume a particular sequence (FSR's SDK sequence
// and Native AA's 8-sample cycle differ).
constexpr float maxMotionFraction = 2.0f; // |motion| > 2 x extent is invalid
constexpr float motionStoreLimit = 32700.0f; // inside half-float range

// Reactive values: 0 = trust history, 1 = use the new frame only.
constexpr float reactiveInvalid = 1.0f;
constexpr float reactiveRigidCabin = 0.05f;   // cabin moving with the aircraft
constexpr float reactiveUnknownCabin = 1.0f;  // cabin pixels without a body transform
constexpr float reactiveCabinGhosting = 0.2f; // "reduce ghosting": cockpit screens animate
constexpr float reactiveSkyGhosting = 0.15f;  // "reduce ghosting": clouds have no vectors

enum class Source : std::uint32_t { CameraDerived = 0, NativeSsr = 1, PerObject = 2 };
inline const char *sourceName(Source source) {
    switch (source) {
    case Source::CameraDerived:
        return "camera-derived";
    case Source::NativeSsr:
        return "native renderer (SSR, experimental)";
    case Source::PerObject:
        return "per-object (future)";
    default:
        return "unknown";
    }
}
inline const char *sourceToken(Source source) {
    switch (source) {
    case Source::CameraDerived:
        return "camera";
    case Source::NativeSsr:
        return "native-renderer";
    case Source::PerObject:
        return "per-object";
    default:
        return "unknown";
    }
}

// Saved-setting token -> source. Anything unknown stays camera-derived.
inline Source parseSource(const char *token) {
    if (token && !std::strcmp(token, "native-renderer"))
        return Source::NativeSsr;
    return Source::CameraDerived;
}

struct Motion {
    float x{}, y{};
    bool valid{};
};

// CPU oracle of motion_contract.glsl xpm_motion(): previousClip is the
// current pixel's point transformed into the previous frame's clip space,
// unjitteredUv the current pixel centre with jitter removed, ySign the
// viewport Y sign (-1 for X-Plane's flipped viewport). Invalid -> (0,0).
inline Motion fromPreviousClip(const float previousClip[4], float unjitteredU,
                               float unjitteredV, float width, float height, float ySign) {
    Motion result;
    const float w = previousClip[3];
    if (!(w > 1e-7f) || !std::isfinite(previousClip[0]) || !std::isfinite(previousClip[1]) ||
        !std::isfinite(w))
        return result;
    float u = previousClip[0] / w, v = previousClip[1] / w;
    v *= ySign;
    u = u * 0.5f + 0.5f;
    v = v * 0.5f + 0.5f;
    const float x = (u - unjitteredU) * width, y = (v - unjitteredV) * height;
    if (!std::isfinite(x) || !std::isfinite(y) || std::fabs(x) > width * maxMotionFraction ||
        std::fabs(y) > height * maxMotionFraction)
        return result;
    result.x = x;
    result.y = y;
    result.valid = true;
    return result;
}
inline float stored(float value) {
    return value < -motionStoreLimit ? -motionStoreLimit
           : value > motionStoreLimit ? motionStoreLimit
                                      : value;
}

// Where the current pixel's content was in the previous jittered image, in
// pixel coordinates (centre convention): current centre, minus this frame's
// jitter, plus motion, plus the previous frame's jitter.
inline void previousSamplePosition(float pixelX, float pixelY, Motion motion, float jitterX,
                                   float jitterY, float previousJitterX, float previousJitterY,
                                   float &outX, float &outY) {
    outX = pixelX + 0.5f - jitterX + motion.x + previousJitterX;
    outY = pixelY + 0.5f - jitterY + motion.y + previousJitterY;
}

// History continuity: every consumer resets on the same events so FSR, frame
// generation and TAA never disagree about whether last frame is usable.
struct HistoryKey {
    std::uint64_t frame{};
    std::uint32_t width{}, height{};
    Source source{Source::CameraDerived};
    bool cameraCut{}, projectionChanged{}, valid{};
};
constexpr std::uint64_t maxFrameGap = 15;
inline bool historyContinues(const HistoryKey &previous, const HistoryKey &current) {
    return previous.valid && current.valid && !current.cameraCut &&
           !current.projectionChanged && previous.width == current.width &&
           previous.height == current.height && previous.source == current.source &&
           current.frame > previous.frame && current.frame - previous.frame <= maxFrameGap;
}

} // namespace xpmotion

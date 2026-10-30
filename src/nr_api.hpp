// SPDX-License-Identifier: MIT
// Original, versioned C boundary; no STL objects or exceptions cross DLLs.
#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include "nr_controls.hpp"
namespace xpfsr::nr {
inline constexpr uint32_t apiVersion = 3;
struct Host {
    uint32_t size = sizeof(Host), version = apiVersion;
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t queueFamily{};
    PFN_vkGetInstanceProcAddr physicalDispatch{};
    void (*lockQueue)(void *){};
    void (*unlockQueue)(void *){};
    void (*log)(const char *){};
    void *user{};
};
struct Config {
    uint32_t size = sizeof(Config), version = apiVersion;
    const wchar_t *root{};
    uint32_t width{}, height{};
    VkFormat format{};
    uint32_t strength = 1; // 1 subtle, 2 balanced, 3 strong
    Controls controls;
};
struct Frame {
    uint32_t size = sizeof(Frame), version = apiVersion;
    VkCommandBuffer commands{};
    VkImage color{};
    VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL;
    VkImageUsageFlags usage{}; // Actual private-image capabilities; zero keeps the copy path.
    uint32_t reset{};
    Controls controls;
};
struct Metrics { float lastGpuMs{},averageGpuMs{},networkGpuMs{}; };
using Create = void *(*)(const Host *, const Config *, char *, uint32_t);
using Record = int (*)(void *, const Frame *, char *, uint32_t);
using Destroy = void (*)(void *);
using Query = int (*)(void *,Metrics *,uint32_t);
} // namespace xpfsr::nr

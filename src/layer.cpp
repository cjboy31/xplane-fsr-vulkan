// SPDX-License-Identifier: MIT
// Explicit Vulkan layer for X-Plane 12.4.4. New implementation.
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include "cockpit_vulkan.hpp"
#include "dispatch_cache.hpp"
#include "feature_sizes.hpp"
#include "framegen.hpp"
#include "frame_sequence.hpp"
#include "fsr24.hpp"
#include "pacing.hpp"
#include "shared.hpp"
#include "scene_frame.hpp"
#include "ground_camera.hpp"
#include "session_logs.hpp"
#include "temporal_spv.hpp"
#include "temporal_msaa_spv.hpp"
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <vector>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>
#include <windows.h>
namespace {
thread_local unsigned sdkNesting = 0;
struct SdkScope {
    SdkScope() { ++sdkNesting; }
    ~SdkScope() { --sdkNesting; }
};
struct SwapchainGeneration {
    std::shared_ptr<xpfsr::Presentation> presentation;
    uint32_t width{}, height{};
    VkFormat format{};
    bool retired{};
};
std::string requestedFrameGeneration() {
    wchar_t mode[32]{};
    GetEnvironmentVariableW(L"XPFSR24_FG", mode, 32);
    if (!wcscmp(mode, L"3.1.6"))
        return "3.1.6";
    if (!wcscmp(mode, L"4.0.2"))
        return "4.0.2";
    return {};
}
void log(const char *format, ...) {
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    wchar_t path[32768]{};
    if (!GetEnvironmentVariableW(L"XPFSR24_LOG", path, 32768))
        return;
    static std::wstring activeLogPath;
    const bool first = activeLogPath != path;
    FILE *file = _wfopen(path, first ? L"w" : L"a");
    if (!file)
        return;
    if (first) {
        try {
            auto header = session::header(session::rendererToken(), "renderer");
            fputs(header.c_str(), file);
        } catch (...) {
            fprintf(file, "XPFSR24 new renderer session; pid=%lu\n", GetCurrentProcessId());
        }
        activeLogPath = path;
    }
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputc('\n', file);
    fclose(file);
}
template <class H> void *key(H handle) {
    return handle ? *reinterpret_cast<void **>(handle) : nullptr;
}
struct Instance {
    VkInstance handle{};
    PFN_vkGetInstanceProcAddr next{};
};
struct Descriptor {
    VkDescriptorType type{};
    VkDescriptorImageInfo image{};
};
using Bindings = std::map<std::pair<uint32_t, uint32_t>, Descriptor>;
struct Template {
    std::vector<VkDescriptorUpdateTemplateEntry> entries;
    uint32_t set{};
    VkPipelineBindPoint point{};
};
enum class Mode { Observe, Upscale, NativeAA };
Mode selectedMode() {
    wchar_t value[32]{};
    GetEnvironmentVariableW(L"XPFSR24_MODE", value, 32);
    if (!wcscmp(value, L"upscale"))
        return Mode::Upscale;
    if (!wcscmp(value, L"native-aa"))
        return Mode::NativeAA;
    return Mode::Observe;
}
struct ImageInfo {
    VkImageCreateInfo create{};
    uint64_t generation{};
};
struct ViewInfo {
    VkImageViewCreateInfo create{};
};
struct Depth {
    VkImageView view{};
    VkImage image{};
    VkImageLayout layout{};
    uint64_t epoch{};
    float jitterX{}, jitterY{}, ySign = -1;
    std::set<VkImage> colors;
    uint64_t order{};
    bool fullArea{}, cameraCoherent = true;
};
struct BindingUse {
    bool known{};
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> access;
};
struct GraphicsViewport {
    VkViewport viewport{};
    std::vector<VkViewport> viewports;
    bool known{}, modified{};
};
struct FrameResources;
struct DepthProbe;
struct DepthCapture;
struct Command {
    VkCommandPool pool{};
    uint32_t family{};
    VkPipeline pipeline{}, graphicsPipeline{};
    VkPipelineLayout layout{};
    std::map<uint32_t, VkDescriptorSet> sets, graphicsSets;
    std::map<uint32_t, Bindings> pushed, graphicsPushed;
    std::set<VkImage> activeColors;
    std::map<uint32_t, std::vector<uint32_t>> offsets;
    std::map<VkImage, VkImageLayout> layouts;
    std::map<VkImage, Depth> depths;
    std::map<VkImage, std::set<VkImage>> depthOrigins;
    std::vector<DepthProbe *> probes;
    uint64_t depthOrder{};
    app::SceneCameraStamp sceneCamera;
    bool activeCaptureEligible{};
    std::map<VkImage, std::shared_ptr<DepthCapture>> capturedDepths;
    uint64_t fgFrame{}, sceneFrame{};
    bool oneTime{}, rendering{}, secondary{}, processed{}, failed{}, submitted{}, dynamicOffsets{};
    uint64_t epoch{};
    struct Push {
        VkPipelineLayout layout{};
        VkShaderStageFlags stages{};
        uint32_t offset{};
        std::vector<char> data;
    };
    std::vector<Push> constants;
    VkViewport viewport{};
    std::vector<VkViewport> viewports;
    bool hasViewport{}, viewportWithCount{}, sceneViewport{};
    VkImage activeDepth{};
    std::set<VkImage> readImages;
    std::vector<VkCommandBuffer> executedChildren;
    float appliedJitterX{}, appliedJitterY{};
    std::unique_ptr<FrameResources> resources;
};
struct Device {
    const uint64_t dispatchIdentity = xpfsr::nextDispatchIdentity();
    std::atomic<uint64_t> dispatchLookups{0};
    xpfsr::Pacing pacing;
    uint64_t lastPacingReport{}, lastPresentRecordings{};
    bool reduceGhosting = true;
    bool cockpitRequested{}, cockpitTint{}, cockpitFailed{};
    std::shared_ptr<cockpit::Compositor> cockpitCompositor;
    PFN_vkSetDeviceLoaderData setLoaderData{};
    VkDevice handle{};
    VkPhysicalDevice physical{};
    std::shared_ptr<Instance> instance;
    PFN_vkGetDeviceProcAddr next{};
    std::recursive_mutex lock;
    std::map<VkImage, ImageInfo> images;
    std::map<VkImageView, ViewInfo> views;
    std::map<VkDescriptorSet, Bindings> descriptors;
    std::map<VkDescriptorUpdateTemplate, Template> templates;
    std::map<VkDescriptorSetLayout, uint32_t> layoutDynamicCounts;
    std::map<VkDescriptorSet, uint32_t> setDynamicCounts;
    std::map<VkPipeline, GraphicsViewport> graphicsViewports;
    std::map<VkShaderModule, BindingUse> shaderUse;
    std::map<VkPipeline, BindingUse> pipelineUse;
    std::map<std::vector<uint8_t>, BindingUse> identifierUse;
    std::map<VkShaderModule, uint32_t> shaders;
    std::map<VkPipeline, uint32_t> pipelines;
    std::map<std::vector<uint8_t>, uint32_t> shaderIdentifiers;
    std::map<VkCommandPool, uint32_t> pools;
    std::map<VkCommandBuffer, Command> commands;
    std::map<VkImage, Depth> depths;
    std::map<VkImage, VkImageLayout> layouts;
    std::map<std::array<uint32_t, 6>, std::unique_ptr<xpfsr::Context>> contexts;
    std::unique_ptr<xpfsr::Runtime> runtime;
    std::vector<std::unique_ptr<DepthProbe>> probes;
    uint64_t imageGeneration{};
    VkImage lastDepth{};
    uint64_t lastDepthGeneration{};
    VkPhysicalDevice applicationPhysical{};
    VkPhysicalDeviceMemoryProperties memory{};
    uint32_t graphicsFamily = UINT32_MAX;
    HANDLE mapping{};
    app::Shared *shared{};
    app::Camera camera{};
    app::Telemetry status{};
    VkDescriptorSetLayout packLayout{};
    VkPipelineLayout packPipelineLayout{};
    VkPipeline packPipeline{}, packMsaaPipeline{};
    VkSampler sampler{};
    Mode mode = Mode::Observe;
    std::array<uint32_t, 6> activeContext{};
    uint64_t jitterFrame{};
    std::string fgRequested = requestedFrameGeneration();
    bool fgFailed{}, fgLiveEnabled = true;
    uint32_t fgQueueBase = UINT32_MAX;
    std::array<VkQueue, 4> fgQueues{};
    std::map<VkImage, std::weak_ptr<SwapchainGeneration>> fgBackbuffers;
    std::map<VkSwapchainKHR, std::shared_ptr<SwapchainGeneration>> fgSwapchains;
    std::set<VkSwapchainKHR> nativeSwapchains;
    std::shared_ptr<SwapchainGeneration> fgCurrent;
    std::unique_ptr<xpfsr::FrameGenerator> frameGenerator;
    std::array<uint32_t, 6> fgKey{};
    xpfsr::FrameSequence fgSequence;
    uint64_t fgPendingSkips{}, fgSourceBypasses{};
    uint64_t viewportCalls{}, secondaryViewportCalls{}, multiViewportCalls{};
    bool runtimeFailed{}, pathReady{}, featuresReady{};
    uint64_t epoch{}, frameSerial{};
    float jitterX{}, jitterY{};
    uint64_t lastCameraFrame{}, lastRecordedCamera{}, sceneFrameRejects{};
    app::GroundCameraSafety groundSafety;
    bool groundCameraActive() {
        const bool before = groundSafety.active;
        const bool active = groundSafety.update(camera);
        if (active != before)
            log("Ground camera safety %s: external=%u aircraft-ground=%g camera-agl=%gm; no "
                "camera/control writes",
                active ? "entered (native rendering)" : "released (temporal restart)",
                !camera.internal, camera.ownSphere[1], camera.ownSphere[0]);
        if (active)
            pathReady = false;
        return active;
    }
    bool enabled = true, separateDepthStencil{};
    uint64_t lastStatusWrite{}, lastControlRead{}, nativeCalls{};
    template <class T> T fn(const char *name) {
        return xpfsr::cachedDeviceFunction<T>(dispatchIdentity, name, [&] {
            dispatchLookups.fetch_add(1, std::memory_order_relaxed);
            return next(handle, name);
        });
    }
    template <class T> T inst(const char *name) {
        return reinterpret_cast<T>(instance->next(instance->handle, name));
    }
    void attach() {
        if (shared)
            return;
        auto name = app::mappingName(GetCurrentProcessId());
        mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (mapping)
            shared = static_cast<app::Shared *>(
                MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(app::Shared)));
    }
    void publish(const char *reason) {
        if (reason != status.reason)
            strncpy(status.reason, reason, sizeof(status.reason) - 1);
        attach();
        if (shared) {
            InterlockedIncrement(&shared->statusSequence);
            MemoryBarrier();
            shared->status = status;
            MemoryBarrier();
            InterlockedIncrement(&shared->statusSequence);
        }
        auto now = GetTickCount64();
        if (now - lastStatusWrite < 500)
            return;
        lastStatusWrite = now;
        wchar_t path[32768]{};
        if (GetEnvironmentVariableW(L"XPFSR24_STATUS", path, 32768)) {
            auto temporary = std::filesystem::path(path).wstring() + L".tmp";
            {
                std::ofstream f(std::filesystem::path(temporary), std::ios::trunc);
                f << status.reason << '\n'
                  << status.provider << '\n'
                  << status.presents << ' ' << status.recordings << ' ' << status.bypasses << ' '
                  << status.renderWidth << ' ' << status.renderHeight << ' ' << status.outputWidth
                  << ' ' << status.outputHeight << ' ' << status.active << '\n'
                  << status.fgProvider << '\n'
                  << status.fgPrepared << ' ' << status.fgGenerated << ' '
                  << status.displayedPresents << ' ' << status.fgActive << '\n'
                  << status.fgReason << '\n'
                  << status.cockpitRecordings << ' ' << status.cockpitActive << ' '
                  << status.cockpitScreens << '\n'
                  << status.cockpitReason << '\n';
            }
            MoveFileExW(temporary.c_str(), path, MOVEFILE_REPLACE_EXISTING);
        }
    }
    void readControl() {
        auto now = GetTickCount64();
        if (now - lastControlRead < 100)
            return;
        lastControlRead = now;
        wchar_t path[32768]{};
        if (GetEnvironmentVariableW(L"XPFSR24_CONTROL", path, 32768)) {
            std::ifstream f{std::filesystem::path(path)};
            char value{};
            if (f.get(value) && (value == '0' || value == '1')) {
                bool was = enabled;
                enabled = value == '1';
                if (!was && enabled)
                    camera.reset = 1;
                if (shared)
                    InterlockedExchange(&shared->enabled, enabled);
            }
        }
        wchar_t fgPath[32768]{};
        if (GetEnvironmentVariableW(L"XPFSR24_FG_CONTROL", fgPath, 32768)) {
            std::ifstream f{std::filesystem::path(fgPath)};
            char value{};
            if (f.get(value) && (value == '0' || value == '1'))
                fgLiveEnabled = value == '1';
        }
    }
    void bypass(const char *reason) {
        ++status.bypasses;
        status.active = 0;
        pathReady = false;
        if (status.bypasses < 8 || status.bypasses % 300 == 0)
            log("Bypass: %s", reason);
        publish(reason);
    }
    uint32_t memoryType(uint32_t allowed, VkMemoryPropertyFlags flags) {
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((allowed & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
        throw std::runtime_error("No compatible memory type");
    }
};
std::mutex registryLock;
std::map<void *, std::shared_ptr<Instance>> instances;
std::map<void *, std::shared_ptr<Device>> devices;
std::shared_ptr<Instance> instanceFor(VkInstance i) {
    std::lock_guard<std::mutex> l(registryLock);
    auto p = instances.find(key(i));
    return p == instances.end() ? nullptr : p->second;
}
template <class H> std::shared_ptr<Device> deviceFor(H h) {
    std::lock_guard<std::mutex> l(registryLock);
    auto p = devices.find(key(h));
    return p == devices.end() ? nullptr : p->second;
}
void check(VkResult r, const char *name) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(name) + " failed: " + std::to_string(r));
}
struct OwnedImage {
    Device *device{};
    xpfsr::Image value{};
    VkDeviceMemory memory{};
    VkImageView view{};
    void create(Device &d, uint32_t w, uint32_t h, VkFormat format) {
        device = &d;
        auto &c = value.description;
        c.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        c.imageType = VK_IMAGE_TYPE_2D;
        c.extent = {w, h, 1};
        c.format = format;
        c.arrayLayers = c.mipLevels = 1;
        c.samples = VK_SAMPLE_COUNT_1_BIT;
        c.tiling = VK_IMAGE_TILING_OPTIMAL;
        c.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        check(d.fn<PFN_vkCreateImage>("vkCreateImage")(d.handle, &c, nullptr, &value.handle),
              "Create private image");
        VkMemoryRequirements r{};
        d.fn<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(d.handle,
                                                                               value.handle, &r);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        a.allocationSize = r.size;
        a.memoryTypeIndex = d.memoryType(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(d.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(d.handle, &a, nullptr, &memory),
              "Allocate private image");
        check(d.fn<PFN_vkBindImageMemory>("vkBindImageMemory")(d.handle, value.handle, memory, 0),
              "Bind private image");
        VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        v.image = value.handle;
        v.viewType = VK_IMAGE_VIEW_TYPE_2D;
        v.format = format;
        v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(d.fn<PFN_vkCreateImageView>("vkCreateImageView")(d.handle, &v, nullptr, &view),
              "Create private view");
    }
    ~OwnedImage() {
        if (!device)
            return;
        auto &d = *device;
        if (view)
            d.fn<PFN_vkDestroyImageView>("vkDestroyImageView")(d.handle, view, nullptr);
        if (value.handle)
            d.fn<PFN_vkDestroyImage>("vkDestroyImage")(d.handle, value.handle, nullptr);
        if (memory)
            d.fn<PFN_vkFreeMemory>("vkFreeMemory")(d.handle, memory, nullptr);
    }
};
struct DepthCapture {
    Device *device{};
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageViewCreateInfo view{};
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint64_t generation{}, frame{};
    uint32_t width{}, height{};
    void create(Device &d, const ImageInfo &source) {
        device = &d;
        generation = source.generation;
        width = source.create.extent.width;
        height = source.create.extent.height;
        VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        c.imageType = VK_IMAGE_TYPE_2D;
        c.format = source.create.format;
        c.extent = {width, height, 1};
        c.mipLevels = c.arrayLayers = 1;
        c.samples = source.create.samples;
        c.tiling = VK_IMAGE_TILING_OPTIMAL;
        c.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        check(d.fn<PFN_vkCreateImage>("vkCreateImage")(d.handle, &c, nullptr, &image),
              "Preserved depth image");
        VkMemoryRequirements mr{};
        d.fn<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(d.handle, image,
                                                                               &mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = d.memoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(d.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(d.handle, &ai, nullptr, &memory),
              "Preserved depth memory");
        check(d.fn<PFN_vkBindImageMemory>("vkBindImageMemory")(d.handle, image, memory, 0),
              "Bind preserved depth");
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image;
        view.format = c.format;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    }
    ~DepthCapture() {
        if (!device)
            return;
        auto &d = *device;
        if (image)
            d.fn<PFN_vkDestroyImage>("vkDestroyImage")(d.handle, image, nullptr);
        if (memory)
            d.fn<PFN_vkFreeMemory>("vkFreeMemory")(d.handle, memory, nullptr);
    }
};
struct FrameResources {
    Device *device{};
    std::shared_ptr<DepthCapture> depthOwner;
    OwnedImage color, depth, motion, reactive, output;
    std::unique_ptr<cockpit::Frame> cockpitFrame;
    VkImageView depthView{};
    VkBuffer uniform{};
    VkDeviceMemory uniformMemory{};
    void *mapped{};
    VkDescriptorPool descriptorPool{};
    VkDescriptorSet set{};
    std::array<uint32_t, 4> sizes{};
    VkImage nativeDepth{};
    uint64_t nativeDepthGeneration{};
    uint32_t depthLayer{};
    VkFormat colorFormat{}, outputFormat{};
    ~FrameResources() {
        if (!device)
            return;
        auto &d = *device;
        if (descriptorPool)
            d.fn<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(d.handle, descriptorPool,
                                                                         nullptr);
        if (depthView)
            d.fn<PFN_vkDestroyImageView>("vkDestroyImageView")(d.handle, depthView, nullptr);
        if (mapped)
            d.fn<PFN_vkUnmapMemory>("vkUnmapMemory")(d.handle, uniformMemory);
        if (uniform)
            d.fn<PFN_vkDestroyBuffer>("vkDestroyBuffer")(d.handle, uniform, nullptr);
        if (uniformMemory)
            d.fn<PFN_vkFreeMemory>("vkFreeMemory")(d.handle, uniformMemory, nullptr);
    }
};
struct DepthProbe {
    Device *device{};
    VkImage source{};
    uint64_t generation{}, frame{};
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    void *mapped{};
    VkFence fence{};
    int phase{};
    bool geometry{}, ambiguous{};
    ~DepthProbe() {
        if (!device)
            return;
        auto &d = *device;
        if (fence)
            d.fn<PFN_vkDestroyFence>("vkDestroyFence")(d.handle, fence, nullptr);
        if (mapped)
            d.fn<PFN_vkUnmapMemory>("vkUnmapMemory")(d.handle, memory);
        if (buffer)
            d.fn<PFN_vkDestroyBuffer>("vkDestroyBuffer")(d.handle, buffer, nullptr);
        if (memory)
            d.fn<PFN_vkFreeMemory>("vkFreeMemory")(d.handle, memory, nullptr);
    }
};
DepthProbe *findProbe(Device &d, VkImage image) {
    auto meta = d.images.find(image);
    if (meta == d.images.end())
        return nullptr;
    for (auto &probe : d.probes)
        if (probe->source == image && probe->generation == meta->second.generation)
            return probe.get();
    return nullptr;
}
void pollProbes(Device &d) {
    for (auto &job : d.probes) {
        if (job->phase != 1)
            continue;
        auto result = d.fn<PFN_vkGetFenceStatus>("vkGetFenceStatus")(d.handle, job->fence);
        if (result != VK_SUCCESS)
            continue;
        MemoryBarrier();
        auto *values = static_cast<float *>(job->mapped);
        unsigned finite = 0, geometry = 0;
        float lo = 1, hi = 0;
        for (unsigned i = 0; i < 256; ++i) {
            float v = values[i];
            if (std::isfinite(v) && v >= 0 && v <= 1) {
                ++finite;
                lo = std::min(lo, v);
                hi = std::max(hi, v);
                if (v > 1e-8f && v < .9999999f)
                    ++geometry;
            }
        }
        job->geometry = finite == 256 && geometry > 0 &&
                        (!job->ambiguous || (hi - lo) > std::max(1e-7f, hi * .001f));
        job->phase = 2;
        log("Completed depth probe: image=%p finite=%u geometry=%u min=%g max=%g frame=%llu",
            reinterpret_cast<void *>(job->source), finite, geometry, lo, hi,
            (unsigned long long)job->frame);
    }
}
DepthProbe &createProbe(Device &d, VkImage image) {
    auto job = std::make_unique<DepthProbe>();
    job->device = &d;
    job->source = image;
    job->generation = d.images.at(image).generation;
    job->frame = d.camera.frame;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = 1024;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(d.fn<PFN_vkCreateBuffer>("vkCreateBuffer")(d.handle, &bi, nullptr, &job->buffer),
          "Depth probe buffer");
    VkMemoryRequirements mr{};
    d.fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(d.handle, job->buffer,
                                                                             &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = d.memoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(d.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(d.handle, &ai, nullptr, &job->memory),
          "Depth probe memory");
    check(d.fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(d.handle, job->buffer, job->memory, 0),
          "Bind depth probe");
    check(d.fn<PFN_vkMapMemory>("vkMapMemory")(d.handle, job->memory, 0, 1024, 0, &job->mapped),
          "Map depth probe");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(d.fn<PFN_vkCreateFence>("vkCreateFence")(d.handle, &fi, nullptr, &job->fence),
          "Depth probe fence");
    d.probes.push_back(std::move(job));
    return *d.probes.back();
}
void submittedProbes(Device &d, VkQueue queue, const std::vector<VkCommandBuffer> &commands) {
    std::vector<VkCommandBuffer> pending = commands;
    std::set<VkCommandBuffer> visited;
    for (size_t i = 0; i < pending.size(); ++i) {
        auto cb = pending[i];
        if (!visited.insert(cb).second)
            continue;
        auto found = d.commands.find(cb);
        if (found == d.commands.end())
            continue;
        auto &command = found->second;
        if (queue == d.fgQueues[0])
            d.fgSequence.submit(command.sceneFrame, command.fgFrame);
        pending.insert(pending.end(), command.executedChildren.begin(),
                       command.executedChildren.end());
        command.submitted = true;
        for (auto *job : command.probes)
            if (job->phase == 0) {
                VkSubmitInfo empty{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                auto result =
                    d.fn<PFN_vkQueueSubmit>("vkQueueSubmit")(queue, 1, &empty, job->fence);
                if (result == VK_SUCCESS)
                    job->phase = 1;
                else {
                    job->phase = 3;
                    log("Depth probe fence submission failed: %d", int(result));
                }
            }
    }
}
void barrier(Device &d, VkCommandBuffer cb, VkImage image, VkImageLayout from, VkImageLayout to,
             VkImageSubresourceRange range) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.image = image;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = range;
    b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED
                          ? 0
                          : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    d.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                                                           nullptr, 0, nullptr, 1, &b);
}
void transition(Device &d, VkCommandBuffer cb, OwnedImage &image, VkImageLayout to) {
    barrier(d, cb, image.value.handle, image.value.layout, to,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
    image.value.layout = to;
}
void captureDepth(Device &d, VkCommandBuffer cb, Command &command) {
    if (!command.activeCaptureEligible || !command.activeDepth)
        return;
    auto depth = command.depths.find(command.activeDepth);
    auto meta = d.images.find(command.activeDepth);
    if (depth == command.depths.end() || meta == d.images.end() || !depth->second.cameraCoherent)
        return;
    auto nativeView = d.views.find(depth->second.view);
    if (nativeView == d.views.end())
        return;
    auto &owner = command.capturedDepths[command.activeDepth];
    if (!owner || owner->generation != meta->second.generation) {
        owner = std::make_shared<DepthCapture>();
        owner->create(d, meta->second);
    }
    auto &captured = *owner;
    captured.frame = depth->second.epoch;
    auto sourceRange = nativeView->second.create.subresourceRange;
    sourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    sourceRange.levelCount = sourceRange.layerCount = 1;
    auto targetRange = captured.view.subresourceRange;
    if (!d.separateDepthStencil && ((captured.view.format == VK_FORMAT_D32_SFLOAT_S8_UINT) ||
                                    (captured.view.format == VK_FORMAT_D24_UNORM_S8_UINT) ||
                                    (captured.view.format == VK_FORMAT_D16_UNORM_S8_UINT))) {
        sourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        targetRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    barrier(d, cb, command.activeDepth, depth->second.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            sourceRange);
    barrier(d, cb, captured.image, captured.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            targetRange);
    VkImageCopy copy{};
    copy.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, sourceRange.baseMipLevel,
                           sourceRange.baseArrayLayer, 1};
    copy.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    copy.extent = {captured.width, captured.height, 1};
    d.fn<PFN_vkCmdCopyImage>("vkCmdCopyImage")(cb, command.activeDepth,
                                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, captured.image,
                                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(d, cb, command.activeDepth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, depth->second.layout,
            sourceRange);
    barrier(d, cb, captured.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, targetRange);
    captured.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
uint32_t fsrShader(const VkShaderModuleCreateInfo *c) {
    if (!c || c->codeSize < 20 || c->codeSize % 4 || c->pCode[0] != 0x07230203)
        return false;
    bool input = false, output = false, data = false;
    for (size_t i = 5; i < c->codeSize / 4;) {
        uint32_t n = c->pCode[i] >> 16, op = c->pCode[i] & 65535;
        if (!n || i + n > c->codeSize / 4)
            return false;
        if (op == 5 && n > 2) {
            std::string s(reinterpret_cast<const char *>(c->pCode + i + 2), (n - 2) * 4);
            input |= s.find("u_input_texture") != std::string::npos;
            output |= s.find("i_output_texture") != std::string::npos;
            data |= s.find("u_fsr_data") != std::string::npos;
        }
        i += n;
    }
    if (!(input && output && data))
        return 0;
    // Bind only to the inspected simulator build. Names alone are insufficient
    // to distinguish a copy, upscaler or sharpening stage across beta updates.
    BCRYPT_ALG_HANDLE alg{};
    BCRYPT_HASH_HANDLE object{};
    unsigned char digest[32]{};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return 0;
    auto ok = BCryptCreateHash(alg, &object, nullptr, 0, nullptr, 0, 0) >= 0;
    if (ok)
        ok = BCryptHashData(object, reinterpret_cast<PUCHAR>(const_cast<uint32_t *>(c->pCode)),
                            ULONG(c->codeSize), 0) >= 0 &&
             BCryptFinishHash(object, digest, 32, 0) >= 0;
    if (object)
        BCryptDestroyHash(object);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok)
        return 0;
    char hash[65]{};
    for (int i = 0; i < 32; ++i)
        sprintf(hash + i * 2, "%02x", digest[i]);
    for (auto *known : {"12413e40210050247e6cfe94c5fa86552516d1a43b0cf71564b30f980199d291",
                        "82f74251046e090f2676288e0ab5228c678b7eb67d6756bb4fa0ee235149a6c4"})
        if (!strcmp(hash, known))
            return 1;
    for (auto *known : {"0ffe938f43183f833cd5f769d9795912cd24552e44e78767cb77387a91ed8a6b",
                        "ab19f6717a03a7fe8dbb67ff3f5fa24d6a8e33dcf0efb5585427a7261fa72420"})
        if (!strcmp(hash, known))
            return 2;
    for (auto *known : {"fc2f94bcfb30834a979bcd01d3a3c45a7c5a23df4a38c9c8cdc28803cd219951",
                        "5784f10cb4f4908a4aa0cd0b1fc1ebb886d781261916dea5ba1bb94c934a0da0"})
        if (!strcmp(hash, known))
            return 3;
    wchar_t process[32768]{};
    GetModuleFileNameW(nullptr, process, 32768);
    if (std::filesystem::path(process).filename() == L"layer_test.exe")
        return 1;
    return 0;
}
BindingUse reflectBindings(const VkShaderModuleCreateInfo *c) {
    BindingUse result;
    if (!c || c->codeSize < 20 || c->codeSize % 4 || c->pCode[0] != 0x07230203)
        return result;
    std::map<uint32_t, uint32_t> sets, bindings, base;
    std::set<uint32_t> noRead, noWrite;
    for (size_t i = 5; i < c->codeSize / 4;) {
        uint32_t n = c->pCode[i] >> 16, op = c->pCode[i] & 65535;
        if (!n || i + n > c->codeSize / 4)
            return result;
        if (op == 71 && n >= 3) {
            auto id = c->pCode[i + 1], dec = c->pCode[i + 2];
            if (dec == 24)
                noWrite.insert(id);
            if (dec == 25)
                noRead.insert(id);
            if (n >= 4) {
                if (dec == 33)
                    bindings[id] = c->pCode[i + 3];
                if (dec == 34)
                    sets[id] = c->pCode[i + 3];
            }
        }
        i += n;
    }
    for (auto &[id, binding] : bindings)
        base[id] = id;
    auto use = [&](uint32_t pointer) {
        auto found = base.find(pointer);
        if (found == base.end())
            return;
        auto id = found->second;
        if (!sets.count(id) || !bindings.count(id))
            return;
        uint32_t flags = (noRead.count(id) ? 0 : 1) | (noWrite.count(id) ? 0 : 2);
        result.access[{sets[id], bindings[id]}] |= flags;
    };
    for (size_t i = 5; i < c->codeSize / 4;) {
        uint32_t n = c->pCode[i] >> 16, op = c->pCode[i] & 65535;
        if (op == 61 && n >= 4)
            use(c->pCode[i + 3]);
        if ((op == 65 || op == 66 || op == 67 || op == 83) && n >= 4) {
            auto found = base.find(c->pCode[i + 3]);
            if (found != base.end()) {
                base[c->pCode[i + 2]] = found->second;
                use(c->pCode[i + 3]);
            }
        }
        i += n;
    }
    result.known = true;
    return result;
}
BindingUse stageUse(Device &d, const VkPipelineShaderStageCreateInfo &stage) {
    auto found = d.shaderUse.find(stage.module);
    if (found != d.shaderUse.end())
        return found->second;
    for (auto *p = static_cast<const VkBaseInStructure *>(stage.pNext); p; p = p->pNext) {
        if (p->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO)
            return reflectBindings(reinterpret_cast<const VkShaderModuleCreateInfo *>(p));
        if (p->sType == VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT) {
            auto *id =
                reinterpret_cast<const VkPipelineShaderStageModuleIdentifierCreateInfoEXT *>(p);
            auto use =
                d.identifierUse.find({id->pIdentifier, id->pIdentifier + id->identifierSize});
            if (use != d.identifierUse.end())
                return use->second;
        }
    }
    return {};
}
void writeDescriptors(Bindings &destination, uint32_t count, const VkWriteDescriptorSet *writes) {
    for (uint32_t i = 0; i < count; ++i) {
        auto &w = writes[i];
        if (!w.pImageInfo)
            continue;
        if (w.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
            w.descriptorType != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE &&
            w.descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
            w.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER)
            continue;
        for (uint32_t n = 0; n < w.descriptorCount; ++n)
            destination[{w.dstBinding, w.dstArrayElement + n}] = {w.descriptorType,
                                                                  w.pImageInfo[n]};
    }
}
void templateDescriptors(Device &d, Bindings &destination, VkDescriptorUpdateTemplate update,
                         const void *data) {
    auto it = d.templates.find(update);
    if (it == d.templates.end())
        return;
    for (auto &entry : it->second.entries) {
        if (entry.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
            entry.descriptorType != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE &&
            entry.descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
            entry.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER)
            continue;
        for (uint32_t n = 0; n < entry.descriptorCount; ++n) {
            VkDescriptorImageInfo info{};
            memcpy(&info, static_cast<const char *>(data) + entry.offset + entry.stride * n,
                   sizeof(info));
            destination[{entry.dstBinding, entry.dstArrayElement + n}] = {entry.descriptorType,
                                                                          info};
        }
    }
}
void setupPack(Device &d) {
    if (d.packPipeline)
        return;
    VkDescriptorSetLayoutBinding b[5]{};
    for (uint32_t i = 0; i < 5; ++i) {
        b[i].binding = i;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        b[i].descriptorType = i == 0   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                              : i == 1 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                       : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 5;
    li.pBindings = b;
    check(d.fn<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(
              d.handle, &li, nullptr, &d.packLayout),
          "Pack descriptor layout");
    VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pi.setLayoutCount = 1;
    pi.pSetLayouts = &d.packLayout;
    check(d.fn<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(d.handle, &pi, nullptr,
                                                                     &d.packPipelineLayout),
          "Pack pipeline layout");
    auto createPackPipeline = [&](const uint32_t *words, size_t bytes, VkPipeline &pipeline) {
        VkShaderModule module{};
        VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        si.codeSize = bytes;
        si.pCode = words;
        check(
            d.fn<PFN_vkCreateShaderModule>("vkCreateShaderModule")(d.handle, &si, nullptr, &module),
            "Pack shader");
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.layout = d.packPipelineLayout;
        ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module = module;
        ci.stage.pName = "main";
        auto result = d.fn<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(
            d.handle, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline);
        d.fn<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(d.handle, module, nullptr);
        check(result, "Pack pipeline");
    };
    createPackPipeline(temporalSpv, sizeof(temporalSpv), d.packPipeline);
    createPackPipeline(temporalMsaaSpv, sizeof(temporalMsaaSpv), d.packMsaaPipeline);
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 0;
    check(d.fn<PFN_vkCreateSampler>("vkCreateSampler")(d.handle, &sampler, nullptr, &d.sampler),
          "Depth sampler");
}
VkPhysicalDevice applicationPhysical(Device &d) {
    if (d.applicationPhysical)
        return d.applicationPhysical;
    // vkCreateDevice's layer-chain physical handle is not necessarily the
    // application-visible loader wrapper. AMD's signed runtime also performs
    // global physical-device queries, so it needs the loader-visible handle.
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    auto path = std::filesystem::path(system) / L"vulkan-1.dll";
    static HMODULE loader = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    xpfsr::require(loader != nullptr, "System Vulkan loader unavailable");
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        GetProcAddress(loader, "vkEnumeratePhysicalDevices"));
    auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        GetProcAddress(loader, "vkGetPhysicalDeviceProperties"));
    VkPhysicalDeviceProperties wanted{};
    d.inst<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(d.physical, &wanted);
    uint32_t count{};
    check(enumerate(d.instance->handle, &count, nullptr), "Enumerate application physical devices");
    std::vector<VkPhysicalDevice> physicals(count);
    check(enumerate(d.instance->handle, &count, physicals.data()),
          "Enumerate application physical devices");
    for (auto physical : physicals) {
        VkPhysicalDeviceProperties p{};
        properties(physical, &p);
        if (p.vendorID == wanted.vendorID && p.deviceID == wanted.deviceID &&
            !memcmp(p.pipelineCacheUUID, wanted.pipelineCacheUUID, VK_UUID_SIZE)) {
            d.applicationPhysical = physical;
            log("Matched loader-visible physical device %s", p.deviceName);
            return physical;
        }
    }
    throw std::runtime_error("Cannot match the loader-visible physical device");
}

void ensureRuntime(Device &d) {
    if (d.runtime)
        return;
    wchar_t path[32768]{};
    if (!GetEnvironmentVariableW(L"XPFSR24_RUNTIME", path, 32768))
        throw std::runtime_error("AMD runtime path missing");
    d.runtime = std::make_unique<xpfsr::Runtime>();
    d.runtime->load(path, !d.fgRequested.empty());
}
void stopGenerator(Device &d) {
    SdkScope sdk;
    if (d.frameGenerator) {
        if (d.fgCurrent)
            d.frameGenerator->configure(d.fgCurrent->presentation->handle(), d.epoch + 1, false);
        // A view/depth convention change can recreate this context while old
        // generated frames still use it. Flush both SDK presents and GPU work.
        if (d.fgCurrent)
            d.fgCurrent->presentation->wait();
        check(d.fn<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(d.handle),
              "Flush FG before context destruction");
        d.frameGenerator.reset();
    }
    d.fgSequence = {};
    d.status.fgActive = 0;
}
void prepareGeneration(Device &d, VkCommandBuffer cb, Command &command, const xpfsr::Frame &frame,
                       uint32_t rw, uint32_t rh, uint32_t ow, uint32_t oh) {
    if (d.fgRequested.empty() || d.fgFailed || !d.fgCurrent || !d.fgLiveEnabled)
        return;
    if (!d.fgSequence.canPrepare()) {
        strncpy(d.status.fgReason,
                "FG skipped: previous source frame is still pending presentation", 159);
        if (++d.fgPendingSkips <= 3 || d.fgPendingSkips % 300 == 0)
            log("FG prepare guard: pending source=%llu; current source=%llu; keep native frame",
                (unsigned long long)d.fgSequence.prepared, (unsigned long long)frame.sequence);
        return;
    }
    auto target = d.fgCurrent;
    if (target->width != ow || target->height != oh) {
        strncpy(d.status.fgReason, "FG needs scene and presentation extents to match", 159);
        return;
    }
    std::array<uint32_t, 6> key = {rw, rh, ow, oh, d.camera.reverseZ, d.camera.infiniteZ};
    SdkScope sdk;
    if (!d.frameGenerator || key != d.fgKey) {
        try {
            stopGenerator(d);
            d.frameGenerator = std::make_unique<xpfsr::FrameGenerator>(d.runtime->api);
            d.frameGenerator->create({d.handle, applicationPhysical(d), d.next}, rw, rh, ow, oh,
                                     target->format, d.camera.reverseZ, d.camera.infiniteZ,
                                     d.fgRequested);
            d.fgKey = key;
            auto name = d.frameGenerator->provider();
            strncpy(d.status.fgProvider, name.c_str(), 63);
            log("Frame generator: %s, swapchain %s", name.c_str(),
                target->presentation->provider().c_str());
        } catch (const std::exception &e) {
            d.frameGenerator.reset();
            d.fgFailed = true;
            strncpy(d.status.fgReason, e.what(), 159);
            log("FG context unavailable: %s", e.what());
            return;
        }
    }
    xpfsr::CameraBasis camera{};
    memcpy(camera.position, d.camera.position, 12);
    memcpy(camera.up, d.camera.up, 12);
    memcpy(camera.right, d.camera.right, 12);
    memcpy(camera.forward, d.camera.forward, 12);
    // A synthetic fixture without an XPLM publisher still supplies a valid basis.
    if (camera.up[0] == 0 && camera.up[1] == 0 && camera.up[2] == 0)
        camera.up[1] = 1;
    if (camera.right[0] == 0 && camera.right[1] == 0 && camera.right[2] == 0)
        camera.right[0] = 1;
    if (camera.forward[0] == 0 && camera.forward[1] == 0 && camera.forward[2] == 0)
        camera.forward[2] = -1;
    d.frameGenerator->prepare(target->presentation->handle(), frame.sequence, frame, camera, true);
    command.fgFrame = d.fgSequence.prepared = frame.sequence;
    ++d.status.fgPrepared;
    if (!d.status.fgActive)
        strncpy(d.status.fgReason, "FG prepared; waiting for SDK presentation callback", 159);
}

struct alignas(16) PackUniform {
    float reprojection[16], inverseProjection[16], bodyReprojection[16], jitter[4], planes[4],
        sphere[4];
};
void setupFrame(Device &d, FrameResources &f, uint32_t rw, uint32_t rh, uint32_t ow, uint32_t oh,
                VkFormat colorFormat, VkFormat outputFormat, const ViewInfo &nativeDepth,
                std::shared_ptr<DepthCapture> depthOwner) {
    f.device = &d;
    f.sizes = {rw, rh, ow, oh};
    f.colorFormat = colorFormat;
    f.outputFormat = outputFormat;
    f.nativeDepth = nativeDepth.create.image;
    f.depthOwner = std::move(depthOwner);
    f.nativeDepthGeneration = f.depthOwner->generation;
    f.depthLayer = nativeDepth.create.subresourceRange.baseArrayLayer;
    f.color.create(d, rw, rh, colorFormat);
    f.depth.create(d, rw, rh, VK_FORMAT_R32_SFLOAT);
    f.motion.create(d, rw, rh, VK_FORMAT_R16G16_SFLOAT);
    f.reactive.create(d, rw, rh, VK_FORMAT_R8_UNORM);
    f.output.create(d, ow, oh, outputFormat);
    auto view = nativeDepth.create;
    view.pNext = nullptr;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view.subresourceRange.levelCount = view.subresourceRange.layerCount = 1;
    check(d.fn<PFN_vkCreateImageView>("vkCreateImageView")(d.handle, &view, nullptr, &f.depthView),
          "Depth sampling view");
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = sizeof(PackUniform);
    bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    check(d.fn<PFN_vkCreateBuffer>("vkCreateBuffer")(d.handle, &bi, nullptr, &f.uniform),
          "Frame constants");
    VkMemoryRequirements requirements{};
    d.fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(d.handle, f.uniform,
                                                                             &requirements);
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize = requirements.size;
    a.memoryTypeIndex =
        d.memoryType(requirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(d.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(d.handle, &a, nullptr, &f.uniformMemory),
          "Frame constant memory");
    check(
        d.fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(d.handle, f.uniform, f.uniformMemory, 0),
        "Bind frame constants");
    check(d.fn<PFN_vkMapMemory>("vkMapMemory")(d.handle, f.uniformMemory, 0, sizeof(PackUniform), 0,
                                               &f.mapped),
          "Map frame constants");
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1;
    pool.poolSizeCount = 3;
    pool.pPoolSizes = sizes;
    check(d.fn<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(d.handle, &pool, nullptr,
                                                                     &f.descriptorPool),
          "Frame descriptor pool");
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = f.descriptorPool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &d.packLayout;
    check(
        d.fn<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(d.handle, &allocate, &f.set),
        "Frame descriptor set");
    VkDescriptorImageInfo images[] = {
        {d.sampler, f.depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, f.depth.view, VK_IMAGE_LAYOUT_GENERAL},
        {VK_NULL_HANDLE, f.motion.view, VK_IMAGE_LAYOUT_GENERAL},
        {VK_NULL_HANDLE, f.reactive.view, VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo buffer{f.uniform, 0, sizeof(PackUniform)};
    VkWriteDescriptorSet writes[5]{};
    for (uint32_t i = 0; i < 5; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = f.set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 0   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                   : i == 1 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                            : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        if (i)
            writes[i].pImageInfo = &images[i - 1];
        else
            writes[i].pBufferInfo = &buffer;
    }
    d.fn<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(d.handle, 5, writes, 0, nullptr);
}
void restoreCockpit(Device &d, VkCommandBuffer cb, FrameResources &f, uint32_t rw, uint32_t rh,
                    uint32_t ow, uint32_t oh, float jitterX, float jitterY, float ySign) {
    d.status.cockpitActive = 0;
    d.status.cockpitScreens = 0;
    const char *reason = nullptr;
    if (!d.cockpitRequested)
        reason = "Cockpit restore Off";
    else if (d.cockpitFailed)
        reason = "Cockpit compositor failed; restart with restore Off";
    else if (!d.fgRequested.empty())
        reason = "Cockpit restore preview needs frame generation Off";
    else if (!d.camera.internal)
        reason = "Cockpit restore bypassed in external view";
    else if (d.camera.screenCount != 2)
        reason = "Waiting for default Cirrus SR22 PFD/MFD geometry";
    else if (!d.camera.screenReady)
        reason = "Waiting for stable camera projection";
    if (reason) {
        strncpy(d.status.cockpitReason, reason, 159);
        return;
    }
    try {
        if (!d.cockpitCompositor)
            d.cockpitCompositor = std::make_shared<cockpit::Compositor>(d.handle, d.next, d.memory);
        if (!f.cockpitFrame)
            f.cockpitFrame = std::make_unique<cockpit::Frame>(
                d.cockpitCompositor, f.color.view, f.depth.view, f.output.view, f.outputFormat);
        cockpit::Params params{};
        memcpy(params.clip, d.camera.screenClip, sizeof(params.clip));
        params.outputInfo[2] = 2;
        params.outputInfo[3] = d.cockpitTint ? 1.f : 0.f;
        params.jitter[0] = jitterX / rw;
        params.jitter[1] = jitterY / rh;
        params.jitter[2] = ySign;
        params.jitter[3] = 2.f * std::max(float(ow) / rw, float(oh) / rh);
        if (d.cockpitCompositor->record(*f.cockpitFrame, cb, params, ow, oh)) {
            ++d.status.cockpitRecordings;
            d.status.cockpitActive = 1;
            d.status.cockpitScreens = 2;
            strncpy(d.status.cockpitReason,
                    d.cockpitTint ? "SR22 screen restore recorded; alignment tint On"
                                  : "SR22 current-frame screen restore recorded; depth gated",
                    159);
            if (d.status.cockpitRecordings <= 3)
                log("Cockpit compositor: frame=%llu current-scene replay, 2 SR22 quads, ySign=%g "
                    "jitter=%g,%g",
                    (unsigned long long)d.camera.frame, ySign, jitterX, jitterY);
        } else
            strncpy(d.status.cockpitReason, "SR22 screens outside supported view", 159);
    } catch (const std::exception &e) {
        d.cockpitFailed = true;
        strncpy(d.status.cockpitReason, e.what(), 159);
        log("Cockpit compositor disabled: %s", e.what());
    }
}
bool replace(Device &d, VkCommandBuffer cb, Command &command) {
    auto pipeline = d.pipelines.find(command.pipeline);
    if (pipeline == d.pipelines.end() || !pipeline->second)
        return false;
    d.attach();
    app::Camera latest{};
    if (app::readCamera(d.shared, latest)) {
        bool was = d.enabled;
        d.camera = latest;
        d.enabled = InterlockedCompareExchange(&d.shared->enabled, 0, 0) != 0;
        if (!was && d.enabled)
            d.camera.reset = 1;
    }
    d.readControl();
    ++d.nativeCalls;
    if (d.nativeCalls <= 4 || d.nativeCalls % 1000 == 0)
        log("Native FSR boundary: call=%llu family=%u secondary=%d camera=%d enabled=%d",
            (unsigned long long)d.nativeCalls, command.family, command.secondary, d.camera.valid,
            d.enabled);
    if (d.mode == Mode::Observe) {
        if (d.nativeCalls <= 8 || d.nativeCalls % 500 == 0) {
            log("Observed boundary: frame=%llu kind=%u primary=%d oneTime=%d renderpass=%d "
                "dynamicMismatch=%d sets=%zu depths=%zu viewport=%gx%g",
                (unsigned long long)d.camera.frame, pipeline->second, !command.secondary,
                command.oneTime, command.rendering, command.dynamicOffsets, command.sets.size(),
                command.depths.size(), command.viewport.width, command.viewport.height);
            Bindings observed;
            auto push = command.pushed.find(0);
            if (push != command.pushed.end())
                observed = push->second;
            else if (command.sets.count(0))
                observed = d.descriptors[command.sets[0]];
            for (auto &[binding, descriptor] : observed) {
                auto view = d.views.find(descriptor.image.imageView);
                if (view == d.views.end())
                    continue;
                auto image = d.images.find(view->second.create.image);
                if (image == d.images.end())
                    continue;
                auto &c = image->second.create;
                log("  binding=%u image=%p size=%ux%u format=%u samples=%u usage=%x layout=%u",
                    binding.first, reinterpret_cast<void *>(view->second.create.image),
                    c.extent.width, c.extent.height, c.format, c.samples, c.usage,
                    descriptor.image.imageLayout);
            }
            for (auto &[image, depth] : command.depths) {
                auto found = d.images.find(image);
                if (found != d.images.end()) {
                    log("  depth=%p size=%ux%u format=%u usage=%x epoch=%llu layout=%u",
                        reinterpret_cast<void *>(image), found->second.create.extent.width,
                        found->second.create.extent.height, found->second.create.format,
                        found->second.create.usage, (unsigned long long)depth.epoch, depth.layout);
                    for (auto color : depth.colors)
                        log("    attached color=%p", reinterpret_cast<void *>(color));
                }
            }
        }
        d.bypass("Diagnostics only; GPU commands and device features unchanged");
        return false;
    }
    if (!d.enabled || !d.shared || !d.camera.valid || d.runtimeFailed) {
        d.bypass(!d.camera.valid ? "Waiting for valid camera metadata"
                                 : "Disabled; native upscaling active");
        return false;
    }
    if (d.groundCameraActive()) {
        strncpy(d.status.fgReason, "FG paused by ground camera safety; selection retained", 159);
        d.bypass("Ground camera safety: native rendering near terrain; raise camera above 2.5m or "
                 "take off");
        return false;
    }
    // Later native copy/sharpen passes belong to this already reconstructed
    // frame. Leave them native without disabling next frame's scene jitter.
    if (d.camera.frame == d.lastRecordedCamera)
        return false;
    if (command.secondary || !command.oneTime || command.rendering || command.processed ||
        command.dynamicOffsets || command.sets.size() > 1 || command.family != d.graphicsFamily) {
        d.bypass(command.family != d.graphicsFamily
                     ? "Native upscaling is on a different queue family"
                     : "Native FSR command state is unsupported");
        return false;
    }
    Bindings bindings;
    auto pushed = command.pushed.find(0);
    if (pushed != command.pushed.end())
        bindings = pushed->second;
    else {
        auto set = command.sets.find(0);
        if (set != command.sets.end())
            bindings = d.descriptors[set->second];
    }
    auto input = bindings.find({1, 0}), output = bindings.find({2, 0});
    if (input == bindings.end() || output == bindings.end()) {
        d.bypass("FSR descriptors unavailable; native rendering active");
        return false;
    }
    auto inView = d.views.find(input->second.image.imageView),
         outView = d.views.find(output->second.image.imageView);
    if (inView == d.views.end() || outView == d.views.end())
        return false;
    auto inImage = d.images.find(inView->second.create.image),
         outImage = d.images.find(outView->second.create.image);
    if (inImage == d.images.end() || outImage == d.images.end())
        return false;
    auto ic = inImage->second.create, oc = outImage->second.create;
    uint32_t rw = std::max(1u,
                           ic.extent.width >> inView->second.create.subresourceRange.baseMipLevel),
             rh = std::max(1u,
                           ic.extent.height >> inView->second.create.subresourceRange.baseMipLevel);
    uint32_t ow = std::max(1u,
                           oc.extent.width >> outView->second.create.subresourceRange.baseMipLevel),
             oh = std::max(1u, oc.extent.height >>
                                   outView->second.create.subresourceRange.baseMipLevel);
    if (d.mode == Mode::NativeAA) {
        if (rw != ow || rh != oh) {
            d.bypass("Native AA needs a full-resolution scene boundary; native rendering active");
            return false;
        }
    } else if (rw >= ow || rh >= oh) {
        if (!d.status.recordings)
            d.bypass("Choose a rendering resolution below Full in X-Plane");
        return false;
    }
    if (rw < 64 || rh < 64 || ow > 4096 || oh > 4096 || uint64_t(ow) * oh > 16777216) {
        d.bypass("Resolution outside this alpha's capture limit");
        return false;
    }
    auto singleLayer = [](const VkImageViewCreateInfo &view, const VkImageCreateInfo &image) {
        auto &r = view.subresourceRange;
        return r.baseArrayLayer < image.arrayLayers &&
               (r.layerCount == 1 || (r.layerCount == VK_REMAINING_ARRAY_LAYERS &&
                                      image.arrayLayers - r.baseArrayLayer == 1));
    };
    constexpr VkImageCreateFlags unsupported =
        VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT |
        VK_IMAGE_CREATE_SPARSE_ALIASED_BIT | VK_IMAGE_CREATE_PROTECTED_BIT |
        VK_IMAGE_CREATE_DISJOINT_BIT | VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT;
    if ((ic.flags & unsupported) || (oc.flags & unsupported) || ic.imageType != VK_IMAGE_TYPE_2D ||
        oc.imageType != VK_IMAGE_TYPE_2D || ic.arrayLayers != 1 || oc.arrayLayers != 1 ||
        !singleLayer(inView->second.create, ic) || !singleLayer(outView->second.create, oc) ||
        inView->second.create.format != ic.format || outView->second.create.format != oc.format) {
        if (d.nativeCalls < 8)
            log("Rejected image/view: flags=%x/%x layers=%u/%u viewLayers=%u/%u format=%u/%u "
                "viewFormat=%u/%u",
                ic.flags, oc.flags, ic.arrayLayers, oc.arrayLayers,
                inView->second.create.subresourceRange.layerCount,
                outView->second.create.subresourceRange.layerCount, ic.format, oc.format,
                inView->second.create.format, outView->second.create.format);
        d.bypass("Image or view shape outside this alpha's capture contract");
        return false;
    }
    if (ic.samples != VK_SAMPLE_COUNT_1_BIT || oc.samples != VK_SAMPLE_COUNT_1_BIT ||
        !(ic.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
        !(oc.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        input->second.image.imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        output->second.image.imageLayout != VK_IMAGE_LAYOUT_GENERAL) {
        d.bypass("Native FSR images do not meet capture requirements");
        return false;
    }
    if (oc.format != VK_FORMAT_R16G16B16A16_SFLOAT && oc.format != VK_FORMAT_R32G32B32A32_SFLOAT) {
        d.bypass("Native output format not supported by this alpha");
        return false;
    }
    pollProbes(d);
    std::vector<Depth> candidates;
    bool mismatchedScene = false;
    uint64_t rejectedDepthFrame{};
    auto &depthMap = command.depths; // Never infer execution order from another command buffer.
    for (auto &[image, depth] : depthMap) {
        auto found = d.images.find(image);
        if (found == d.images.end())
            continue;
        const bool belongs = depth.fullArea && found->second.create.extent.width == rw &&
                             found->second.create.extent.height == rh &&
                             (depth.colors.count(inView->second.create.image) ||
                              command.depthOrigins[inView->second.create.image].count(image));
        if (belongs && (!depth.cameraCoherent || depth.epoch != d.camera.frame)) {
            mismatchedScene = true;
            rejectedDepthFrame = depth.epoch;
        }
        if (!depth.cameraCoherent || depth.epoch != d.camera.frame)
            continue;
        auto &c = found->second.create;
        if (command.capturedDepths.count(image) &&
            command.capturedDepths.at(image)->frame == d.camera.frame && depth.fullArea &&
            (depth.colors.count(inView->second.create.image) ||
             command.depthOrigins[inView->second.create.image].count(image)) &&
            c.extent.width == rw && c.extent.height == rh)
            candidates.push_back(depth);
    }
    // Follow the scene's actual render order, then disprove clear/invalid
    // candidates with a separately fenced GPU content readback. This follows
    // the community mod's last-full-viewport approach, without pointer scoring.
    const size_t depthCandidateCount = candidates.size();
    std::sort(candidates.begin(), candidates.end(),
              [](const Depth &a, const Depth &b) { return a.order > b.order; });
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [&](const Depth &depth) {
                                        auto *probe = findProbe(d, depth.image);
                                        return probe && probe->phase == 2 && !probe->geometry;
                                    }),
                     candidates.end());
    if (candidates.size() > 1)
        candidates.resize(1);
    if (candidates.size() != 1) {
        if (mismatchedScene) {
            if (++d.sceneFrameRejects <= 4 || d.sceneFrameRejects % 300 == 0)
                log("Scene camera mismatch: current=%llu recorded-depth=%llu rejected=%llu; native "
                    "frame retained",
                    (unsigned long long)d.camera.frame, (unsigned long long)rejectedDepthFrame,
                    (unsigned long long)d.sceneFrameRejects);
            d.bypass("Scene camera frame mismatch; native frame retained");
            return false;
        }
        if (d.nativeCalls < 8) {
            log("Depth provenance: input=%p candidates=%zu tracked=%zu",
                reinterpret_cast<void *>(inView->second.create.image), candidates.size(),
                command.depths.size());
            for (auto &[image, value] : command.depths) {
                auto meta = d.images.find(image);
                if (meta != d.images.end())
                    log(" depth=%p format=%u samples=%u usage=%x colors=%zu",
                        reinterpret_cast<void *>(image), meta->second.create.format,
                        meta->second.create.samples, meta->second.create.usage,
                        value.colors.size());
                for (auto color : value.colors)
                    log(" attached color=%p", reinterpret_cast<void *>(color));
            }
        }
        bool multisampleDepth =
            std::any_of(depthMap.begin(), depthMap.end(), [&](const auto &entry) {
                auto image = d.images.find(entry.first);
                return image != d.images.end() &&
                       image->second.create.samples != VK_SAMPLE_COUNT_1_BIT;
            });
        d.bypass(candidates.empty() ? multisampleDepth
                                          ? "MSAA depth/resolve capture unavailable; try MSAA Off"
                                          : "Waiting for scene depth matching the FSR input"
                                    : "Scene depth is ambiguous; native rendering active");
        return false;
    }
    Depth depth = candidates.front();
    auto depthView = d.views.find(depth.view);
    if (depthView == d.views.end())
        return false;
    auto captured = command.capturedDepths.at(depth.image);
    ViewInfo capturedView{captured->view};
    VkImageLayout depthLayout = captured->layout;
    if (depthLayout == VK_IMAGE_LAYOUT_UNDEFINED || depthLayout == VK_IMAGE_LAYOUT_PREINITIALIZED) {
        d.bypass("Scene depth layout is unknown");
        return false;
    }
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    if (!frequency.QuadPart || double(now.QuadPart - d.camera.ticks) / frequency.QuadPart > 0.25) {
        d.pathReady = false;
        d.bypass("Camera metadata is stale; native rendering active");
        return false;
    }
    bool recordingStarted = false;
    try {
        if (!d.runtime) {
            if (d.runtimeFailed)
                return false;
            wchar_t path[32768]{};
            if (!GetEnvironmentVariableW(L"XPFSR24_RUNTIME", path, 32768))
                throw std::runtime_error("AMD runtime path is missing");
            d.runtime = std::make_unique<xpfsr::Runtime>();
            d.runtime->load(path, !d.fgRequested.empty());
        }
        setupPack(d);
        std::array<uint32_t, 6> configKey = {rw, rh, ow, oh, d.camera.reverseZ, d.camera.infiniteZ};
        auto &context = d.contexts[configKey];
        if (!context) {
            if (d.contexts.size() > 2)
                throw std::runtime_error("Resize limit reached; restart the simulator");
            xpfsr::Config config{rw, rh, ow, oh};
            config.invertedDepth = d.camera.reverseZ;
            config.infiniteDepth = d.camera.infiniteZ;
            VkDeviceContext backend{d.handle, applicationPhysical(d), d.next};
            wchar_t requested[32]{};
            GetEnvironmentVariableW(L"XPFSR24_PROVIDER", requested, 32);
            std::string version = !wcscmp(requested, L"4.1.2") ? "4.1.2" : "3.1.6";
            config.providerId = xpfsr::providerVersion(d.runtime->api, backend, version);
            context = std::make_unique<xpfsr::Context>(d.runtime->api);
            context->create(config, backend);
            auto name = context->provider();
            strncpy(d.status.provider, name.c_str(), sizeof(d.status.provider) - 1);
            log("FSR provider: %s (%ux%u -> %ux%u)", name.c_str(), rw, rh, ow, oh);
        }
        if (!command.resources ||
            command.resources->sizes != std::array<uint32_t, 4>{rw, rh, ow, oh} ||
            command.resources->colorFormat != ic.format ||
            command.resources->outputFormat != oc.format ||
            command.resources->nativeDepth != captured->image ||
            command.resources->nativeDepthGeneration != d.images.at(depth.image).generation ||
            command.resources->depthLayer != 0) {
            command.resources.reset();
            command.resources = std::make_unique<FrameResources>();
            setupFrame(d, *command.resources, rw, rh, ow, oh, ic.format, oc.format, capturedView,
                       captured);
        }
        auto &f = *command.resources;
        PackUniform uniform{};
        memcpy(uniform.reprojection, d.camera.reprojection, 64);
        memcpy(uniform.inverseProjection, d.camera.inverseProjection, 64);
        memcpy(uniform.bodyReprojection, d.camera.bodyReprojection, 64);
        memcpy(uniform.sphere, d.camera.ownSphere, 16);
        uniform.jitter[0] = depth.jitterX;
        uniform.jitter[1] = depth.jitterY;
        uniform.jitter[2] = depth.ySign;
        uniform.jitter[3] = float(d.camera.internal);
        uniform.planes[0] = float(d.camera.reverseZ);
        uniform.planes[1] = float(d.camera.bodyValid);
        uniform.planes[2] = d.reduceGhosting ? 1.f : 0.f;
        memcpy(f.mapped, &uniform, sizeof(uniform));
        recordingStarted = true;
        // Global dependency also orders the shared FSR history against prior work on this queue.
        VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        memory.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        memory.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        d.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
            cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
            &memory, 0, nullptr, 0, nullptr);
        auto inputRange = inView->second.create.subresourceRange;
        inputRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        inputRange.levelCount = inputRange.layerCount = 1;
        auto depthRange = capturedView.create.subresourceRange;
        depthRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (!d.separateDepthStencil &&
            (capturedView.create.format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
             capturedView.create.format == VK_FORMAT_D24_UNORM_S8_UINT ||
             capturedView.create.format == VK_FORMAT_D16_UNORM_S8_UINT))
            depthRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        depthRange.levelCount = depthRange.layerCount = 1;
        barrier(d, cb, inView->second.create.image, input->second.image.imageLayout,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, inputRange);
        transition(d, cb, f.color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageCopy copy{};
        copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, inputRange.baseMipLevel,
                               inputRange.baseArrayLayer, 1};
        copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.extent = {rw, rh, 1};
        d.fn<PFN_vkCmdCopyImage>("vkCmdCopyImage")(
            cb, inView->second.create.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            f.color.value.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier(d, cb, inView->second.create.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                input->second.image.imageLayout, inputRange);
        transition(d, cb, f.color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        barrier(d, cb, captured->image, depthLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                depthRange);
        for (auto *target : {&f.depth, &f.motion, &f.reactive})
            transition(d, cb, *target, VK_IMAGE_LAYOUT_GENERAL);
        auto bindPipeline = d.fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline");
        auto bindSets = d.fn<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets");
        bindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                     d.images.at(depth.image).create.samples == VK_SAMPLE_COUNT_1_BIT
                         ? d.packPipeline
                         : d.packMsaaPipeline);
        bindSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, d.packPipelineLayout, 0, 1, &f.set, 0,
                 nullptr);
        d.fn<PFN_vkCmdDispatch>("vkCmdDispatch")(cb, (rw + 7) / 8, (rh + 7) / 8, 1);
        for (auto *target : {&f.depth, &f.motion, &f.reactive})
            transition(d, cb, *target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        barrier(d, cb, captured->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, depthLayout,
                depthRange);
        transition(d, cb, f.output, VK_IMAGE_LAYOUT_GENERAL);
        xpfsr::Frame frame{};
        frame.commands = cb;
        frame.color = f.color.value;
        frame.depth = f.depth.value;
        frame.motion = f.motion.value;
        frame.reactive = f.reactive.value;
        if (d.reduceGhosting)
            frame.composition = f.reactive.value;
        frame.output = f.output.value;
        frame.jitterX = depth.jitterX;
        frame.jitterY = depth.jitterY;
        frame.motionScaleX = frame.motionScaleY = 1;
        frame.deltaMilliseconds = d.camera.milliseconds;
        frame.nearPlane = d.camera.nearPlane;
        frame.farPlane = d.camera.farPlane;
        frame.verticalFovRadians = d.camera.verticalFov;
        frame.sequence = d.camera.frame;
        frame.reset = d.camera.reset || !d.pathReady || d.lastDepth != depth.image ||
                      d.lastDepthGeneration != d.images.at(depth.image).generation;
        if (!findProbe(d, depth.image)) {
            if (d.probes.size() >= 16)
                throw std::runtime_error("Depth probe resize limit reached; restart X-Plane");
            auto &probe = createProbe(d, depth.image);
            probe.ambiguous = depthCandidateCount > 1;
            transition(d, cb, f.depth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy regions[256]{};
            for (uint32_t y = 0; y < 16; ++y)
                for (uint32_t x = 0; x < 16; ++x) {
                    auto &r = regions[y * 16 + x];
                    r.bufferOffset = (y * 16 + x) * 4;
                    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    r.imageOffset = {int32_t((2 * x + 1) * rw / 32), int32_t((2 * y + 1) * rh / 32),
                                     0};
                    r.imageExtent = {1, 1, 1};
                }
            d.fn<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(
                cb, f.depth.value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, probe.buffer, 256,
                regions);
            VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            host.buffer = probe.buffer;
            host.size = 1024;
            d.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
                cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                &host, 0, nullptr);
            transition(d, cb, f.depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            command.probes.push_back(&probe);
        }
        context->record(frame);
        restoreCockpit(d, cb, f, rw, rh, ow, oh, depth.jitterX, depth.jitterY, depth.ySign);
        prepareGeneration(d, cb, command, frame, rw, rh, ow, oh);
        transition(d, cb, f.output, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        auto outputRange = outView->second.create.subresourceRange;
        outputRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        outputRange.levelCount = outputRange.layerCount = 1;
        barrier(d, cb, outView->second.create.image, VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, outputRange);
        copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, outputRange.baseMipLevel,
                               outputRange.baseArrayLayer, 1};
        copy.extent = {ow, oh, 1};
        d.fn<PFN_vkCmdCopyImage>("vkCmdCopyImage")(
            cb, f.output.value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            outView->second.create.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier(d, cb, outView->second.create.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL, outputRange);
        bindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, command.pipeline);
        // Native FSR uses set 0. Rebind only the tracked original set; subsequent app
        // dispatches must rebind their own resources normally after this boundary.
        if (command.sets.count(0) && command.layout) {
            auto &values = command.offsets[0];
            bindSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, command.layout, 0, 1, &command.sets[0],
                     uint32_t(values.size()), values.data());
        }
        if (command.pushed.count(0) && command.layout) {
            std::vector<VkWriteDescriptorSet> writes;
            std::vector<VkDescriptorImageInfo> infos;
            auto &saved = command.pushed[0];
            infos.reserve(saved.size());
            writes.reserve(saved.size());
            for (auto &[binding, value] : saved) {
                infos.push_back(value.image);
                VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                w.dstBinding = binding.first;
                w.dstArrayElement = binding.second;
                w.descriptorCount = 1;
                w.descriptorType = value.type;
                w.pImageInfo = &infos.back();
                writes.push_back(w);
            }
            auto push = d.fn<PFN_vkCmdPushDescriptorSetKHR>("vkCmdPushDescriptorSetKHR");
            if (!push)
                push = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(
                    d.next(d.handle, "vkCmdPushDescriptorSet"));
            if (push)
                push(cb, VK_PIPELINE_BIND_POINT_COMPUTE, command.layout, 0, uint32_t(writes.size()),
                     writes.data());
        }
        for (auto &saved : command.constants)
            if (saved.stages & VK_SHADER_STAGE_COMPUTE_BIT)
                d.fn<PFN_vkCmdPushConstants>("vkCmdPushConstants")(
                    cb, saved.layout, saved.stages, saved.offset, uint32_t(saved.data.size()),
                    saved.data.data());
        command.processed = true;
        command.sceneFrame = d.camera.frame;
        d.pathReady = true;
        if (d.activeContext != configKey)
            d.jitterFrame = 0;
        d.activeContext = configKey;
        d.lastRecordedCamera = d.camera.frame;
        d.lastDepth = depth.image;
        d.lastDepthGeneration = d.images.at(depth.image).generation;
        d.status.active = 1;
        d.status.renderWidth = rw;
        d.status.renderHeight = rh;
        d.status.outputWidth = ow;
        d.status.outputHeight = oh;
        ++d.status.recordings;
        if (d.status.recordings <= 4 || frame.reset || d.status.recordings % 1000 == 0)
            log("Recorded frame=%llu depth=%p jitter=(%g,%g) trackedStaticViewports=%zu "
                "candidates=%zu viewports=%llu secondary=%llu multi=%llu body=%u internal=%u "
                "reset=%u near=%g far=%g reverse=%u infinite=%u camera_agl=%g aircraft_ground=%g "
                "ground_metadata=%u",
                (unsigned long long)d.camera.frame, reinterpret_cast<void *>(depth.image),
                depth.jitterX, depth.jitterY, d.graphicsViewports.size(), depthCandidateCount,
                (unsigned long long)d.viewportCalls, (unsigned long long)d.secondaryViewportCalls,
                (unsigned long long)d.multiViewportCalls, d.camera.bodyValid, d.camera.internal,
                frame.reset, d.camera.nearPlane, d.camera.farPlane, d.camera.reverseZ,
                d.camera.infiniteZ, d.camera.ownSphere[0], d.camera.ownSphere[1],
                d.camera.ownSphere[3] == app::groundCameraMarker);
        d.publish(d.mode == Mode::NativeAA
                      ? "Native AA recorded; experimental camera-derived motion"
                      : "Temporal upscaling recorded; experimental camera-derived motion");
        return true;
    } catch (const std::exception &e) {
        d.runtimeFailed = true;
        d.pathReady = false;
        d.status.active = 0;
        d.publish(e.what());
        log("FSR disabled: %s", e.what());
        // Prevent submission of a partially recorded SDK dispatch. EndCommandBuffer
        // reports a failure to the host instead of silently submitting corrupt work.
        if (recordingStarted) {
            command.failed = true;
            return true;
        }
        return false;
    }
}
} // namespace

extern "C" __declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
XP_GetInstanceProcAddr(VkInstance, const char *);
extern "C" __declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
XP_GetDeviceProcAddr(VkDevice, const char *);
static VKAPI_ATTR VkResult VKAPI_CALL L_CreateInstance(const VkInstanceCreateInfo *ci,
                                                       const VkAllocationCallbacks *allocator,
                                                       VkInstance *out) {
    auto *link = reinterpret_cast<VkLayerInstanceCreateInfo *>(const_cast<void *>(ci->pNext));
    while (link && (link->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
                    link->function != VK_LAYER_LINK_INFO))
        link = reinterpret_cast<VkLayerInstanceCreateInfo *>(const_cast<void *>(link->pNext));
    if (!link || !link->u.pLayerInfo)
        return VK_ERROR_INITIALIZATION_FAILED;
    auto next = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    auto create = reinterpret_cast<PFN_vkCreateInstance>(next(VK_NULL_HANDLE, "vkCreateInstance"));
    auto result = create(ci, allocator, out);
    if (result == VK_SUCCESS) {
        auto instance = std::make_shared<Instance>();
        instance->handle = *out;
        instance->next = next;
        std::lock_guard<std::mutex> lock(registryLock);
        instances[key(*out)] = instance;
        log("Instance created; explicit FSR layer active");
        if (ci->pApplicationInfo) {
            auto &a = *ci->pApplicationInfo;
            log("Host application=%s app_version=%u engine=%s engine_version=%u vulkan_api=%u",
                a.pApplicationName ? a.pApplicationName : "unknown", a.applicationVersion,
                a.pEngineName ? a.pEngineName : "unknown", a.engineVersion, a.apiVersion);
        }
    }
    return result;
}
static VKAPI_ATTR void VKAPI_CALL L_DestroyInstance(VkInstance instance,
                                                    const VkAllocationCallbacks *allocator) {
    auto state = instanceFor(instance);
    if (!state)
        return;
    auto fn = reinterpret_cast<PFN_vkDestroyInstance>(state->next(instance, "vkDestroyInstance"));
    {
        std::lock_guard<std::mutex> l(registryLock);
        instances.erase(key(instance));
    }
    fn(instance, allocator);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_CreateDevice(VkPhysicalDevice physical,
                                                     const VkDeviceCreateInfo *ci,
                                                     const VkAllocationCallbacks *allocator,
                                                     VkDevice *out) {
    std::shared_ptr<Instance> instance;
    {
        std::lock_guard<std::mutex> l(registryLock);
        auto it = instances.find(key(physical));
        if (it != instances.end())
            instance = it->second;
        else if (instances.size() == 1)
            instance = instances.begin()->second;
    }
    if (!instance)
        return VK_ERROR_INITIALIZATION_FAILED;
    auto *link = reinterpret_cast<VkLayerDeviceCreateInfo *>(const_cast<void *>(ci->pNext));
    while (link && (link->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
                    link->function != VK_LAYER_LINK_INFO))
        link = reinterpret_cast<VkLayerDeviceCreateInfo *>(const_cast<void *>(link->pNext));
    if (!link || !link->u.pLayerInfo)
        return VK_ERROR_INITIALIZATION_FAILED;
    auto nextInstance = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    auto next = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    auto create =
        reinterpret_cast<PFN_vkCreateDevice>(nextInstance(instance->handle, "vkCreateDevice"));
    std::vector<std::unique_ptr<unsigned char[]>> nodes;
    VkBaseOutStructure *first = nullptr;
    VkBaseOutStructure **tail = &first;
    bool cloned = selectedMode() != Mode::Observe;
    if (cloned)
        for (auto *node = static_cast<const VkBaseInStructure *>(ci->pNext); node;
             node = node->pNext) {
            size_t bytes = node->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO
                               ? sizeof(VkLayerDeviceCreateInfo)
                               : featureStructureSize(node->sType);
            if (!bytes) {
                cloned = false;
                log("Unknown device pNext type %d; preserve original feature chain",
                    int(node->sType));
                break;
            }
            auto buffer = std::make_unique<unsigned char[]>(bytes);
            memcpy(buffer.get(), node, bytes);
            auto *copy = reinterpret_cast<VkBaseOutStructure *>(buffer.get());
            copy->pNext = nullptr;
            *tail = copy;
            tail = &copy->pNext;
            nodes.push_back(std::move(buffer));
        }
    std::vector<const char *> extensions(ci->ppEnabledExtensionNames,
                                         ci->ppEnabledExtensionNames + ci->enabledExtensionCount);
    auto enumerate = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        instance->next(instance->handle, "vkEnumerateDeviceExtensionProperties"));
    uint32_t count = 0;
    enumerate(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    enumerate(physical, nullptr, &count, available.data());
    auto add = [&](const char *name) {
        bool supported = false;
        for (auto &p : available)
            if (!strcmp(name, p.extensionName))
                supported = true;
        if (!supported)
            return false;
        for (auto *p : extensions)
            if (!strcmp(p, name))
                return true;
        extensions.push_back(name);
        return true;
    };
    VkPhysicalDeviceFeatures2 support{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceShaderFloat16Int8Features floats{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR coop{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};
    VkPhysicalDeviceShaderFloat8FeaturesEXT fp8{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT};
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivatives{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
    support.pNext = &floats;
    floats.pNext = &coop;
    coop.pNext = &fp8;
    fp8.pNext = &derivatives;
    auto getFeatures = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        instance->next(instance->handle, "vkGetPhysicalDeviceFeatures2"));
    getFeatures(physical, &support);
    VkPhysicalDeviceShaderFloat16Int8Features requestFloats{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR requestCoop{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};
    VkPhysicalDeviceShaderFloat8FeaturesEXT requestFp8{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT};
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR requestDerivatives{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
    bool foundDerivatives = false;
    VkPhysicalDeviceFeatures2 timelineSupport{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    timelineSupport.pNext = &timeline;
    getFeatures(physical, &timelineSupport);
    VkPhysicalDeviceTimelineSemaphoreFeatures requestTimeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    bool foundTimeline = false;
    bool foundFloats = false, foundCoop = false, foundFp8 = false;
    if (cloned) {
        for (auto *node = first; node; node = node->pNext) {
            if (node->sType ==
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR) {
                auto *f =
                    reinterpret_cast<VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR *>(node);
                f->computeDerivativeGroupLinear |= derivatives.computeDerivativeGroupLinear;
                f->computeDerivativeGroupQuads |= derivatives.computeDerivativeGroupQuads;
                foundDerivatives = true;
            }
            if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {
                auto *f = reinterpret_cast<VkPhysicalDeviceVulkan12Features *>(node);
                f->shaderFloat16 |= floats.shaderFloat16;
                f->shaderInt8 |= floats.shaderInt8;
                if (!requestedFrameGeneration().empty())
                    f->timelineSemaphore |= timeline.timelineSemaphore;
                foundTimeline = true;
                foundFloats = true;
            }
            if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES) {
                auto *f = reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreFeatures *>(node);
                if (!requestedFrameGeneration().empty())
                    f->timelineSemaphore |= timeline.timelineSemaphore;
                foundTimeline = true;
            }
            if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES) {
                auto *f = reinterpret_cast<VkPhysicalDeviceShaderFloat16Int8Features *>(node);
                f->shaderFloat16 |= floats.shaderFloat16;
                f->shaderInt8 |= floats.shaderInt8;
                foundFloats = true;
            }
            if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR) {
                auto *f = reinterpret_cast<VkPhysicalDeviceCooperativeMatrixFeaturesKHR *>(node);
                f->cooperativeMatrix |= coop.cooperativeMatrix;
                foundCoop = true;
            }
            if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT) {
                auto *f = reinterpret_cast<VkPhysicalDeviceShaderFloat8FeaturesEXT *>(node);
                f->shaderFloat8 |= fp8.shaderFloat8;
                f->shaderFloat8CooperativeMatrix |= fp8.shaderFloat8CooperativeMatrix;
                foundFp8 = true;
            }
        }
        auto append = [&](auto &value) {
            auto *node = reinterpret_cast<VkBaseOutStructure *>(&value);
            node->pNext = nullptr;
            *tail = node;
            tail = &node->pNext;
        };
        if ((derivatives.computeDerivativeGroupLinear || derivatives.computeDerivativeGroupQuads) &&
            add(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME) && !foundDerivatives) {
            requestDerivatives.computeDerivativeGroupLinear =
                derivatives.computeDerivativeGroupLinear;
            requestDerivatives.computeDerivativeGroupQuads =
                derivatives.computeDerivativeGroupQuads;
            append(requestDerivatives);
        }
        if (!requestedFrameGeneration().empty()) {
            add(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
            if (!foundTimeline) {
                requestTimeline.timelineSemaphore = timeline.timelineSemaphore;
                append(requestTimeline);
            }
        }
        if (!foundFloats) {
            requestFloats.shaderFloat16 = floats.shaderFloat16;
            requestFloats.shaderInt8 = floats.shaderInt8;
            append(requestFloats);
        }
        if (coop.cooperativeMatrix && add(VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME) && !foundCoop) {
            requestCoop.cooperativeMatrix = VK_TRUE;
            append(requestCoop);
        }
        if (fp8.shaderFloat8 && add(VK_EXT_SHADER_FLOAT8_EXTENSION_NAME) && !foundFp8) {
            requestFp8.shaderFloat8 = VK_TRUE;
            requestFp8.shaderFloat8CooperativeMatrix = fp8.shaderFloat8CooperativeMatrix;
            append(requestFp8);
        }
    }
    VkDeviceCreateInfo modified = *ci;
    if (cloned) {
        modified.pNext = first;
        modified.enabledExtensionCount = uint32_t(extensions.size());
        modified.ppEnabledExtensionNames = extensions.data();
    }
    uint32_t fgBase = UINT32_MAX, fgFamily = UINT32_MAX;
    std::vector<VkDeviceQueueCreateInfo> queueInfos(
        ci->pQueueCreateInfos, ci->pQueueCreateInfos + ci->queueCreateInfoCount);
    std::vector<std::vector<float>> priorities(queueInfos.size());
    if (!requestedFrameGeneration().empty() && cloned) {
        uint32_t familiesCount = 0;
        auto queueFn = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            instance->next(instance->handle, "vkGetPhysicalDeviceQueueFamilyProperties"));
        queueFn(physical, &familiesCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familiesCount);
        queueFn(physical, &familiesCount, families.data());
        for (size_t i = 0; i < queueInfos.size(); ++i) {
            auto &q = queueInfos[i];
            if (q.flags || q.queueCount != 1 || q.queueFamilyIndex >= families.size() ||
                (families[q.queueFamilyIndex].queueFlags &
                 (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) !=
                    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT) ||
                families[q.queueFamilyIndex].queueCount < q.queueCount + 3)
                continue;
            fgBase = q.queueCount;
            fgFamily = q.queueFamilyIndex;
            priorities[i].assign(q.pQueuePriorities, q.pQueuePriorities + q.queueCount);
            priorities[i].resize(q.queueCount + 3, 1.f);
            q.queueCount += 3;
            q.pQueuePriorities = priorities[i].data();
            break;
        }
        if (fgBase != UINT32_MAX) {
            modified.queueCreateInfoCount = uint32_t(queueInfos.size());
            modified.pQueueCreateInfos = queueInfos.data();
        }
    }
    VkResult result = create(physical, &modified, allocator, out);
    if (result != VK_SUCCESS) {
        log("Device creation failed: %d", int(result));
        return result;
    }
    PFN_vkSetDeviceLoaderData setLoaderData = nullptr;
    for (auto *node = static_cast<const VkBaseInStructure *>(ci->pNext); node; node = node->pNext)
        if (node->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) {
            auto *data = reinterpret_cast<const VkLayerDeviceCreateInfo *>(node);
            if (data->function == VK_LOADER_DATA_CALLBACK)
                setLoaderData = data->u.pfnSetDeviceLoaderData;
        }
    auto state = std::make_shared<Device>();
    state->handle = *out;
    state->setLoaderData = setLoaderData;
    state->physical = physical;
    state->next = next;
    state->instance = instance;
    state->featuresReady = cloned;
    state->mode = selectedMode();
    wchar_t cockpitSetting[8]{};
    if (GetEnvironmentVariableW(L"XPFSR24_COCKPIT_RESTORE", cockpitSetting, 8)) {
        state->cockpitRequested = wcscmp(cockpitSetting, L"0") != 0;
        state->cockpitTint = !wcscmp(cockpitSetting, L"2");
    }
    wchar_t ghostSetting[8]{};
    if (GetEnvironmentVariableW(L"XPFSR24_GHOST_REDUCTION", ghostSetting, 8))
        state->reduceGhosting = wcscmp(ghostSetting, L"0") != 0;
    log("Quality preview: conservative cockpit/sky history rejection=%u; device dispatch cache "
        "enabled",
        state->reduceGhosting);
    for (auto *node = static_cast<const VkBaseInStructure *>(ci->pNext); node; node = node->pNext) {
        if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
            state->separateDepthStencil =
                reinterpret_cast<const VkPhysicalDeviceVulkan12Features *>(node)
                    ->separateDepthStencilLayouts;
        if (node->sType ==
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES)
            state->separateDepthStencil =
                reinterpret_cast<const VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures *>(node)
                    ->separateDepthStencilLayouts;
    }
    auto memory = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
        instance->next(instance->handle, "vkGetPhysicalDeviceMemoryProperties"));
    memory(physical, &state->memory);
    auto queueProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        instance->next(instance->handle, "vkGetPhysicalDeviceQueueFamilyProperties"));
    count = 0;
    queueProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    queueProperties(physical, &count, families.data());
    for (uint32_t i = 0; i < ci->queueCreateInfoCount; ++i) {
        auto &q = ci->pQueueCreateInfos[i];
        if (q.queueFamilyIndex < families.size() &&
            (families[q.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            q.queueCount == 1) {
            state->graphicsFamily = q.queueFamilyIndex;
            break;
        }
    }
    if (!state->fgRequested.empty() && fgBase != UINT32_MAX) {
        state->fgQueueBase = fgBase;
        auto getQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(next(*out, "vkGetDeviceQueue"));
        getQueue(*out, fgFamily, 0, &state->fgQueues[0]);
        for (uint32_t i = 1; i < 4; ++i)
            getQueue(*out, fgFamily, fgBase + i - 1, &state->fgQueues[i]);
        if (setLoaderData) {
            for (auto q : state->fgQueues)
                check(setLoaderData(*out, q), "Initialize SDK queue dispatch");
        } else {
            state->fgFailed = true;
            strncpy(state->status.fgReason, "Vulkan loader dispatch callback missing", 159);
        }
        log("FG reserved queues: family=%u application=0 reserved=%u,%u,%u", fgFamily, fgBase,
            fgBase + 1, fgBase + 2);
    } else if (!state->fgRequested.empty()) {
        state->fgFailed = true;
        strncpy(state->status.fgReason, "No three spare graphics queues for FG", 159);
    }
    {
        std::lock_guard<std::mutex> l(registryLock);
        devices[key(*out)] = state;
    }
    log("Device ready; graphics family=%u, feature chain upgraded=%d", state->graphicsFamily,
        cloned);
    return result;
}
static VKAPI_ATTR void VKAPI_CALL L_DestroyDevice(VkDevice device,
                                                  const VkAllocationCallbacks *allocator) {
    auto d = deviceFor(device);
    if (!d)
        return;
    d->fn<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(device);
    stopGenerator(*d);
    {
        SdkScope sdk;
        d->fgCurrent.reset();
        d->fgSwapchains.clear();
    }
    d->fn<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(device);
    d->commands.clear();
    d->cockpitCompositor.reset();
    d->probes.clear();
    d->contexts.clear();
    d->runtime.reset();
    log("Quality preview: native function lookup misses=%llu",
        (unsigned long long)d->dispatchLookups.load());
    if (d->packPipeline)
        d->fn<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device, d->packPipeline, nullptr);
    if (d->packMsaaPipeline)
        d->fn<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device, d->packMsaaPipeline, nullptr);
    if (d->packPipelineLayout)
        d->fn<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(device, d->packPipelineLayout,
                                                                      nullptr);
    if (d->packLayout)
        d->fn<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(
            device, d->packLayout, nullptr);
    if (d->sampler)
        d->fn<PFN_vkDestroySampler>("vkDestroySampler")(device, d->sampler, nullptr);
    if (d->shared)
        UnmapViewOfFile(d->shared);
    if (d->mapping)
        CloseHandle(d->mapping);
    auto destroy = d->fn<PFN_vkDestroyDevice>("vkDestroyDevice");
    {
        std::lock_guard<std::mutex> l(registryLock);
        devices.erase(key(device));
    }
    destroy(device, allocator);
    log("Device destroyed");
}
#include "layer_framegen.inc"
static VKAPI_ATTR void VKAPI_CALL L_GetDeviceQueue(VkDevice device, uint32_t family, uint32_t index,
                                                   VkQueue *queue) {
    auto d = deviceFor(device);
    d->fn<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(device, family, index, queue);
    if (*queue && d->setLoaderData)
        d->setLoaderData(device, *queue);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_CreateImage(VkDevice device, const VkImageCreateInfo *c,
                                                    const VkAllocationCallbacks *a, VkImage *out) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCreateImage>("vkCreateImage");
        if (direct)
            return direct(device, c, a, out);
    }
    auto copy = *c; // Preserve every application image creation flag and usage bit.
    auto result = d->fn<PFN_vkCreateImage>("vkCreateImage")(device, &copy, a, out);
    if (result == VK_SUCCESS) {
        copy.pNext = nullptr;
        copy.pQueueFamilyIndices = nullptr;
        std::lock_guard<std::recursive_mutex> l(d->lock);
        d->images[*out] = {copy, ++d->imageGeneration};
    }
    return result;
}
static VKAPI_ATTR void VKAPI_CALL L_DestroyImage(VkDevice device, VkImage image,
                                                 const VkAllocationCallbacks *a) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkDestroyImage>("vkDestroyImage");
        if (direct)
            return direct(device, image, a);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        d->images.erase(image);
        d->depths.erase(image);
        d->layouts.erase(image);
    }
    d->fn<PFN_vkDestroyImage>("vkDestroyImage")(device, image, a);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_CreateImageView(VkDevice device,
                                                        const VkImageViewCreateInfo *c,
                                                        const VkAllocationCallbacks *a,
                                                        VkImageView *out) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCreateImageView>("vkCreateImageView");
        if (direct)
            return direct(device, c, a, out);
    }
    auto result = d->fn<PFN_vkCreateImageView>("vkCreateImageView")(device, c, a, out);
    if (result == VK_SUCCESS) {
        auto copy = *c;
        copy.pNext = nullptr;
        std::lock_guard<std::recursive_mutex> l(d->lock);
        d->views[*out] = {copy};
    }
    return result;
}
static VKAPI_ATTR void VKAPI_CALL L_DestroyImageView(VkDevice device, VkImageView view,
                                                     const VkAllocationCallbacks *a) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkDestroyImageView>("vkDestroyImageView");
        if (direct)
            return direct(device, view, a);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        d->views.erase(view);
    }
    d->fn<PFN_vkDestroyImageView>("vkDestroyImageView")(device, view, a);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_CreateShaderModule(VkDevice device,
                                                           const VkShaderModuleCreateInfo *c,
                                                           const VkAllocationCallbacks *a,
                                                           VkShaderModule *out) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCreateShaderModule>("vkCreateShaderModule");
        if (direct)
            return direct(device, c, a, out);
    }
    auto result = d->fn<PFN_vkCreateShaderModule>("vkCreateShaderModule")(device, c, a, out);
    if (result == VK_SUCCESS) {
        uint32_t fsr = fsrShader(c);
        std::lock_guard<std::recursive_mutex> l(d->lock);
        d->shaders[*out] = fsr;
        d->shaderUse[*out] = reflectBindings(c);
        if (fsr)
            log("Recognized native FSR compute shader (%zu bytes)", c->codeSize);
    }
    return result;
}
static VKAPI_ATTR void VKAPI_CALL L_DestroyShaderModule(VkDevice device, VkShaderModule shader,
                                                        const VkAllocationCallbacks *a) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkDestroyShaderModule>("vkDestroyShaderModule");
        if (direct)
            return direct(device, shader, a);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        d->shaders.erase(shader);
        d->shaderUse.erase(shader);
    }
    d->fn<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(device, shader, a);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_CreateComputePipelines(
    VkDevice device, VkPipelineCache cache, uint32_t count, const VkComputePipelineCreateInfo *c,
    const VkAllocationCallbacks *a, VkPipeline *out) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCreateComputePipelines>("vkCreateComputePipelines");
        if (direct)
            return direct(device, cache, count, c, a, out);
    }
    auto result = d->fn<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(device, cache,
                                                                                  count, c, a, out);
    std::lock_guard<std::recursive_mutex> l(d->lock);
    for (uint32_t i = 0; i < count; ++i)
        if (out[i]) {
            uint32_t known = d->shaders[c[i].stage.module];
            for (auto *p = static_cast<const VkBaseInStructure *>(c[i].stage.pNext); p;
                 p = p->pNext) {
                if (p->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO)
                    known = std::max(
                        known, fsrShader(reinterpret_cast<const VkShaderModuleCreateInfo *>(p)));
                if (p->sType ==
                    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT) {
                    auto *id = reinterpret_cast<
                        const VkPipelineShaderStageModuleIdentifierCreateInfoEXT *>(p);
                    if (id->identifierSize)
                        known = std::max(
                            known, d->shaderIdentifiers[{id->pIdentifier,
                                                         id->pIdentifier + id->identifierSize}]);
                }
            }
            d->pipelines[out[i]] = known;
            d->pipelineUse[out[i]] = stageUse(*d, c[i].stage);
            if (known)
                log("Recognized native FSR pipeline %p", reinterpret_cast<void *>(out[i]));
        }
    return result;
}
static VKAPI_ATTR void VKAPI_CALL L_GetShaderModuleIdentifierEXT(VkDevice device,
                                                                 VkShaderModule shader,
                                                                 VkShaderModuleIdentifierEXT *out) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkGetShaderModuleIdentifierEXT>("vkGetShaderModuleIdentifierEXT");
        if (direct)
            return direct(device, shader, out);
    }
    d->fn<PFN_vkGetShaderModuleIdentifierEXT>("vkGetShaderModuleIdentifierEXT")(device, shader,
                                                                                out);
    std::lock_guard<std::recursive_mutex> l(d->lock);
    if (out->identifierSize) {
        d->shaderIdentifiers[{out->identifier, out->identifier + out->identifierSize}] =
            d->shaders[shader];
        d->identifierUse[{out->identifier, out->identifier + out->identifierSize}] =
            d->shaderUse[shader];
    }
}
static VKAPI_ATTR void VKAPI_CALL L_GetShaderModuleCreateInfoIdentifierEXT(
    VkDevice device, const VkShaderModuleCreateInfo *c, VkShaderModuleIdentifierEXT *out) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkGetShaderModuleCreateInfoIdentifierEXT>(
            "vkGetShaderModuleCreateInfoIdentifierEXT");
        if (direct)
            return direct(device, c, out);
    }
    d->fn<PFN_vkGetShaderModuleCreateInfoIdentifierEXT>("vkGetShaderModuleCreateInfoIdentifierEXT")(
        device, c, out);
    std::lock_guard<std::recursive_mutex> l(d->lock);
    if (out->identifierSize) {
        d->shaderIdentifiers[{out->identifier, out->identifier + out->identifierSize}] =
            fsrShader(c);
        d->identifierUse[{out->identifier, out->identifier + out->identifierSize}] =
            reflectBindings(c);
    }
}
static VKAPI_ATTR VkResult VKAPI_CALL L_CreateGraphicsPipelines(
    VkDevice device, VkPipelineCache cache, uint32_t count, const VkGraphicsPipelineCreateInfo *c,
    const VkAllocationCallbacks *a, VkPipeline *out) {
    auto d = deviceFor(device);
    std::vector<VkGraphicsPipelineCreateInfo> infos(c, c + count);
    std::vector<VkPipelineViewportStateCreateInfo> viewports(count);
    std::vector<VkPipelineDynamicStateCreateInfo> dynamic(count);
    std::vector<std::vector<VkDynamicState>> states(count);
    std::vector<GraphicsViewport> tracked(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto *vp = c[i].pViewportState;
        if (!vp || (!vp->viewportCount || vp->viewportCount > 64) || !vp->pViewports ||
            (c[i].flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR))
            continue;
        bool hasDynamic = false;
        if (c[i].pDynamicState)
            for (uint32_t n = 0; n < c[i].pDynamicState->dynamicStateCount; ++n) {
                auto state = c[i].pDynamicState->pDynamicStates[n];
                states[i].push_back(state);
                if (state == VK_DYNAMIC_STATE_VIEWPORT ||
                    state == VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT)
                    hasDynamic = true;
            }
        if (hasDynamic)
            continue;
        tracked[i].known = true;
        tracked[i].viewport = vp->pViewports[0];
        tracked[i].viewports.assign(vp->pViewports, vp->pViewports + vp->viewportCount);
        if (d->mode != Mode::Observe) {
            viewports[i] = *vp;
            infos[i].pViewportState = &viewports[i];
            if (c[i].pDynamicState)
                dynamic[i] = *c[i].pDynamicState;
            else
                dynamic[i].sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            states[i].push_back(VK_DYNAMIC_STATE_VIEWPORT);
            dynamic[i].dynamicStateCount = uint32_t(states[i].size());
            dynamic[i].pDynamicStates = states[i].data();
            infos[i].pDynamicState = &dynamic[i];
            tracked[i].modified = true;
        }
    }
    auto result = d->fn<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(
        device, cache, count, infos.data(), a, out);
    std::lock_guard<std::recursive_mutex> l(d->lock);
    for (uint32_t i = 0; i < count; ++i)
        if (out[i]) {
            if (tracked[i].known)
                d->graphicsViewports[out[i]] = tracked[i];
            BindingUse use;
            use.known = c[i].stageCount > 0;
            for (uint32_t n = 0; n < c[i].stageCount; ++n) {
                auto stage = stageUse(*d, c[i].pStages[n]);
                use.known &= stage.known;
                for (auto &[binding, access] : stage.access)
                    use.access[binding] |= access;
            }
            d->pipelineUse[out[i]] = std::move(use);
        }
    return result;
}
static bool applyViewport(Device &, VkCommandBuffer, Command &);
static VKAPI_ATTR void VKAPI_CALL L_DestroyPipeline(VkDevice device, VkPipeline pipeline,
                                                    const VkAllocationCallbacks *a) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkDestroyPipeline>("vkDestroyPipeline");
        if (direct)
            return direct(device, pipeline, a);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        d->pipelines.erase(pipeline);
        d->graphicsViewports.erase(pipeline);
        d->pipelineUse.erase(pipeline);
    }
    d->fn<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device, pipeline, a);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_CreateCommandPool(VkDevice device,
                                                          const VkCommandPoolCreateInfo *c,
                                                          const VkAllocationCallbacks *a,
                                                          VkCommandPool *out) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCreateCommandPool>("vkCreateCommandPool");
        if (direct)
            return direct(device, c, a, out);
    }
    auto result = d->fn<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device, c, a, out);
    if (result == VK_SUCCESS) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        d->pools[*out] = c->queueFamilyIndex;
    }
    return result;
}
static VKAPI_ATTR void VKAPI_CALL L_DestroyCommandPool(VkDevice device, VkCommandPool pool,
                                                       const VkAllocationCallbacks *a) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkDestroyCommandPool>("vkDestroyCommandPool");
        if (direct)
            return direct(device, pool, a);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        for (auto it = d->commands.begin(); it != d->commands.end();)
            if (it->second.pool == pool)
                it = d->commands.erase(it);
            else
                ++it;
        d->pools.erase(pool);
    }
    d->fn<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(device, pool, a);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_AllocateCommandBuffers(VkDevice device,
                                                               const VkCommandBufferAllocateInfo *c,
                                                               VkCommandBuffer *out) {
    auto d = deviceFor(device);
    auto result = d->fn<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(device, c, out);
    if (result == VK_SUCCESS && d->setLoaderData)
        for (uint32_t i = 0; i < c->commandBufferCount; ++i) {
            auto setup = d->setLoaderData(device, out[i]);
            if (setup != VK_SUCCESS)
                return setup;
        }
    if (sdkNesting)
        return result;
    if (result == VK_SUCCESS) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        for (uint32_t i = 0; i < c->commandBufferCount; ++i) {
            auto &command = d->commands[out[i]];
            command.pool = c->commandPool;
            command.family = d->pools[c->commandPool];
            command.secondary = c->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        }
    }
    return result;
}
static VKAPI_ATTR void VKAPI_CALL L_FreeCommandBuffers(VkDevice device, VkCommandPool pool,
                                                       uint32_t count,
                                                       const VkCommandBuffer *buffers) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkFreeCommandBuffers>("vkFreeCommandBuffers");
        if (direct)
            return direct(device, pool, count, buffers);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        for (uint32_t i = 0; i < count; ++i)
            d->commands.erase(buffers[i]);
    }
    d->fn<PFN_vkFreeCommandBuffers>("vkFreeCommandBuffers")(device, pool, count, buffers);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_BeginCommandBuffer(VkCommandBuffer cb,
                                                           const VkCommandBufferBeginInfo *c) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer");
        if (direct)
            return direct(cb, c);
    }
    auto result = d->fn<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb, c);
    if (result == VK_SUCCESS) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        auto &old = d->commands[cb];
        auto pool = old.pool;
        auto family = old.family;
        auto secondary = old.secondary;
        auto resources = std::move(old.resources);
        auto captures = std::move(old.capturedDepths);
        if (!old.submitted) {
            resources.reset();
            captures.clear();
        } else
            for (auto it = captures.begin(); it != captures.end();) {
                auto meta = d->images.find(it->first);
                if (meta == d->images.end() || meta->second.generation != it->second->generation)
                    it = captures.erase(it);
                else
                    ++it;
            }
        old = Command{};
        old.pool = pool;
        old.family = family;
        old.secondary = secondary;
        old.epoch = d->epoch;
        old.oneTime = (c->flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != 0;
        if (secondary && (c->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT) &&
            c->pInheritanceInfo)
            for (auto *node = static_cast<const VkBaseInStructure *>(c->pInheritanceInfo->pNext);
                 node; node = node->pNext)
                if (node->sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO) {
                    auto *inherited =
                        reinterpret_cast<const VkCommandBufferInheritanceRenderingInfo *>(node);
                    old.sceneViewport = inherited->depthAttachmentFormat != VK_FORMAT_UNDEFINED &&
                                        inherited->colorAttachmentCount > 0;
                }
        old.resources = std::move(resources);
        old.capturedDepths = std::move(captures);
    }
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL L_EndCommandBuffer(VkCommandBuffer cb) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkEndCommandBuffer>("vkEndCommandBuffer");
        if (direct)
            return direct(cb);
    }
    auto result = d->fn<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb);
    std::lock_guard<std::recursive_mutex> l(d->lock);
    return d->commands[cb].failed ? VK_ERROR_UNKNOWN : result;
}
static VKAPI_ATTR void VKAPI_CALL L_CmdBindPipeline(VkCommandBuffer cb, VkPipelineBindPoint point,
                                                    VkPipeline pipeline) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline");
        if (direct)
            return direct(cb, point, pipeline);
    }
    std::lock_guard<std::recursive_mutex> l(d->lock);
    auto &command = d->commands[cb];
    if (point == VK_PIPELINE_BIND_POINT_COMPUTE)
        command.pipeline = pipeline;
    d->fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(cb, point, pipeline);
    if (point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        command.graphicsPipeline = pipeline;
        auto found = d->graphicsViewports.find(pipeline);
        if (found != d->graphicsViewports.end()) {
            command.viewport = found->second.viewport;
            command.viewports = found->second.viewports;
            command.hasViewport = true;
            command.viewportWithCount = false;
            if (found->second.modified)
                applyViewport(*d, cb, command);
            else if (command.activeDepth) {
                command.depths[command.activeDepth].ySign = command.viewport.height < 0 ? -1 : 1;
            }
        }
    }
}
static void bindGraphicsSets(Command &command, uint32_t first, uint32_t count,
                             const VkDescriptorSet *sets) {
    for (uint32_t i = 0; i < count; ++i) {
        command.graphicsSets[first + i] = sets[i];
        command.graphicsPushed.erase(first + i);
    }
}
static void bindSets(Device &d, VkCommandBuffer cb, VkPipelineLayout layout, uint32_t first,
                     uint32_t count, const VkDescriptorSet *sets, uint32_t offsetCount,
                     const uint32_t *offsets) {
    auto &command = d.commands[cb];
    command.layout = layout;
    command.dynamicOffsets = false;
    uint32_t used = 0;
    for (uint32_t i = 0; i < count; ++i) {
        command.sets[first + i] = sets[i];
        command.pushed.erase(first + i);
        uint32_t needed = d.setDynamicCounts[sets[i]];
        if (used + needed > offsetCount) {
            command.dynamicOffsets = true;
            command.offsets[first + i].clear();
        } else {
            auto &values = command.offsets[first + i];
            values.clear();
            if (needed)
                values.assign(offsets + used, offsets + used + needed);
        }
        used += needed;
    }
    if (used != offsetCount)
        command.dynamicOffsets = true;
}
static VKAPI_ATTR void VKAPI_CALL L_CmdBindDescriptorSets(
    VkCommandBuffer cb, VkPipelineBindPoint point, VkPipelineLayout layout, uint32_t first,
    uint32_t count, const VkDescriptorSet *sets, uint32_t offsets, const uint32_t *values) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets");
        if (direct)
            return direct(cb, point, layout, first, count, sets, offsets, values);
    }
    if (point == VK_PIPELINE_BIND_POINT_COMPUTE) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        bindSets(*d, cb, layout, first, count, sets, offsets, values);
    } else if (point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        bindGraphicsSets(d->commands[cb], first, count, sets);
    }
    d->fn<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(cb, point, layout, first, count,
                                                                  sets, offsets, values);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdBindDescriptorSets2(VkCommandBuffer cb,
                                                           const VkBindDescriptorSetsInfo *c) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdBindDescriptorSets2>("vkCmdBindDescriptorSets2");
        if (direct)
            return direct(cb, c);
    }
    if (c->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        bindSets(*d, cb, c->layout, c->firstSet, c->descriptorSetCount, c->pDescriptorSets,
                 c->dynamicOffsetCount, c->pDynamicOffsets);
    }
    if (c->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        bindGraphicsSets(d->commands[cb], c->firstSet, c->descriptorSetCount, c->pDescriptorSets);
    }
    auto fn = d->fn<PFN_vkCmdBindDescriptorSets2>("vkCmdBindDescriptorSets2");
    if (!fn)
        fn = d->fn<PFN_vkCmdBindDescriptorSets2>("vkCmdBindDescriptorSets2KHR");
    fn(cb, c);
}
static VKAPI_ATTR VkResult VKAPI_CALL
L_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *c,
                            const VkAllocationCallbacks *a, VkDescriptorSetLayout *out) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout");
        if (direct)
            return direct(device, c, a, out);
    }
    auto result =
        d->fn<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(device, c, a, out);
    if (result == VK_SUCCESS) {
        uint32_t count = 0;
        for (uint32_t i = 0; i < c->bindingCount; ++i)
            if (c->pBindings[i].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                c->pBindings[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
                count += c->pBindings[i].descriptorCount;
        std::lock_guard<std::recursive_mutex> l(d->lock);
        d->layoutDynamicCounts[*out] = count;
    }
    return result;
}
static VKAPI_ATTR void VKAPI_CALL L_DestroyDescriptorSetLayout(VkDevice device,
                                                               VkDescriptorSetLayout layout,
                                                               const VkAllocationCallbacks *a) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout");
        if (direct)
            return direct(device, layout, a);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        d->layoutDynamicCounts.erase(layout);
    }
    d->fn<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(device, layout, a);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_AllocateDescriptorSets(VkDevice device,
                                                               const VkDescriptorSetAllocateInfo *c,
                                                               VkDescriptorSet *out) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets");
        if (direct)
            return direct(device, c, out);
    }
    auto result = d->fn<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(device, c, out);
    if (result == VK_SUCCESS) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        for (uint32_t i = 0; i < c->descriptorSetCount; ++i) {
            d->descriptors.erase(out[i]);
            d->setDynamicCounts[out[i]] = d->layoutDynamicCounts[c->pSetLayouts[i]];
        }
    }
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL L_FreeDescriptorSets(VkDevice device, VkDescriptorPool pool,
                                                           uint32_t count,
                                                           const VkDescriptorSet *sets) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkFreeDescriptorSets>("vkFreeDescriptorSets");
        if (direct)
            return direct(device, pool, count, sets);
    }
    auto result =
        d->fn<PFN_vkFreeDescriptorSets>("vkFreeDescriptorSets")(device, pool, count, sets);
    if (result == VK_SUCCESS) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        for (uint32_t i = 0; i < count; ++i) {
            d->setDynamicCounts.erase(sets[i]);
            d->descriptors.erase(sets[i]);
        }
    }
    return result;
}
static VKAPI_ATTR void VKAPI_CALL L_UpdateDescriptorSets(VkDevice device, uint32_t count,
                                                         const VkWriteDescriptorSet *writes,
                                                         uint32_t copyCount,
                                                         const VkCopyDescriptorSet *copies) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets");
        if (direct)
            return direct(device, count, writes, copyCount, copies);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        for (uint32_t i = 0; i < count; ++i)
            writeDescriptors(d->descriptors[writes[i].dstSet], 1, writes + i);
        for (uint32_t i = 0; i < copyCount; ++i) {
            auto &c = copies[i];
            for (uint32_t n = 0; n < c.descriptorCount; ++n)
                d->descriptors[c.dstSet][{c.dstBinding, c.dstArrayElement + n}] =
                    d->descriptors[c.srcSet][{c.srcBinding, c.srcArrayElement + n}];
        }
    }
    d->fn<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(device, count, writes, copyCount,
                                                                copies);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdPushDescriptorSet(VkCommandBuffer cb,
                                                         VkPipelineBindPoint point,
                                                         VkPipelineLayout layout, uint32_t set,
                                                         uint32_t count,
                                                         const VkWriteDescriptorSet *writes) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdPushDescriptorSet>("vkCmdPushDescriptorSet");
        if (direct)
            return direct(cb, point, layout, set, count, writes);
    }
    if (point == VK_PIPELINE_BIND_POINT_COMPUTE) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        auto &command = d->commands[cb];
        command.layout = layout;
        command.sets.erase(set);
        writeDescriptors(command.pushed[set], count, writes);
    } else if (point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        auto &command = d->commands[cb];
        command.graphicsSets.erase(set);
        writeDescriptors(command.graphicsPushed[set], count, writes);
    }
    auto fn = d->fn<PFN_vkCmdPushDescriptorSet>("vkCmdPushDescriptorSet");
    if (!fn)
        fn = d->fn<PFN_vkCmdPushDescriptorSet>("vkCmdPushDescriptorSetKHR");
    fn(cb, point, layout, set, count, writes);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdPushDescriptorSet2(VkCommandBuffer cb,
                                                          const VkPushDescriptorSetInfo *c) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdPushDescriptorSet2>("vkCmdPushDescriptorSet2");
        if (direct)
            return direct(cb, c);
    }
    if (c->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        auto &command = d->commands[cb];
        command.layout = c->layout;
        command.sets.erase(c->set);
        writeDescriptors(command.pushed[c->set], c->descriptorWriteCount, c->pDescriptorWrites);
    }
    if (c->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        auto &command = d->commands[cb];
        command.graphicsSets.erase(c->set);
        writeDescriptors(command.graphicsPushed[c->set], c->descriptorWriteCount,
                         c->pDescriptorWrites);
    }
    auto fn = d->fn<PFN_vkCmdPushDescriptorSet2>("vkCmdPushDescriptorSet2");
    if (!fn)
        fn = d->fn<PFN_vkCmdPushDescriptorSet2>("vkCmdPushDescriptorSet2KHR");
    fn(cb, c);
}
static VKAPI_ATTR VkResult VKAPI_CALL
L_CreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo *c,
                                 const VkAllocationCallbacks *a, VkDescriptorUpdateTemplate *out) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct =
            d->fn<PFN_vkCreateDescriptorUpdateTemplate>("vkCreateDescriptorUpdateTemplate");
        if (direct)
            return direct(device, c, a, out);
    }
    auto fn = d->fn<PFN_vkCreateDescriptorUpdateTemplate>("vkCreateDescriptorUpdateTemplate");
    if (!fn)
        fn = d->fn<PFN_vkCreateDescriptorUpdateTemplate>("vkCreateDescriptorUpdateTemplateKHR");
    auto result = fn(device, c, a, out);
    if (result == VK_SUCCESS) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        d->templates[*out] = {{c->pDescriptorUpdateEntries,
                               c->pDescriptorUpdateEntries + c->descriptorUpdateEntryCount},
                              c->set,
                              c->pipelineBindPoint};
    }
    return result;
}
static VKAPI_ATTR void VKAPI_CALL L_DestroyDescriptorUpdateTemplate(
    VkDevice device, VkDescriptorUpdateTemplate value, const VkAllocationCallbacks *a) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct =
            d->fn<PFN_vkDestroyDescriptorUpdateTemplate>("vkDestroyDescriptorUpdateTemplate");
        if (direct)
            return direct(device, value, a);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        d->templates.erase(value);
    }
    auto fn = d->fn<PFN_vkDestroyDescriptorUpdateTemplate>("vkDestroyDescriptorUpdateTemplate");
    if (!fn)
        fn = d->fn<PFN_vkDestroyDescriptorUpdateTemplate>("vkDestroyDescriptorUpdateTemplateKHR");
    fn(device, value, a);
}
static VKAPI_ATTR void VKAPI_CALL L_UpdateDescriptorSetWithTemplate(
    VkDevice device, VkDescriptorSet set, VkDescriptorUpdateTemplate value, const void *data) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        auto direct =
            d->fn<PFN_vkUpdateDescriptorSetWithTemplate>("vkUpdateDescriptorSetWithTemplate");
        if (direct)
            return direct(device, set, value, data);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        templateDescriptors(*d, d->descriptors[set], value, data);
    }
    auto fn = d->fn<PFN_vkUpdateDescriptorSetWithTemplate>("vkUpdateDescriptorSetWithTemplate");
    if (!fn)
        fn = d->fn<PFN_vkUpdateDescriptorSetWithTemplate>("vkUpdateDescriptorSetWithTemplateKHR");
    fn(device, set, value, data);
}
static void pushTemplate(Device &d, VkCommandBuffer cb, VkDescriptorUpdateTemplate value,
                         VkPipelineLayout layout, uint32_t set, const void *data) {
    auto &command = d.commands[cb];
    if (d.templates[value].point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        command.graphicsSets.erase(set);
        templateDescriptors(d, command.graphicsPushed[set], value, data);
        return;
    }
    command.layout = layout;
    command.sets.erase(set);
    templateDescriptors(d, command.pushed[set], value, data);
}
static VKAPI_ATTR void VKAPI_CALL
L_CmdPushDescriptorSetWithTemplate(VkCommandBuffer cb, VkDescriptorUpdateTemplate value,
                                   VkPipelineLayout layout, uint32_t set, const void *data) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct =
            d->fn<PFN_vkCmdPushDescriptorSetWithTemplate>("vkCmdPushDescriptorSetWithTemplate");
        if (direct)
            return direct(cb, value, layout, set, data);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        pushTemplate(*d, cb, value, layout, set, data);
    }
    auto fn = d->fn<PFN_vkCmdPushDescriptorSetWithTemplate>("vkCmdPushDescriptorSetWithTemplate");
    if (!fn)
        fn = d->fn<PFN_vkCmdPushDescriptorSetWithTemplate>("vkCmdPushDescriptorSetWithTemplateKHR");
    fn(cb, value, layout, set, data);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdPushDescriptorSetWithTemplate2(
    VkCommandBuffer cb, const VkPushDescriptorSetWithTemplateInfo *c) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct =
            d->fn<PFN_vkCmdPushDescriptorSetWithTemplate2>("vkCmdPushDescriptorSetWithTemplate2");
        if (direct)
            return direct(cb, c);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        pushTemplate(*d, cb, c->descriptorUpdateTemplate, c->layout, c->set, c->pData);
    }
    auto fn = d->fn<PFN_vkCmdPushDescriptorSetWithTemplate2>("vkCmdPushDescriptorSetWithTemplate2");
    if (!fn)
        fn = d->fn<PFN_vkCmdPushDescriptorSetWithTemplate2>(
            "vkCmdPushDescriptorSetWithTemplate2KHR");
    fn(cb, c);
}
static void recordPush(Device &d, VkCommandBuffer cb, VkPipelineLayout layout,
                       VkShaderStageFlags stages, uint32_t offset, uint32_t size,
                       const void *data) {
    if (!(stages & VK_SHADER_STAGE_COMPUTE_BIT))
        return;
    auto &values = d.commands[cb].constants;
    values.push_back({layout,
                      stages,
                      offset,
                      {static_cast<const char *>(data), static_cast<const char *>(data) + size}});
    if (values.size() > 64)
        values.erase(values.begin());
}
static VKAPI_ATTR void VKAPI_CALL L_CmdPushConstants(VkCommandBuffer cb, VkPipelineLayout layout,
                                                     VkShaderStageFlags stages, uint32_t offset,
                                                     uint32_t size, const void *data) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdPushConstants>("vkCmdPushConstants");
        if (direct)
            return direct(cb, layout, stages, offset, size, data);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        recordPush(*d, cb, layout, stages, offset, size, data);
    }
    d->fn<PFN_vkCmdPushConstants>("vkCmdPushConstants")(cb, layout, stages, offset, size, data);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdPushConstants2(VkCommandBuffer cb,
                                                      const VkPushConstantsInfo *c) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdPushConstants2>("vkCmdPushConstants2");
        if (direct)
            return direct(cb, c);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        recordPush(*d, cb, c->layout, c->stageFlags, c->offset, c->size, c->pValues);
    }
    auto fn = d->fn<PFN_vkCmdPushConstants2>("vkCmdPushConstants2");
    if (!fn)
        fn = d->fn<PFN_vkCmdPushConstants2>("vkCmdPushConstants2KHR");
    fn(cb, c);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdPipelineBarrier(
    VkCommandBuffer cb, VkPipelineStageFlags src, VkPipelineStageFlags dst, VkDependencyFlags flags,
    uint32_t memoryCount, const VkMemoryBarrier *memory, uint32_t bufferCount,
    const VkBufferMemoryBarrier *buffers, uint32_t imageCount, const VkImageMemoryBarrier *images) {
    auto d = deviceFor(cb);
    std::vector<VkImageMemoryBarrier> forwarded;
    if (imageCount)
        forwarded.assign(images, images + imageCount);
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        for (auto &image : forwarded) {
            auto target = d->fgBackbuffers.find(image.image);
            if (target != d->fgBackbuffers.end() && !target->second.expired()) {
                if (image.oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
                    image.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    image.srcAccessMask |= VK_ACCESS_SHADER_READ_BIT;
                    src |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                }
                if (image.newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
                    image.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    image.dstAccessMask |= VK_ACCESS_SHADER_READ_BIT;
                    dst |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                }
            }
            d->commands[cb].layouts[image.image] = image.newLayout;
            d->layouts[image.image] = image.newLayout;
        }
    }
    d->fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
        cb, src, dst, flags, memoryCount, memory, bufferCount, buffers, imageCount,
        forwarded.empty() ? images : forwarded.data());
}
static VKAPI_ATTR void VKAPI_CALL L_CmdPipelineBarrier2(VkCommandBuffer cb,
                                                        const VkDependencyInfo *c) {
    auto d = deviceFor(cb);
    auto forwarded = *c;
    std::vector<VkImageMemoryBarrier2> images;
    if (c->imageMemoryBarrierCount)
        images.assign(c->pImageMemoryBarriers,
                      c->pImageMemoryBarriers + c->imageMemoryBarrierCount);
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        for (auto &image : images) {
            auto target = d->fgBackbuffers.find(image.image);
            if (target != d->fgBackbuffers.end() && !target->second.expired()) {
                if (image.oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
                    image.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    image.srcAccessMask |= VK_ACCESS_2_SHADER_READ_BIT;
                    image.srcStageMask |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                }
                if (image.newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
                    image.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    image.dstAccessMask |= VK_ACCESS_2_SHADER_READ_BIT;
                    image.dstStageMask |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                }
            }
            d->commands[cb].layouts[image.image] = image.newLayout;
            d->layouts[image.image] = image.newLayout;
        }
    }
    forwarded.pImageMemoryBarriers = images.empty() ? c->pImageMemoryBarriers : images.data();
    auto fn = d->fn<PFN_vkCmdPipelineBarrier2>("vkCmdPipelineBarrier2");
    if (!fn)
        fn = d->fn<PFN_vkCmdPipelineBarrier2>("vkCmdPipelineBarrier2KHR");
    fn(cb, &forwarded);
}
static bool applyViewport(Device &d, VkCommandBuffer cb, Command &command) {
    if (!command.hasViewport || command.viewports.empty())
        return false;
    if (command.secondary) {
        d.attach();
        app::Camera latest{};
        if (app::readCamera(d.shared, latest))
            d.camera = latest;
    }
    if (d.shared)
        d.enabled = InterlockedCompareExchange(&d.shared->enabled, 0, 0) != 0;
    d.readControl();
    auto adjusted = command.viewports;
    uint32_t width = d.status.renderWidth, height = d.status.renderHeight;
    auto active = d.images.find(command.activeDepth);
    uint32_t representativeWidth =
                 active != d.images.end() ? active->second.create.extent.width : width,
             representativeHeight =
                 active != d.images.end() ? active->second.create.extent.height : height;
    size_t representative = 0;
    for (size_t i = 0; i < adjusted.size(); ++i)
        if (std::abs(adjusted[i].width - float(representativeWidth)) < 2 &&
            std::abs(std::abs(adjusted[i].height) - float(representativeHeight)) < 2) {
            representative = i;
            break;
        }
    command.viewport = command.viewports[representative];
    float jx = 0, jy = 0;
    bool scope = command.sceneViewport &&
                 (command.secondary ||
                  (active != d.images.end() && active->second.create.extent.width == width &&
                   active->second.create.extent.height == height));
    bool coherent = true;
    if (scope) {
        command.sceneCamera.observe(d.camera.valid ? d.camera.frame : 0);
        coherent = command.sceneCamera.matches(d.camera.frame);
        if (command.activeDepth && command.depths.count(command.activeDepth)) {
            auto &depth = command.depths.at(command.activeDepth);
            depth.cameraCoherent &= command.sceneCamera.matches(depth.epoch);
            coherent &= depth.cameraCoherent;
        }
    }
    const bool groundContact = d.mode != Mode::Observe && d.groundCameraActive();
    if (d.mode != Mode::Observe && !groundContact && scope && coherent && d.enabled &&
        d.pathReady && d.camera.valid) {
        auto context = d.contexts.find(d.activeContext);
        if (context != d.contexts.end()) {
            try {
                if (d.jitterFrame != d.camera.frame) {
                    auto jitter = context->second->jitter(d.camera.frame);
                    d.jitterX = jitter[0];
                    d.jitterY = jitter[1];
                    d.jitterFrame = d.camera.frame;
                }
                jx = d.jitterX;
                jy = d.jitterY;
            } catch (const std::exception &e) {
                d.runtimeFailed = true;
                d.pathReady = false;
                d.publish(e.what());
            }
        }
    }
    command.appliedJitterX = command.appliedJitterY = 0;
    for (size_t i = 0; i < adjusted.size(); ++i)
        if (std::abs(adjusted[i].width - float(width)) < 2 &&
            std::abs(std::abs(adjusted[i].height) - float(height)) < 2) {
            adjusted[i].x += jx;
            adjusted[i].y += jy;
            if (i == representative) {
                command.appliedJitterX = jx;
                command.appliedJitterY = jy;
            }
        }
    if (command.sceneViewport && command.activeDepth) {
        auto &depth = command.depths[command.activeDepth];
        depth.jitterX = command.appliedJitterX;
        depth.jitterY = command.appliedJitterY;
        depth.ySign = command.viewport.height < 0 ? -1 : 1;
        d.depths[command.activeDepth] = depth;
    }
    if (command.viewportWithCount) {
        auto fn = d.fn<PFN_vkCmdSetViewportWithCount>("vkCmdSetViewportWithCount");
        if (!fn)
            fn = d.fn<PFN_vkCmdSetViewportWithCount>("vkCmdSetViewportWithCountEXT");
        fn(cb, uint32_t(adjusted.size()), adjusted.data());
    } else
        d.fn<PFN_vkCmdSetViewport>("vkCmdSetViewport")(cb, 0, uint32_t(adjusted.size()),
                                                       adjusted.data());
    return true;
}
static VKAPI_ATTR void VKAPI_CALL L_CmdBeginRendering(VkCommandBuffer cb,
                                                      const VkRenderingInfo *c) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdBeginRendering>("vkCmdBeginRendering");
        if (direct)
            return direct(cb, c);
    }
    VkRenderingInfo forwarded = *c;
    VkRenderingAttachmentInfo storedDepth{};
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        auto &command = d->commands[cb];
        command.activeCaptureEligible = false;
        command.rendering = true;
        command.activeColors.clear();
        for (uint32_t i = 0; i < c->colorAttachmentCount; ++i) {
            auto color = d->views.find(c->pColorAttachments[i].imageView);
            if (color != d->views.end())
                command.activeColors.insert(color->second.create.image);
        }
        command.sceneViewport = false;
        command.activeDepth = VK_NULL_HANDLE;
        if (c->pDepthAttachment && c->pDepthAttachment->imageView && c->colorAttachmentCount &&
            command.family == d->graphicsFamily) {
            d->attach();
            app::Camera camera{};
            if (app::readCamera(d->shared, camera))
                d->camera = camera;
            auto view = d->views.find(c->pDepthAttachment->imageView);
            if (view != d->views.end()) {
                Depth depth{c->pDepthAttachment->imageView, view->second.create.image,
                            c->pDepthAttachment->imageLayout, d->camera.frame};
                auto previous = command.depths.find(depth.image);
                if (previous != command.depths.end() && previous->second.epoch == depth.epoch)
                    depth = previous->second;
                for (uint32_t i = 0; i < c->colorAttachmentCount; ++i) {
                    for (auto handle : {c->pColorAttachments[i].imageView,
                                        c->pColorAttachments[i].resolveMode != VK_RESOLVE_MODE_NONE
                                            ? c->pColorAttachments[i].resolveImageView
                                            : VK_NULL_HANDLE}) {
                        auto color = d->views.find(handle);
                        if (color != d->views.end()) {
                            depth.colors.insert(color->second.create.image);
                            command.depthOrigins[color->second.create.image].insert(depth.image);
                        }
                    }
                }
                depth.layout = c->pDepthAttachment->imageLayout;
                depth.view = c->pDepthAttachment->imageView;
                depth.order = ++command.depthOrder;
                auto meta = d->images.find(depth.image);
                depth.fullArea = meta != d->images.end() && c->renderArea.offset.x == 0 &&
                                 c->renderArea.offset.y == 0 &&
                                 c->renderArea.extent.width == meta->second.create.extent.width &&
                                 c->renderArea.extent.height == meta->second.create.extent.height;
                if (d->shared)
                    d->enabled = InterlockedCompareExchange(&d->shared->enabled, 0, 0) != 0;
                d->readControl();
                auto source = d->images.find(depth.image);
                if (d->mode != Mode::Observe && !d->groundCameraActive() && d->enabled &&
                    d->camera.valid && depth.fullArea && source != d->images.end() &&
                    (source->second.create.samples &
                     (VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT |
                      VK_SAMPLE_COUNT_8_BIT)) &&
                    source->second.create.arrayLayers == 1 &&
                    (source->second.create.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
                    !(c->flags & VK_RENDERING_SUSPENDING_BIT) &&
                    view->second.create.subresourceRange.baseMipLevel == 0 &&
                    view->second.create.subresourceRange.baseArrayLayer == 0 &&
                    source->second.create.extent.width >= 64 &&
                    source->second.create.extent.height >= 64 && d->camera.height &&
                    std::abs(double(source->second.create.extent.width) /
                                 source->second.create.extent.height -
                             double(d->camera.width) / d->camera.height) < .02) {
                    command.activeCaptureEligible = true;
                    storedDepth = *c->pDepthAttachment;
                    storedDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    forwarded.pDepthAttachment = &storedDepth;
                }
                command.depths[depth.image] = depth;
                d->depths[depth.image] = depth;
                command.layouts[depth.image] = depth.layout;
                d->layouts[depth.image] = depth.layout;
                command.activeDepth = depth.image;
                command.sceneViewport = true;
            }
        }
    }
    auto fn = d->fn<PFN_vkCmdBeginRendering>("vkCmdBeginRendering");
    if (!fn)
        fn = d->fn<PFN_vkCmdBeginRendering>("vkCmdBeginRenderingKHR");
    fn(cb, &forwarded);
    std::lock_guard<std::recursive_mutex> l(d->lock);
    if (d->mode != Mode::Observe && d->commands[cb].sceneViewport)
        applyViewport(*d, cb, d->commands[cb]);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdEndRendering(VkCommandBuffer cb) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdEndRendering>("vkCmdEndRendering");
        if (direct)
            return direct(cb);
    }
    auto fn = d->fn<PFN_vkCmdEndRendering>("vkCmdEndRendering");
    if (!fn)
        fn = d->fn<PFN_vkCmdEndRendering>("vkCmdEndRenderingKHR");
    fn(cb);
    std::lock_guard<std::recursive_mutex> l(d->lock);
    auto &command = d->commands[cb];
    try {
        captureDepth(*d, cb, command);
    } catch (const std::exception &e) {
        command.failed = true;
        d->runtimeFailed = true;
        d->pathReady = false;
        d->publish(e.what());
        log("Depth preservation failed: %s", e.what());
    }
    bool restore = command.sceneViewport;
    command.rendering = command.sceneViewport = false;
    command.activeColors.clear();
    command.activeDepth = VK_NULL_HANDLE;
    if (restore && d->mode != Mode::Observe)
        applyViewport(*d, cb, command);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdSetViewport(VkCommandBuffer cb, uint32_t first,
                                                   uint32_t count, const VkViewport *viewports) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdSetViewport>("vkCmdSetViewport");
        if (direct)
            return direct(cb, first, count, viewports);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        auto &command = d->commands[cb];
        ++d->viewportCalls;
        if (command.secondary)
            ++d->secondaryViewportCalls;
        if (count != 1)
            ++d->multiViewportCalls;
        if (d->viewportCalls <= 4)
            log("Viewport array: first=%u count=%u size=%gx%g secondary=%d", first, count,
                count ? viewports[0].width : 0, count ? viewports[0].height : 0, command.secondary);
        if (first == 0 && count > 0 && count <= 64) {
            command.viewport = viewports[0];
            command.viewports.assign(viewports, viewports + count);
            command.hasViewport = true;
            command.viewportWithCount = false;
            applyViewport(*d, cb, command);
            return;
        }
        command.hasViewport = false;
        command.viewports.clear();
    }
    d->fn<PFN_vkCmdSetViewport>("vkCmdSetViewport")(cb, first, count, viewports);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdSetViewportWithCount(VkCommandBuffer cb, uint32_t count,
                                                            const VkViewport *viewports) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdSetViewportWithCount>("vkCmdSetViewportWithCount");
        if (direct)
            return direct(cb, count, viewports);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        auto &command = d->commands[cb];
        ++d->viewportCalls;
        if (command.secondary)
            ++d->secondaryViewportCalls;
        if (count != 1)
            ++d->multiViewportCalls;
        if (d->viewportCalls <= 4)
            log("Viewport-with-count array: count=%u size=%gx%g secondary=%d", count,
                count ? viewports[0].width : 0, count ? viewports[0].height : 0, command.secondary);
        if (count > 0 && count <= 64) {
            command.viewport = viewports[0];
            command.viewports.assign(viewports, viewports + count);
            command.hasViewport = true;
            command.viewportWithCount = true;
            applyViewport(*d, cb, command);
            return;
        }
        command.hasViewport = false;
        command.viewports.clear();
    }
    auto fn = d->fn<PFN_vkCmdSetViewportWithCount>("vkCmdSetViewportWithCount");
    if (!fn)
        fn = d->fn<PFN_vkCmdSetViewportWithCount>("vkCmdSetViewportWithCountEXT");
    fn(cb, count, viewports);
}
// Track read-color/depth dependencies through post-processing. Multiple possible
// scene depths remain ambiguous and are rejected at the FSR boundary.
static void trackDispatch(Device &d, Command &command) {
    std::map<uint32_t, Bindings> sets = command.pushed;
    for (auto &[index, set] : command.sets)
        if (!sets.count(index))
            sets[index] = d.descriptors[set];
    std::set<VkImage> inputs, outputs, origins;
    auto reflected = d.pipelineUse.find(command.pipeline);
    for (auto &[index, bindings] : sets)
        for (auto &[binding, descriptor] : bindings) {
            uint32_t access = 3;
            if (reflected != d.pipelineUse.end() && reflected->second.known) {
                auto used = reflected->second.access.find({index, binding.first});
                if (used == reflected->second.access.end())
                    continue;
                access = used->second;
            }
            auto view = d.views.find(descriptor.image.imageView);
            if (view == d.views.end())
                continue;
            auto image = view->second.create.image;
            if (access & 1)
                inputs.insert(image);
            if ((access & 2) && descriptor.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                outputs.insert(image);
        }
    for (auto image : inputs) {
        auto found = command.depthOrigins.find(image);
        if (found != command.depthOrigins.end())
            origins.insert(found->second.begin(), found->second.end());
        if (command.depths.count(image))
            origins.insert(image);
    }
    for (auto output : outputs)
        command.depthOrigins[output].insert(origins.begin(), origins.end());
}
static void trackDraw(Device &d, Command &command) {
    std::map<uint32_t, Bindings> sets = command.graphicsPushed;
    for (auto &[index, set] : command.graphicsSets)
        if (!sets.count(index))
            sets[index] = d.descriptors[set];
    std::set<VkImage> origins;
    auto reflected = d.pipelineUse.find(command.graphicsPipeline);
    for (auto &[index, bindings] : sets)
        for (auto &[binding, descriptor] : bindings) {
            if (reflected != d.pipelineUse.end() && reflected->second.known) {
                auto used = reflected->second.access.find({index, binding.first});
                if (used == reflected->second.access.end() || !(used->second & 1))
                    continue;
            }
            auto view = d.views.find(descriptor.image.imageView);
            if (view == d.views.end())
                continue;
            auto image = view->second.create.image;
            command.readImages.insert(image);
            auto found = command.depthOrigins.find(image);
            if (found != command.depthOrigins.end())
                origins.insert(found->second.begin(), found->second.end());
            if (command.depths.count(image))
                origins.insert(image);
        }
    for (auto color : command.activeColors)
        command.depthOrigins[color].insert(origins.begin(), origins.end());
}
static VKAPI_ATTR void VKAPI_CALL L_CmdExecuteCommands(VkCommandBuffer cb, uint32_t count,
                                                       const VkCommandBuffer *children) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdExecuteCommands>("vkCmdExecuteCommands");
        if (direct)
            return direct(cb, count, children);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        auto &parent = d->commands[cb];
        for (uint32_t i = 0; i < count; ++i) {
            auto found = d->commands.find(children[i]);
            if (found == d->commands.end())
                continue;
            auto &child = found->second;
            parent.executedChildren.push_back(children[i]);
            std::vector<Depth> childDepths;
            for (auto &[image, depth] : child.depths)
                childDepths.push_back(depth);
            std::sort(childDepths.begin(), childDepths.end(),
                      [](const Depth &a, const Depth &b) { return a.order < b.order; });
            for (auto depth : childDepths) {
                depth.cameraCoherent &= child.sceneCamera.matches(depth.epoch);
                depth.order = ++parent.depthOrder;
                parent.depths[depth.image] = depth;
                d->depths[depth.image] = depth;
                auto copy = child.capturedDepths.find(depth.image);
                if (copy != child.capturedDepths.end()) {
                    parent.capturedDepths[depth.image] = copy->second;
                }
            }
            for (auto &[image, layout] : child.layouts)
                parent.layouts[image] = layout;
            std::set<VkImage> origins;
            for (auto image : child.readImages) {
                auto previous = parent.depthOrigins.find(image);
                if (previous != parent.depthOrigins.end())
                    origins.insert(previous->second.begin(), previous->second.end());
                if (parent.depths.count(image))
                    origins.insert(image);
            }
            for (auto color : parent.activeColors)
                parent.depthOrigins[color].insert(origins.begin(), origins.end());
            for (auto &[image, source] : child.depthOrigins)
                parent.depthOrigins[image].insert(source.begin(), source.end());
            if (parent.activeDepth && child.sceneViewport && child.hasViewport) {
                auto image = d->images.find(parent.activeDepth);
                if (image != d->images.end() &&
                    std::abs(child.viewport.width - float(image->second.create.extent.width)) < 2 &&
                    std::abs(std::abs(child.viewport.height) -
                             float(image->second.create.extent.height)) < 2) {
                    auto &depth = parent.depths[parent.activeDepth];
                    depth.cameraCoherent &= child.sceneCamera.matches(depth.epoch);
                    depth.jitterX = child.appliedJitterX;
                    depth.jitterY = child.appliedJitterY;
                    depth.ySign = child.viewport.height < 0 ? -1 : 1;
                    d->depths[parent.activeDepth] = depth;
                }
            }
        }
    }
    d->fn<PFN_vkCmdExecuteCommands>("vkCmdExecuteCommands")(cb, count, children);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdDraw(VkCommandBuffer cb, uint32_t vertices,
                                            uint32_t instances, uint32_t first,
                                            uint32_t firstInstance) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdDraw>("vkCmdDraw");
        if (direct)
            return direct(cb, vertices, instances, first, firstInstance);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        trackDraw(*d, d->commands[cb]);
    }
    d->fn<PFN_vkCmdDraw>("vkCmdDraw")(cb, vertices, instances, first, firstInstance);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdDrawIndexed(VkCommandBuffer cb, uint32_t indices,
                                                   uint32_t instances, uint32_t first,
                                                   int32_t vertexOffset, uint32_t firstInstance) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdDrawIndexed>("vkCmdDrawIndexed");
        if (direct)
            return direct(cb, indices, instances, first, vertexOffset, firstInstance);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        trackDraw(*d, d->commands[cb]);
    }
    d->fn<PFN_vkCmdDrawIndexed>("vkCmdDrawIndexed")(cb, indices, instances, first, vertexOffset,
                                                    firstInstance);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdDrawIndirect(VkCommandBuffer cb, VkBuffer buffer,
                                                    VkDeviceSize offset, uint32_t count,
                                                    uint32_t stride) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdDrawIndirect>("vkCmdDrawIndirect");
        if (direct)
            return direct(cb, buffer, offset, count, stride);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        trackDraw(*d, d->commands[cb]);
    }
    d->fn<PFN_vkCmdDrawIndirect>("vkCmdDrawIndirect")(cb, buffer, offset, count, stride);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdDrawIndexedIndirect(VkCommandBuffer cb, VkBuffer buffer,
                                                           VkDeviceSize offset, uint32_t count,
                                                           uint32_t stride) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdDrawIndexedIndirect>("vkCmdDrawIndexedIndirect");
        if (direct)
            return direct(cb, buffer, offset, count, stride);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        trackDraw(*d, d->commands[cb]);
    }
    d->fn<PFN_vkCmdDrawIndexedIndirect>("vkCmdDrawIndexedIndirect")(cb, buffer, offset, count,
                                                                    stride);
}
static void copyOrigins(Device &d, VkCommandBuffer cb, VkImage src, VkImage dst) {
    auto &command = d.commands[cb];
    auto &origins = command.depthOrigins[dst];
    auto found = command.depthOrigins.find(src);
    if (found != command.depthOrigins.end())
        origins.insert(found->second.begin(), found->second.end());
    if (command.depths.count(src))
        origins.insert(src);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdCopyImage(VkCommandBuffer cb, VkImage src, VkImageLayout sl,
                                                 VkImage dst, VkImageLayout dl, uint32_t count,
                                                 const VkImageCopy *regions) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdCopyImage>("vkCmdCopyImage");
        if (direct)
            return direct(cb, src, sl, dst, dl, count, regions);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        copyOrigins(*d, cb, src, dst);
    }
    d->fn<PFN_vkCmdCopyImage>("vkCmdCopyImage")(cb, src, sl, dst, dl, count, regions);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdCopyImage2(VkCommandBuffer cb, const VkCopyImageInfo2 *c) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdCopyImage2>("vkCmdCopyImage2");
        if (direct)
            return direct(cb, c);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        copyOrigins(*d, cb, c->srcImage, c->dstImage);
    }
    auto fn = d->fn<PFN_vkCmdCopyImage2>("vkCmdCopyImage2");
    if (!fn)
        fn = d->fn<PFN_vkCmdCopyImage2>("vkCmdCopyImage2KHR");
    fn(cb, c);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdResolveImage(VkCommandBuffer cb, VkImage src,
                                                    VkImageLayout sl, VkImage dst, VkImageLayout dl,
                                                    uint32_t count, const VkImageResolve *regions) {
    auto d = deviceFor(cb);
    if (!sdkNesting) {
        std::lock_guard<std::recursive_mutex> guard(d->lock);
        copyOrigins(*d, cb, src, dst);
    }
    d->fn<PFN_vkCmdResolveImage>("vkCmdResolveImage")(cb, src, sl, dst, dl, count, regions);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdResolveImage2(VkCommandBuffer cb,
                                                     const VkResolveImageInfo2 *info) {
    auto d = deviceFor(cb);
    if (!sdkNesting) {
        std::lock_guard<std::recursive_mutex> guard(d->lock);
        copyOrigins(*d, cb, info->srcImage, info->dstImage);
    }
    auto fn = d->fn<PFN_vkCmdResolveImage2>("vkCmdResolveImage2");
    if (!fn)
        fn = d->fn<PFN_vkCmdResolveImage2>("vkCmdResolveImage2KHR");
    fn(cb, info);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdBlitImage(VkCommandBuffer cb, VkImage src, VkImageLayout sl,
                                                 VkImage dst, VkImageLayout dl, uint32_t count,
                                                 const VkImageBlit *regions, VkFilter filter) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdBlitImage>("vkCmdBlitImage");
        if (direct)
            return direct(cb, src, sl, dst, dl, count, regions, filter);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        copyOrigins(*d, cb, src, dst);
    }
    d->fn<PFN_vkCmdBlitImage>("vkCmdBlitImage")(cb, src, sl, dst, dl, count, regions, filter);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdBlitImage2(VkCommandBuffer cb, const VkBlitImageInfo2 *c) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdBlitImage2>("vkCmdBlitImage2");
        if (direct)
            return direct(cb, c);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        copyOrigins(*d, cb, c->srcImage, c->dstImage);
    }
    auto fn = d->fn<PFN_vkCmdBlitImage2>("vkCmdBlitImage2");
    if (!fn)
        fn = d->fn<PFN_vkCmdBlitImage2>("vkCmdBlitImage2KHR");
    fn(cb, c);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdDispatch(VkCommandBuffer cb, uint32_t x, uint32_t y,
                                                uint32_t z) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdDispatch>("vkCmdDispatch");
        if (direct)
            return direct(cb, x, y, z);
    }
    bool handled = false;
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        trackDispatch(*d, d->commands[cb]);
        handled = replace(*d, cb, d->commands[cb]);
    }
    if (!handled)
        d->fn<PFN_vkCmdDispatch>("vkCmdDispatch")(cb, x, y, z);
}
static VKAPI_ATTR void VKAPI_CALL L_CmdDispatchBase(VkCommandBuffer cb, uint32_t bx, uint32_t by,
                                                    uint32_t bz, uint32_t x, uint32_t y,
                                                    uint32_t z) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdDispatchBase>("vkCmdDispatchBase");
        if (direct)
            return direct(cb, bx, by, bz, x, y, z);
    }
    bool handled = false;
    if (!bx && !by && !bz) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        trackDispatch(*d, d->commands[cb]);
        handled = replace(*d, cb, d->commands[cb]);
    }
    if (!handled) {
        auto fn = d->fn<PFN_vkCmdDispatchBase>("vkCmdDispatchBase");
        if (!fn)
            fn = d->fn<PFN_vkCmdDispatchBase>("vkCmdDispatchBaseKHR");
        fn(cb, bx, by, bz, x, y, z);
    }
}
static VKAPI_ATTR void VKAPI_CALL L_CmdDispatchIndirect(VkCommandBuffer cb, VkBuffer buffer,
                                                        VkDeviceSize offset) {
    auto d = deviceFor(cb);
    if (sdkNesting) {
        auto direct = d->fn<PFN_vkCmdDispatchIndirect>("vkCmdDispatchIndirect");
        if (direct)
            return direct(cb, buffer, offset);
    }
    {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        auto &command = d->commands[cb];
        if (d->pipelines[command.pipeline])
            d->bypass("Native FSR uses indirect dispatch; this alpha leaves it native");
    }
    d->fn<PFN_vkCmdDispatchIndirect>("vkCmdDispatchIndirect")(cb, buffer, offset);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_QueueSubmit(VkQueue queue, uint32_t count,
                                                    const VkSubmitInfo *submits, VkFence fence) {
    auto d = deviceFor(queue);
    auto fn = d->fn<PFN_vkQueueSubmit>("vkQueueSubmit");
    if (sdkNesting || (!d->fgRequested.empty() && d->fgQueues[0] && queue != d->fgQueues[0]))
        return fn(queue, count, submits, fence);
    // SDK Present can submit generation to the game queue. Serialize the
    // application's submission itself, rather than only its tracking data.
    std::lock_guard<std::recursive_mutex> submissionGuard(d->lock);
    auto result = fn(queue, count, submits, fence);
    if (result == VK_SUCCESS) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        std::vector<VkCommandBuffer> commands;
        for (uint32_t i = 0; i < count; ++i)
            for (uint32_t n = 0; n < submits[i].commandBufferCount; ++n)
                commands.push_back(submits[i].pCommandBuffers[n]);
        submittedProbes(*d, queue, commands);
    }
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL L_QueueSubmit2(VkQueue queue, uint32_t count,
                                                     const VkSubmitInfo2 *submits, VkFence fence) {
    auto d = deviceFor(queue);
    auto fn = d->fn<PFN_vkQueueSubmit2>("vkQueueSubmit2");
    if (!fn)
        fn = d->fn<PFN_vkQueueSubmit2>("vkQueueSubmit2KHR");
    if (sdkNesting || (!d->fgRequested.empty() && d->fgQueues[0] && queue != d->fgQueues[0]))
        return fn(queue, count, submits, fence);
    std::lock_guard<std::recursive_mutex> submissionGuard(d->lock);
    auto result = fn(queue, count, submits, fence);
    if (result == VK_SUCCESS) {
        std::lock_guard<std::recursive_mutex> l(d->lock);
        std::vector<VkCommandBuffer> commands;
        for (uint32_t i = 0; i < count; ++i)
            for (uint32_t n = 0; n < submits[i].commandBufferInfoCount; ++n)
                commands.push_back(submits[i].pCommandBufferInfos[n].commandBuffer);
        submittedProbes(*d, queue, commands);
    }
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL L_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *c) {
    return presentFrame(*deviceFor(queue), queue, c);
}

extern "C" __declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
XP_GetDeviceProcAddr(VkDevice device, const char *name) {
    if (!name)
        return nullptr;
    if (!strcmp(name, "vkAllocateCommandBuffers"))
        return reinterpret_cast<PFN_vkVoidFunction>(L_AllocateCommandBuffers);
    if (!strcmp(name, "vkGetDeviceQueue"))
        return reinterpret_cast<PFN_vkVoidFunction>(L_GetDeviceQueue);
    if (sdkNesting) {
        auto d = deviceFor(device);
        if (d)
            return d->next(device, name);
    }
#define HOOK(label, fn)                                                                            \
    if (!strcmp(name, label))                                                                      \
    return reinterpret_cast<PFN_vkVoidFunction>(fn)
    HOOK("vkDeviceWaitIdle", L_DeviceWaitIdle);
    HOOK("vkQueueWaitIdle", L_QueueWaitIdle);
    HOOK("vkCreateSwapchainKHR", L_CreateSwapchainKHR);
    HOOK("vkDestroySwapchainKHR", L_DestroySwapchainKHR);
    HOOK("vkGetSwapchainImagesKHR", L_GetSwapchainImagesKHR);
    HOOK("vkAcquireNextImageKHR", L_AcquireNextImageKHR);
    HOOK("vkAcquireNextImage2KHR", L_AcquireNextImage2KHR);
    HOOK("vkSetHdrMetadataEXT", L_SetHdrMetadataEXT);
    HOOK("vkGetDeviceProcAddr", XP_GetDeviceProcAddr);
    HOOK("vkDestroyDevice", L_DestroyDevice);
    HOOK("vkCreateImage", L_CreateImage);
    HOOK("vkDestroyImage", L_DestroyImage);
    HOOK("vkCreateImageView", L_CreateImageView);
    HOOK("vkDestroyImageView", L_DestroyImageView);
    HOOK("vkCreateShaderModule", L_CreateShaderModule);
    HOOK("vkDestroyShaderModule", L_DestroyShaderModule);
    HOOK("vkCreateComputePipelines", L_CreateComputePipelines);
    HOOK("vkCreateGraphicsPipelines", L_CreateGraphicsPipelines);
    HOOK("vkDestroyPipeline", L_DestroyPipeline);
    HOOK("vkGetShaderModuleIdentifierEXT", L_GetShaderModuleIdentifierEXT);
    HOOK("vkGetShaderModuleCreateInfoIdentifierEXT", L_GetShaderModuleCreateInfoIdentifierEXT);
    HOOK("vkCreateCommandPool", L_CreateCommandPool);
    HOOK("vkDestroyCommandPool", L_DestroyCommandPool);
    HOOK("vkAllocateCommandBuffers", L_AllocateCommandBuffers);
    HOOK("vkFreeCommandBuffers", L_FreeCommandBuffers);
    HOOK("vkBeginCommandBuffer", L_BeginCommandBuffer);
    HOOK("vkEndCommandBuffer", L_EndCommandBuffer);
    HOOK("vkCmdBindPipeline", L_CmdBindPipeline);
    HOOK("vkCmdBindDescriptorSets", L_CmdBindDescriptorSets);
    HOOK("vkCmdBindDescriptorSets2", L_CmdBindDescriptorSets2);
    HOOK("vkCmdBindDescriptorSets2KHR", L_CmdBindDescriptorSets2);
    HOOK("vkUpdateDescriptorSets", L_UpdateDescriptorSets);
    HOOK("vkCreateDescriptorSetLayout", L_CreateDescriptorSetLayout);
    HOOK("vkDestroyDescriptorSetLayout", L_DestroyDescriptorSetLayout);
    HOOK("vkAllocateDescriptorSets", L_AllocateDescriptorSets);
    HOOK("vkFreeDescriptorSets", L_FreeDescriptorSets);
    HOOK("vkCmdPushDescriptorSet", L_CmdPushDescriptorSet);
    HOOK("vkCmdPushDescriptorSetKHR", L_CmdPushDescriptorSet);
    HOOK("vkCmdPushDescriptorSet2", L_CmdPushDescriptorSet2);
    HOOK("vkCmdPushDescriptorSet2KHR", L_CmdPushDescriptorSet2);
    HOOK("vkCreateDescriptorUpdateTemplate", L_CreateDescriptorUpdateTemplate);
    HOOK("vkCreateDescriptorUpdateTemplateKHR", L_CreateDescriptorUpdateTemplate);
    HOOK("vkDestroyDescriptorUpdateTemplate", L_DestroyDescriptorUpdateTemplate);
    HOOK("vkDestroyDescriptorUpdateTemplateKHR", L_DestroyDescriptorUpdateTemplate);
    HOOK("vkUpdateDescriptorSetWithTemplate", L_UpdateDescriptorSetWithTemplate);
    HOOK("vkUpdateDescriptorSetWithTemplateKHR", L_UpdateDescriptorSetWithTemplate);
    HOOK("vkCmdPushDescriptorSetWithTemplate", L_CmdPushDescriptorSetWithTemplate);
    HOOK("vkCmdPushDescriptorSetWithTemplateKHR", L_CmdPushDescriptorSetWithTemplate);
    HOOK("vkCmdPushDescriptorSetWithTemplate2", L_CmdPushDescriptorSetWithTemplate2);
    HOOK("vkCmdPushDescriptorSetWithTemplate2KHR", L_CmdPushDescriptorSetWithTemplate2);
    HOOK("vkCmdPushConstants", L_CmdPushConstants);
    HOOK("vkCmdPushConstants2", L_CmdPushConstants2);
    HOOK("vkCmdPushConstants2KHR", L_CmdPushConstants2);
    HOOK("vkCmdPipelineBarrier", L_CmdPipelineBarrier);
    HOOK("vkCmdPipelineBarrier2", L_CmdPipelineBarrier2);
    HOOK("vkCmdPipelineBarrier2KHR", L_CmdPipelineBarrier2);
    HOOK("vkCmdBeginRendering", L_CmdBeginRendering);
    HOOK("vkCmdBeginRenderingKHR", L_CmdBeginRendering);
    HOOK("vkCmdEndRendering", L_CmdEndRendering);
    HOOK("vkCmdEndRenderingKHR", L_CmdEndRendering);
    HOOK("vkCmdSetViewport", L_CmdSetViewport);
    HOOK("vkCmdSetViewportWithCount", L_CmdSetViewportWithCount);
    HOOK("vkCmdSetViewportWithCountEXT", L_CmdSetViewportWithCount);
    HOOK("vkCmdDispatch", L_CmdDispatch);
    HOOK("vkCmdExecuteCommands", L_CmdExecuteCommands);
    HOOK("vkCmdDraw", L_CmdDraw);
    HOOK("vkCmdDrawIndexed", L_CmdDrawIndexed);
    HOOK("vkCmdDrawIndirect", L_CmdDrawIndirect);
    HOOK("vkCmdDrawIndexedIndirect", L_CmdDrawIndexedIndirect);
    HOOK("vkCmdCopyImage", L_CmdCopyImage);
    HOOK("vkCmdCopyImage2", L_CmdCopyImage2);
    HOOK("vkCmdCopyImage2KHR", L_CmdCopyImage2);
    HOOK("vkCmdResolveImage", L_CmdResolveImage);
    HOOK("vkCmdResolveImage2", L_CmdResolveImage2);
    HOOK("vkCmdResolveImage2KHR", L_CmdResolveImage2);
    HOOK("vkCmdBlitImage", L_CmdBlitImage);
    HOOK("vkCmdBlitImage2", L_CmdBlitImage2);
    HOOK("vkCmdBlitImage2KHR", L_CmdBlitImage2);
    HOOK("vkCmdDispatchBase", L_CmdDispatchBase);
    HOOK("vkCmdDispatchBaseKHR", L_CmdDispatchBase);
    HOOK("vkCmdDispatchIndirect", L_CmdDispatchIndirect);
    HOOK("vkQueueSubmit", L_QueueSubmit);
    HOOK("vkQueueSubmit2", L_QueueSubmit2);
    HOOK("vkQueueSubmit2KHR", L_QueueSubmit2);
    HOOK("vkQueuePresentKHR", L_QueuePresentKHR);
    auto d = deviceFor(device);
    return d ? d->next(device, name) : nullptr;
}
extern "C" __declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
XP_GetInstanceProcAddr(VkInstance instance, const char *name) {
    if (!name)
        return nullptr;
    HOOK("vkGetInstanceProcAddr", XP_GetInstanceProcAddr);
    HOOK("vkGetDeviceProcAddr", XP_GetDeviceProcAddr);
    HOOK("vkCreateInstance", L_CreateInstance);
    HOOK("vkDestroyInstance", L_DestroyInstance);
    HOOK("vkCreateDevice", L_CreateDevice);
    // Device entry points can also be obtained through the instance resolver.
    if (instance) {
        auto d = XP_GetDeviceProcAddr(VK_NULL_HANDLE, name);
        if (d)
            return d;
        auto state = instanceFor(instance);
        return state ? state->next(instance, name) : nullptr;
    }
    return nullptr;
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *c) {
    if (!c || c->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
        return VK_ERROR_INITIALIZATION_FAILED;
    c->loaderLayerInterfaceVersion = std::min(c->loaderLayerInterfaceVersion, 2u);
    c->pfnGetInstanceProcAddr = XP_GetInstanceProcAddr;
    c->pfnGetDeviceProcAddr = XP_GetDeviceProcAddr;
    c->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}

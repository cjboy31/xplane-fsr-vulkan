// SPDX-License-Identifier: MIT
// Independent AMD FSR SDK 2.4 Vulkan adapter. No X-Plane/LuminaXP code.
#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <array>
#include <cmath>
#include <cstdint>
#include <ffx_api.h>
#include <ffx_upscale.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <vk/ffx_api_vk.h>
#include <vulkan/vulkan.h>
#include <windows.h>

namespace xpfsr {
inline void require(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
inline void check(ffxReturnCode_t rc, const char *operation) {
    if (rc != FFX_API_RETURN_OK)
        throw std::runtime_error(std::string(operation) + " returned FFX code " +
                                 std::to_string(rc));
}
struct Api {
    PfnFfxCreateContext create{};
    PfnFfxDestroyContext destroy{};
    PfnFfxQuery query{};
    PfnFfxDispatch dispatch{};
    PfnFfxConfigure configure{};
};
class Runtime {
    // Effects stay loaded until after the loader, and every context must die first.
    HMODULE effect_{};
    HMODULE loader_{};
    HMODULE framegen_{};

  public:
    Api api{};
    Runtime() = default;
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    void load(const std::filesystem::path &directory, bool frameGeneration = false) {
        require(!loader_ && !effect_,
                "Runtime load already attempted; construct a fresh Runtime to retry");
        require(directory.is_absolute(), "Runtime directory must be absolute");
        constexpr DWORD flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
        const auto effect = directory / L"amd_fidelityfx_upscaler_vk.dll";
        const auto loader = directory / L"amd_fidelityfx_loader_vk.dll";
        effect_ = LoadLibraryExW(effect.c_str(), nullptr, flags);
        require(effect_ != nullptr, "Cannot load signed AMD Vulkan upscaler DLL");
        if (frameGeneration) {
            framegen_ = LoadLibraryExW(
                (directory / L"amd_fidelityfx_framegeneration_vk.dll").c_str(), nullptr, flags);
            require(framegen_ != nullptr, "Cannot load signed AMD Vulkan frame generation DLL");
        }
        loader_ = LoadLibraryExW(loader.c_str(), nullptr, flags);
        require(loader_ != nullptr, "Cannot load signed AMD Vulkan loader DLL");
        api.create =
            reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(loader_, "ffxCreateContext"));
        api.destroy =
            reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(loader_, "ffxDestroyContext"));
        api.query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(loader_, "ffxQuery"));
        api.dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(loader_, "ffxDispatch"));
        api.configure = reinterpret_cast<PfnFfxConfigure>(GetProcAddress(loader_, "ffxConfigure"));
        require(api.create && api.destroy && api.query && api.dispatch,
                "Missing AMD FSR API entry point");
    }
    ~Runtime() {
        if (loader_)
            FreeLibrary(loader_);
        if (framegen_)
            FreeLibrary(framegen_);
        if (effect_)
            FreeLibrary(effect_);
    }
};
struct Version {
    uint64_t id{};
    std::string name;
};
inline std::vector<Version> versions(const Api &api, VkDeviceContext &device,
                                     uint64_t type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE) {
    ffxQueryDescGetVersions q{};
    q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    q.createDescType = type;
    q.device = &device;
    uint64_t count{};
    q.outputCount = &count;
    check(api.query(nullptr, &q.header), "GetVersions(count)");
    require(count > 0 && count <= 128, "No supported upscaler provider or invalid version count");
    const uint64_t capacity = count;
    std::vector<uint64_t> ids(count);
    std::vector<const char *> names(count);
    q.versionIds = ids.data();
    q.versionNames = names.data();
    check(api.query(nullptr, &q.header), "GetVersions(list)");
    require(count <= capacity, "Provider list changed; retry enumeration");
    std::vector<Version> result;
    for (size_t i = 0; i < count; ++i)
        result.push_back({ids[i], names[i] ? names[i] : "unnamed"});
    return result;
}
inline uint64_t providerVersion(const Api &api, VkDeviceContext device, const std::string &name,
                                uint64_t type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE) {
    for (auto &version : versions(api, device, type))
        if (version.name == name)
            return version.id;
    throw std::runtime_error("Requested FSR provider is unavailable: " + name);
}
struct Config {
    uint32_t renderWidth{}, renderHeight{}, outputWidth{}, outputHeight{};
    bool hdr = true;
    bool invertedDepth = true;
    bool infiniteDepth = true;
    bool autoExposure = true;
    // A zero ID leaves AMD's default selection and future driver upgrades enabled.
    uint64_t providerId{};
};
struct Image {
    VkImage handle{};
    VkImageCreateInfo description{};
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};
struct Frame {
    VkCommandBuffer commands{};
    Image color, depth, motion, output;
    Image reactive, composition;          // optional R8_UNORM masks, render resolution
    float jitterX{}, jitterY{};           // exact applied jitter, render pixels
    float motionScaleX{}, motionScaleY{}; // signed conversion to previous-minus-current pixels
    float deltaMilliseconds{};
    float nearPlane{}, farPlane{}, verticalFovRadians{};
    float preExposure = 1.0f;
    uint64_t sequence{};
    bool reset{};
};
inline void validateImage(const Image &image, uint32_t w, uint32_t h, bool output) {
    const auto &d = image.description;
    require(image.handle != VK_NULL_HANDLE, "Missing image");
    require(d.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, "Missing original image description");
    require(d.imageType == VK_IMAGE_TYPE_2D && d.arrayLayers == 1 && d.extent.depth == 1,
            "Prototype accepts a single 2D view only; extract array layer first");
    require(d.samples == VK_SAMPLE_COUNT_1_BIT && d.mipLevels == 1,
            "Resolve MSAA and select one mip before FSR");
    require(d.extent.width == w && d.extent.height == h,
            "Image extent differs from active context");
    require(ffxApiGetSurfaceFormatVK(d.format) != FFX_API_SURFACE_FORMAT_UNKNOWN,
            "Unsupported image format");
    require(d.tiling == VK_IMAGE_TILING_OPTIMAL && d.flags == 0,
            "Prototype requires ordinary optimal-tiling images; copy special images first");
    if (output) {
        require((d.usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0, "Output lacks STORAGE usage");
        require(image.layout == VK_IMAGE_LAYOUT_GENERAL,
                "Output must be tracked and transitioned to GENERAL");
    } else {
        require((d.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0, "Input lacks SAMPLED usage");
        require(image.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                "Input must be tracked and transitioned to SHADER_READ_ONLY_OPTIMAL");
    }
}
inline void validate(const Config &c, const Frame &f) {
    require(f.commands != VK_NULL_HANDLE, "Missing recording command buffer");
    validateImage(f.color, c.renderWidth, c.renderHeight, false);
    validateImage(f.depth, c.renderWidth, c.renderHeight, false);
    validateImage(f.motion, c.renderWidth, c.renderHeight, false);
    validateImage(f.output, c.outputWidth, c.outputHeight, true);
    require(f.depth.description.format == VK_FORMAT_R32_SFLOAT,
            "Prototype requires a plain R32_SFLOAT depth copy");
    require(f.motion.description.format == VK_FORMAT_R16G16_SFLOAT ||
                f.motion.description.format == VK_FORMAT_R32G32_SFLOAT,
            "Extract XY velocity to RG16F or RG32F first");
    const Image *inputs[] = {&f.color, &f.depth, &f.motion, &f.reactive, &f.composition};
    for (auto *input : inputs) {
        if (!input->handle)
            continue;
        require(input->handle != f.output.handle, "Output aliases an input");
    }
    for (auto *mask : {&f.reactive, &f.composition}) {
        if (!mask->handle)
            continue;
        validateImage(*mask, c.renderWidth, c.renderHeight, false);
        require(mask->description.format == VK_FORMAT_R8_UNORM, "Mask must be R8_UNORM");
    }
    require(std::isfinite(f.jitterX) && std::isfinite(f.jitterY) && std::abs(f.jitterX) <= 1 &&
                std::abs(f.jitterY) <= 1,
            "Invalid applied jitter");
    require(std::isfinite(f.motionScaleX) && std::isfinite(f.motionScaleY) && f.motionScaleX != 0 &&
                f.motionScaleY != 0,
            "Invalid velocity units");
    require(std::isfinite(f.deltaMilliseconds) && f.deltaMilliseconds > 0,
            "Frame duration must be positive milliseconds");
    require(std::isfinite(f.preExposure) && f.preExposure > 0, "Pre-exposure must be positive");
    require(std::isfinite(f.nearPlane) && f.nearPlane > 0 &&
                (c.infiniteDepth || (std::isfinite(f.farPlane) && f.farPlane > f.nearPlane)),
            "Invalid clip planes");
    require(std::isfinite(f.verticalFovRadians) && f.verticalFovRadians > 0 &&
                f.verticalFovRadians < 3.14159265f,
            "Vertical field of view must be radians");
}
inline FfxApiResource resource(const Image &image, bool output = false) {
    if (!image.handle)
        return {};
    auto d = ffxApiGetImageResourceDescriptionVK(image.handle, image.description, 0);
    return ffxApiGetResourceVK(reinterpret_cast<void *>(image.handle), d,
                               output ? FFX_API_RESOURCE_STATE_UNORDERED_ACCESS
                                      : FFX_API_RESOURCE_STATE_COMPUTE_READ);
}
class Context {
    Api api_;
    ffxContext context_{};
    // All descriptors persist: ffx_api.h requires their lifetime through destruction.
    ffxCreateContextDescUpscale create_{};
    ffxCreateBackendVKDesc backend_{};
    ffxOverrideVersion override_{};
    Config config_{};
    uint64_t last_{};
    bool recorded_{};
    bool poisoned_{};

  public:
    explicit Context(Api api) : api_(api) {
        require(api.create && api.destroy && api.query && api.dispatch, "Incomplete runtime API");
    }
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
    ~Context() {
        if (context_)
            api_.destroy(&context_, nullptr);
    }
    void create(const Config &c, VkDeviceContext device) {
        require(!context_, "Destroy context after GPU completion before recreating");
        require(c.renderWidth && c.renderHeight && c.outputWidth && c.outputHeight &&
                    c.renderWidth <= c.outputWidth && c.renderHeight <= c.outputHeight,
                "Invalid context dimensions");
        require(device.vkDevice && device.vkPhysicalDevice && device.vkDeviceProcAddr,
                "Invalid Vulkan device context");
        config_ = c;
        backend_.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backend_.deviceContext = device;
        create_.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        create_.header.pNext = &backend_.header;
        create_.maxRenderSize = {c.renderWidth, c.renderHeight};
        create_.maxUpscaleSize = {c.outputWidth, c.outputHeight};
        create_.flags = (c.hdr ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : 0) |
                        (c.invertedDepth ? FFX_UPSCALE_ENABLE_DEPTH_INVERTED : 0) |
                        (c.infiniteDepth ? FFX_UPSCALE_ENABLE_DEPTH_INFINITE : 0) |
                        (c.autoExposure ? FFX_UPSCALE_ENABLE_AUTO_EXPOSURE : 0);
        if (c.providerId) {
            override_.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
            override_.versionId = c.providerId;
            backend_.header.pNext = &override_.header;
        }
        check(api_.create(&context_, &create_.header, nullptr), "CreateContext");
        require(context_ != nullptr, "Runtime returned a null context");
    }
    std::string provider() {
        require(context_ != nullptr, "Context is not created");
        ffxQueryGetProviderVersion q{};
        q.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        check(api_.query(&context_, &q.header), "GetProviderVersion");
        return q.versionName ? q.versionName : "unknown";
    }
    std::array<float, 2> jitter(uint64_t frame) {
        require(context_ != nullptr, "Context is not created");
        int32_t phases{};
        ffxQueryDescUpscaleGetJitterPhaseCount count{};
        count.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
        count.renderWidth = config_.renderWidth;
        count.displayWidth = config_.outputWidth;
        count.pOutPhaseCount = &phases;
        check(api_.query(&context_, &count.header), "GetJitterPhaseCount");
        require(phases > 0 && phases <= 4096, "Invalid SDK jitter phase count");
        std::array<float, 2> result{};
        ffxQueryDescUpscaleGetJitterOffset offset{};
        offset.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
        offset.index = int32_t(frame % uint64_t(phases));
        offset.phaseCount = phases;
        offset.pOutX = &result[0];
        offset.pOutY = &result[1];
        check(api_.query(&context_, &offset.header), "GetJitterOffset");
        require(std::isfinite(result[0]) && std::isfinite(result[1]) &&
                    std::abs(result[0]) <= .5f && std::abs(result[1]) <= .5f,
                "Invalid SDK jitter offset");
        return result;
    }
    void record(const Frame &f) {
        require(context_ && !poisoned_,
                "Context is unavailable or failed; recreate after GPU completion");
        validate(config_, f);
        require(!recorded_ || f.sequence > last_, "Duplicate or out-of-order source frame");
        ffxDispatchDescUpscale d{};
        d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        d.commandList = reinterpret_cast<void *>(f.commands);
        d.color = resource(f.color);
        d.depth = resource(f.depth);
        d.motionVectors = resource(f.motion);
        d.reactive = resource(f.reactive);
        d.transparencyAndComposition = resource(f.composition);
        d.output = resource(f.output, true);
        d.jitterOffset = {f.jitterX, f.jitterY};
        d.motionVectorScale = {f.motionScaleX, f.motionScaleY};
        d.renderSize = {config_.renderWidth, config_.renderHeight};
        d.upscaleSize = {config_.outputWidth, config_.outputHeight};
        d.frameTimeDelta = f.deltaMilliseconds;
        d.preExposure = f.preExposure;
        d.reset = f.reset || !recorded_ || f.sequence != last_ + 1;
        d.cameraNear = f.nearPlane;
        d.cameraFar = config_.infiniteDepth ? INFINITY : f.farPlane;
        d.cameraFovAngleVertical = f.verticalFovRadians;
        d.viewSpaceToMetersFactor = 1.0f;
        const auto rc = api_.dispatch(&context_, &d.header);
        if (rc != FFX_API_RETURN_OK)
            poisoned_ = true;
        check(rc, "Dispatch (discard partially recorded command buffer on failure)");
        last_ = f.sequence;
        recorded_ = true;
    }
};
} // namespace xpfsr

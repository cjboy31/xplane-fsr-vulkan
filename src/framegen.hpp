// SPDX-License-Identifier: MIT
#pragma once
#include "fsr24.hpp"
#include "framegen_callback_state.hpp"
#include <atomic>
#include <ffx_framegeneration.h>
#include <vk/ffx_api_framegeneration_vk.h>
#include "framegen_pixel_probe.hpp"
#include "framegen_encoded_color.hpp"

namespace xpfsr {
struct CameraBasis {
    float position[3]{}, up[3]{0, 1, 0}, right[3]{1, 0, 0}, forward[3]{0, 0, -1};
};

// The SDK owns interpolation, presentation queues and frame pacing. The
// application retains a distinct object for each Vulkan swapchain generation.
class Presentation {
    Api api_{};
    ffxContext context_{};
    ffxCreateContextDescFrameGenerationSwapChainVK create_{};
    ffxCreateContextDescFrameGenerationSwapChainVersionVK version_{};
    ffxCreateContextDescFrameGenerationSwapChainModeVK mode_{};
    ffxOverrideVersion override_{};
    VkSwapchainKHR swapchain_{};
    bool stopped_{};
    std::string label_ = "SDK automatic";
    VkSurfaceFullScreenExclusiveInfoEXT fullscreen_{
        VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT};
    VkSurfaceFullScreenExclusiveWin32InfoEXT monitor_{
        VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT};
    VkImageFormatListCreateInfo formats_{VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO};
    std::vector<VkFormat> viewFormats_;
    void captureInfo(const VkSwapchainCreateInfoKHR &info) {
        create_.createInfo = info;
        create_.createInfo.pNext = nullptr;
        for (auto *node = static_cast<const VkBaseInStructure *>(info.pNext); node;
             node = node->pNext) {
            if (node->sType == VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT) {
                fullscreen_ = *reinterpret_cast<const VkSurfaceFullScreenExclusiveInfoEXT *>(node);
                require(fullscreen_.fullScreenExclusive !=
                            VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT,
                        "Application-controlled fullscreen is not supported with FG");
                fullscreen_.pNext = const_cast<void *>(create_.createInfo.pNext);
                create_.createInfo.pNext = &fullscreen_;
            } else if (node->sType ==
                       VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT) {
                monitor_ =
                    *reinterpret_cast<const VkSurfaceFullScreenExclusiveWin32InfoEXT *>(node);
                monitor_.pNext = create_.createInfo.pNext;
                create_.createInfo.pNext = &monitor_;
            } else if (node->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO) {
                formats_ = *reinterpret_cast<const VkImageFormatListCreateInfo *>(node);
                viewFormats_.assign(formats_.pViewFormats,
                                    formats_.pViewFormats + formats_.viewFormatCount);
                formats_.pViewFormats = viewFormats_.data();
                formats_.pNext = create_.createInfo.pNext;
                create_.createInfo.pNext = &formats_;
            } else
                throw std::runtime_error("Unsupported FG swapchain creation extension");
        }
    }

  public:
    ffxQueryDescSwapchainReplacementFunctionsVK functions{};
    explicit Presentation(Api api) : api_(api) {}
    Presentation(const Presentation &) = delete;
    Presentation &operator=(const Presentation &) = delete;
    void create(VkDeviceContext device, const VkSwapchainCreateInfoKHR &info,
                const std::array<VkQueue, 4> &queues, uint32_t family,
                const std::string &provider = "") {
        require(!context_ && api_.configure, "Invalid frame generation runtime");
        if (!provider.empty())
            label_ = provider;
        require(info.imageArrayLayers == 1, "Frame generation swapchain needs a single view");
        for (size_t i = 0; i < queues.size(); ++i) {
            require(queues[i] != VK_NULL_HANDLE, "FG needs four reserved queues");
            for (size_t j = 0; j < i; ++j)
                require(queues[i] != queues[j], "FG queues must be distinct");
        }
        create_.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
        create_.physicalDevice = device.vkPhysicalDevice;
        create_.device = device.vkDevice;
        create_.swapchain = &swapchain_;
        captureInfo(info);
        create_.createInfo.oldSwapchain = VK_NULL_HANDLE;
        require(info.imageSharingMode == VK_SHARING_MODE_EXCLUSIVE,
                "FG preview requires exclusive image sharing");
        create_.createInfo.pQueueFamilyIndices = nullptr;
        create_.createInfo.queueFamilyIndexCount = 0;
        create_.gameQueue = {queues[0], family, nullptr};
        create_.asyncComputeQueue = {queues[1], family, nullptr};
        create_.presentQueue = {queues[2], family, nullptr};
        create_.imageAcquireQueue = {queues[3], family, nullptr};
        version_.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_VK;
        version_.version = FFX_FRAMEGENERATION_SWAPCHAIN_VK_VERSION;
        mode_.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_MODE_VK;
        mode_.composeOnPresentQueue = true;
        create_.header.pNext = &version_.header;
        version_.header.pNext = &mode_.header;
        if (!provider.empty()) {
            override_.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
            override_.versionId = providerVersion(api_, device, provider,
                                                  FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK);
            mode_.header.pNext = &override_.header;
        }
        check(api_.create(&context_, &create_.header, nullptr), "Create FG presentation context");
        require(context_ && swapchain_, "SDK returned no FG swapchain");
        functions.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
        check(api_.query(&context_, &functions.header), "Get FG swapchain functions");
        require(functions.pOutQueuePresentKHR && functions.pOutAcquireNextImageKHR &&
                    functions.pOutGetSwapchainImagesKHR && functions.pOutCreateSwapchainFFXAPI &&
                    functions.pOutDestroySwapchainFFXAPI && functions.pOutGetLastPresentCountFFXAPI,
                "Incomplete FG presentation function table");
    }
    VkSwapchainKHR handle() const { return swapchain_; }
    std::string provider() {
        ffxQueryGetProviderVersion q{};
        q.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        auto result = api_.query(&context_, &q.header);
        return result == FFX_API_RETURN_OK && q.versionName ? q.versionName : label_;
    }
    void wait() {
        if (!context_)
            return;
        ffxDispatchDescFrameGenerationSwapChainWaitForPresentsVK q{};
        q.header.type = FFX_API_DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK;
        check(api_.dispatch(&context_, &q.header), "Wait for generated presents");
    }
    VkResult recreate(const VkSwapchainCreateInfoKHR &info) {
        require(info.imageArrayLayers == 1 && info.imageSharingMode == VK_SHARING_MODE_EXCLUSIVE,
                "Unsupported FG resize");
        wait();
        captureInfo(info);
        create_.createInfo.oldSwapchain = swapchain_;
        return functions.pOutCreateSwapchainFFXAPI(create_.device, &create_.createInfo, nullptr,
                                                   &swapchain_,
                                                   reinterpret_cast<void *>(swapchain_));
    }
    uint64_t displayed() const {
        return swapchain_ && functions.pOutGetLastPresentCountFFXAPI
                   ? functions.pOutGetLastPresentCountFFXAPI(swapchain_)
                   : 0;
    }
    void close() {
        if (stopped_)
            return;
        wait();
        auto old = swapchain_;
        if (context_)
            check(api_.destroy(&context_, nullptr), "Destroy FG presentation context");
        if (old && functions.pOutDestroySwapchainFFXAPI)
            functions.pOutDestroySwapchainFFXAPI(create_.device, old, nullptr,
                                                 reinterpret_cast<void *>(old));
        swapchain_ = VK_NULL_HANDLE;
        stopped_ = true;
    }
    ~Presentation() {
        try {
            close();
        } catch (...) {
        }
    }
};

class FrameGenerator {
    Api api_{};
    ffxContext context_{};
    ffxCreateContextDescFrameGeneration create_{};
    ffxCreateContextDescFrameGenerationVersion version_{};
    ffxCreateBackendVKDesc backend_{};
    ffxOverrideVersion override_{};
    ffxConfigureDescFrameGeneration configure_{};
    uint32_t width_{}, height_{};
    uint64_t prepared_{};
    bool reset_{true}, configured_{}, enabled_{}, async_{};
    std::atomic<bool> resetAfterSkip_{true};
    FrameCallbackState callbackFrames_;
    std::unique_ptr<FramePixelProbe> pixelProbe_;
    std::unique_ptr<FrameEncodedColor> encodedColor_;
    static ffxReturnCode_t generate(ffxDispatchDescFrameGeneration *params, void *pointer) try {
        auto &self = *static_cast<FrameGenerator *>(pointer);
        bool reset=true;
        if (!self.callbackFrames_.consume(params->frameID,reset)) {
            // The SDK presenter checks numGeneratedFrames even when a callback
            // fails. Never advertise an unwritten interpolation surface. Zero
            // leaves the complete real backbuffer visible (hold/repeat), using
            // the SDK's own acquire/semaphore/present ownership.
            params->numGeneratedFrames=0;params->reset=true;
            self.resetAfterSkip_=true;++self.skipped;
            return FFX_API_RETURN_OK;
        }
        params->reset = params->reset || reset;
        if(params->reset)++self.resetDispatches;
        auto dispatch=*params;
        if(self.encodedColor_)self.encodedColor_->before(*params,dispatch);
        auto result = self.api_.dispatch(&self.context_, &dispatch.header);
        params->numGeneratedFrames=dispatch.numGeneratedFrames;
        if(result==FFX_API_RETURN_OK && params->numGeneratedFrames && self.encodedColor_)self.encodedColor_->after(*params);
        if (result == FFX_API_RETURN_OK) {
            // A valid reset dispatch primes the SDK's history. Its defined
            // reset output must complete so the presenter can leave reset mode.
            self.generated.fetch_add(params->numGeneratedFrames);
        } else {
            params->numGeneratedFrames=0;self.resetAfterSkip_=true;++self.skipped;
            self.failure.store(result);
        }
#ifdef XPFSR_FG_TEST_READBACK
        if (result == FFX_API_RETURN_OK && params->numGeneratedFrames && self.inspect)
            self.inspect(*params, self.inspectContext);
#endif
        if (result == FFX_API_RETURN_OK && params->numGeneratedFrames && self.pixelProbe_)
            self.pixelProbe_->record(*params);
        return result;
    }catch(...){
        params->numGeneratedFrames=0;
        auto &self=*static_cast<FrameGenerator*>(pointer);self.failure=FFX_API_RETURN_ERROR_RUNTIME_ERROR;
        self.resetAfterSkip_=true;++self.skipped;
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }

  public:
    std::atomic<uint64_t> generated{};
    std::atomic<uint64_t> skipped{};
    std::atomic<uint32_t> failure{};
    std::atomic<uint64_t> resetDispatches{};
    void enablePixelProbe(const VkPhysicalDeviceMemoryProperties &memory) {
        require(context_ && !pixelProbe_, "FG pixel probe needs an initialized context");
        pixelProbe_=std::make_unique<FramePixelProbe>(backend_.deviceContext,memory);
    }
    FramePixelProbe *pixelProbe() {return pixelProbe_.get();}
    bool encodedColorActive() const {return bool(encodedColor_);}
#ifdef XPFSR_FG_TEST_READBACK
    bool unadaptedSrgbForTest{};
    void (*inspect)(const ffxDispatchDescFrameGeneration &, void *){};
    void *inspectContext{};
    void discardPreparedForTest(uint64_t frame){bool reset{};callbackFrames_.consume(frame,reset);}
#endif
    explicit FrameGenerator(Api api) : api_(api) {}
    FrameGenerator(const FrameGenerator &) = delete;
    FrameGenerator &operator=(const FrameGenerator &) = delete;
    void create(VkDeviceContext device, uint32_t rw, uint32_t rh, uint32_t ow, uint32_t oh,
                VkFormat format, bool reverse, bool infinite, const std::string &provider,
                bool asyncWorkloads = false,const VkPhysicalDeviceMemoryProperties *memory = nullptr) {
        require(!context_ && api_.configure && rw && rh && ow && oh,
                "Invalid FG context parameters");
        width_ = ow;
        height_ = oh;
        async_=asyncWorkloads;
        create_.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        create_.maxRenderSize = {rw, rh};
        create_.displaySize = {ow, oh};
        bool adaptColor=encodedColorFormat(format)!=format;
#ifdef XPFSR_FG_TEST_READBACK
        adaptColor &= !unadaptedSrgbForTest;
#endif
        require(!adaptColor || memory,"sRGB FG requires encoded-color adapter memory properties");
        create_.backBufferFormat = ffxApiGetSurfaceFormatVK(adaptColor?encodedColorFormat(format):format);
        require(create_.backBufferFormat != FFX_API_SURFACE_FORMAT_UNKNOWN,
                "FG backbuffer format unsupported");
        create_.flags = (reverse ? FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED : 0) |
                        (infinite ? FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE : 0);
        if(async_)create_.flags|=FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
        if (format == VK_FORMAT_R16G16B16A16_SFLOAT || format == VK_FORMAT_A2B10G10R10_UNORM_PACK32)
            create_.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;
        version_.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
        version_.version = FFX_FRAMEGENERATION_VERSION;
        backend_.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backend_.deviceContext = device;
        create_.header.pNext = &version_.header;
        version_.header.pNext = &backend_.header;
        if (!provider.empty()) {
            override_.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
            override_.versionId = providerVersion(api_, device, provider,
                                                  FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION);
            backend_.header.pNext = &override_.header;
        }
        check(api_.create(&context_, &create_.header, nullptr), "Create frame generator");
        if(adaptColor)encodedColor_=std::make_unique<FrameEncodedColor>(device,encodedColorFormat(format),ow,oh,*memory);
    }
    std::string provider() {
        ffxQueryGetProviderVersion q{};
        q.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        check(api_.query(&context_, &q.header), "FG provider");
        return q.versionName ? q.versionName : "unknown";
    }
    void configure(VkSwapchainKHR swapchain, uint64_t frame, bool enabled) {
        if (!enabled && configured_ && !enabled_)
            return;
        if(configured_ && enabled_==enabled && configure_.frameID==frame)
            return;
        configure_ = {};
        configure_.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        configure_.swapChain = reinterpret_cast<void *>(swapchain);
        configure_.frameID = frame;
        configure_.frameGenerationEnabled = enabled;
        configure_.allowAsyncWorkloads = async_;
        configure_.onlyPresentGenerated = false;
        configure_.generationRect = {0, 0, int32_t(width_), int32_t(height_)};
        if (enabled) {
            configure_.frameGenerationCallback = generate;
            configure_.frameGenerationCallbackUserContext = this;
        }
        check(api_.configure(&context_, &configure_.header), "Configure frame generation");
        configured_ = true;
        enabled_ = enabled;
    }
    void prepare(VkSwapchainKHR swapchain, uint64_t frame, const Frame &input,
                 const CameraBasis &camera) {
        require(context_ && input.commands && input.depth.handle && input.motion.handle,
                "Missing FG prepare inputs");
        const bool resetAfterSkip=resetAfterSkip_.exchange(false);
        reset_ = input.reset || resetAfterSkip || !prepared_ || frame != prepared_ + 1;
        // Configure this frame before PrepareV2, as required by the SDK. The
        // layer still rechecks submitted source identity immediately before
        // Present and disables FG there if this recorded prepare was not used.
        configure(swapchain, frame, true);
        ffxDispatchDescFrameGenerationPrepareV2 p{};
        p.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
        p.commandList = reinterpret_cast<void *>(input.commands);
        p.frameID = frame;
        p.renderSize = {input.depth.description.extent.width,
                        input.depth.description.extent.height};
        p.depth = resource(input.depth);
        p.motionVectors = resource(input.motion);
        p.jitterOffset = {input.jitterX, input.jitterY};
        p.motionVectorScale = {input.motionScaleX, input.motionScaleY};
        p.frameTimeDelta = input.deltaMilliseconds;
        p.reset = reset_;
        p.cameraNear = input.nearPlane;
        p.cameraFar = input.farPlane;
        p.cameraFovAngleVertical = input.verticalFovRadians;
        p.viewSpaceToMetersFactor = 1;
        memcpy(p.cameraPosition, camera.position, 12);
        memcpy(p.cameraUp, camera.up, 12);
        memcpy(p.cameraRight, camera.right, 12);
        memcpy(p.cameraForward, camera.forward, 12);
        check(api_.dispatch(&context_, &p.header), "Prepare frame generation V2");
        callbackFrames_.prepared(frame,reset_);
        prepared_ = frame;
    }
    ~FrameGenerator() {
        if (context_)
            api_.destroy(&context_, nullptr);
    }
};
} // namespace xpfsr

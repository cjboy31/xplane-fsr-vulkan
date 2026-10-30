// SPDX-License-Identifier: MIT
#define VK_USE_PLATFORM_WIN32_KHR
#define wmain unusedBaselineMain
#include "probe_support.hpp"
#undef wmain
#define XPFSR_FG_TEST_READBACK
#include "framegen.hpp"
#include <algorithm>
#include <fstream>
struct Staging {
    Vulkan &vk;
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    void *mapped{};
    explicit Staging(Vulkan &v, size_t bytes) : vk(v) {
        VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        b.size = bytes;
        b.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkcheck(vk.dev<PFN_vkCreateBuffer>("vkCreateBuffer")(vk.device, &b, nullptr, &buffer),
                "FG staging buffer");
        VkMemoryRequirements r{};
        vk.dev<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(vk.device,
                                                                                   buffer, &r);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        a.allocationSize = r.size;
        a.memoryTypeIndex =
            vk.memoryType(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkcheck(vk.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(vk.device, &a, nullptr, &memory),
                "FG staging allocation");
        vkcheck(vk.dev<PFN_vkBindBufferMemory>("vkBindBufferMemory")(vk.device, buffer, memory, 0),
                "FG staging bind");
        vkcheck(vk.dev<PFN_vkMapMemory>("vkMapMemory")(vk.device, memory, 0, bytes, 0, &mapped),
                "FG staging map");
    }
    ~Staging() {
        if (mapped)
            vk.dev<PFN_vkUnmapMemory>("vkUnmapMemory")(vk.device, memory);
        if (buffer)
            vk.dev<PFN_vkDestroyBuffer>("vkDestroyBuffer")(vk.device, buffer, nullptr);
        if (memory)
            vk.dev<PFN_vkFreeMemory>("vkFreeMemory")(vk.device, memory, nullptr);
    }
};
struct Observation {
    Vulkan *vk{};
    VkBuffer buffer{};
    VkExtent2D size{};
    bool copied{};
    uint64_t firstCheckedFrame{3};
};
static void inspectGenerated(const ffxDispatchDescFrameGeneration &params, void *pointer) {
    auto &target = *static_cast<Observation *>(pointer);
    auto &vk = *target.vk;
    if (params.frameID < target.firstCheckedFrame || !params.numGeneratedFrames ||
        !params.outputs[0].resource)
        return;
    auto cb = static_cast<VkCommandBuffer>(params.commandList);
    auto image = reinterpret_cast<VkImage>(params.outputs[0].resource);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.image = image;
    b.oldLayout = params.outputs[0].state == FFX_API_RESOURCE_STATE_UNORDERED_ACCESS
                      ? VK_IMAGE_LAYOUT_GENERAL
                      : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto barrier = vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");
    barrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
            0, nullptr, 1, &b);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {target.size.width, target.size.height, 1};
    vk.dev<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(
        cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.buffer, 1, &copy);
    auto original = b.oldLayout;
    b.oldLayout = b.newLayout;
    b.newLayout = original;
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0,
            nullptr, 1, &b);
    target.copied = true;
}
static float edge(float x, float y, float frame) {
    return x > 50 + frame * 3 + .28f * y ? 210.f / 255 : 31.f / 255;
}
static void ppm(const std::filesystem::path &file, const std::vector<float> &values,
                VkExtent2D size) {
    std::ofstream out(file, std::ios::binary);
    out << "P6\n" << size.width << ' ' << size.height << "\n255\n";
    for (float value : values) {
        auto byte = uint8_t(std::round(std::clamp(value, 0.f, 1.f) * 255));
        for (int channel = 0; channel < 3; ++channel)
            out.write(reinterpret_cast<const char *>(&byte), 1);
    }
}
static LRESULT CALLBACK windowProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    return DefWindowProcW(w, m, a, b);
}
int wmain(int argc, wchar_t **argv) {
    std::cout.setf(std::ios::unitbuf);
    SetEnvironmentVariableW(L"DISABLE_VULKAN_OBS_CAPTURE", L"1");
    try {
        require(argc >= 3, "FrameGenCheck RUNTIME_DIR RESULT_DIR [3.1.6|4.0.2]");
        std::filesystem::path directory = argv[2];
        std::filesystem::create_directories(directory);
        std::string wanted = argc > 3 && std::wstring(argv[3]) == L"4.0.2" ? "4.0.2" : "3.1.6";
        Vulkan vk;
        vk.open(true);
        Runtime runtime;
        runtime.load(argv[1], true);
        VkDeviceContext backend{vk.device, vk.physical, vk.gdpa};
        for (auto type : {uint64_t(FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION),
                          uint64_t(FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK)})
            for (auto &item : versions(runtime.api, backend, type))
                std::cout << "FG provider type=" << type << " name=" << item.name << '\n';
        WNDCLASSW wc{};
        wc.lpfnWndProc = windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"XPFSR24FrameGenCheck";
        RegisterClassW(&wc);
        HWND window = CreateWindowW(wc.lpszClassName, L"FSR Vulkan frame generation check",
                                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 980, 590,
                                    nullptr, nullptr, wc.hInstance, nullptr);
        require(window != nullptr, "FG window creation failed");
        ShowWindow(window, SW_SHOW);
        VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        surfaceInfo.hinstance = wc.hInstance;
        surfaceInfo.hwnd = window;
        VkSurfaceKHR surface{};
        vkcheck(vk.inst<PFN_vkCreateWin32SurfaceKHR>("vkCreateWin32SurfaceKHR")(
                    vk.instance, &surfaceInfo, nullptr, &surface),
                "FG surface");
        VkBool32 supported{};
        vkcheck(vk.inst<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
                    "vkGetPhysicalDeviceSurfaceSupportKHR")(vk.physical, vk.family, surface,
                                                            &supported),
                "FG surface support");
        require(supported, "Graphics family cannot present");
        VkSurfaceCapabilitiesKHR caps{};
        vkcheck(vk.inst<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
                    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")(vk.physical, surface, &caps),
                "FG surface caps");
        uint32_t n = 0;
        auto formatsFn = vk.inst<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
        formatsFn(vk.physical, surface, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(n);
        formatsFn(vk.physical, surface, &n, formats.data());
        auto format = formats.front();
        auto option=[&](const wchar_t*name){for(int i=4;i<argc;++i)if(std::wstring(argv[i])==name)return true;return false;};
        const bool srgb=option(L"--srgb"),injectGaps=option(L"--inject-gaps"),async=option(L"--async"),benchmark=option(L"--benchmark");
        for (auto value : formats)
            if (value.format == (srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM) &&
                value.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                format = value;
        require(!srgb || format.format == VK_FORMAT_B8G8R8A8_SRGB,
                "Requested sRGB test format unavailable");
        VkExtent2D size = caps.currentExtent;
        if (size.width == UINT32_MAX)
            size = {960, 540};
        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface = surface;
        ci.minImageCount = std::max(2u, caps.minImageCount);
        if (caps.maxImageCount)
            ci.minImageCount = std::min(ci.minImageCount, caps.maxImageCount);
        ci.imageFormat = format.format;
        ci.imageColorSpace = format.colorSpace;
        ci.imageExtent = size;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if(option(L"--immediate")){
            auto getModes=vk.inst<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>("vkGetPhysicalDeviceSurfacePresentModesKHR");
            uint32_t count{};vkcheck(getModes(vk.physical,surface,&count,nullptr),"FG present modes count");std::vector<VkPresentModeKHR> modes(count);
            vkcheck(getModes(vk.physical,surface,&count,modes.data()),"FG present modes");
            require(std::find(modes.begin(),modes.end(),VK_PRESENT_MODE_IMMEDIATE_KHR)!=modes.end(),"Immediate test presentation unavailable");
            ci.presentMode=VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
        ci.clipped = VK_TRUE;
        std::array<VkQueue, 4> queues{};
        for (uint32_t i = 0; i < 4; ++i)
            vk.dev<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(vk.device, vk.family, i, &queues[i]);
        {
            Presentation presentation(runtime.api);
            presentation.create(backend, ci, queues, vk.family,
                                option(L"--presenter-3.1.7")?std::string("3.1.7"):std::string{});
            std::cout << "Swapchain provider=" << presentation.provider()
                      << " format=" << format.format << " size=" << size.width << 'x' << size.height
                      << '\n';
            FrameGenerator generator(runtime.api);
            generator.unadaptedSrgbForTest=option(L"--unadapted-srgb");
            generator.create(backend, size.width, size.height, size.width, size.height,
                             format.format, true, true, wanted,async,&vk.memory);
            if(option(L"--diagnostics"))generator.enablePixelProbe(vk.memory);
            std::cout << "Frame generator=" << generator.provider() << "; async="<<async<<"; benchmark="<<benchmark<<'\n';
            GpuImage depth(vk), motion(vk);
            depth.create(size.width, size.height, VK_FORMAT_R32_SFLOAT);
            motion.create(size.width, size.height, VK_FORMAT_R16G16_SFLOAT);
            n = 0;
            vkcheck(presentation.functions.pOutGetSwapchainImagesKHR(
                        vk.device, presentation.handle(), &n, nullptr),
                    "FG images count");
            std::vector<VkImage> images(n);
            vkcheck(presentation.functions.pOutGetSwapchainImagesKHR(
                        vk.device, presentation.handle(), &n, images.data()),
                    "FG images");
            std::vector<bool> used(n);
            VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            poolInfo.queueFamilyIndex = vk.family;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            VkCommandPool pool{};
            vkcheck(vk.dev<PFN_vkCreateCommandPool>("vkCreateCommandPool")(vk.device, &poolInfo,
                                                                           nullptr, &pool),
                    "FG pool");
            VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            alloc.commandPool = pool;
            alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 1;
            VkCommandBuffer cb{};
            vkcheck(vk.dev<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(vk.device,
                                                                                     &alloc, &cb),
                    "FG commands");
            VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            VkFence fence{};
            vkcheck(
                vk.dev<PFN_vkCreateFence>("vkCreateFence")(vk.device, &fenceInfo, nullptr, &fence),
                "FG fence");
            VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VkSemaphore acquired{};
            vkcheck(vk.dev<PFN_vkCreateSemaphore>("vkCreateSemaphore")(vk.device, &semInfo, nullptr,
                                                                       &acquired),
                    "FG acquire semaphore");
            std::vector<VkSemaphore> ready(n);
            for (auto &s : ready)
                vkcheck(vk.dev<PFN_vkCreateSemaphore>("vkCreateSemaphore")(vk.device, &semInfo,
                                                                           nullptr, &s),
                        "FG present semaphore");
            Staging staging(vk, size_t(size.width) * size.height * 4);
            Observation observation{&vk, staging.buffer, size};
            constexpr uint64_t sourceBase = 10000;
            observation.firstCheckedFrame = sourceBase + (injectGaps?1:3);
            generator.inspect = benchmark?nullptr:inspectGenerated;
            generator.inspectContext = &observation;
            const uint64_t frames = benchmark?240:90;
            LARGE_INTEGER benchmarkStart{},benchmarkEnd{},benchmarkFrequency{};QueryPerformanceFrequency(&benchmarkFrequency);QueryPerformanceCounter(&benchmarkStart);
            unsigned nonBlackFrames = 0;
            for (uint64_t frame = 1; frame <= frames; ++frame) {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                vkcheck(vk.dev<PFN_vkWaitForFences>("vkWaitForFences")(vk.device, 1, &fence,
                                                                       VK_TRUE, 10000000000ull),
                        "FG previous frame");
                uint32_t index{};
                auto result = presentation.functions.pOutAcquireNextImageKHR(
                    vk.device, presentation.handle(), 10000000000ull, acquired, VK_NULL_HANDLE,
                    &index);
                require(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR, "FG acquire failed");
                auto *upload = static_cast<uint8_t *>(staging.mapped);
                for (uint32_t y = 0; y < size.height; ++y)
                    for (uint32_t x = 0; x < size.width; ++x) {
                        auto offset = (size_t(y) * size.width + x) * 4;
                        auto value =
                            uint8_t(std::round(edge(x + .5f, y + .5f, float(frame)) * 255));
                        upload[offset] = upload[offset + 1] = upload[offset + 2] = value;
                        upload[offset + 3] = 255;
                    }
                vkcheck(vk.dev<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(cb, 0),
                        "FG reset");
                VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkcheck(vk.dev<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb, &begin),
                        "FG begin");
                VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                barrier.image = images[index];
                barrier.oldLayout = used[index] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                : VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
                    cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                    nullptr, 0, nullptr, 1, &barrier);
                VkBufferImageCopy uploadRegion{};
                uploadRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                uploadRegion.imageExtent = {size.width, size.height, 1};
                vk.dev<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(
                    cb, staging.buffer, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                    &uploadRegion);
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
                    cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                    nullptr, 0, nullptr, 1, &barrier);
                used[index] = true;
                observation.copied = false;
                VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                for (auto *image : {&depth, &motion}) {
                    image->transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                    VkClearColorValue value{};
                    if (image == &depth)
                        value.float32[0] = .5f;
                    else
                        value.float32[0] = -3.f;
                    vk.dev<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(
                        cb, image->value.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1,
                        &range);
                    image->transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
                Frame input{};
                input.commands = cb;
                input.depth = depth.value;
                input.motion = motion.value;
                input.motionScaleX = input.motionScaleY = 1;
                input.deltaMilliseconds = 33.333f;
                input.nearPlane = .1f;
                input.farPlane = INFINITY;
                input.verticalFovRadians = 1.0472f;
                input.reset = frame == 1;
                CameraBasis camera{};
                generator.prepare(presentation.handle(), sourceBase + frame, input, camera);
                if(injectGaps&&(frame==15||frame==35||frame==55))generator.discardPreparedForTest(sourceBase+frame);
                vkcheck(vk.dev<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb), "FG end");
                vkcheck(vk.dev<PFN_vkResetFences>("vkResetFences")(vk.device, 1, &fence),
                        "FG reset fence");
                VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submit.waitSemaphoreCount = 1;
                submit.pWaitSemaphores = &acquired;
                submit.pWaitDstStageMask = &stage;
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &cb;
                submit.signalSemaphoreCount = 1;
                submit.pSignalSemaphores = &ready[index];
                vkcheck(vk.dev<PFN_vkQueueSubmit>("vkQueueSubmit")(queues[0], 1, &submit, fence),
                        "FG submit");
                auto swapchain = presentation.handle();
                VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
                present.waitSemaphoreCount = 1;
                present.pWaitSemaphores = &ready[index];
                present.swapchainCount = 1;
                present.pSwapchains = &swapchain;
                present.pImageIndices = &index;
                result = presentation.functions.pOutQueuePresentKHR(queues[0], &present);
                require(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR, "FG present failed");
                if(option(L"--synchronized"))presentation.wait();
                require(!generator.failure.load(), "FG callback failed");
                if(!benchmark){presentation.wait();vkcheck(vk.waitIdle(vk.device), "Generated-frame readback completion");}
                if (observation.copied) {
                    auto *pixels = static_cast<uint8_t *>(staging.mapped);
                    double sum = 0;
                    unsigned dark = 0;
                    for (size_t i = 0; i < size_t(size.width) * size.height; ++i) {
                        unsigned rgb =
                            unsigned(pixels[i * 4]) + pixels[i * 4 + 1] + pixels[i * 4 + 2];
                        sum += rgb / (3.0 * 255);
                        // sRGB sources can be linearized in the intermediate
                        // generation surface. Count near-zero pixels, not valid
                        // dark linear-light background samples, as black.
                        dark += rgb < 3;
                    }
                    if (frame == 3)
                        std::cout << "Generated readback: mean=" << sum / (size.width * size.height)
                                  << " zero-fraction=" << double(dark) / (size.width * size.height)
                                  << '\n';
                    require(sum / (size.width * size.height) > .05 &&
                                double(dark) / (size.width * size.height) < .05,
                            "Generated frame contains an unexpected black flash/region");
                    // The SDK no-UI presenter copies bytes, so these must be
                    // display-encoded values just like the real backbuffer.
                    // A linear-light reference here hid alternating dim frames.
                    double low=0,high=0;
                    for(uint32_t y=0;y<size.height;++y){low+=pixels[(size_t(y)*size.width+2)*4];high+=pixels[(size_t(y)*size.width+size.width-3)*4];}
                    low/=size.height;high/=size.height;
                    if(frame==3 || std::abs(low-31)>=4 || std::abs(high-210)>=4)std::cout<<"Encoded output levels: dark="<<low<<" expected=31 bright="<<high<<" expected=210 adapter="<<generator.encodedColorActive()<<'\n';
                    require(std::abs(low-31)<4 && std::abs(high-210)<4,"Generated frame changed display encoding/brightness versus the real frame");
                    ++nonBlackFrames;
                }
                Sleep(8);
            }
            generator.configure(presentation.handle(), sourceBase + frames + 1, false);
            presentation.wait();
            QueryPerformanceCounter(&benchmarkEnd);
            if(!benchmark)require(nonBlackFrames >= frames - 3,
                    "Insufficient generated frames were checked for black flashes");
            std::cout << "PASS: " << nonBlackFrames
                      << " generated-frame readbacks without black flashes; format="
                      << format.format << '\n';
            vkcheck(vk.waitIdle(vk.device), "FG idle");
            if(auto *probe=generator.pixelProbe()){
                probe->poll();auto &s=probe->stats;
                std::cout<<"FG pixel probe: checked="<<s.checked<<" generated-blank="<<s.generatedBlank<<" both-dark="<<s.bothDark<<" busy="<<s.busy<<" unsupported="<<s.unsupported<<" errors="<<s.errors<<'\n';
                require(s.checked>0 && !s.generatedBlank && !s.unsupported && !s.errors,"FG pixel probe found unexpected blanks or incompatible SDK resources");
            }
            auto displayed = presentation.displayed();
            std::cout << "Rendered=" << frames
                      << " generated dispatch outputs=" << generator.generated.load()
                      << " SDK actual presents=" << displayed << '\n';
            require(generator.generated.load() >= frames - (injectGaps?3:2), "No generated frames");
            require(displayed >= frames * 2 - 4, "No additional SDK presents");
            if(injectGaps)require(generator.skipped.load()>=3,"Missing-source callbacks did not hold real frames and reset history");
            std::cout<<"Safe real-frame holds="<<generator.skipped.load()<<"; missing-source injection="<<injectGaps<<'\n';
            if(benchmark){
                const double seconds=double(benchmarkEnd.QuadPart-benchmarkStart.QuadPart)/benchmarkFrequency.QuadPart;
                std::ofstream timing(directory/L"async-benchmark.json");
                timing<<"{\"syntheticOnly\":true,\"async\":"<<(async?"true":"false")<<",\"frames\":"<<frames<<",\"seconds\":"<<seconds<<",\"sourceThroughput\":"<<frames/seconds<<",\"sdkPresents\":"<<displayed<<",\"safeHolds\":"<<generator.skipped.load()<<",\"presentMode\":\"FIFO\",\"perFrameWaitIdle\":false}";
                std::cout<<"Synthetic source throughput="<<frames/seconds<<"/s, async="<<async<<"; FIFO/CPU-upload fixture, not simulator FPS or scanout timing\n";
            }else{
            require(observation.copied, "No generated-image readback was recorded");
            std::vector<float> generated(size_t(size.width) * size.height), raw(generated.size()),
                reference(generated.size());
            auto *downloaded = static_cast<const uint8_t *>(staging.mapped);
            auto referenceColor = [](float value) {return value;};
            double generatedError = 0, rawError = 0;
            uint64_t samples = 0;
            for (uint32_t y = 0; y < size.height; ++y)
                for (uint32_t x = 0; x < size.width; ++x) {
                    auto i = size_t(y) * size.width + x;
                    generated[i] = downloaded[i * 4] / 255.f;
                    raw[i] = referenceColor(edge(x + .5f, y + .5f, 90));
                    float value = 0;
                    for (int k = 0; k < 16; ++k)
                        value += referenceColor(edge(x + (k + .5f) / 16, y + .5f, 89.5f)) / 16;
                    reference[i] = value;
                    if (y > 16 && y < size.height - 16 &&
                        std::abs(float(x) + .5f - (50 + 89.5f * 3 + .28f * (y + .5f))) < 7) {
                        rawError += std::abs(raw[i] - value);
                        generatedError += std::abs(generated[i] - value);
                        ++samples;
                    }
                }
            rawError /= samples;
            generatedError /= samples;
            double improvement = (1 - generatedError / rawError) * 100;
            ppm(directory / L"generated.ppm", generated, size);
            ppm(directory / L"current-rendered.ppm", raw, size);
            ppm(directory / L"midpoint-reference.ppm", reference, size);
            std::cout << "Moving-edge error: current=" << rawError
                      << " generated=" << generatedError << " improvement=" << improvement << "%\n";
            std::ofstream metrics(directory / L"metrics.json");
            metrics << "{\"provider\":\"" << generator.provider() << "\",\"swapchain_provider\":\""
                    << presentation.provider() << "\",\"rendered\":" << frames
                    << ",\"generated_dispatch_outputs\":" << generator.generated.load()
                    << ",\"non_black_generated_frames\":" << nonBlackFrames
                    << ",\"sdk_actual_presents\":" << displayed
                    << ",\"raw_midpoint_error\":" << rawError
                    << ",\"generated_midpoint_error\":" << generatedError
                    << ",\"midpoint_error_improvement_percent\":" << improvement << "}\n";
            require(generatedError < rawError * .95,
                    "Generated frame did not improve the moving-edge midpoint fixture");
            }
            for (auto s : ready)
                vk.dev<PFN_vkDestroySemaphore>("vkDestroySemaphore")(vk.device, s, nullptr);
            vk.dev<PFN_vkDestroySemaphore>("vkDestroySemaphore")(vk.device, acquired, nullptr);
            vk.dev<PFN_vkDestroyFence>("vkDestroyFence")(vk.device, fence, nullptr);
            vk.dev<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(vk.device, pool, nullptr);
        }
        vk.inst<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR")(vk.instance, surface, nullptr);
        DestroyWindow(window);
        std::cout << "PASS: SDK frame generation with replacement swapchain and additional paced "
                     "presents.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

// SPDX-License-Identifier: MIT
#define VK_USE_PLATFORM_WIN32_KHR
#define wmain unusedBaselineMain
#include "probe_support.hpp"
#undef wmain
#include "framegen.hpp"
#include "test_layer_identity.hpp"
#include <fstream>
static LRESULT CALLBACK windowProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    return DefWindowProcW(w, m, a, b);
}
int wmain(int argc, wchar_t **argv) {
    std::cout.setf(std::ios::unitbuf);
    try {
        require(argc >= 3, "FrameGenCheck RUNTIME_DIR RESULT_DIR [3.1.6|4.0.2]");
        std::filesystem::path directory = argv[2];
        std::filesystem::create_directories(directory);
        std::string wanted = argc > 3 && std::wstring(argv[3]) == L"4.0.2" ? "4.0.2" : "3.1.6";
        const bool captureTest=argc>3&&std::wstring(argv[3])==L"--capture-hook";
        if(captureTest)SetEnvironmentVariableW(L"DISABLE_VULKAN_OBS_CAPTURE",nullptr);
        Vulkan vk;
        vk.open(true, 1);
        const bool captureLoaded=GetModuleHandleW(L"graphics-hook64.dll")!=nullptr;
        std::cout<<"Capture hook loaded="<<captureLoaded<<'\n';
        require(!captureTest||captureLoaded,"Capture lifecycle test did not load the installed OBS layer");
        fixture::verifyLayerIdentity();
        if(argc>3&&std::wstring(argv[3])==L"--identity-only")return 0;
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
        for (auto value : formats)
            if (value.format == VK_FORMAT_B8G8R8A8_SRGB &&
                value.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                format = value;
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
        ci.clipped = VK_TRUE;
        std::array<VkQueue, 4> queues{};
        queues[0] = vk.queue;
        {
            struct NativePresentation {
                Vulkan &vk;
                VkSwapchainKHR chain{};
                ffxQueryDescSwapchainReplacementFunctionsVK functions{};
                NativePresentation(Vulkan &v, const VkSwapchainCreateInfoKHR &ci) : vk(v) {
                    std::cout << "Calling native-shaped CreateSwapchain\n";
                    vkcheck(vk.dev<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR")(
                                vk.device, &ci, nullptr, &chain),
                            "Create native-shaped swapchain");
                    functions.pOutAcquireNextImageKHR =
                        vk.dev<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR");
                    functions.pOutGetSwapchainImagesKHR =
                        vk.dev<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR");
                    functions.pOutQueuePresentKHR =
                        vk.dev<PFN_vkQueuePresentKHR>("vkQueuePresentKHR");
                }
                VkSwapchainKHR handle() { return chain; }
                ~NativePresentation() {
                    vk.dev<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(vk.device, chain,
                                                                               nullptr);
                }
            } presentation(vk, ci);
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
            constexpr uint64_t frames = 90;
            for (uint64_t frame = 1; frame <= frames; ++frame) {
                if (frame == 45) {
                    vkcheck(vk.waitIdle(vk.device), "Idle before swapchain recreation");
                    auto old = presentation.chain;
                    auto next = ci;
                    next.oldSwapchain = old;
                    VkSwapchainKHR replacement{};
                    vkcheck(vk.dev<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR")(
                                vk.device, &next, nullptr, &replacement),
                            "Recreate native-shaped swapchain");
                    require(old != replacement, "App-facing generations reused the same handle");
                    vk.dev<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(vk.device, old,
                                                                               nullptr);
                    presentation.chain = replacement;
                    uint32_t updated = 0;
                    vkcheck(presentation.functions.pOutGetSwapchainImagesKHR(vk.device, replacement,
                                                                             &updated, nullptr),
                            "Resized images count");
                    require(updated == images.size(), "Fixture image count changed");
                    vkcheck(presentation.functions.pOutGetSwapchainImagesKHR(
                                vk.device, replacement, &updated, images.data()),
                            "Resized images");
                    used.assign(updated, false);
                    std::cout << "PASS: recreated swapchain and retired old handle\n";
                }
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
                vkcheck(vk.dev<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(cb, 0),
                        "FG reset");
                VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkcheck(vk.dev<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb, &begin),
                        "FG begin");
                VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                barrier.image = images[index];
                barrier.oldLayout =
                    used[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
                    cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                    nullptr, 0, nullptr, 1, &barrier);
                VkClearColorValue color{{float(frame % 30) / 30.f, .15f, .45f, 1}};
                vk.dev<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(
                    cb, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1,
                    &barrier.subresourceRange);
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
                    cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                    nullptr, 0, nullptr, 1, &barrier);
                used[index] = true;
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
                if (frame <= 4)
                    std::cout << "Native-shaped frame " << frame << " presented\n";
                Sleep(8);
            }
            vkcheck(vk.waitIdle(vk.device), "Native-shaped idle");
            for (auto s : ready)
                vk.dev<PFN_vkDestroySemaphore>("vkDestroySemaphore")(vk.device, s, nullptr);
            vk.dev<PFN_vkDestroySemaphore>("vkDestroySemaphore")(vk.device, acquired, nullptr);
            vk.dev<PFN_vkDestroyFence>("vkDestroyFence")(vk.device, fence, nullptr);
            vk.dev<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(vk.device, pool, nullptr);
        }
        vk.inst<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR")(vk.instance, surface, nullptr);
        DestroyWindow(window);
        std::cout << "PASS: native-shaped swapchain lifecycle through the layer.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

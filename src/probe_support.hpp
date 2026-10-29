// SPDX-License-Identifier: MIT
// Standalone Vulkan/FSR diagnostic. Does not load X-Plane or install a layer.
#include "fsr24.hpp"
#include <cstring>
#include <iostream>
#include <memory>
using namespace xpfsr;
static void vkcheck(VkResult result, const char *operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " VkResult=" + std::to_string(result));
}
struct Vulkan {
    HMODULE library{};
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t family{};
    PFN_vkGetInstanceProcAddr gipa{};
    PFN_vkGetDeviceProcAddr gdpa{};
    PFN_vkDestroyDevice destroyDevice{};
    PFN_vkDestroyInstance destroyInstance{};
    PFN_vkDeviceWaitIdle waitIdle{};
    VkPhysicalDeviceMemoryProperties memory{};
    template <class T> T inst(const char *name) {
        auto p = gipa(instance, name);
        require(p != nullptr, name);
        return reinterpret_cast<T>(p);
    }
    template <class T> T dev(const char *name) {
        auto p = gdpa(device, name);
        require(p != nullptr, name);
        return reinterpret_cast<T>(p);
    }
    void open(bool presentation = false, uint32_t presentationQueues = 4) {
        wchar_t system[MAX_PATH]{};
        require(GetSystemDirectoryW(system, MAX_PATH) > 0, "System directory lookup failed");
        const auto path = std::filesystem::path(system) / L"vulkan-1.dll";
        library = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        require(library != nullptr, "System Vulkan loader is unavailable");
        gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            GetProcAddress(library, "vkGetInstanceProcAddr"));
        require(gipa != nullptr, "vkGetInstanceProcAddr unavailable");
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "XPlaneFSR24 standalone probe";
        application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo = &application;
        const char *windowExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, "VK_KHR_win32_surface"};
        if (presentation) {
            ici.enabledExtensionCount = 2;
            ici.ppEnabledExtensionNames = windowExtensions;
        }
        vkcheck(inst<PFN_vkCreateInstance>("vkCreateInstance")(&ici, nullptr, &instance),
                "CreateInstance");
        destroyInstance = inst<PFN_vkDestroyInstance>("vkDestroyInstance");
        auto enumerate = inst<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
        auto properties = inst<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties");
        uint32_t count{};
        vkcheck(enumerate(instance, &count, nullptr), "EnumeratePhysicalDevices");
        require(count > 0, "No Vulkan physical devices");
        std::vector<VkPhysicalDevice> devices(count);
        vkcheck(enumerate(instance, &count, devices.data()), "EnumeratePhysicalDevices");
        int best = -1;
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties p{};
            properties(candidate, &p);
            std::cout << "GPU: " << p.deviceName << " vendor=0x" << std::hex << p.vendorID
                      << std::dec << " Vulkan=" << VK_API_VERSION_MAJOR(p.apiVersion) << '.'
                      << VK_API_VERSION_MINOR(p.apiVersion) << '.'
                      << VK_API_VERSION_PATCH(p.apiVersion) << " raw_driver=" << p.driverVersion
                      << '\n';
            int score = (p.vendorID == 0x1002 ? 10 : 0) +
                        (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 5 : 0);
            if (score > best) {
                best = score;
                physical = candidate;
            }
        }
        VkPhysicalDeviceDriverProperties driver{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 prop{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        prop.pNext = &driver;
        inst<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2")(physical, &prop);
        std::cout << "Selected: " << prop.properties.deviceName << " / " << driver.driverName
                  << " / " << driver.driverInfo << '\n';
        require(prop.properties.apiVersion >= VK_API_VERSION_1_3,
                "Probe requires Vulkan 1.3 (SDK itself supports 1.2)");
        auto enumerateExtensions =
            inst<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
        count = 0;
        vkcheck(enumerateExtensions(physical, nullptr, &count, nullptr),
                "EnumerateDeviceExtensions");
        std::vector<VkExtensionProperties> extensions(count);
        vkcheck(enumerateExtensions(physical, nullptr, &count, extensions.data()),
                "EnumerateDeviceExtensions");
        auto has = [&](const char *name) {
            for (auto &e : extensions)
                if (!strcmp(e.extensionName, name))
                    return true;
            return false;
        };
        std::vector<const char *> enabled;
        if (presentation) {
            enabled.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            if (has(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME))
                enabled.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
        }
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceCooperativeMatrixFeaturesKHR coop{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};
        VkPhysicalDeviceShaderFloat8FeaturesEXT fp8{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT};
        VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivatives{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
        features.pNext = &f11;
        f11.pNext = &f12;
        f12.pNext = &f13;
        void **tail = &f13.pNext;
        if (has(VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME)) {
            enabled.push_back(VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME);
            *tail = &coop;
            tail = &coop.pNext;
        }
        if (has(VK_EXT_SHADER_FLOAT8_EXTENSION_NAME)) {
            enabled.push_back(VK_EXT_SHADER_FLOAT8_EXTENSION_NAME);
            *tail = &fp8;
            tail = &fp8.pNext;
        }
        if (has(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME)) {
            enabled.push_back(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
            *tail = &derivatives;
            tail = &derivatives.pNext;
        }
        inst<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2")(physical, &features);
        std::cout << "Supported/enabled: shaderFloat16=" << f12.shaderFloat16
                  << " shaderInt8=" << f12.shaderInt8
                  << " cooperativeMatrix=" << coop.cooperativeMatrix
                  << " shaderFloat8=" << fp8.shaderFloat8
                  << " computeDerivativeLinear=" << derivatives.computeDerivativeGroupLinear
                  << " advertisedNVCooperativeMatrix2="
                  << has(VK_NV_COOPERATIVE_MATRIX_2_EXTENSION_NAME) << '\n';
        auto queueProperties = inst<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            "vkGetPhysicalDeviceQueueFamilyProperties");
        count = 0;
        queueProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        queueProperties(physical, &count, queues.data());
        family = UINT32_MAX;
        for (uint32_t i = 0; i < count; ++i)
            if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                family = i;
                break;
            }
        require(family != UINT32_MAX, "No compute queue");
        const float priorities[] = {1, 1, 1, 1};
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        require(!presentation || queues[family].queueCount >= 4,
                "Frame generation test needs four graphics queues");
        qci.queueFamilyIndex = family;
        qci.queueCount = presentation ? presentationQueues : 1;
        qci.pQueuePriorities = priorities;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.pNext = &features;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
        dci.ppEnabledExtensionNames = enabled.data();
        vkcheck(inst<PFN_vkCreateDevice>("vkCreateDevice")(physical, &dci, nullptr, &device),
                "CreateDevice");
        gdpa = inst<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
        destroyDevice = dev<PFN_vkDestroyDevice>("vkDestroyDevice");
        waitIdle = dev<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle");
        dev<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(device, family, 0, &queue);
        inst<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(
            physical, &memory);
    }
    uint32_t memoryType(uint32_t allowed, VkMemoryPropertyFlags flags) {
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((allowed & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
        throw std::runtime_error("Suitable memory type unavailable");
    }
    ~Vulkan() {
        if (device && destroyDevice) {
            if (waitIdle)
                waitIdle(device);
            destroyDevice(device, nullptr);
        }
        if (instance && destroyInstance)
            destroyInstance(instance, nullptr);
        if (library)
            FreeLibrary(library);
    }
};
struct GpuImage {
    Vulkan &vk;
    Image value{};
    VkDeviceMemory allocation{};
    explicit GpuImage(Vulkan &owner) : vk(owner) {}
    void create(uint32_t w, uint32_t h, VkFormat format, VkImageCreateFlags flags = 0,
                VkImageUsageFlags extraUsage = 0,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) {
        auto &d = value.description;
        d.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        d.flags = flags;
        d.imageType = VK_IMAGE_TYPE_2D;
        d.format = format;
        d.extent = {w, h, 1};
        d.mipLevels = d.arrayLayers = 1;
        d.samples = samples;
        d.tiling = VK_IMAGE_TILING_OPTIMAL;
        d.usage = extraUsage | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        vkcheck(vk.dev<PFN_vkCreateImage>("vkCreateImage")(vk.device, &d, nullptr, &value.handle),
                "CreateImage");
        VkMemoryRequirements requirements{};
        vk.dev<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(
            vk.device, value.handle, &requirements);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = requirements.size;
        ai.memoryTypeIndex =
            vk.memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkcheck(
            vk.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(vk.device, &ai, nullptr, &allocation),
            "AllocateImageMemory");
        vkcheck(vk.dev<PFN_vkBindImageMemory>("vkBindImageMemory")(vk.device, value.handle,
                                                                   allocation, 0),
                "BindImageMemory");
    }
    void transition(VkCommandBuffer cb, VkImageLayout target) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.image = value.handle;
        b.oldLayout = value.layout;
        b.newLayout = target;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = value.layout == VK_IMAGE_LAYOUT_UNDEFINED
                              ? 0
                              : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
            cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
            nullptr, 0, nullptr, 1, &b);
        value.layout = target;
    }
    ~GpuImage() {
        if (value.handle)
            vk.dev<PFN_vkDestroyImage>("vkDestroyImage")(vk.device, value.handle, nullptr);
        if (allocation)
            vk.dev<PFN_vkFreeMemory>("vkFreeMemory")(vk.device, allocation, nullptr);
    }
};
static float half(uint16_t h) {
    const int sign = h >> 15, exponent = (h >> 10) & 31, fraction = h & 1023;
    if (exponent == 31)
        return fraction ? NAN : (sign ? -INFINITY : INFINITY);
    const float value = exponent ? std::ldexp(1.0f + float(fraction) / 1024, exponent - 15)
                                 : std::ldexp(float(fraction), -24);
    return sign ? -value : value;
}
static void smoke(Vulkan &vk, const Api &api) {
    // One queue, one source frame in flight; every reuse waits for its fence.
    GpuImage color(vk), depth(vk), motion(vk), output(vk);
    color.create(256, 144, VK_FORMAT_R16G16B16A16_SFLOAT);
    depth.create(256, 144, VK_FORMAT_R32_SFLOAT);
    motion.create(256, 144, VK_FORMAT_R16G16_SFLOAT);
    output.create(384, 216, VK_FORMAT_R16G16B16A16_SFLOAT);
    VkCommandPool pool{};
    VkFence fence{};
    VkBuffer readback{};
    VkDeviceMemory hostMemory{};
    // Handles cleaned below. On a driver failure the OS reclaims this standalone process.
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = vk.family;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkcheck(vk.dev<PFN_vkCreateCommandPool>("vkCreateCommandPool")(vk.device, &pci, nullptr, &pool),
            "CreateCommandPool");
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb{};
    vkcheck(vk.dev<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(vk.device, &cai, &cb),
            "AllocateCommandBuffers");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkcheck(vk.dev<PFN_vkCreateFence>("vkCreateFence")(vk.device, &fi, nullptr, &fence),
            "CreateFence");
    const VkDeviceSize bytes = 384 * 216 * 8;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    vkcheck(vk.dev<PFN_vkCreateBuffer>("vkCreateBuffer")(vk.device, &bci, nullptr, &readback),
            "CreateReadback");
    VkMemoryRequirements mr{};
    vk.dev<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(vk.device, readback,
                                                                               &mr);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex =
        vk.memoryType(mr.memoryTypeBits,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkcheck(vk.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(vk.device, &mai, nullptr, &hostMemory),
            "AllocateReadback");
    vkcheck(
        vk.dev<PFN_vkBindBufferMemory>("vkBindBufferMemory")(vk.device, readback, hostMemory, 0),
        "BindReadback");
    Context context(api);
    try {
        Config config{256, 144, 384, 216};
        context.create(config, {vk.device, vk.physical, vk.gdpa});
        std::cout << "Actual selected provider: " << context.provider() << '\n';
        for (uint64_t frame = 1; frame <= 8; ++frame) {
            vkcheck(vk.dev<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(cb, 0),
                    "ResetCommandBuffer");
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            vkcheck(vk.dev<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb, &bi),
                    "BeginCommandBuffer");
            GpuImage *inputs[] = {&color, &depth, &motion};
            VkClearColorValue clears[3] = {};
            clears[0].float32[0] = 0.2f;
            clears[0].float32[1] = 0.4f;
            clears[0].float32[2] = 0.6f;
            clears[0].float32[3] = 1;
            clears[1].float32[0] = 0.5f;
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            for (int i = 0; i < 3; ++i) {
                inputs[i]->transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                vk.dev<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(
                    cb, inputs[i]->value.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clears[i],
                    1, &range);
                inputs[i]->transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            output.transition(cb, VK_IMAGE_LAYOUT_GENERAL);
            Frame f{};
            f.commands = cb;
            f.color = color.value;
            f.depth = depth.value;
            f.motion = motion.value;
            f.output = output.value;
            f.motionScaleX = 256;
            f.motionScaleY = 144;
            f.deltaMilliseconds = 16.6667f;
            f.nearPlane = 0.1f;
            f.verticalFovRadians = 1.0472f;
            f.sequence = frame;
            context.record(f);
            output.transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {384, 216, 1};
            vk.dev<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(
                cb, output.value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
            VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
                cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hostBarrier,
                0, nullptr, 0, nullptr);
            vkcheck(vk.dev<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb), "EndCommandBuffer");
            vkcheck(vk.dev<PFN_vkResetFences>("vkResetFences")(vk.device, 1, &fence), "ResetFence");
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cb;
            vkcheck(vk.dev<PFN_vkQueueSubmit>("vkQueueSubmit")(vk.queue, 1, &si, fence),
                    "QueueSubmit");
            vkcheck(vk.dev<PFN_vkWaitForFences>("vkWaitForFences")(vk.device, 1, &fence, VK_TRUE,
                                                                   30000000000ull),
                    "WaitForFence");
            void *mapped{};
            vkcheck(
                vk.dev<PFN_vkMapMemory>("vkMapMemory")(vk.device, hostMemory, 0, bytes, 0, &mapped),
                "MapReadback");
            const auto *pixels = static_cast<const uint16_t *>(mapped);
            float minValue = INFINITY, maxValue = -INFINITY;
            for (size_t p = 0; p < 384 * 216; ++p)
                for (int channel = 0; channel < 3; ++channel) {
                    const float v = half(pixels[p * 4 + channel]);
                    require(std::isfinite(v), "FSR output contains NaN/Inf");
                    minValue = std::min(minValue, v);
                    maxValue = std::max(maxValue, v);
                }
            const size_t center = (108 * 384 + 192) * 4;
            const float rgb[] = {half(pixels[center]), half(pixels[center + 1]),
                                 half(pixels[center + 2])};
            vk.dev<PFN_vkUnmapMemory>("vkUnmapMemory")(vk.device, hostMemory);
            require(minValue >= -0.01f && maxValue > 0.1f && maxValue < 2.0f,
                    "FSR output failed finite/nonempty range check");
            require(std::abs(rgb[0] - 0.2f) < 0.05f && std::abs(rgb[1] - 0.4f) < 0.05f &&
                        std::abs(rgb[2] - 0.6f) < 0.05f,
                    "FSR output failed constant-color readback check");
            std::cout << "GPU frame " << frame << ": fence completed; center RGB=" << rgb[0] << ','
                      << rgb[1] << ',' << rgb[2] << '\n';
        }
        vkcheck(vk.waitIdle(vk.device), "DeviceWaitIdle");
    } catch (...) {
        vk.waitIdle(vk.device);
        throw;
    }
    vk.dev<PFN_vkDestroyBuffer>("vkDestroyBuffer")(vk.device, readback, nullptr);
    vk.dev<PFN_vkFreeMemory>("vkFreeMemory")(vk.device, hostMemory, nullptr);
    vk.dev<PFN_vkDestroyFence>("vkDestroyFence")(vk.device, fence, nullptr);
    vk.dev<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(vk.device, pool, nullptr);
    std::cout << "PASS: 8 submitted/completed synthetic FSR frames. X-Plane capture, motion "
                 "quality and FPS remain untested.\n";
}
int wmain(int argc, wchar_t **argv) {
    std::cout.setf(std::ios::unitbuf);
    try {
        require(argc >= 2, "Usage: gpu_probe.exe ABSOLUTE_AMD_SIGNEDBIN_DIRECTORY [--smoke]");
        Runtime runtime;
        runtime.load(argv[1]);
        Vulkan vk;
        vk.open();
        VkDeviceContext device{vk.device, vk.physical, vk.gdpa};
        for (auto &v : versions(runtime.api, device))
            std::cout << "Provider: " << v.name << " id=" << v.id << '\n';
        if (argc >= 3 && std::wstring(argv[2]) == L"--smoke")
            smoke(vk, runtime.api);
        else
            std::cout << "Enumeration only; use --smoke for offscreen dispatch/readback.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

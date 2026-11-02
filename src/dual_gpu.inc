// v2 (P3, owner priority): frame generation on a second GPU. Included by layer.cpp
// after layer_framegen.inc. Original code.
//
// X-Plane renders on GPU A (the device our layer wraps). The monitor is on GPU
// B. With XPFSR24_FG_GPU=second, X-Plane's swapchain becomes a *virtual*
// swapchain on A: X-Plane renders into images we own, and at present we copy
// the frame (plus the depth and motion FSR already produced) into host memory.
// A worker thread uploads them to a private device on B, where AMD's frame
// generation swapchain interpolates and presents to the same window. If B
// cannot run AMD's FG swapchain, a plain swapchain on B shows the real frames
// (still a measurable two-GPU path). Any failure before the first frame falls
// back to the normal single-GPU path. Transfer uses host-visible staging (the
// correctness baseline in MULTI-GPU-DESIGN.md); peer/external memory comes later.


}  // namespace (layer)
#include <condition_variable>
#include <deque>
namespace {
namespace dualgpu {
inline uint32_t texelBytes(VkFormat f) {
    switch (f) {
    case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB: case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB: case VK_FORMAT_A2B10G10R10_UNORM_PACK32: case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R16G16_SFLOAT: case VK_FORMAT_D32_SFLOAT: return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
    default: return 0;
    }
}
struct Loader {
    HMODULE module{};
    PFN_vkEnumeratePhysicalDevices enumerate{}; PFN_vkGetPhysicalDeviceProperties properties{};
    PFN_vkGetPhysicalDeviceProperties2 properties2{}; PFN_vkGetPhysicalDeviceQueueFamilyProperties families{};
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR surfaceSupport{}; PFN_vkGetPhysicalDeviceFeatures2 features2{};
    PFN_vkEnumerateDeviceExtensionProperties extensions{}; PFN_vkCreateDevice createDevice{};
    PFN_vkGetDeviceProcAddr gdpa{}; PFN_vkGetPhysicalDeviceMemoryProperties memory{};
    bool load() {
        wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system, MAX_PATH);
        module = LoadLibraryExW((std::filesystem::path(system) / L"vulkan-1.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return false;
        auto get = [&](const char *n) { return reinterpret_cast<void *>(GetProcAddress(module, n)); };
        enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get("vkEnumeratePhysicalDevices"));
        properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get("vkGetPhysicalDeviceProperties"));
        properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(get("vkGetPhysicalDeviceProperties2"));
        families = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(get("vkGetPhysicalDeviceQueueFamilyProperties"));
        surfaceSupport = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(get("vkGetPhysicalDeviceSurfaceSupportKHR"));
        features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(get("vkGetPhysicalDeviceFeatures2"));
        extensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(get("vkEnumerateDeviceExtensionProperties"));
        createDevice = reinterpret_cast<PFN_vkCreateDevice>(get("vkCreateDevice"));
        gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(get("vkGetDeviceProcAddr"));
        memory = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(get("vkGetPhysicalDeviceMemoryProperties"));
        return enumerate && properties && properties2 && families && surfaceSupport && features2 && extensions && createDevice && gdpa && memory;
    }
};
// A buffer with host-visible memory on one device (readback on A, upload on B).
struct HostBuffer {
    VkDevice device{}; PFN_vkGetDeviceProcAddr gdpa{}; VkBuffer buffer{}; VkDeviceMemory memory{}; void *mapped{}; VkDeviceSize size{};
    template <class T> T fn(const char *n) const { return reinterpret_cast<T>(gdpa(device, n)); }
    void create(VkDevice dev, PFN_vkGetDeviceProcAddr get, const VkPhysicalDeviceMemoryProperties &props, VkDeviceSize bytes, VkBufferUsageFlags usage, bool readback) {
        device = dev; gdpa = get; size = bytes;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size = bytes;bi.usage = usage;
        check(fn<PFN_vkCreateBuffer>("vkCreateBuffer")(device, &bi, nullptr, &buffer), "Dual GPU staging buffer");
        VkMemoryRequirements req{};fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(device, buffer, &req);
        uint32_t type = UINT32_MAX;
        const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (int pass = 0; pass < 2 && type == UINT32_MAX; ++pass)
            for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
                const auto flags = props.memoryTypes[i].propertyFlags;
                const bool cached = flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
                if ((req.memoryTypeBits & (1u << i)) && (flags & wanted) == wanted && (pass || cached == readback)) { type = i; break; }
            }
        xpfsr::require(type != UINT32_MAX, "No host-visible memory for dual GPU staging");
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize = req.size;ai.memoryTypeIndex = type;
        check(fn<PFN_vkAllocateMemory>("vkAllocateMemory")(device, &ai, nullptr, &memory), "Dual GPU staging memory");
        check(fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(device, buffer, memory, 0), "Bind dual GPU staging");
        check(fn<PFN_vkMapMemory>("vkMapMemory")(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped), "Map dual GPU staging");
    }
    void destroy() {
        if (memory) fn<PFN_vkFreeMemory>("vkFreeMemory")(device, memory, nullptr);
        if (buffer) fn<PFN_vkDestroyBuffer>("vkDestroyBuffer")(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE; memory = VK_NULL_HANDLE; mapped = nullptr;
    }
};
// What frame generation needs from one real frame, captured on A.
struct Guides {
    bool valid{}; uint64_t sequence{};
    uint32_t renderWidth{}, renderHeight{}; VkFormat depthFormat{}, motionFormat{};
    xpfsr::Frame params{}; xpfsr::CameraBasis camera{}; bool reverse{}, infinite{};
};
struct Slot {  // one real frame in flight from A to B
    HostBuffer readback;        // A: color, then depth, then motion
    VkCommandBuffer commands{}; VkFence fence{};
    bool busy{}; uint32_t image{}; Guides guides; uint64_t sequence{};
};
}  // namespace dualgpu

struct DualSwapchain {
    Device *a{};
    // --- virtual swapchain on A ---
    VkFormat format{}; VkExtent2D extent{}; VkImageUsageFlags usage{}; VkColorSpaceKHR colorSpace{};
    std::vector<VkImage> images; std::vector<VkDeviceMemory> imageMemory;
    std::vector<int> imageSlot;          // slot whose readback still reads the image, or -1
    uint32_t nextImage{};
    VkCommandPool poolA{}; VkQueue queueA{}; uint32_t familyA{};
    std::array<dualgpu::Slot, 3> slots; uint32_t nextSlot{};
    // --- private device on B ---
    VkPhysicalDevice physicalB{}; VkDevice deviceB{}; PFN_vkGetDeviceProcAddr gdpaB{}; std::string nameB;
    VkPhysicalDeviceMemoryProperties memoryB{};
    std::array<VkQueue, 4> queuesB{}; std::array<uint32_t, 4> familiesB{};
    VkCommandPool poolB{}; std::array<VkCommandBuffer, 3> commandsB{}; std::array<VkFence, 3> fencesB{};
    std::array<VkSemaphore, 3> acquiredB{}, renderedB{}; std::array<dualgpu::HostBuffer, 3> uploadB{};
    uint32_t ringB{};
    VkImage depthB{}, motionB{}; VkDeviceMemory depthMemoryB{}, motionMemoryB{}; VkImageCreateInfo depthInfoB{}, motionInfoB{};
    VkImageLayout depthLayoutB{VK_IMAGE_LAYOUT_UNDEFINED}, motionLayoutB{VK_IMAGE_LAYOUT_UNDEFINED};
    std::unique_ptr<xpfsr::Presentation> presentation;  // AMD FG swapchain on B (preferred)
    VkSwapchainKHR plainB{};                            // fallback: plain swapchain on B, real frames only
    std::vector<VkImage> backbuffersB;
    std::unique_ptr<xpfsr::FrameGenerator> generator; std::array<uint32_t, 6> generatorKey{};
    // --- worker ---
    std::thread worker; std::mutex lock; std::condition_variable wake; std::deque<uint32_t> ready; bool stopping{};
    std::atomic<uint64_t> presented{0}, uploadNs{0}, copyNs{0}, dropped{0};
    PFN_vkWaitForFences waitA{};  // resolved once; the worker never uses the layer's dispatch cache
    template <class T> T fnB(const char *n) const { return reinterpret_cast<T>(gdpaB(deviceB, n)); }
};

static std::string dualReason(Device &d) {
    return std::string(d.status.fgReason, strnlen(d.status.fgReason, sizeof(d.status.fgReason)));
}
static bool dualRequested(Device &d) {
    return !d.fgRequested.empty() && d.mode != Mode::Observe && app::environment(L"XPFSR24_FG_GPU") == L"second";
}

// Picks GPU B: another physical device that can present to this window.
static void dualCreateDeviceB(Device &d, DualSwapchain &s, VkSurfaceKHR surface) {
    static dualgpu::Loader loader;
    static bool loaded = loader.load();
    xpfsr::require(loaded, "System Vulkan loader functions unavailable for the second GPU");
    VkPhysicalDeviceIDProperties idA{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 propsA{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};propsA.pNext = &idA;
    loader.properties2(applicationPhysical(d), &propsA);
    uint32_t count{};check(loader.enumerate(d.instance->handle, &count, nullptr), "Enumerate GPUs");
    std::vector<VkPhysicalDevice> physicals(count);check(loader.enumerate(d.instance->handle, &count, physicals.data()), "Enumerate GPUs");
    for (auto p : physicals) {
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext = &id;
        loader.properties2(p, &props);
        const bool same = !memcmp(id.deviceUUID, idA.deviceUUID, VK_UUID_SIZE);
        log("Second GPU candidate: %s (%s, Vulkan %u.%u)", props.properties.deviceName, same ? "X-Plane renders here" : "other GPU",
            VK_API_VERSION_MAJOR(props.properties.apiVersion), VK_API_VERSION_MINOR(props.properties.apiVersion));
        if (same || props.properties.apiVersion < VK_API_VERSION_1_3 || s.physicalB) continue;
        uint32_t familyCount{};loader.families(p, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);loader.families(p, &familyCount, families.data());
        VkBool32 canPresent = VK_FALSE;
        for (uint32_t f = 0; f < familyCount && !canPresent; ++f) loader.surfaceSupport(p, f, surface, &canPresent);
        if (!canPresent) { log("Second GPU candidate %s cannot present to this window (is the monitor connected to it?)", props.properties.deviceName); continue; }
        s.physicalB = p; s.nameB = props.properties.deviceName;
    }
    xpfsr::require(s.physicalB != VK_NULL_HANDLE, "No second GPU can present to this window. Connect the monitor to the second GPU");
    uint32_t familyCount{};loader.families(s.physicalB, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);loader.families(s.physicalB, &familyCount, families.data());
    for (uint32_t f = 0; f < familyCount; ++f)
        log("Second GPU queue family %u: flags=0x%x count=%u", f, families[f].queueFlags, families[f].queueCount);
    // Four distinct queues: game (graphics+present), async compute, present, acquire.
    std::vector<uint32_t> used(familyCount, 0);
    auto take = [&](auto accept) -> int {
        for (uint32_t f = 0; f < familyCount; ++f) if (used[f] < families[f].queueCount && accept(f)) return int(f);
        return -1;
    };
    auto present = [&](uint32_t f) { VkBool32 ok = VK_FALSE; loader.surfaceSupport(s.physicalB, f, surface, &ok); return ok == VK_TRUE; };
    const int game = take([&](uint32_t f) { return (families[f].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT) && present(f); });
    xpfsr::require(game >= 0, "Second GPU has no graphics queue that can present");
    std::array<int, 4> pick{game, -1, -1, -1}; std::array<uint32_t, 4> index{0, 0, 0, 0};
    index[0] = used[game]++;
    for (int role = 1; role < 4; ++role) {
        int f = -1;
        if (role == 1) f = take([&](uint32_t c) { return (families[c].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0; });
        if (role == 2) f = take([&](uint32_t c) { return present(c); });
        if (role == 3) f = take([&](uint32_t c) { return (families[c].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) != 0; });
        if (f >= 0) { pick[role] = f; index[role] = used[f]++; }
    }
    // Device B: every core feature the GPU supports (AMD's FG shaders vary by
    // provider), swapchain plus the presentation extensions it offers.
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};v12.pNext = &v13;
    VkPhysicalDeviceVulkan11Features v11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};v11.pNext = &v12;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext = &v11;
    loader.features2(s.physicalB, &features);
    features.features.robustBufferAccess = VK_FALSE;
    uint32_t extCount{};loader.extensions(s.physicalB, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);loader.extensions(s.physicalB, nullptr, &extCount, available.data());
    std::vector<const char *> enabled;
    for (const char *name : {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME, VK_KHR_PRESENT_ID_EXTENSION_NAME,
                             VK_KHR_PRESENT_WAIT_EXTENSION_NAME, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME})
        for (auto &e : available) if (!strcmp(e.extensionName, name)) { enabled.push_back(name); break; }
    xpfsr::require(!enabled.empty() && !strcmp(enabled[0], VK_KHR_SWAPCHAIN_EXTENSION_NAME), "Second GPU has no swapchain support");
    VkPhysicalDevicePresentIdFeaturesKHR presentId{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR presentWait{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    for (auto *name : enabled) {
        if (!strcmp(name, VK_KHR_PRESENT_ID_EXTENSION_NAME)) { presentId.presentId = VK_TRUE; presentId.pNext = v13.pNext; v13.pNext = &presentId; }
        if (!strcmp(name, VK_KHR_PRESENT_WAIT_EXTENSION_NAME)) { presentWait.presentWait = VK_TRUE; presentWait.pNext = v13.pNext; v13.pNext = &presentWait; }
    }
    std::vector<VkDeviceQueueCreateInfo> queues; std::vector<std::vector<float>> priorities(familyCount);
    for (uint32_t f = 0; f < familyCount; ++f) if (used[f]) {
        priorities[f].assign(used[f], 1.f);
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueFamilyIndex = f;q.queueCount = used[f];q.pQueuePriorities = priorities[f].data();
        queues.push_back(q);
    }
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};ci.pNext = &features;
    ci.queueCreateInfoCount = uint32_t(queues.size());ci.pQueueCreateInfos = queues.data();
    ci.enabledExtensionCount = uint32_t(enabled.size());ci.ppEnabledExtensionNames = enabled.data();
    creatingPrivateDevice = true;  // our own vkCreateDevice hook leaves this device untouched
    const auto result = loader.createDevice(s.physicalB, &ci, nullptr, &s.deviceB);
    creatingPrivateDevice = false;
    check(result, "Create the second GPU's device");
    s.gdpaB = loader.gdpa;
    loader.memory(s.physicalB, &s.memoryB);
    auto getQueue = s.fnB<PFN_vkGetDeviceQueue>("vkGetDeviceQueue");
    for (int role = 0; role < 4; ++role)
        if (pick[role] >= 0) { getQueue(s.deviceB, uint32_t(pick[role]), index[role], &s.queuesB[role]); s.familiesB[role] = uint32_t(pick[role]); }
    log("Second GPU device ready: %s; queues game=%d async=%d present=%d acquire=%d; extensions=%zu",
        s.nameB.c_str(), pick[0], pick[1], pick[2], pick[3], enabled.size());
}

static void dualCreateImageB(DualSwapchain &s, VkFormat format, uint32_t w, uint32_t h, VkImage &image, VkDeviceMemory &memory, VkImageCreateInfo &info) {
    info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;info.format = format;info.extent = {w, h, 1};info.mipLevels = info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    check(s.fnB<PFN_vkCreateImage>("vkCreateImage")(s.deviceB, &info, nullptr, &image), "Second GPU guide image");
    VkMemoryRequirements req{};s.fnB<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(s.deviceB, image, &req);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < s.memoryB.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) && (s.memoryB.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { type = i; break; }
    xpfsr::require(type != UINT32_MAX, "No device memory for second GPU guides");
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize = req.size;ai.memoryTypeIndex = type;
    check(s.fnB<PFN_vkAllocateMemory>("vkAllocateMemory")(s.deviceB, &ai, nullptr, &memory), "Second GPU guide memory");
    check(s.fnB<PFN_vkBindImageMemory>("vkBindImageMemory")(s.deviceB, image, memory, 0), "Bind second GPU guide");
}
static void dualBarrierB(DualSwapchain &s, VkCommandBuffer cb, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags sa, VkAccessFlags da,
                         VkPipelineStageFlags src, VkPipelineStageFlags dst) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout = from;b.newLayout = to;b.srcAccessMask = sa;b.dstAccessMask = da;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;b.image = image;b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    s.fnB<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(cb, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// B presentation: AMD's FG swapchain when four queues exist, else a plain one.
static void dualCreatePresentationB(Device &d, DualSwapchain &s, const VkSwapchainCreateInfoKHR &info) {
    auto b = info;b.oldSwapchain = VK_NULL_HANDLE;b.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    b.queueFamilyIndexCount = 0;b.pQueueFamilyIndices = nullptr;b.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    std::string why;
    const bool four = std::all_of(s.queuesB.begin(), s.queuesB.end(), [](VkQueue q) { return q != VK_NULL_HANDLE; });
    if (four) {
        try {
            SdkScope sdk;ensureRuntime(d);
            auto p = std::make_unique<xpfsr::Presentation>(d.runtime->api);
            p->create({s.deviceB, s.physicalB, s.gdpaB}, b, s.queuesB, s.familiesB, d.fgPresenter);
            uint32_t n{};check(p->functions.pOutGetSwapchainImagesKHR(s.deviceB, p->handle(), &n, nullptr), "Second GPU FG images");
            s.backbuffersB.resize(n);check(p->functions.pOutGetSwapchainImagesKHR(s.deviceB, p->handle(), &n, s.backbuffersB.data()), "Second GPU FG images");
            s.presentation = std::move(p);
            log("Second GPU FG swapchain ready on %s: provider=%s, %u images", s.nameB.c_str(), s.presentation->provider().c_str(), n);
            return;
        } catch (const std::exception &e) { why = e.what(); }
    } else why = "the second GPU does not offer four separate queues (needed by AMD's FG swapchain)";
    log("Second GPU: AMD FG swapchain unavailable (%s); showing real frames only on %s", why.c_str(), s.nameB.c_str());
    check(s.fnB<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR")(s.deviceB, &b, nullptr, &s.plainB), "Second GPU swapchain");
    uint32_t n{};check(s.fnB<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR")(s.deviceB, s.plainB, &n, nullptr), "Second GPU images");
    s.backbuffersB.resize(n);check(s.fnB<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR")(s.deviceB, s.plainB, &n, s.backbuffersB.data()), "Second GPU images");
    snprintf(d.status.fgReason, sizeof(d.status.fgReason), "Second GPU %s shows real frames only: %s", s.nameB.c_str(), why.c_str());
}

static void dualWorker(Device *d, DualSwapchain *s);
static void dualDestroy(Device &d, DualSwapchain &s);

// Called from L_CreateSwapchainKHR. Throws on any failure (caller falls back).
static std::shared_ptr<DualSwapchain> dualCreate(Device &d, const VkSwapchainCreateInfoKHR &info) {
    xpfsr::require(info.imageArrayLayers == 1, "Two-GPU FG needs a single-layer swapchain");
    xpfsr::require(dualgpu::texelBytes(info.imageFormat) != 0, "Two-GPU FG does not support this swapchain format yet");
    auto s = std::make_shared<DualSwapchain>();
    s->a = &d; s->format = info.imageFormat; s->extent = info.imageExtent; s->colorSpace = info.imageColorSpace;
    s->usage = info.imageUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    s->waitA = d.fn<PFN_vkWaitForFences>("vkWaitForFences");
    try {
        dualCreateDeviceB(d, *s, info.surface);
        dualCreatePresentationB(d, *s, info);
        // A: virtual swapchain images that X-Plane renders into.
        const uint32_t count = std::max<uint32_t>(info.minImageCount, 3);
        VkImageFormatListCreateInfo list{VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO};
        for (auto *node = static_cast<const VkBaseInStructure *>(info.pNext); node; node = node->pNext)
            if (node->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO) list = *reinterpret_cast<const VkImageFormatListCreateInfo *>(node);
        list.pNext = nullptr;
        for (uint32_t i = 0; i < count; ++i) {
            VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            ci.flags = (info.flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0;
            if (ci.flags && list.viewFormatCount) ci.pNext = &list;
            ci.imageType = VK_IMAGE_TYPE_2D;ci.format = info.imageFormat;ci.extent = {info.imageExtent.width, info.imageExtent.height, 1};
            ci.mipLevels = ci.arrayLayers = 1;ci.samples = VK_SAMPLE_COUNT_1_BIT;ci.tiling = VK_IMAGE_TILING_OPTIMAL;ci.usage = s->usage;
            VkImage image{};check(d.fn<PFN_vkCreateImage>("vkCreateImage")(d.handle, &ci, nullptr, &image), "Virtual swapchain image");
            s->images.push_back(image);
            VkMemoryRequirements req{};d.fn<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(d.handle, image, &req);
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize = req.size;
            ai.memoryTypeIndex = d.memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VkDeviceMemory memory{};check(d.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(d.handle, &ai, nullptr, &memory), "Virtual swapchain memory");
            s->imageMemory.push_back(memory);
            check(d.fn<PFN_vkBindImageMemory>("vkBindImageMemory")(d.handle, image, memory, 0), "Bind virtual swapchain image");
        }
        s->imageSlot.assign(count, -1);
        // A: the queue X-Plane presents on (family of its graphics queue).
        s->familyA = d.graphicsFamily;
        d.fn<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(d.handle, s->familyA, 0, &s->queueA);
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pool.queueFamilyIndex = s->familyA;
        check(d.fn<PFN_vkCreateCommandPool>("vkCreateCommandPool")(d.handle, &pool, nullptr, &s->poolA), "Two-GPU command pool");
        const VkDeviceSize colorBytes = VkDeviceSize(s->extent.width) * s->extent.height * dualgpu::texelBytes(s->format);
        // Guides at render size are at most output size; 8 bytes covers depth + motion.
        const VkDeviceSize slotBytes = colorBytes + VkDeviceSize(s->extent.width) * s->extent.height * 8 + 4096;
        for (auto &slot : s->slots) {
            slot.readback.create(d.handle, d.next, d.memory, slotBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
            VkCommandBufferAllocateInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cb.commandPool = s->poolA;cb.commandBufferCount = 1;
            check(d.fn<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(d.handle, &cb, &slot.commands), "Two-GPU command buffer");
            if (d.setLoaderData) d.setLoaderData(d.handle, slot.commands);
            VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            check(d.fn<PFN_vkCreateFence>("vkCreateFence")(d.handle, &fi, nullptr, &slot.fence), "Two-GPU fence");
        }
        // B: upload ring and command buffers.
        VkCommandPoolCreateInfo poolB{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolB.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;poolB.queueFamilyIndex = s->familiesB[0];
        check(s->fnB<PFN_vkCreateCommandPool>("vkCreateCommandPool")(s->deviceB, &poolB, nullptr, &s->poolB), "Second GPU command pool");
        VkCommandBufferAllocateInfo cbB{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cbB.commandPool = s->poolB;cbB.commandBufferCount = 3;
        check(s->fnB<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(s->deviceB, &cbB, s->commandsB.data()), "Second GPU command buffers");
        for (uint32_t i = 0; i < 3; ++i) {
            s->uploadB[i].create(s->deviceB, s->gdpaB, s->memoryB, slotBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
            VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            check(s->fnB<PFN_vkCreateFence>("vkCreateFence")(s->deviceB, &fi, nullptr, &s->fencesB[i]), "Second GPU fence");
            VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            check(s->fnB<PFN_vkCreateSemaphore>("vkCreateSemaphore")(s->deviceB, &si, nullptr, &s->acquiredB[i]), "Second GPU semaphore");
            check(s->fnB<PFN_vkCreateSemaphore>("vkCreateSemaphore")(s->deviceB, &si, nullptr, &s->renderedB[i]), "Second GPU semaphore");
        }
    } catch (...) {
        dualDestroy(d, *s);  // releases whatever was created; the worker never started
        throw;
    }
    s->worker = std::thread(dualWorker, &d, s.get());
    snprintf(d.status.fgReason, sizeof(d.status.fgReason), "Two-GPU FG: X-Plane renders here, %s %s", s->nameB.c_str(),
             s->presentation ? "generates and shows frames" : "shows real frames");
    d.status.fgActive = s->presentation ? 1 : 0;
    log("Two-GPU presentation ready: virtual swapchain %ux%u format=%u images=%zu; display GPU %s", s->extent.width, s->extent.height, s->format,
        s->images.size(), s->nameB.c_str());
    return s;
}

// Closes the second-GPU side (worker, device B). X-Plane's virtual images on A
// stay valid until X-Plane destroys the swapchain (dualReleaseFirst).
static void dualCloseSecond(Device &d, DualSwapchain &s) {
    {std::lock_guard<std::mutex> l(s.lock);s.stopping = true;}
    s.wake.notify_all();
    if (s.worker.joinable()) s.worker.join();
    if (s.queueA) d.fn<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(s.queueA);
    if (s.deviceB) s.fnB<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(s.deviceB);
    {SdkScope sdk;s.generator.reset();s.presentation.reset();}
    if (s.deviceB) {
        if (s.plainB) s.fnB<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(s.deviceB, s.plainB, nullptr);
        for (auto &u : s.uploadB) u.destroy();
        for (uint32_t i = 0; i < 3; ++i) {
            if (s.fencesB[i]) s.fnB<PFN_vkDestroyFence>("vkDestroyFence")(s.deviceB, s.fencesB[i], nullptr);
            if (s.acquiredB[i]) s.fnB<PFN_vkDestroySemaphore>("vkDestroySemaphore")(s.deviceB, s.acquiredB[i], nullptr);
            if (s.renderedB[i]) s.fnB<PFN_vkDestroySemaphore>("vkDestroySemaphore")(s.deviceB, s.renderedB[i], nullptr);
        }
        for (auto [image, memory] : {std::pair{s.depthB, s.depthMemoryB}, std::pair{s.motionB, s.motionMemoryB}}) {
            if (image) s.fnB<PFN_vkDestroyImage>("vkDestroyImage")(s.deviceB, image, nullptr);
            if (memory) s.fnB<PFN_vkFreeMemory>("vkFreeMemory")(s.deviceB, memory, nullptr);
        }
        if (s.poolB) s.fnB<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(s.deviceB, s.poolB, nullptr);
        s.fnB<PFN_vkDestroyDevice>("vkDestroyDevice")(s.deviceB, nullptr);
        s.deviceB = VK_NULL_HANDLE;
        log("Two-GPU presentation closed: presented=%llu dropped=%llu avg copy-to-host=%.2f ms avg upload=%.2f ms",
            (unsigned long long)s.presented.load(), (unsigned long long)s.dropped.load(),
            s.presented ? s.copyNs.load() / 1e6 / double(s.presented) : 0.0, s.presented ? s.uploadNs.load() / 1e6 / double(s.presented) : 0.0);
    }
}
static void dualReleaseFirst(Device &d, DualSwapchain &s) {
    if (s.queueA) d.fn<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(s.queueA);
    for (auto &slot : s.slots) {
        slot.readback.destroy();
        if (slot.fence) d.fn<PFN_vkDestroyFence>("vkDestroyFence")(d.handle, slot.fence, nullptr);
    }
    if (s.poolA) d.fn<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(d.handle, s.poolA, nullptr);
    for (size_t i = 0; i < s.images.size(); ++i) {
        d.fn<PFN_vkDestroyImage>("vkDestroyImage")(d.handle, s.images[i], nullptr);
        if (i < s.imageMemory.size()) d.fn<PFN_vkFreeMemory>("vkFreeMemory")(d.handle, s.imageMemory[i], nullptr);
    }
    s.images.clear();s.imageMemory.clear();
    for (auto &slot : s.slots) slot.fence = VK_NULL_HANDLE;
    s.poolA = VK_NULL_HANDLE;
}
static void dualDestroy(Device &d, DualSwapchain &s) {dualCloseSecond(d, s);dualReleaseFirst(d, s);}

// prepareGeneration in two-GPU mode: copy FSR's depth and motion for this
// frame into host memory in X-Plane's own command buffer (they are only valid
// now), and remember the camera parameters for the second GPU.
static void dualCaptureGuides(Device &d, DualSwapchain &s, VkCommandBuffer cb, FrameResources &f, const xpfsr::Frame &frame) {
    const uint32_t rw = f.depth.value.description.extent.width, rh = f.depth.value.description.extent.height;
    const uint32_t depthBytes = dualgpu::texelBytes(f.depth.value.description.format), motionBytes = dualgpu::texelBytes(f.motion.value.description.format);
    if (!depthBytes || !motionBytes || VkDeviceSize(rw) * rh * (depthBytes + motionBytes) > VkDeviceSize(s.extent.width) * s.extent.height * 8) return;
    auto &slot = s.slots[s.nextSlot];
    {std::lock_guard<std::mutex> l(s.lock);if (slot.busy) { ++s.dropped; return; }}  // B is behind: no generation for this frame
    const VkDeviceSize colorBytes = VkDeviceSize(s.extent.width) * s.extent.height * dualgpu::texelBytes(s.format);
    transition(d, cb, f.depth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition(d, cb, f.motion, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy depth{};depth.bufferOffset = colorBytes;depth.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};depth.imageExtent = {rw, rh, 1};
    VkBufferImageCopy motion = depth;motion.bufferOffset = colorBytes + VkDeviceSize(rw) * rh * depthBytes;
    auto copy = d.fn<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer");
    copy(cb, f.depth.value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.readback.buffer, 1, &depth);
    copy(cb, f.motion.value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.readback.buffer, 1, &motion);
    auto &g = slot.guides;
    g.valid = true; g.sequence = frame.sequence; g.renderWidth = rw; g.renderHeight = rh;
    g.depthFormat = f.depth.value.description.format; g.motionFormat = f.motion.value.description.format;
    g.params = frame; g.reverse = d.camera.reverseZ; g.infinite = d.camera.infiniteZ;
    memcpy(g.camera.position, d.camera.position, 12);memcpy(g.camera.up, d.camera.up, 12);
    memcpy(g.camera.right, d.camera.right, 12);memcpy(g.camera.forward, d.camera.forward, 12);
    if (!g.camera.up[0] && !g.camera.up[1] && !g.camera.up[2]) g.camera.up[1] = 1;
    if (!g.camera.right[0] && !g.camera.right[1] && !g.camera.right[2]) g.camera.right[0] = 1;
    if (!g.camera.forward[0] && !g.camera.forward[1] && !g.camera.forward[2]) g.camera.forward[2] = -1;
    g.params.reset |= d.fgResumeReset; d.fgResumeReset = false;
    ++d.status.fgPrepared;
}

static VkResult dualImages(DualSwapchain &s, uint32_t *count, VkImage *images) {
    if (!images) { *count = uint32_t(s.images.size()); return VK_SUCCESS; }
    const uint32_t n = std::min<uint32_t>(*count, uint32_t(s.images.size()));
    std::copy(s.images.begin(), s.images.begin() + n, images);
    *count = n;
    return n < s.images.size() ? VK_INCOMPLETE : VK_SUCCESS;
}
static VkResult dualAcquire(Device &d, DualSwapchain &s, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *index) {
    const uint32_t i = s.nextImage;
    // The image is free once the readback that copied it has finished on A.
    if (s.imageSlot[i] >= 0) {
        auto &slot = s.slots[size_t(s.imageSlot[i])];
        const auto r = d.fn<PFN_vkWaitForFences>("vkWaitForFences")(d.handle, 1, &slot.fence, VK_TRUE, timeout);
        if (r != VK_SUCCESS) return r == VK_TIMEOUT ? (timeout ? VK_TIMEOUT : VK_NOT_READY) : r;
        s.imageSlot[i] = -1;
    }
    s.nextImage = (i + 1) % uint32_t(s.images.size());
    if (semaphore || fence) {
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.signalSemaphoreCount = semaphore ? 1 : 0;si.pSignalSemaphores = &semaphore;
        const auto r = d.fn<PFN_vkQueueSubmit>("vkQueueSubmit")(s.queueA, 1, &si, fence);
        if (r != VK_SUCCESS) return r;
    }
    *index = i;
    return VK_SUCCESS;
}

// X-Plane's present on A: copy its image to host memory and hand it to B.
static VkResult dualPresent(Device &d, DualSwapchain &s, VkQueue queue, const VkPresentInfoKHR &info, uint32_t imageIndex) {
    const auto start = qpcNow();
    auto &slot = s.slots[s.nextSlot];
    if (slot.busy) {
        // B is more than three frames behind; wait briefly for the oldest.
        std::unique_lock<std::mutex> l(s.lock);
        s.wake.wait_for(l, std::chrono::milliseconds(100), [&] { return !slot.busy || s.stopping; });
        if (slot.busy) { ++s.dropped; return VK_SUCCESS; }
    }
    auto cb = slot.commands;
    d.fn<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(cb, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(d.fn<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb, &begin), "Two-GPU readback begin");
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = s.images[imageIndex];b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto barrier = d.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");
    barrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy region{};region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};region.imageExtent = {s.extent.width, s.extent.height, 1};
    d.fn<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(cb, s.images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.readback.buffer, 1, &region);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask = 0;
    barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
    check(d.fn<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb), "Two-GPU readback end");
    std::vector<VkPipelineStageFlags> stages(info.waitSemaphoreCount, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.waitSemaphoreCount = info.waitSemaphoreCount;si.pWaitSemaphores = info.pWaitSemaphores;
    si.pWaitDstStageMask = stages.data();si.commandBufferCount = 1;si.pCommandBuffers = &cb;
    d.fn<PFN_vkResetFences>("vkResetFences")(d.handle, 1, &slot.fence);
    const auto r = d.fn<PFN_vkQueueSubmit>("vkQueueSubmit")(queue, 1, &si, slot.fence);
    if (r != VK_SUCCESS) return r;
    s.imageSlot[imageIndex] = int(s.nextSlot);
    {
        std::lock_guard<std::mutex> l(s.lock);
        slot.busy = true; slot.image = imageIndex; slot.sequence = ++s.ringB;
        s.ready.push_back(s.nextSlot);
    }
    s.wake.notify_all();
    s.nextSlot = (s.nextSlot + 1) % uint32_t(s.slots.size());
    s.copyNs += uint64_t(qpcToNs(qpcNow() - start));
    return VK_SUCCESS;
}

// Worker: wait for A's readback, upload to B, generate and present on B.
static void dualWorker(Device *dp, DualSwapchain *sp) {
    auto &d = *dp; auto &s = *sp;
    SdkScope sdk;  // nothing on this thread should re-enter our hooks
    uint32_t ring = 0;
    for (;;) {
        uint32_t index;
        {
            std::unique_lock<std::mutex> l(s.lock);
            s.wake.wait(l, [&] { return s.stopping || !s.ready.empty(); });
            if (s.stopping) return;
            index = s.ready.front(); s.ready.pop_front();
        }
        auto &slot = s.slots[index];
        try {
            check(s.waitA(d.handle, 1, &slot.fence, VK_TRUE, 2000000000ull), "Wait for render GPU readback");
            const auto start = qpcNow();
            auto &upload = s.uploadB[ring]; auto fence = s.fencesB[ring]; auto cb = s.commandsB[ring];
            check(s.fnB<PFN_vkWaitForFences>("vkWaitForFences")(s.deviceB, 1, &fence, VK_TRUE, 2000000000ull), "Wait for second GPU upload slot");
            const auto &g = slot.guides;
            const VkDeviceSize colorBytes = VkDeviceSize(s.extent.width) * s.extent.height * dualgpu::texelBytes(s.format);
            const VkDeviceSize guideBytes = g.valid ? VkDeviceSize(g.renderWidth) * g.renderHeight * (dualgpu::texelBytes(g.depthFormat) + dualgpu::texelBytes(g.motionFormat)) : 0;
            memcpy(upload.mapped, slot.readback.mapped, size_t(colorBytes + guideBytes));
            dualgpu::Guides guides = slot.guides;
            {std::lock_guard<std::mutex> l(s.lock);slot.busy = false;slot.guides.valid = false;}
            s.wake.notify_all();
            // Acquire a B backbuffer.
            uint32_t image{};
            const auto acquire = s.presentation
                ? s.presentation->functions.pOutAcquireNextImageKHR(s.deviceB, s.presentation->handle(), 1000000000ull, s.acquiredB[ring], VK_NULL_HANDLE, &image)
                : s.fnB<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR")(s.deviceB, s.plainB, 1000000000ull, s.acquiredB[ring], VK_NULL_HANDLE, &image);
            if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) { ++s.dropped; continue; }
            check(s.fnB<PFN_vkResetFences>("vkResetFences")(s.deviceB, 1, &fence), "Second GPU fence reset");
            s.fnB<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(cb, 0);
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check(s.fnB<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb, &begin), "Second GPU begin");
            auto copy = s.fnB<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage");
            const VkPipelineStageFlags all = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dualBarrierB(s, cb, s.backbuffersB[image], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, all, VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy region{};region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};region.imageExtent = {s.extent.width, s.extent.height, 1};
            copy(cb, upload.buffer, s.backbuffersB[image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            dualBarrierB(s, cb, s.backbuffersB[image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, all);
            bool prepared = false;
            if (s.presentation && guides.valid) {
                try {
                    // Guide images on B (recreated only when the render size changes).
                    if (!s.depthB || s.depthInfoB.extent.width != guides.renderWidth || s.depthInfoB.extent.height != guides.renderHeight || s.depthInfoB.format != guides.depthFormat) {
                        check(s.fnB<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(s.deviceB), "Second GPU idle before resize");
                        for (auto [image2, memory] : {std::pair{&s.depthB, &s.depthMemoryB}, std::pair{&s.motionB, &s.motionMemoryB}}) {
                            if (*image2) s.fnB<PFN_vkDestroyImage>("vkDestroyImage")(s.deviceB, *image2, nullptr);
                            if (*memory) s.fnB<PFN_vkFreeMemory>("vkFreeMemory")(s.deviceB, *memory, nullptr);
                            *image2 = VK_NULL_HANDLE; *memory = VK_NULL_HANDLE;
                        }
                        dualCreateImageB(s, guides.depthFormat, guides.renderWidth, guides.renderHeight, s.depthB, s.depthMemoryB, s.depthInfoB);
                        dualCreateImageB(s, guides.motionFormat, guides.renderWidth, guides.renderHeight, s.motionB, s.motionMemoryB, s.motionInfoB);
                        s.depthLayoutB = s.motionLayoutB = VK_IMAGE_LAYOUT_UNDEFINED;
                    }
                    VkBufferImageCopy guide{};guide.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};guide.imageExtent = {guides.renderWidth, guides.renderHeight, 1};
                    guide.bufferOffset = colorBytes;
                    dualBarrierB(s, cb, s.depthB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, all, VK_PIPELINE_STAGE_TRANSFER_BIT);
                    copy(cb, upload.buffer, s.depthB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &guide);
                    guide.bufferOffset = colorBytes + VkDeviceSize(guides.renderWidth) * guides.renderHeight * dualgpu::texelBytes(guides.depthFormat);
                    dualBarrierB(s, cb, s.motionB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, all, VK_PIPELINE_STAGE_TRANSFER_BIT);
                    copy(cb, upload.buffer, s.motionB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &guide);
                    for (auto img : {s.depthB, s.motionB})
                        dualBarrierB(s, cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    const std::array<uint32_t, 6> key{guides.renderWidth, guides.renderHeight, s.extent.width, s.extent.height, uint32_t(guides.reverse), uint32_t(guides.infinite)};
                    if (!s.generator || key != s.generatorKey) {
                        s.generator.reset();
                        s.generator = std::make_unique<xpfsr::FrameGenerator>(d.runtime->api);
                        s.generator->create({s.deviceB, s.physicalB, s.gdpaB}, guides.renderWidth, guides.renderHeight, s.extent.width, s.extent.height,
                                            s.format, guides.reverse, guides.infinite, d.fgRequested, false, &s.memoryB);
                        s.generatorKey = key;
                        log("Second GPU frame generator: %s on %s", s.generator->provider().c_str(), s.nameB.c_str());
                    }
                    auto input = guides.params;
                    input.commands = cb;
                    input.depth = {s.depthB, s.depthInfoB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                    input.motion = {s.motionB, s.motionInfoB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                    s.generator->prepare(s.presentation->handle(), guides.sequence, input, guides.camera);
                    prepared = true;
                } catch (const std::exception &e) {
                    s.generator.reset();
                    log("Second GPU FG prepare unavailable: %s; real frames continue", e.what());
                }
            }
            if (s.presentation && !prepared && s.generator) s.generator->configure(s.presentation->handle(), guides.sequence, false);
            check(s.fnB<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb), "Second GPU end");
            const VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.waitSemaphoreCount = 1;si.pWaitSemaphores = &s.acquiredB[ring];si.pWaitDstStageMask = &wait;
            si.commandBufferCount = 1;si.pCommandBuffers = &cb;si.signalSemaphoreCount = 1;si.pSignalSemaphores = &s.renderedB[ring];
            check(s.fnB<PFN_vkQueueSubmit>("vkQueueSubmit")(s.queuesB[0], 1, &si, fence), "Second GPU submit");
            VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount = 1;present.pWaitSemaphores = &s.renderedB[ring];
            VkSwapchainKHR chain = s.presentation ? s.presentation->handle() : s.plainB;
            present.swapchainCount = 1;present.pSwapchains = &chain;present.pImageIndices = &image;
            const auto shown = s.presentation ? s.presentation->functions.pOutQueuePresentKHR(s.queuesB[0], &present)
                                              : s.fnB<PFN_vkQueuePresentKHR>("vkQueuePresentKHR")(s.queuesB[2] ? s.queuesB[2] : s.queuesB[0], &present);
            if (shown != VK_SUCCESS && shown != VK_SUBOPTIMAL_KHR) ++s.dropped;
            ++s.presented;
            s.uploadNs += uint64_t(qpcToNs(qpcNow() - start));
            if (s.generator) d.status.fgGenerated = s.generator->generated.load();
            d.status.displayedPresents = s.presented.load() + d.status.fgGenerated;
            if (s.presented % 600 == 1)
                log("Two-GPU frames: presented=%llu generated=%llu dropped=%llu avg copy-to-host=%.2f ms avg upload+present=%.2f ms",
                    (unsigned long long)s.presented.load(), (unsigned long long)d.status.fgGenerated, (unsigned long long)s.dropped.load(),
                    s.copyNs.load() / 1e6 / double(s.presented), s.uploadNs.load() / 1e6 / double(s.presented));
            ring = (ring + 1) % 3;
        } catch (const std::exception &e) {
            {std::lock_guard<std::mutex> l(s.lock);slot.busy = false;}
            s.wake.notify_all();
            ++s.dropped;
            log("Two-GPU frame skipped: %s", e.what());
        }
    }
}

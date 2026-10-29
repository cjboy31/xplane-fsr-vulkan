// SPDX-License-Identifier: MIT
// Exercise production shader recognition, descriptors, dispatch capture, GPU
// projection history and state restoration. No simulator code/assets required.
#include "layer.cpp"
#define wmain unusedBaselineMain
#include "probe_support.hpp"
#undef wmain
#include <iostream>
#include <cmath>
static std::vector<uint32_t> loadCode(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    xpfsr::require(bool(f), "Fixture shader missing");
    size_t bytes = size_t(f.tellg());
    xpfsr::require(bytes >= 20 && bytes % 4 == 0, "Fixture length");
    std::vector<uint32_t> w(bytes / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char *>(w.data()), bytes);
    return w;
}
static void identity(float *m) {
    for (int i = 0; i < 16; ++i)
        m[i] = i % 5 == 0 ? 1.f : 0.f;
}
static std::array<double, 4> transformPoint(const float *m, std::array<double, 4> v) {
    std::array<double, 4> out{};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            out[row] += double(m[col * 4 + row]) * v[col];
    return out;
}
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc == 3 && std::wstring(argv[1]) == L"--classify") {
            auto w = loadCode(argv[2]);
            const char *reason = nullptr;
            auto role = classifyShader(w.data(), w.size(), &reason);
            std::cout << roleName(role) << " " << (reason ? reason : "") << '\n';
            return role == ShaderRole::SsrScene2D || role == ShaderRole::SsrSceneArray ||
                           role == ShaderRole::SsrSceneMsaa
                       ? 0
                       : 1;
        }
        xpfsr::require(argc == 3, "native_capture_tests FIXTURE_SPV array=0/1");
        const bool array = std::wstring(argv[2]) == L"1";
        const uint32_t W = 64, H = 64;
        auto code = loadCode(argv[1]);
        auto expectedRole = array ? ShaderRole::SsrSceneArray : ShaderRole::SsrScene2D;
        xpfsr::require(classifyShader(code.data(), code.size()) == expectedRole,
                       "SSR interface classification");
        unsigned rejected = 0;
        for (size_t i = 5; i < code.size();) {
            uint32_t n = code[i] >> 16, op = code[i] & 65535;
            if (op == 72 && n >= 5 && code[i + 3] == 35 && code[i + 4] == 128) {
                auto bad = code;
                bad[i + 4] += 16;
                xpfsr::require(classifyShader(bad.data(), bad.size()) == ShaderRole::None,
                               "Changed uniform offset accepted");
                ++rejected;
            }
            if (op == 71 && n >= 4 && code[i + 2] == 33 && code[i + 3] == 3) {
                auto bad = code;
                bad[i + 3] = 9;
                xpfsr::require(classifyShader(bad.data(), bad.size()) == ShaderRole::None,
                               "Changed binding accepted");
                ++rejected;
            }
            i += n;
        }
        xpfsr::require(rejected >= 2, "Classifier tamper coverage");
        auto truncated = code;
        truncated[5] = 0;
        xpfsr::require(classifyShader(truncated.data(), truncated.size()) == ShaderRole::None,
                       "Malformed module accepted");
        Vulkan vk;
        vk.open();
        auto state = std::make_shared<Device>();
        auto &d = *state;
        d.handle = vk.device;
        d.physical = vk.physical;
        d.next = vk.gdpa;
        d.memory = vk.memory;
        d.graphicsFamily = vk.family;
        d.mode = Mode::Upscale;
        d.nativeMotionRequested = true;
        d.instance = std::make_shared<Instance>();
        d.instance->handle = vk.instance;
        d.instance->next = vk.gipa;
        {
            std::lock_guard<std::mutex> l(registryLock);
            devices[key(vk.device)] = state;
        }
        GpuImage source(vk), flags(vk), scratch(vk);
        flags.create(W, H, VK_FORMAT_R32_UINT);
        scratch.create(W, H, VK_FORMAT_R32_SFLOAT);
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_D32_SFLOAT;
        ci.extent = {W, H, 1};
        ci.mipLevels = ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        check(vk.dev<PFN_vkCreateImage>("vkCreateImage")(vk.device, &ci, nullptr,
                                                         &source.value.handle),
              "Fixture depth");
        source.value.description = ci;
        VkMemoryRequirements req{};
        vk.dev<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(
            vk.device, source.value.handle, &req);
        VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ma.allocationSize = req.size;
        ma.memoryTypeIndex = vk.memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vk.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(vk.device, &ma, nullptr,
                                                               &source.allocation),
              "Fixture depth memory");
        check(vk.dev<PFN_vkBindImageMemory>("vkBindImageMemory")(vk.device, source.value.handle,
                                                                 source.allocation, 0),
              "Fixture bind");
        auto view = [&](GpuImage &image, VkImageAspectFlags aspect, VkImageViewType type) {
            VkImageViewCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            c.image = image.value.handle;
            c.viewType = type;
            c.format = image.value.description.format;
            c.subresourceRange = {aspect, 0, 1, 0, 1};
            VkImageView out{};
            check(L_CreateImageView(vk.device, &c, nullptr, &out), "Fixture view");
            return out;
        };
        // Register only the metadata normally captured by the production hooks.
        d.images[source.value.handle] = {ci, 1};
        d.images[flags.value.handle] = {flags.value.description, 2};
        auto type = array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        auto posView = view(source, VK_IMAGE_ASPECT_DEPTH_BIT, type),
             flagView = view(flags, VK_IMAGE_ASPECT_COLOR_BIT, type),
             scratchView = view(scratch, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D);
        VkBuffer buffer{};
        VkDeviceMemory memory{};
        void *mapped{};
        VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        b.size = 4096 + W * H * 32;
        b.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        check(vk.dev<PFN_vkCreateBuffer>("vkCreateBuffer")(vk.device, &b, nullptr, &buffer),
              "Fixture buffer");
        vk.dev<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(vk.device,
                                                                                   buffer, &req);
        ma.allocationSize = req.size;
        ma.memoryTypeIndex =
            vk.memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vk.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(vk.device, &ma, nullptr, &memory),
              "Fixture host memory");
        check(vk.dev<PFN_vkBindBufferMemory>("vkBindBufferMemory")(vk.device, buffer, memory, 0),
              "Fixture buffer bind");
        check(
            vk.dev<PFN_vkMapMemory>("vkMapMemory")(vk.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped),
            "Fixture map");
        VkDescriptorSetLayoutBinding bindings[] = {
            {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
            {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
            {11, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
            {12, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
            {15, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT}};
        VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        lc.bindingCount = 5;
        lc.pBindings = bindings;
        VkDescriptorSetLayout layout{};
        check(L_CreateDescriptorSetLayout(vk.device, &lc, nullptr, &layout), "Fixture layout");
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pc.maxSets = 1;
        pc.poolSizeCount = 3;
        pc.pPoolSizes = sizes;
        VkDescriptorPool pool{};
        check(vk.dev<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(vk.device, &pc, nullptr,
                                                                           &pool),
              "Fixture pool");
        VkDescriptorSetAllocateInfo sa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        sa.descriptorPool = pool;
        sa.descriptorSetCount = 1;
        sa.pSetLayouts = &layout;
        VkDescriptorSet set{};
        check(L_AllocateDescriptorSets(vk.device, &sa, &set), "Fixture set");
        VkDescriptorImageInfo imgs[] = {{0, posView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                        {0, flagView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                        {0, scratchView, VK_IMAGE_LAYOUT_GENERAL}};
        VkDescriptorBufferInfo ubos[] = {{buffer, 0, 240}, {buffer, 512, 384}};
        VkWriteDescriptorSet writes[5]{};
        for (int i = 0; i < 5; ++i) {
            auto &w = writes[i];
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = set;
            w.dstBinding = bindings[i].binding;
            w.descriptorCount = 1;
            w.descriptorType = bindings[i].descriptorType;
            if (i < 2)
                w.pImageInfo = &imgs[i];
            else if (i < 4)
                w.pBufferInfo = &ubos[i - 2];
            else
                w.pImageInfo = &imgs[2];
        }
        L_UpdateDescriptorSets(vk.device, 5, writes, 0, nullptr);
        xpfsr::require(d.descriptors.at(set).at({11, 0}).buffer.range == 240,
                       "UBO descriptor tracking");
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &layout;
        VkPipelineLayout pipelineLayout{};
        check(vk.dev<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(vk.device, &pl, nullptr,
                                                                           &pipelineLayout),
              "Fixture pipeline layout");
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        sm.codeSize = code.size() * 4;
        sm.pCode = code.data();
        VkShaderModule module{};
        check(L_CreateShaderModule(vk.device, &sm, nullptr, &module), "Fixture module");
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cp.layout = pipelineLayout;
        cp.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cp.stage.module = module;
        cp.stage.pName = "main";
        VkPipeline pipeline{};
        check(L_CreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &cp, nullptr, &pipeline),
              "Fixture pipeline");
        xpfsr::require(d.nativeMotionPipelines.at(pipeline) == expectedRole,
                       "Production pipeline recognition");
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.queueFamilyIndex = vk.family;
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkCommandPool commandPool{};
        check(L_CreateCommandPool(vk.device, &pci, nullptr, &commandPool), "Fixture command pool");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ca.commandPool = commandPool;
        ca.commandBufferCount = 1;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        VkCommandBuffer cb{};
        check(L_AllocateCommandBuffers(vk.device, &ca, &cb), "Fixture command buffer");
        float previousProjection[16]{};
        bool previousValid = false;
        unsigned checks = 0;
        for (uint32_t frame = 1; frame <= 20; ++frame) {
            check(vk.dev<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(cb, 0), "Fixture reset");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check(L_BeginCommandBuffer(cb, &begin), "Fixture begin");
            d.camera = {};
            d.camera.valid = 1;
            d.camera.frame = frame;
            d.camera.reverseZ = 1;
            d.camera.reset = frame == 14;
            float *gb = static_cast<float *>(mapped), *ssr = gb + 128;
            memset(mapped, 0, 1024);
            gb[0] = W;
            gb[1] = H;
            gb[2] = 1.f / W;
            gb[3] = 1.f / H;
            float scale = frame >= 5 ? 1.25f : 1.f;
            gb[8] = 2.f / scale;
            gb[9] = 2.f / scale;
            gb[10] = -1.f / scale;
            gb[11] = -1.f / scale;
            gb[16] = 0;
            gb[17] = -.1f;
            gb[18] = 1;
            gb[19] = 0;
            ssr[0] = scale;
            ssr[5] = scale;
            ssr[11] = -1;
            ssr[14] = .1f;
            float angle = frame >= 8 ? .02f : 0.f;
            for (int slot : {32, 64}) {
                identity(ssr + slot);
                ssr[slot] = std::cos(angle);
                ssr[slot + 8] = std::sin(angle);
                ssr[slot + 2] = -std::sin(angle);
                ssr[slot + 10] = std::cos(angle);
            }
            ssr[44] = .025f;
            ssr[76] = -.05f;
            if (frame == 15)
                ssr[32] = NAN;
            if (frame == 16)
                ssr[44] = 10000;
            if (frame == 17)
                gb[0] = W + 7;
            auto *inputDepth = reinterpret_cast<float *>(static_cast<char *>(mapped) + 4096);
            auto *inputFlags = reinterpret_cast<uint32_t *>(inputDepth + W * H);
            for (uint32_t y = 0; y < H; ++y)
                for (uint32_t x = 0; x < W; ++x) {
                    size_t i = y * W + x;
                    inputDepth[i] = y < 8 ? 0.f : .01f;
                    inputFlags[i] = x < W / 2 ? 0 : 4;
                    if (frame == 18 && y >= 8)
                        inputDepth[i] = NAN;
                }
            auto upload = [&](GpuImage &image, VkImageAspectFlags aspect, uint64_t offset) {
                VkImageSubresourceRange range{aspect, 0, 1, 0, 1};
                barrier(d, cb, image.value.handle, image.value.layout,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range);
                VkBufferImageCopy c{};
                c.bufferOffset = offset;
                c.imageSubresource = {aspect, 0, 0, 1};
                c.imageExtent = {W, H, 1};
                vk.dev<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(
                    cb, buffer, image.value.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
                barrier(d, cb, image.value.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);
                image.value.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            };
            upload(source, VK_IMAGE_ASPECT_DEPTH_BIT, 4096);
            upload(flags, VK_IMAGE_ASPECT_COLOR_BIT, 4096 + W * H * 4);
            barrier(d, cb, scratch.value.handle, scratch.value.layout, VK_IMAGE_LAYOUT_GENERAL,
                    {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            scratch.value.layout = VK_IMAGE_LAYOUT_GENERAL;
            auto &command = d.commands[cb];
            Depth depth{};
            depth.image = source.value.handle;
            depth.view = posView;
            depth.epoch = frame;
            depth.fullArea = true;
            depth.jitterX = .25f;
            depth.jitterY = -.125f;
            command.depths[source.value.handle] = depth;
            // Proof gate only; the snapshot reads native SSR images, not this private copy.
            command.capturedDepths[source.value.handle] = std::make_shared<DepthCapture>();
            L_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            L_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set,
                                    0, nullptr);
            if (frame == 19)
                command.depths[source.value.handle].epoch = 0;
            if (frame == 20)
                command.secondary = true;
            L_CmdDispatch(cb, W / 8, H / 8, 1);
            bool captured = command.nativeMotion && command.nativeMotion->captured;
            xpfsr::require(captured == (frame < 19),
                           "Unsupported/stale native input should bypass");
            if (captured) {
                uint64_t offset = 4096 + W * H * 8;
                for (auto *image : {&command.nativeMotion->depth, &command.nativeMotion->motion,
                                    &command.nativeMotion->reactive}) {
                    transition(d, cb, *image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                    VkBufferImageCopy c{};
                    c.bufferOffset = offset;
                    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    c.imageExtent = {W, H, 1};
                    vk.dev<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(
                        cb, image->value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                        &c);
                    offset += W * H * 4;
                }
                // The app dispatch must still see its original pipeline/set after capture.
                vk.dev<PFN_vkCmdDispatch>("vkCmdDispatch")(cb, W / 8, H / 8, 1);
            }
            nativeDependency(d, cb);
            check(L_EndCommandBuffer(cb), "Fixture end");
            VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            sub.commandBufferCount = 1;
            sub.pCommandBuffers = &cb;
            check(L_QueueSubmit(vk.queue, 1, &sub, VK_NULL_HANDLE), "Fixture submit");
            check(vk.dev<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(vk.queue), "Fixture completion");
            if (captured) {
                const auto *depths =
                    reinterpret_cast<const float *>(static_cast<char *>(mapped) + 4096 + W * H * 8);
                const auto *motions = reinterpret_cast<const uint16_t *>(depths + W * H);
                const auto *reactive = reinterpret_cast<const uint8_t *>(depths + W * H * 2);
                for (uint32_t y = 0; y < H; ++y)
                    for (uint32_t x = 0; x < W; ++x) {
                        size_t i = y * W + x;
                        bool sky = y < 8, local = x >= W / 2 && !sky;
                        bool bad = frame == 17 || (frame == 18 && !sky) ||
                                   (!local && (frame == 15 || (frame == 16 && !sky)));
                        bool history = previousValid && frame != 14;
                        double mx = 0, my = 0;
                        if (history && !bad) {
                            double z = sky ? -1.0 : -10.0;
                            std::array<double, 4> point{
                                ((double(x) + .5 - .25) / W * 2 - 1) / scale * -z,
                                ((double(y) + .5 + .125) / H * 2 - 1) / scale * -z, z,
                                sky ? 0.0 : 1.0};
                            auto current = transformPoint(ssr, point);
                            auto prior = transformPoint(
                                previousProjection, transformPoint(ssr + (local ? 64 : 32), point));
                            mx = (prior[0] / prior[3] - current[0] / current[3]) * .5 * W;
                            my = (prior[1] / prior[3] - current[1] / current[3]) * .5 * H;
                        }
                        xpfsr::require(std::isfinite(half(motions[i * 2])) &&
                                           std::abs(half(motions[i * 2]) - mx) < .025 &&
                                           std::abs(half(motions[i * 2 + 1]) - my) < .025,
                                       "Native vector differs from independently projected point");
                        xpfsr::require(std::abs(depths[i] - (bad ? 0.f : inputDepth[i])) < 1e-5,
                                       "Native depth differs");
                        xpfsr::require((reactive[i] >= 254) == (bad || !history),
                                       "History rejection differs");
                        ++checks;
                    }
                memcpy(previousProjection, ssr, 64);
                previousValid = true;
                d.pathReady = true;
            }
            command.secondary = false;
            std::cout << "Native renderer frame " << frame << ": "
                      << (captured ? "GPU vectors/depth/history checked" : "safely bypassed")
                      << '\n';
        }
        check(vk.waitIdle(vk.device), "Fixture idle");
        d.commands.clear();
        for (auto p : {d.nativeMotion2D, d.nativeMotionArray, d.nativeMotionHistoryPipeline})
            if (p)
                vk.dev<PFN_vkDestroyPipeline>("vkDestroyPipeline")(vk.device, p, nullptr);
        vk.dev<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(
            vk.device, d.nativeMotionPipelineLayout, nullptr);
        vk.dev<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(
            vk.device, d.nativeMotionLayout, nullptr);
        vk.dev<PFN_vkDestroyBuffer>("vkDestroyBuffer")(vk.device, d.nativeMotionHistory, nullptr);
        vk.dev<PFN_vkFreeMemory>("vkFreeMemory")(vk.device, d.nativeMotionHistoryMemory, nullptr);
        vk.dev<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(vk.device, commandPool, nullptr);
        L_DestroyPipeline(vk.device, pipeline, nullptr);
        L_DestroyShaderModule(vk.device, module, nullptr);
        vk.dev<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(vk.device, pipelineLayout,
                                                                       nullptr);
        vk.dev<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(vk.device, pool, nullptr);
        L_DestroyDescriptorSetLayout(vk.device, layout, nullptr);
        for (auto v : {posView, flagView, scratchView})
            L_DestroyImageView(vk.device, v, nullptr);
        vk.dev<PFN_vkUnmapMemory>("vkUnmapMemory")(vk.device, memory);
        vk.dev<PFN_vkDestroyBuffer>("vkDestroyBuffer")(vk.device, buffer, nullptr);
        vk.dev<PFN_vkFreeMemory>("vkFreeMemory")(vk.device, memory, nullptr);
        {
            std::lock_guard<std::mutex> l(registryLock);
            devices.erase(key(vk.device));
        }
        std::cout << "PASS: " << checks
                  << " pixel comparisons; shader/descriptor/dispatch hooks, "
                     "world/body/sky/zoom/jitter/reset/invalid inputs, state restore and "
                     "unsupported captures; array="
                  << array << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

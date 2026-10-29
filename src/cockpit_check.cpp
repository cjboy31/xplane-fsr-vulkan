// SPDX-License-Identifier: MIT
// Isolated animated-texture fixture. Does not start or modify X-Plane.
#define wmain unusedProbeMain
#include "probe_support.hpp"
#undef wmain
#include "cockpit_vulkan.hpp"
#include "temporal_spv.hpp"
#include <fstream>
constexpr unsigned W = 128, H = 72;
struct Buffer {
    Vulkan &vk;
    VkBuffer handle{};
    VkDeviceMemory memory{};
    void *mapped{};
    void create(VkDeviceSize bytes, VkBufferUsageFlags usage) {
        VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        c.size = bytes;
        c.usage = usage;
        vkcheck(vk.dev<PFN_vkCreateBuffer>("vkCreateBuffer")(vk.device, &c, nullptr, &handle),
                "buffer");
        VkMemoryRequirements r{};
        vk.dev<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(vk.device,
                                                                                   handle, &r);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        a.allocationSize = r.size;
        a.memoryTypeIndex =
            vk.memoryType(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkcheck(vk.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(vk.device, &a, nullptr, &memory),
                "buffer memory");
        vkcheck(vk.dev<PFN_vkBindBufferMemory>("vkBindBufferMemory")(vk.device, handle, memory, 0),
                "buffer bind");
        vkcheck(vk.dev<PFN_vkMapMemory>("vkMapMemory")(vk.device, memory, 0, bytes, 0, &mapped),
                "buffer map");
    }
    ~Buffer() {
        if (mapped)
            vk.dev<PFN_vkUnmapMemory>("vkUnmapMemory")(vk.device, memory);
        if (handle)
            vk.dev<PFN_vkDestroyBuffer>("vkDestroyBuffer")(vk.device, handle, nullptr);
        if (memory)
            vk.dev<PFN_vkFreeMemory>("vkFreeMemory")(vk.device, memory, nullptr);
    }
};
struct alignas(16) Uniform {
    float reprojection[16]{}, inverseProjection[16]{}, bodyReprojection[16]{}, jitter[4]{},
        planes[4]{}, sphere[4]{};
};
static float picture(float x, float y, unsigned frame) {
    // A moving display tape with no geometry motion, and a soft cloud-like band.
    if (y < 36) {
        float center = 12 + float((frame * 3) % 96);
        return std::abs(x - center) < 3 ? .85f : .08f;
    }
    float center = 24 + float((frame * 2) % 72);
    return .12f + .65f * std::exp(-std::pow((x - center) / 7, 2));
}
int wmain(int argc, wchar_t **argv) {
    try {
        require(argc >= 2, "runtime directory required");
        Vulkan vk;
        vk.open();
        Runtime runtime;
        runtime.load(argv[1]);
        GpuImage source(vk), nativeDepth(vk), depth(vk), motion(vk), reactive(vk), output(vk);
        source.create(W, H, VK_FORMAT_R32G32B32A32_SFLOAT);
        nativeDepth.create(W, H, VK_FORMAT_R32_SFLOAT);
        depth.create(W, H, VK_FORMAT_R32_SFLOAT);
        motion.create(W, H, VK_FORMAT_R16G16_SFLOAT);
        reactive.create(W, H, VK_FORMAT_R8_UNORM);
        output.create(W, H, VK_FORMAT_R16G16B16A16_SFLOAT);
        Buffer uniform{vk}, staging{vk}, readback{vk};
        uniform.create(sizeof(Uniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        staging.create(W * H * 20, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        readback.create(W * H * 16 + W * H, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        VkImageView views[6]{};
        GpuImage *images[] = {&nativeDepth, &depth, &motion, &reactive, &output, &source};
        for (unsigned i = 0; i < 6; ++i) {
            VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            v.image = images[i]->value.handle;
            v.format = images[i]->value.description.format;
            v.viewType = i ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkcheck(vk.dev<PFN_vkCreateImageView>("vkCreateImageView")(vk.device, &v, nullptr,
                                                                       &views[i]),
                    "view");
        }
        VkSampler sampler{};
        VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        s.magFilter = s.minFilter = VK_FILTER_NEAREST;
        s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkcheck(vk.dev<PFN_vkCreateSampler>("vkCreateSampler")(vk.device, &s, nullptr, &sampler),
                "sampler");
        VkDescriptorSetLayoutBinding bindings[5]{};
        for (unsigned i = 0; i < 5; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            bindings[i].descriptorType = i == 0   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                         : i == 1 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                  : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        }
        VkDescriptorSetLayout layout{};
        VkDescriptorSetLayoutCreateInfo l{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        l.bindingCount = 5;
        l.pBindings = bindings;
        vkcheck(vk.dev<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(
                    vk.device, &l, nullptr, &layout),
                "layout");
        VkPipelineLayout pipelineLayout{};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &layout;
        vkcheck(vk.dev<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(
                    vk.device, &pl, nullptr, &pipelineLayout),
                "pipeline layout");
        VkShaderModule module{};
        VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        m.codeSize = sizeof(temporalSpv);
        m.pCode = temporalSpv;
        vkcheck(vk.dev<PFN_vkCreateShaderModule>("vkCreateShaderModule")(vk.device, &m, nullptr,
                                                                         &module),
                "shader");
        VkPipeline pipeline{};
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cp.layout = pipelineLayout;
        cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cp.stage.module = module;
        cp.stage.pName = "main";
        vkcheck(vk.dev<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(
                    vk.device, {}, 1, &cp, nullptr, &pipeline),
                "pipeline");
        VkDescriptorPool pool{};
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3}};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.maxSets = 1;
        dp.poolSizeCount = 3;
        dp.pPoolSizes = sizes;
        vkcheck(vk.dev<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(vk.device, &dp,
                                                                             nullptr, &pool),
                "descriptor pool");
        VkDescriptorSet set{};
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        da.descriptorPool = pool;
        da.descriptorSetCount = 1;
        da.pSetLayouts = &layout;
        vkcheck(
            vk.dev<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(vk.device, &da, &set),
            "set");
        VkDescriptorBufferInfo ub{uniform.handle, 0, sizeof(Uniform)};
        VkDescriptorImageInfo ii[4]{};
        for (unsigned i = 0; i < 4; ++i) {
            ii[i].imageView = views[i];
            ii[i].sampler = i ? VK_NULL_HANDLE : sampler;
            ii[i].imageLayout =
                i ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        VkWriteDescriptorSet writes[5]{};
        for (unsigned i = 0; i < 5; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = bindings[i].descriptorType;
            if (i)
                writes[i].pImageInfo = &ii[i - 1];
            else
                writes[i].pBufferInfo = &ub;
        }
        vk.dev<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(vk.device, 5, writes, 0,
                                                                     nullptr);
        VkCommandPool commands{};
        VkCommandPoolCreateInfo cc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cc.queueFamilyIndex = vk.family;
        cc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vkcheck(vk.dev<PFN_vkCreateCommandPool>("vkCreateCommandPool")(vk.device, &cc, nullptr,
                                                                       &commands),
                "commands");
        VkCommandBuffer cb{};
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ca.commandPool = commands;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;
        vkcheck(
            vk.dev<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(vk.device, &ca, &cb),
            "command");
        VkFence fence{};
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vkcheck(vk.dev<PFN_vkCreateFence>("vkCreateFence")(vk.device, &fc, nullptr, &fence),
                "fence");
        const std::string wanted =
            argc > 2 && std::wstring(argv[2]) == L"4.1.2" ? "4.1.2" : "3.1.6";
        std::cout << "Animated-texture provider: " << wanted << '\n';
        double errors[2]{};
        float masks[2][2]{};
        unsigned errorSamples[2]{};
        auto compositor = std::make_shared<cockpit::Compositor>(vk.device, vk.gdpa, vk.memory);
        cockpit::Frame restore(compositor, views[5], views[1], views[4],
                               VK_FORMAT_R16G16B16A16_SFLOAT);
        for (unsigned enabled = 0; enabled < 2; ++enabled) {
            Context context(runtime.api);
            Config config{W, H, W, H};
            VkDeviceContext backend{vk.device, vk.physical, vk.gdpa};
            config.providerId = providerVersion(runtime.api, backend, wanted);
            context.create(config, backend);
            for (unsigned frame = 1; frame <= 48; ++frame) {
                auto jitter = context.jitter(frame);
                Uniform u{};
                for (unsigned n = 0; n < 16; ++n)
                    u.reprojection[n] = u.inverseProjection[n] = u.bodyReprojection[n] =
                        (n % 5 == 0);
                u.jitter[0] = jitter[0];
                u.jitter[1] = jitter[1];
                u.jitter[2] = 1;
                u.jitter[3] = 1;
                u.planes[1] = 1;
                u.planes[2] = 0;
                memcpy(uniform.mapped, &u, sizeof(u));
                auto *pixels = static_cast<float *>(staging.mapped);
                for (unsigned y = 0; y < H; ++y)
                    for (unsigned x = 0; x < W; ++x) {
                        auto n = (y * W + x) * 4;
                        float value = picture(x + .5f - jitter[0], y + .5f - jitter[1], frame);
                        pixels[n] = pixels[n + 1] = pixels[n + 2] = value;
                        pixels[n + 3] = 1;
                        pixels[W * H * 4 + y * W + x] =
                            (x >= 20 && x < 28 && y >= 16 && y < 24) ? .2f : (y < 36 ? .5f : 1.f);
                    }
                vkcheck(vk.dev<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(cb, 0), "reset");
                VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkcheck(vk.dev<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb, &begin),
                        "begin");
                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent = {W, H, 1};
                source.transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                vk.dev<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(
                    cb, staging.handle, source.value.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &copy);
                source.transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                nativeDepth.transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                copy.bufferOffset = W * H * 16;
                vk.dev<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(
                    cb, staging.handle, nativeDepth.value.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                copy.bufferOffset = 0;
                nativeDepth.transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                for (auto *image : {&depth, &motion, &reactive})
                    image->transition(cb, VK_IMAGE_LAYOUT_GENERAL);
                vk.dev<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(
                    cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
                vk.dev<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(
                    cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
                vk.dev<PFN_vkCmdDispatch>("vkCmdDispatch")(cb, (W + 7) / 8, (H + 7) / 8, 1);
                for (auto *image : {&depth, &motion, &reactive})
                    image->transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                output.transition(cb, VK_IMAGE_LAYOUT_GENERAL);
                Frame f{};
                f.commands = cb;
                f.color = source.value;
                f.depth = depth.value;
                f.motion = motion.value;
                f.reactive = reactive.value;
                f.output = output.value;
                f.jitterX = jitter[0];
                f.jitterY = jitter[1];
                f.motionScaleX = f.motionScaleY = 1;
                f.deltaMilliseconds = 16.667f;
                f.nearPlane = .1f;
                f.verticalFovRadians = 1.0472f;
                f.sequence = frame;
                context.record(f);
                VkBufferImageCopy before{};
                before.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                before.imageExtent = {W, H, 1};
                output.transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                vk.dev<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(
                    cb, output.value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle,
                    1, &before);
                output.transition(cb, VK_IMAGE_LAYOUT_GENERAL);
                if (enabled) {
                    cockpit::Params p{};
                    p.outputInfo[2] = 2;
                    p.jitter[0] = jitter[0] / W;
                    p.jitter[1] = jitter[1] / H;
                    p.jitter[2] = 1;
                    p.jitter[3] = 2;
                    for (unsigned q = 0; q < 2; ++q) {
                        float left = q ? 68 : 8, right = q ? 120 : 60;
                        float xy[4][2] = {{left, 8}, {right, 8}, {right, 32}, {left, 32}};
                        for (unsigned i = 0; i < 4; ++i) {
                            auto *v = p.clip[q * 4 + i];
                            v[0] = xy[i][0] / W * 2 - 1;
                            v[1] = xy[i][1] / H * 2 - 1;
                            v[2] = .5f;
                            v[3] = 1;
                        }
                    }
                    require(compositor->record(restore, cb, p, W, H),
                            "Cockpit compositor recorded");
                }
                output.transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                copy.bufferOffset = W * H * 8;
                vk.dev<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(
                    cb, output.value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle,
                    1, &copy);
                reactive.transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                copy.bufferOffset = W * H * 16;
                vk.dev<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(
                    cb, reactive.value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    readback.handle, 1, &copy);
                VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
                host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                host.buffer = readback.handle;
                host.size = VK_WHOLE_SIZE;
                vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
                    cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
                    1, &host, 0, nullptr);
                vkcheck(vk.dev<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb), "end");
                vkcheck(vk.dev<PFN_vkResetFences>("vkResetFences")(vk.device, 1, &fence),
                        "reset fence");
                VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &cb;
                vkcheck(vk.dev<PFN_vkQueueSubmit>("vkQueueSubmit")(vk.queue, 1, &submit, fence),
                        "submit");
                vkcheck(vk.dev<PFN_vkWaitForFences>("vkWaitForFences")(vk.device, 1, &fence,
                                                                       VK_TRUE, 10000000000ull),
                        "completion");
                auto *mask = static_cast<unsigned char *>(readback.mapped) + W * H * 16;
                require(std::abs(mask[10 * W + 20] / 255.f - .05f) < .005, "cockpit mask value");
                require(std::abs(mask[55 * W + 20] / 255.f - 0.f) < .005, "sky mask value");
                masks[enabled][0] = mask[10 * W + 20] / 255.f;
                masks[enabled][1] = mask[55 * W + 20] / 255.f;
                auto *raw = static_cast<uint16_t *>(readback.mapped);
                auto *result = raw + W * H * 4;
                for (unsigned y = 0; y < H; ++y)
                    for (unsigned x = 0; x < W; ++x) {
                        bool outside = y < 8 || y >= 32 || x < 8 || x >= 120 || (x >= 60 && x < 68);
                        bool occluded = x >= 22 && x < 26 && y >= 18 && y < 22;
                        if (outside || occluded)
                            require(!memcmp(raw + (y * W + x) * 4, result + (y * W + x) * 4, 8),
                                    "Compositor changed outside/occluded pixels");
                    }
                if (frame > 8) {
                    for (unsigned y = 12; y < 28; ++y)
                        for (unsigned x = 12; x < 116; ++x) {
                            if (x >= 56 && x < 72)
                                continue;
                            if (x >= 18 && x < 30 && y >= 14 && y < 26)
                                continue;
                            float value = half(result[(y * W + x) * 4]);
                            require(std::isfinite(value), "finite cockpit output");
                            errors[enabled] += std::abs(value - picture(x + .5f, y + .5f, frame));
                            ++errorSamples[enabled];
                        }
                }
                if (frame == 48 && argc > 3) {
                    auto directory = std::filesystem::path(argv[3]);
                    std::filesystem::create_directories(directory);
                    std::ofstream image(directory / (enabled ? "restored.ppm" : "baseline.ppm"),
                                        std::ios::binary);
                    image << "P6\n" << W << ' ' << H << "\n255\n";
                    for (unsigned n = 0; n < W * H; ++n)
                        for (unsigned c = 0; c < 3; ++c) {
                            unsigned char value = static_cast<unsigned char>(
                                std::clamp(half(result[n * 4 + c]), 0.f, 1.f) * 255);
                            image.write(reinterpret_cast<char *>(&value), 1);
                        }
                    BITMAPFILEHEADER fileHeader{};
                    fileHeader.bfType = 0x4d42;
                    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
                    fileHeader.bfSize = fileHeader.bfOffBits + W * H * 3;
                    BITMAPINFOHEADER info{};
                    info.biSize = sizeof(info);
                    info.biWidth = W;
                    info.biHeight = -int(H);
                    info.biPlanes = 1;
                    info.biBitCount = 24;
                    info.biCompression = BI_RGB;
                    std::ofstream bmp(directory / (enabled ? "restored.bmp" : "baseline.bmp"),
                                      std::ios::binary);
                    bmp.write(reinterpret_cast<char *>(&fileHeader), sizeof(fileHeader));
                    bmp.write(reinterpret_cast<char *>(&info), sizeof(info));
                    for (unsigned n = 0; n < W * H; ++n)
                        for (int c = 2; c >= 0; --c) {
                            unsigned char v = static_cast<unsigned char>(
                                std::clamp(half(result[n * 4 + c]), 0.f, 1.f) * 255);
                            bmp.write(reinterpret_cast<char *>(&v), 1);
                        }
                }
            }
        }
        for (unsigned i = 0; i < 2; ++i)
            errors[i] /= errorSamples[i];
        std::cout << "Animated cockpit/cloud-like texture MAE: baseline=" << errors[0]
                  << " restored=" << errors[1]
                  << " improvement=" << (1 - errors[1] / errors[0]) * 100
                  << "%; cockpit mask=" << masks[0][0] << " -> " << masks[1][0]
                  << "; sky mask=" << masks[0][1] << " -> " << masks[1][1] << '\n';
        require(errors[1] < errors[0], "Cockpit restoration must reduce animated-screen error");
        if (argc > 3) {
            std::ofstream html(std::filesystem::path(argv[3]) / "comparison.html");
            html << "<!doctype html><meta charset=utf-8><title>Vulkan cockpit compositor "
                    "check</title><style>body{font:18px system-ui;max-width:1000px;margin:40px "
                    "auto;background:#101c2e;color:white}img{width:100%;image-rendering:pixelated}"
                    "section{display:grid;grid-template-columns:1fr "
                    "1fr;gap:20px}</style><h1>Vulkan cockpit compositor check</h1><p>Analytical "
                    "scene, not an X-Plane screenshot. Provider "
                 << wanted
                 << ". Lower screen error is better.</p><section><div><h2>FSR output</h2><img "
                    "src=baseline.bmp></div><div><h2>After current-frame restore</h2><img "
                    "src=restored.bmp></div></section><p>Mean error: "
                 << errors[0] << " to " << errors[1]
                 << ". Outside and depth-occluded pixels checked unchanged. Live SR22 alignment "
                    "remains unverified.</p>";
        }
        vkcheck(vk.waitIdle(vk.device), "idle");
        vk.dev<PFN_vkDestroyFence>("vkDestroyFence")(vk.device, fence, nullptr);
        vk.dev<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(vk.device, commands, nullptr);
        vk.dev<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(vk.device, pool, nullptr);
        vk.dev<PFN_vkDestroyPipeline>("vkDestroyPipeline")(vk.device, pipeline, nullptr);
        vk.dev<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(vk.device, module, nullptr);
        vk.dev<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(vk.device, pipelineLayout,
                                                                       nullptr);
        vk.dev<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(vk.device, layout,
                                                                                 nullptr);
        vk.dev<PFN_vkDestroySampler>("vkDestroySampler")(vk.device, sampler, nullptr);
        for (auto view : views)
            vk.dev<PFN_vkDestroyImageView>("vkDestroyImageView")(vk.device, view, nullptr);
        std::cout << "PASS: 96 completed GPU frames, actual FSR then Vulkan cockpit restoration; "
                     "outside/occluded pixels unchanged. This is not live X-Plane validation.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

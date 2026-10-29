// SPDX-License-Identifier: MIT
// Execute the production depth/motion packing shader and inspect its GPU output.
#define wmain unusedBaselineMain
#include "probe_support.hpp"
#undef wmain
#include <array>
#include <fstream>
#include "shared.hpp"
struct alignas(16) Uniform {
    float reprojection[16]{}, inverseProjection[16]{}, body[16]{}, jitter[4]{}, planes[4]{},
        sphere[4]{};
};
int wmain(int argc, wchar_t **argv) {
    try {
        bool benchmark = (argc == 5 || argc == 7) && std::wstring(argv[argc - 1]) == L"--benchmark";
        require(argc == 2 || argc == 4 || benchmark,
                "temporal_tests PRODUCTION_TEMPORAL_SPV [MSAA_COUNT FILL_SPV]");
        unsigned sampleCount = (argc == 4 || argc == 7) ? std::stoul(argv[2]) : 1;
        require(sampleCount == 1 || sampleCount == 2 || sampleCount == 4 || sampleCount == 8,
                "MSAA count");
        Vulkan vk;
        vk.open();
        uint32_t W = benchmark ? std::stoul(argv[sampleCount > 1 ? 4 : 2]) : 32;
        uint32_t H = benchmark ? std::stoul(argv[sampleCount > 1 ? 5 : 3]) : 16;
        require(W && H && W % 8 == 0 && H % 8 == 0 && W <= 4096 && H <= 2160,
                "Benchmark dimensions");
        GpuImage source(vk), depth(vk), motion(vk), reactive(vk);
        source.create(W, H, VK_FORMAT_R32_SFLOAT, 0, 0, VkSampleCountFlagBits(sampleCount));
        depth.create(W, H, VK_FORMAT_R32_SFLOAT);
        motion.create(W, H, VK_FORMAT_R16G16_SFLOAT);
        reactive.create(W, H, VK_FORMAT_R8_UNORM);
        VkBuffer buffer{};
        VkDeviceMemory memory{};
        void *mapped{};
        VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bc.size = 1024 + uint64_t(W) * H * 12;
        bc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkcheck(vk.dev<PFN_vkCreateBuffer>("vkCreateBuffer")(vk.device, &bc, nullptr, &buffer),
                "Stability buffer");
        VkMemoryRequirements mr{};
        vk.dev<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(vk.device,
                                                                                   buffer, &mr);
        VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ma.allocationSize = mr.size;
        ma.memoryTypeIndex =
            vk.memoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkcheck(vk.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(vk.device, &ma, nullptr, &memory),
                "Stability memory");
        vkcheck(vk.dev<PFN_vkBindBufferMemory>("vkBindBufferMemory")(vk.device, buffer, memory, 0),
                "Stability bind");
        vkcheck(
            vk.dev<PFN_vkMapMemory>("vkMapMemory")(vk.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped),
            "Stability map");
        VkImageView arrayView{};
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = source.value.handle;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vi.format = VK_FORMAT_R32_SFLOAT;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkcheck(
            vk.dev<PFN_vkCreateImageView>("vkCreateImageView")(vk.device, &vi, nullptr, &arrayView),
            "Depth array view");
        VkImageView outputViews[3]{};
        unsigned viewIndex = 0;
        for (auto *image : {&depth, &motion, &reactive}) {
            vi.image = image->value.handle;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = image->value.description.format;
            vkcheck(vk.dev<PFN_vkCreateImageView>("vkCreateImageView")(vk.device, &vi, nullptr,
                                                                       &outputViews[viewIndex++]),
                    "Packed output view");
        }
        VkSampler sampler{};
        VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sc.magFilter = sc.minFilter = VK_FILTER_NEAREST;
        sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkcheck(vk.dev<PFN_vkCreateSampler>("vkCreateSampler")(vk.device, &sc, nullptr, &sampler),
                "Stability sampler");
        VkDescriptorSetLayoutBinding bindings[6]{};
        for (uint32_t i = 0; i < (sampleCount > 1 ? 6u : 5u); ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            bindings[i].descriptorType = i == 0   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                         : i == 1 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                  : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        }
        VkDescriptorSetLayout layout{};
        VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        lc.bindingCount = sampleCount > 1 ? 6 : 5;
        lc.pBindings = bindings;
        vkcheck(vk.dev<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(
                    vk.device, &lc, nullptr, &layout),
                "Stability descriptor layout");
        VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                                            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4}};
        VkDescriptorPool pool{};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pc.maxSets = 1;
        pc.poolSizeCount = 3;
        pc.pPoolSizes = poolSizes;
        vkcheck(vk.dev<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(vk.device, &pc,
                                                                             nullptr, &pool),
                "Stability descriptor pool");
        VkDescriptorSet set{};
        VkDescriptorSetAllocateInfo sa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        sa.descriptorPool = pool;
        sa.descriptorSetCount = 1;
        sa.pSetLayouts = &layout;
        vkcheck(
            vk.dev<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(vk.device, &sa, &set),
            "Stability descriptor set");
        VkDescriptorBufferInfo bi{buffer, 0, sizeof(Uniform)};
        VkDescriptorImageInfo images[] = {
            {sampler, arrayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {0, outputViews[0], VK_IMAGE_LAYOUT_GENERAL},
            {0, outputViews[1], VK_IMAGE_LAYOUT_GENERAL},
            {0, outputViews[2], VK_IMAGE_LAYOUT_GENERAL},
            {0, arrayView, VK_IMAGE_LAYOUT_GENERAL}};
        VkWriteDescriptorSet writes[6]{};
        for (uint32_t i = 0; i < (sampleCount > 1 ? 6u : 5u); ++i) {
            auto &w = writes[i];
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = set;
            w.dstBinding = i;
            w.descriptorCount = 1;
            w.descriptorType = bindings[i].descriptorType;
            if (i == 0)
                w.pBufferInfo = &bi;
            else
                w.pImageInfo = &images[i - 1];
        }
        vk.dev<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(
            vk.device, sampleCount > 1 ? 6 : 5, writes, 0, nullptr);
        std::ifstream in(std::filesystem::path(argv[1]), std::ios::binary | std::ios::ate);
        require(bool(in), "Temporal shader missing");
        size_t bytes = size_t(in.tellg());
        std::vector<uint32_t> code(bytes / 4);
        require(bytes && bytes % 4 == 0, "Temporal shader size");
        in.seekg(0);
        in.read(reinterpret_cast<char *>(code.data()), bytes);
        VkShaderModule module{};
        VkShaderModuleCreateInfo mc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mc.codeSize = bytes;
        mc.pCode = code.data();
        vkcheck(vk.dev<PFN_vkCreateShaderModule>("vkCreateShaderModule")(vk.device, &mc, nullptr,
                                                                         &module),
                "Temporal module");
        VkPipelineLayout pipelineLayout{};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &layout;
        vkcheck(vk.dev<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(
                    vk.device, &pl, nullptr, &pipelineLayout),
                "Temporal pipeline layout");
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cp.layout = pipelineLayout;
        cp.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cp.stage.module = module;
        cp.stage.pName = "main";
        VkPipeline pipeline{};
        vkcheck(vk.dev<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(
                    vk.device, 0, 1, &cp, nullptr, &pipeline),
                "Temporal pipeline");
        VkShaderModule fillModule{};
        VkPipeline fillPipeline{};
        if (sampleCount > 1) {
            std::ifstream fill(std::filesystem::path(argv[3]), std::ios::binary | std::ios::ate);
            require(bool(fill), "MSAA fill shader missing");
            size_t count = size_t(fill.tellg());
            require(count && count % 4 == 0, "MSAA fill shader size");
            std::vector<uint32_t> fillCode(count / 4);
            fill.seekg(0);
            fill.read(reinterpret_cast<char *>(fillCode.data()), count);
            mc.codeSize = count;
            mc.pCode = fillCode.data();
            vkcheck(vk.dev<PFN_vkCreateShaderModule>("vkCreateShaderModule")(vk.device, &mc,
                                                                             nullptr, &fillModule),
                    "MSAA fill module");
            cp.stage.module = fillModule;
            vkcheck(vk.dev<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(
                        vk.device, 0, 1, &cp, nullptr, &fillPipeline),
                    "MSAA fill pipeline");
        }
        VkCommandPool commandPool{};
        VkCommandPoolCreateInfo cc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cc.queueFamilyIndex = vk.family;
        vkcheck(vk.dev<PFN_vkCreateCommandPool>("vkCreateCommandPool")(vk.device, &cc, nullptr,
                                                                       &commandPool),
                "Temporal command pool");
        VkCommandBuffer cb{};
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ca.commandPool = commandPool;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;
        vkcheck(
            vk.dev<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(vk.device, &ca, &cb),
            "Temporal commands");
        VkQueryPool timing{};
        VkPhysicalDeviceProperties timingProperties{};
        if (benchmark) {
            vk.inst<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(
                vk.physical, &timingProperties);
            require(timingProperties.limits.timestampComputeAndGraphics,
                    "Compute timestamps required");
            VkQueryPoolCreateInfo qc{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qc.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qc.queryCount = 2;
            vkcheck(vk.dev<PFN_vkCreateQueryPool>("vkCreateQueryPool")(vk.device, &qc, nullptr,
                                                                       &timing),
                    "Timing pool");
        }
        for (unsigned test = 0; test < (benchmark ? 2u : 20u); ++test) {
            if (!benchmark && sampleCount == 1 && test >= 8 && test < 12)
                continue;
            Uniform u{};
            for (int k = 0; k < 16; ++k)
                u.reprojection[k] = u.inverseProjection[k] = u.body[k] = k % 5 == 0;
            u.jitter[2] = -1;
            u.planes[0] = 1;
            u.sphere[3] = 20;
            float sample = .5f;
            if (test == 1) {
                sample = 0;
                u.planes[2] = 1;
            }
            if (test == 2)
                sample = NAN;
            if (test == 3)
                sample = INFINITY;
            if (test == 4)
                u.reprojection[12] = 1e6;
            if (test == 5)
                u.reprojection[15] = -1;
            if (test == 6) {
                u.reprojection[12] = .25f;
                u.reprojection[13] = .1f;
            }
            if (test == 7) {
                u.jitter[0] = .25f;
                u.jitter[1] = -.25f;
            }
            double referenceFx = 1.3, referenceFy = 1.7, previousFx = referenceFx,
                   previousFy = referenceFy;
            double referenceA = 0, referenceB = .1, tx = 0, ty = 0, angle = 0;
            if (test >= 12) {
                if (test == 18) {
                    u.planes[0] = 0;
                    referenceA = 100. / (.1 - 100.);
                    referenceB = 100. * .1 / (.1 - 100.);
                }
                if (test == 12 || test == 15) {
                    tx = .03;
                    angle = .04;
                }
                if (test == 13 || test == 18) {
                    previousFx *= 1.15;
                    previousFy *= 1.15;
                }
                if (test == 14) {
                    u.jitter[3] = 1;
                    u.planes[1] = 1;
                    tx = 50;
                }
                if (test == 16) {
                    u.jitter[3] = 1;
                }
                if (test == 19) {
                    u.jitter[2] = 1;
                    ty = .02;
                }
                double inverseP[16] = {1 / referenceFx,
                                       0,
                                       0,
                                       0,
                                       0,
                                       1 / referenceFy,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       1 / referenceB,
                                       0,
                                       0,
                                       -1,
                                       referenceA / referenceB};
                double previousP[16] = {previousFx, 0,          0,  0, 0, previousFy, 0, 0, 0,
                                        0,          referenceA, -1, 0, 0, referenceB, 0};
                double c = std::cos(angle), s = std::sin(angle);
                double previousView[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, tx, ty, 0, 1},
                       pv[16], reprojection[16];
                app::multiply(pv, previousP, previousView);
                app::multiply(reprojection, pv, inverseP);
                for (int k = 0; k < 16; ++k) {
                    u.inverseProjection[k] = float(inverseP[k]);
                    u.reprojection[k] = float(reprojection[k]);
                }
                if (test == 16)
                    u.inverseProjection[0] = NAN;
                if (test == 17)
                    u.reprojection[15] = -1;
            }
            if (benchmark) {
                sample = .5f;
                u.jitter[3] = float(test);
                u.planes[1] = 1;
                u.body[12] = .01f;
            }
            u.planes[3] = float(test);
            u.sphere[0] = sample;
            if (test == 8 || test == 10)
                u.planes[0] = 0;
            memcpy(mapped, &u, sizeof(u));
            vkcheck(vk.dev<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(cb, 0),
                    "Temporal reset");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            vkcheck(vk.dev<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb, &begin),
                    "Temporal begin");
            if (sampleCount > 1) {
                source.transition(cb, VK_IMAGE_LAYOUT_GENERAL);
                vk.dev<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(
                    cb, VK_PIPELINE_BIND_POINT_COMPUTE, fillPipeline);
                vk.dev<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(
                    cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
                vk.dev<PFN_vkCmdDispatch>("vkCmdDispatch")(cb, W / 8, H / 8, 1);
            } else {
                source.transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                VkClearColorValue clear{{sample, 0, 0, 0}};
                VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vk.dev<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(
                    cb, source.value.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1,
                    &range);
            }
            source.transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            for (auto *image : {&depth, &motion, &reactive})
                image->transition(cb, VK_IMAGE_LAYOUT_GENERAL);
            vk.dev<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                               pipeline);
            vk.dev<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(
                cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
            unsigned iterations = benchmark ? 64 : 1;
            if (benchmark) {
                vk.dev<PFN_vkCmdResetQueryPool>("vkCmdResetQueryPool")(cb, timing, 0, 2);
                vk.dev<PFN_vkCmdWriteTimestamp>("vkCmdWriteTimestamp")(
                    cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timing, 0);
            }
            for (unsigned iteration = 0; iteration < iterations; ++iteration) {
                vk.dev<PFN_vkCmdDispatch>("vkCmdDispatch")(cb, W / 8, H / 8, 1);
                if (benchmark) {
                    VkMemoryBarrier reuse{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                    reuse.srcAccessMask = reuse.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
                        cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &reuse, 0, nullptr, 0, nullptr);
                }
            }
            if (benchmark)
                vk.dev<PFN_vkCmdWriteTimestamp>("vkCmdWriteTimestamp")(
                    cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing, 1);
            uint64_t offset = 1024;
            for (auto *image : {&depth, &motion, &reactive}) {
                image->transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                VkBufferImageCopy copy{};
                copy.bufferOffset = offset;
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent = {W, H, 1};
                vk.dev<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(
                    cb, image->value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                    &copy);
                offset += W * H * 4;
            }
            VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
                cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0,
                nullptr, 0, nullptr);
            vkcheck(vk.dev<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb), "Temporal end");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &cb;
            vkcheck(vk.dev<PFN_vkQueueSubmit>("vkQueueSubmit")(vk.queue, 1, &submit, 0),
                    "Temporal submit");
            vkcheck(vk.waitIdle(vk.device), "Temporal completion");
            if (benchmark) {
                uint64_t ticks[2]{};
                vkcheck(vk.dev<PFN_vkGetQueryPoolResults>("vkGetQueryPoolResults")(
                            vk.device, timing, 0, 2, sizeof(ticks), ticks, sizeof(uint64_t),
                            VK_QUERY_RESULT_64_BIT),
                        "GPU timing readback");
                std::cout << "TIMING width=" << W << " height=" << H << " samples=" << sampleCount
                          << " internal=" << test << " iterations=" << iterations << " ms="
                          << double(ticks[1] - ticks[0]) * timingProperties.limits.timestampPeriod /
                                 iterations / 1e6
                          << "\n";
                continue;
            }
            auto *packedDepth = reinterpret_cast<float *>(static_cast<char *>(mapped) + 1024);
            auto *packedMotion =
                reinterpret_cast<uint16_t *>(static_cast<char *>(mapped) + 1024 + W * H * 4);
            auto *mask =
                reinterpret_cast<uint8_t *>(static_cast<char *>(mapped) + 1024 + W * H * 8);
            for (unsigned pixel = 0; pixel < W * H; ++pixel) {
                require(std::isfinite(packedDepth[pixel]) && packedDepth[pixel] >= 0 &&
                            packedDepth[pixel] <= 1,
                        "Invalid packed depth escaped");
                float mx = half(packedMotion[pixel * 2]), my = half(packedMotion[pixel * 2 + 1]);
                require(std::isfinite(mx) && std::isfinite(my), "Invalid packed motion escaped");
                if (test == 0 || test == 7)
                    require(mask[pixel] == 0 && std::abs(mx) < .001 && std::abs(my) < .001,
                            "External ground or jitter discarded valid history");
                if (test == 1)
                    require(mask[pixel] <= 64, "Sky history rejected too aggressively for AA");
                if (test >= 2 && test <= 5)
                    require(mask[pixel] == 255 && mx == 0 && my == 0,
                            "Invalid depth/projection was not rejected");
                if (test >= 8 && test < 12) {
                    float expected = test == 8 ? .2f : test == 9 ? .8f : test == 10 ? .35f : 0.f;
                    require(std::abs(packedDepth[pixel] - expected) < .00001f,
                            "Multisample depth must select nearest valid geometry in either Z "
                            "convention");
                    require(mask[pixel] == (test == 11 ? 255 : 0), "MSAA invalid-sample handling");
                }
                if (test >= 12) {
                    float expectedX = 0, expectedY = 0;
                    if (test == 16 || test == 17) {
                        require(mask[pixel] == 255 && mx == 0 && my == 0,
                                "Invalid camera matrix must reject history");
                    } else {
                        unsigned x = pixel % W, y = pixel / W;
                        double u0 = (x + .5) / W, v0 = (y + .5) / H;
                        double distance = referenceB / (.5 + referenceA);
                        double wx = (u0 * 2 - 1) * distance / referenceFx;
                        double wy = (v0 * 2 - 1) * u.jitter[2] * distance / referenceFy;
                        double wz = -distance;
                        double px = std::cos(angle) * wx + std::sin(angle) * wz + tx;
                        double py = wy + ty, pz = -std::sin(angle) * wx + std::cos(angle) * wz;
                        if (test != 14) {
                            expectedX = float(((previousFx * px / -pz) * .5 + .5 - u0) * W);
                            expectedY =
                                float(((previousFy * py / -pz) * u.jitter[2] * .5 + .5 - v0) * H);
                        }
                        require(std::abs(mx - expectedX) < .01 && std::abs(my - expectedY) < .01,
                                "Motion differs from independently projected 3D scene flow");
                        require(mask[pixel] == (test == 14 ? 13 : 0),
                                "Camera/body classification differs");
                    }
                }
                if (test == 6)
                    require(std::abs(mx - 4) < .01 && std::abs(my + .8) < .01,
                            "External pan reprojection sign/scale differs");
            }
            std::cout << "PASS temporal GPU samples=" << sampleCount << " case " << test << ": "
                      << W * H << " pixels verified\n";
        }
        if (timing)
            vk.dev<PFN_vkDestroyQueryPool>("vkDestroyQueryPool")(vk.device, timing, nullptr);
        vk.dev<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(vk.device, commandPool, nullptr);
        vk.dev<PFN_vkDestroyPipeline>("vkDestroyPipeline")(vk.device, pipeline, nullptr);
        if (fillPipeline)
            vk.dev<PFN_vkDestroyPipeline>("vkDestroyPipeline")(vk.device, fillPipeline, nullptr);
        if (fillModule)
            vk.dev<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(vk.device, fillModule,
                                                                       nullptr);
        vk.dev<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(vk.device, pipelineLayout,
                                                                       nullptr);
        vk.dev<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(vk.device, module, nullptr);
        vk.dev<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(vk.device, pool, nullptr);
        vk.dev<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(vk.device, layout,
                                                                                 nullptr);
        vk.dev<PFN_vkDestroySampler>("vkDestroySampler")(vk.device, sampler, nullptr);
        vk.dev<PFN_vkDestroyImageView>("vkDestroyImageView")(vk.device, arrayView, nullptr);
        for (auto view : outputViews)
            vk.dev<PFN_vkDestroyImageView>("vkDestroyImageView")(vk.device, view, nullptr);
        vk.dev<PFN_vkUnmapMemory>("vkUnmapMemory")(vk.device, memory);
        vk.dev<PFN_vkDestroyBuffer>("vkDestroyBuffer")(vk.device, buffer, nullptr);
        vk.dev<PFN_vkFreeMemory>("vkFreeMemory")(vk.device, memory, nullptr);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

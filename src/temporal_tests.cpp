// SPDX-License-Identifier: MIT
// Execute the production depth/motion packing shader and inspect its GPU output.
#define wmain unusedBaselineMain
#include "probe_support.hpp"
#undef wmain
#include <array>
#include <fstream>
struct alignas(16) Uniform {
    float reprojection[16]{}, inverseProjection[16]{}, body[16]{}, jitter[4]{}, planes[4]{},
        sphere[4]{};
};
int wmain(int argc, wchar_t **argv) {
    try {
        require(argc == 2, "temporal_tests PRODUCTION_TEMPORAL_SPV");
        Vulkan vk;
        vk.open();
        constexpr uint32_t W = 32, H = 16;
        GpuImage source(vk), depth(vk), motion(vk), reactive(vk);
        source.create(W, H, VK_FORMAT_R32_SFLOAT);
        depth.create(W, H, VK_FORMAT_R32_SFLOAT);
        motion.create(W, H, VK_FORMAT_R16G16_SFLOAT);
        reactive.create(W, H, VK_FORMAT_R8_UNORM);
        VkBuffer buffer{};
        VkDeviceMemory memory{};
        void *mapped{};
        VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bc.size = 8192;
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
        VkDescriptorSetLayoutBinding bindings[5]{};
        for (uint32_t i = 0; i < 5; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            bindings[i].descriptorType = i == 0   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                         : i == 1 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                  : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        }
        VkDescriptorSetLayout layout{};
        VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        lc.bindingCount = 5;
        lc.pBindings = bindings;
        vkcheck(vk.dev<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(
                    vk.device, &lc, nullptr, &layout),
                "Stability descriptor layout");
        VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                                            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3}};
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
            {0, outputViews[2], VK_IMAGE_LAYOUT_GENERAL}};
        VkWriteDescriptorSet writes[5]{};
        for (uint32_t i = 0; i < 5; ++i) {
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
        vk.dev<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(vk.device, 5, writes, 0,
                                                                     nullptr);
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
        for (unsigned test = 0; test < 8; ++test) {
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
            memcpy(mapped, &u, sizeof(u));
            vkcheck(vk.dev<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(cb, 0),
                    "Temporal reset");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            vkcheck(vk.dev<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb, &begin),
                    "Temporal begin");
            source.transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkClearColorValue clear{{sample, 0, 0, 0}};
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vk.dev<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(
                cb, source.value.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
            source.transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            for (auto *image : {&depth, &motion, &reactive})
                image->transition(cb, VK_IMAGE_LAYOUT_GENERAL);
            vk.dev<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                               pipeline);
            vk.dev<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(
                cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
            vk.dev<PFN_vkCmdDispatch>("vkCmdDispatch")(cb, W / 8, H / 8, 1);
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
                if (test == 6)
                    require(std::abs(mx - 4) < .01 && std::abs(my + .8) < .01,
                            "External pan reprojection sign/scale differs");
            }
            std::cout << "PASS temporal GPU case " << test << ": " << W * H << " pixels verified\n";
        }
        vk.dev<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(vk.device, commandPool, nullptr);
        vk.dev<PFN_vkDestroyPipeline>("vkDestroyPipeline")(vk.device, pipeline, nullptr);
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

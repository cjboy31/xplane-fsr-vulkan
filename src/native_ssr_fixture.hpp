// SPDX-License-Identifier: MIT
// Original SSR-interface stand-in for the loader -> native pack -> FSR fixture.
#pragma once
struct NativeSsrFixture {
    Vulkan &vk;
    GpuImage flags, scratch;
    VkImageView depthView{}, flagView{}, scratchView{};
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    void *mapped{};
    VkDescriptorSetLayout layout{};
    VkDescriptorPool pool{};
    VkDescriptorSet set{};
    VkPipelineLayout pipelineLayout{};
    VkPipeline pipeline{};
    NativeSsrFixture(Vulkan &owner, VkImage depth, bool array, const std::filesystem::path &shader)
        : vk(owner), flags(owner), scratch(owner) {
        flags.create(256, 144, VK_FORMAT_R32_UINT);
        scratch.create(256, 144, VK_FORMAT_R32_SFLOAT);
        auto view = [&](VkImage image, VkFormat format, VkImageAspectFlags aspect,
                        VkImageViewType type) {
            VkImageViewCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            c.image = image;
            c.viewType = type;
            c.format = format;
            c.subresourceRange = {aspect, 0, 1, 0, 1};
            VkImageView out{};
            vkcheck(
                vk.dev<PFN_vkCreateImageView>("vkCreateImageView")(vk.device, &c, nullptr, &out),
                "SSR fixture view");
            return out;
        };
        auto type = array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        depthView = view(depth, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT, type);
        flagView = view(flags.value.handle, VK_FORMAT_R32_UINT, VK_IMAGE_ASPECT_COLOR_BIT, type);
        scratchView = view(scratch.value.handle, VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_VIEW_TYPE_2D);
        VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        b.size = 1024;
        b.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        vkcheck(vk.dev<PFN_vkCreateBuffer>("vkCreateBuffer")(vk.device, &b, nullptr, &buffer),
                "SSR fixture UBO");
        VkMemoryRequirements req{};
        vk.dev<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(vk.device,
                                                                                   buffer, &req);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        a.allocationSize = req.size;
        a.memoryTypeIndex =
            vk.memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkcheck(vk.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(vk.device, &a, nullptr, &memory),
                "SSR fixture memory");
        vkcheck(vk.dev<PFN_vkBindBufferMemory>("vkBindBufferMemory")(vk.device, buffer, memory, 0),
                "SSR fixture bind");
        vkcheck(
            vk.dev<PFN_vkMapMemory>("vkMapMemory")(vk.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped),
            "SSR fixture map");
        memset(mapped, 0, 1024);
        auto *gb = static_cast<float *>(mapped);
        gb[0] = 256;
        gb[1] = 144;
        gb[2] = 1.f / 256;
        gb[3] = 1.f / 144;
        gb[8] = gb[9] = 2;
        gb[10] = gb[11] = -1;
        gb[17] = -.1f;
        gb[18] = 1;
        auto *ssr = gb + 128;
        ssr[0] = ssr[5] = 1;
        ssr[11] = -1;
        ssr[14] = .1f;
        for (unsigned m : {32u, 64u})
            for (unsigned i = 0; i < 16; ++i)
                ssr[m + i] = i % 5 == 0 ? 1.f : 0.f;
        VkDescriptorSetLayoutBinding bindings[] = {
            {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
            {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
            {11, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
            {12, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
            {15, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT}};
        VkDescriptorSetLayoutCreateInfo l{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        l.bindingCount = 5;
        l.pBindings = bindings;
        vkcheck(vk.dev<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(
                    vk.device, &l, nullptr, &layout),
                "SSR fixture layout");
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
        VkDescriptorPoolCreateInfo p{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        p.maxSets = 1;
        p.poolSizeCount = 3;
        p.pPoolSizes = sizes;
        vkcheck(vk.dev<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(vk.device, &p, nullptr,
                                                                             &pool),
                "SSR fixture pool");
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = pool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &layout;
        vkcheck(vk.dev<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(vk.device, &alloc,
                                                                                 &set),
                "SSR fixture set");
        VkDescriptorImageInfo imgs[] = {{0, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                        {0, flagView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                        {0, scratchView, VK_IMAGE_LAYOUT_GENERAL}};
        VkDescriptorBufferInfo ubos[] = {{buffer, 0, 240}, {buffer, 512, 384}};
        VkWriteDescriptorSet writes[5]{};
        for (int i = 0; i < 5; ++i) {
            auto &w = writes[i];
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = set;
            w.dstBinding = bindings[i].binding;
            w.descriptorType = bindings[i].descriptorType;
            w.descriptorCount = 1;
            if (i < 2)
                w.pImageInfo = &imgs[i];
            else if (i < 4)
                w.pBufferInfo = &ubos[i - 2];
            else
                w.pImageInfo = &imgs[2];
        }
        vk.dev<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(vk.device, 5, writes, 0,
                                                                     nullptr);
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &layout;
        vkcheck(vk.dev<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(
                    vk.device, &pl, nullptr, &pipelineLayout),
                "SSR fixture pipeline layout");
        auto words = loadSpv(shader.c_str());
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        sm.codeSize = words.size() * 4;
        sm.pCode = words.data();
        VkShaderModule module{};
        vkcheck(vk.dev<PFN_vkCreateShaderModule>("vkCreateShaderModule")(vk.device, &sm, nullptr,
                                                                         &module),
                "SSR fixture module");
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cp.layout = pipelineLayout;
        cp.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cp.stage.module = module;
        cp.stage.pName = "main";
        auto result = vk.dev<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(
            vk.device, 0, 1, &cp, nullptr, &pipeline);
        vk.dev<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(vk.device, module, nullptr);
        vkcheck(result, "SSR fixture pipeline");
    }
    void record(VkCommandBuffer cb, GpuImage &depth) {
        auto barrier = [&](GpuImage &image, VkImageLayout target, VkImageAspectFlags aspect) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.srcAccessMask = image.value.layout == VK_IMAGE_LAYOUT_UNDEFINED
                                  ? 0
                                  : VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
            b.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
            b.oldLayout = image.value.layout;
            b.newLayout = target;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image.value.handle;
            b.subresourceRange = {aspect, 0, 1, 0, 1};
            vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
                cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                nullptr, 0, nullptr, 1, &b);
            image.value.layout = target;
        };
        barrier(depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
        barrier(flags, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VkClearColorValue clear{};
        clear.uint32[0] = 4;
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vk.dev<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(
            cb, flags.value.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
        barrier(flags, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        barrier(scratch, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        vk.dev<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                           pipeline);
        vk.dev<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(
            cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
        vk.dev<PFN_vkCmdDispatch>("vkCmdDispatch")(cb, 32, 18, 1);
    }
    ~NativeSsrFixture() {
        vk.dev<PFN_vkDestroyPipeline>("vkDestroyPipeline")(vk.device, pipeline, nullptr);
        vk.dev<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(vk.device, pipelineLayout,
                                                                       nullptr);
        vk.dev<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(vk.device, pool, nullptr);
        vk.dev<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(vk.device, layout,
                                                                                 nullptr);
        for (auto v : {depthView, flagView, scratchView})
            vk.dev<PFN_vkDestroyImageView>("vkDestroyImageView")(vk.device, v, nullptr);
        vk.dev<PFN_vkUnmapMemory>("vkUnmapMemory")(vk.device, memory);
        vk.dev<PFN_vkDestroyBuffer>("vkDestroyBuffer")(vk.device, buffer, nullptr);
        vk.dev<PFN_vkFreeMemory>("vkFreeMemory")(vk.device, memory, nullptr);
    }
};

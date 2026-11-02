// SPDX-License-Identifier: MIT
// Windows capture/lifetime management for opt-in renderer-native vectors.
// v2: output follows the shared motion contract v1. When capture is not
// possible the FSR boundary falls back to camera-derived motion and logs why.
struct NativeMotionFrame {
    Device *device{};
    OwnedImage depth, motion, reactive;
    VkDescriptorPool pool{};
    VkDescriptorSet set{};
    VkImage source{};
    uint64_t generation{}, frame{};
    uint32_t width{}, height{};
    bool captured{};
    float jitterX{}, jitterY{};
    ~NativeMotionFrame() {
        if (pool)
            device->fn<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(device->handle, pool,
                                                                               nullptr);
    }
};
void restoreCompute(Device &d, VkCommandBuffer cb, Command &command) {
    d.fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                     command.pipeline);
    if (!command.layout)
        return;
    for (auto &[number, set] : command.sets) {
        auto &values = command.offsets[number];
        d.fn<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(
            cb, VK_PIPELINE_BIND_POINT_COMPUTE, command.layout, number, 1, &set,
            uint32_t(values.size()), values.data());
    }
    for (auto &[number, bindings] : command.pushed) {
        std::vector<VkWriteDescriptorSet> writes;
        writes.reserve(bindings.size());
        for (auto &[binding, value] : bindings) {
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstBinding = binding.first;
            w.dstArrayElement = binding.second;
            w.descriptorCount = 1;
            w.descriptorType = value.type;
            w.pImageInfo = &value.image;
            w.pBufferInfo = &value.buffer;
            writes.push_back(w);
        }
        auto push = d.fn<PFN_vkCmdPushDescriptorSetKHR>("vkCmdPushDescriptorSetKHR");
        if (!push)
            push = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(
                d.next(d.handle, "vkCmdPushDescriptorSet"));
        if (push && !writes.empty())
            push(cb, VK_PIPELINE_BIND_POINT_COMPUTE, command.layout, number,
                 uint32_t(writes.size()), writes.data());
    }
    for (auto &saved : command.constants)
        if (saved.stages & VK_SHADER_STAGE_COMPUTE_BIT)
            d.fn<PFN_vkCmdPushConstants>("vkCmdPushConstants")(
                cb, saved.layout, saved.stages, saved.offset, uint32_t(saved.data.size()),
                saved.data.data());
}
void nativeDependency(Device &d, VkCommandBuffer cb) {
    VkMemoryBarrier m{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    m.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    m.dstAccessMask = m.srcAccessMask;
    d.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
                                                           &m, 0, nullptr, 0, nullptr);
}
void setupNativeMotion(Device &d) {
    if (d.nativeMotionLayout)
        return;
    VkDescriptorSetLayoutBinding bindings[8]{};
    for (uint32_t i = 0; i < 8; ++i) {
        bindings[i] = {i,
                       i < 2   ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                       : i < 4 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                       : i < 7 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                               : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                       1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo l{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    l.bindingCount = 8;
    l.pBindings = bindings;
    check(d.fn<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(
              d.handle, &l, nullptr, &d.nativeMotionLayout),
          "Native motion descriptor layout");
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 32};
    VkPipelineLayoutCreateInfo p{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    p.setLayoutCount = 1;
    p.pSetLayouts = &d.nativeMotionLayout;
    p.pushConstantRangeCount = 1;
    p.pPushConstantRanges = &range;
    check(d.fn<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(d.handle, &p, nullptr,
                                                                     &d.nativeMotionPipelineLayout),
          "Native motion pipeline layout");
    auto pipeline = [&](const uint32_t *words, size_t bytes, VkPipeline &out) {
        VkShaderModule module{};
        VkShaderModuleCreateInfo s{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        s.codeSize = bytes;
        s.pCode = words;
        check(
            d.fn<PFN_vkCreateShaderModule>("vkCreateShaderModule")(d.handle, &s, nullptr, &module),
            "Native motion shader");
        VkComputePipelineCreateInfo c{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        c.layout = d.nativeMotionPipelineLayout;
        c.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        c.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        c.stage.module = module;
        c.stage.pName = "main";
        auto result = d.fn<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(
            d.handle, VK_NULL_HANDLE, 1, &c, nullptr, &out);
        d.fn<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(d.handle, module, nullptr);
        check(result, "Native motion pipeline");
    };
    pipeline(nativeMotion2D, sizeof(nativeMotion2D), d.nativeMotion2D);
    pipeline(nativeMotionArray, sizeof(nativeMotionArray), d.nativeMotionArray);
    pipeline(nativeMotionHistory, sizeof(nativeMotionHistory), d.nativeMotionHistoryPipeline);
    VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    b.size = 80;
    b.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(d.fn<PFN_vkCreateBuffer>("vkCreateBuffer")(d.handle, &b, nullptr, &d.nativeMotionHistory),
          "Native projection history");
    VkMemoryRequirements req{};
    d.fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(
        d.handle, d.nativeMotionHistory, &req);
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize = req.size;
    a.memoryTypeIndex = d.memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(d.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(d.handle, &a, nullptr,
                                                         &d.nativeMotionHistoryMemory),
          "Native history memory");
    check(d.fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(d.handle, d.nativeMotionHistory,
                                                             d.nativeMotionHistoryMemory, 0),
          "Bind native history");
}
void captureNativeMotion(Device &d, VkCommandBuffer cb, Command &command) {
    auto role = d.nativeMotionPipelines.find(command.pipeline);
    if (!d.nativeMotionRequested || role == d.nativeMotionPipelines.end())
        return;
    if (role->second == ShaderRole::SsrSceneMsaa) {
        d.nativeMotionBlocker = "MSAA is on";
        return;
    }
    if (role->second != ShaderRole::SsrScene2D && role->second != ShaderRole::SsrSceneArray)
        return;
    if (!d.fgRequested.empty()) {
        d.nativeMotionBlocker = "frame generation is on";
        return;
    }
    if (d.cockpitRequested) {
        d.nativeMotionBlocker = "cockpit screen fix is on";
        return;
    }
    // Initial experiment is deliberately constrained to one application graphics
    // queue and one-time primary command buffers. No cross-buffer resource guess.
    if (d.mode == Mode::Observe || d.groundCameraActive() || command.secondary ||
        !command.oneTime || command.rendering || command.processed || command.dynamicOffsets ||
        command.sets.size() > 1 || command.pushed.size() > 1 ||
        command.family != d.graphicsFamily || d.graphicsFamily == UINT32_MAX ||
        !d.camera.valid || !d.camera.reverseZ ||
        !d.enabled || d.runtimeFailed || (command.nativeMotion && command.nativeMotion->captured))
        return;
    const Bindings *bindings = nullptr;
    if (command.pushed.count(0))
        bindings = &command.pushed.at(0);
    else if (command.sets.count(0) && d.descriptors.count(command.sets.at(0)))
        bindings = &d.descriptors.at(command.sets.at(0));
    if (!bindings)
        return;
    auto pos = bindings->find({3, 0}), flags = bindings->find({4, 0}), gb = bindings->find({11, 0}),
         ssr = bindings->find({12, 0});
    if (pos == bindings->end() || flags == bindings->end() || gb == bindings->end() ||
        ssr == bindings->end() || pos->second.type != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
        flags->second.type != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
        gb->second.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
        ssr->second.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || !gb->second.buffer.buffer ||
        !ssr->second.buffer.buffer || gb->second.buffer.range < 240 ||
        ssr->second.buffer.range < 384)
        return;
    auto pv = d.views.find(pos->second.image.imageView),
         fv = d.views.find(flags->second.image.imageView);
    if (pv == d.views.end() || fv == d.views.end())
        return;
    auto image = d.images.find(pv->second.create.image),
         fi = d.images.find(fv->second.create.image);
    if (image == d.images.end() || fi == d.images.end())
        return;
    auto width = image->second.create.extent.width, height = image->second.create.extent.height;
    auto sampled = [&](const ViewInfo &v, const ImageInfo &i, bool stencil) {
        auto &c = i.create;
        auto &r = v.create.subresourceRange;
        auto type = role->second == ShaderRole::SsrSceneArray ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                              : VK_IMAGE_VIEW_TYPE_2D;
        return c.samples == VK_SAMPLE_COUNT_1_BIT && c.arrayLayers == 1 &&
               c.imageType == VK_IMAGE_TYPE_2D && c.extent.width == width &&
               c.extent.height == height && (c.usage & VK_IMAGE_USAGE_SAMPLED_BIT) &&
               v.create.viewType == type && r.baseArrayLayer == 0 && r.layerCount == 1 &&
               r.baseMipLevel == 0 && r.levelCount == 1 &&
               (stencil ? r.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT ||
                              v.create.format == VK_FORMAT_R8_UINT ||
                              v.create.format == VK_FORMAT_R32_UINT
                        : r.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT);
    };
    auto depth = command.depths.find(pv->second.create.image);
    if (width < 64 || height < 64 || width > 4096 || height > 4096 ||
        !sampled(pv->second, image->second, false) || !sampled(fv->second, fi->second, true) ||
        pos->second.image.imageLayout == VK_IMAGE_LAYOUT_UNDEFINED ||
        flags->second.image.imageLayout == VK_IMAGE_LAYOUT_UNDEFINED ||
        depth == command.depths.end() || !depth->second.fullArea || !depth->second.cameraCoherent ||
        depth->second.epoch != d.camera.frame ||
        !command.capturedDepths.count(pv->second.create.image))
        return;
    bool started = false;
    try {
        setupNativeMotion(d);
        if (!command.nativeMotion || command.nativeMotion->width != width ||
            command.nativeMotion->height != height) {
            command.nativeMotion = std::make_unique<NativeMotionFrame>();
            auto &n = *command.nativeMotion;
            n.device = &d;
            n.width = width;
            n.height = height;
            n.depth.create(d, width, height, VK_FORMAT_R32_SFLOAT);
            n.motion.create(d, width, height, VK_FORMAT_R16G16_SFLOAT);
            n.reactive.create(d, width, height, VK_FORMAT_R8_UNORM);
            VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2},
                                            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
                                            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
                                            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
            VkDescriptorPoolCreateInfo p{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            p.maxSets = 1;
            p.poolSizeCount = 4;
            p.pPoolSizes = sizes;
            check(d.fn<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(d.handle, &p, nullptr,
                                                                             &n.pool),
                  "Native motion pool");
            VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            a.descriptorPool = n.pool;
            a.descriptorSetCount = 1;
            a.pSetLayouts = &d.nativeMotionLayout;
            check(d.fn<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(d.handle, &a,
                                                                                 &n.set),
                  "Native motion set");
        }
        auto &n = *command.nativeMotion;
        VkDescriptorImageInfo imgs[] = {pos->second.image,
                                        flags->second.image,
                                        {VK_NULL_HANDLE, n.depth.view, VK_IMAGE_LAYOUT_GENERAL},
                                        {VK_NULL_HANDLE, n.motion.view, VK_IMAGE_LAYOUT_GENERAL},
                                        {VK_NULL_HANDLE, n.reactive.view, VK_IMAGE_LAYOUT_GENERAL}};
        VkDescriptorBufferInfo buffers[] = {
            gb->second.buffer, ssr->second.buffer, {d.nativeMotionHistory, 0, 80}};
        VkWriteDescriptorSet writes[8]{};
        for (uint32_t i = 0; i < 8; ++i) {
            auto &w = writes[i];
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = n.set;
            w.dstBinding = i;
            w.descriptorCount = 1;
            w.descriptorType = i < 2   ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                               : i < 4 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                               : i < 7 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                       : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            if (i < 2)
                w.pImageInfo = &imgs[i];
            else if (i < 4)
                w.pBufferInfo = &buffers[i - 2];
            else if (i < 7)
                w.pImageInfo = &imgs[i - 2];
            else
                w.pBufferInfo = &buffers[2];
        }
        d.fn<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(d.handle, 8, writes, 0, nullptr);
        started = true;
        nativeDependency(d, cb);
        if (!d.nativeHistorySubmitted) {
            d.fn<PFN_vkCmdFillBuffer>("vkCmdFillBuffer")(cb, d.nativeMotionHistory, 0, 80, 0);
            nativeDependency(d, cb);
        }
        for (auto *out : {&n.depth, &n.motion, &n.reactive})
            transition(d, cb, *out, VK_IMAGE_LAYOUT_GENERAL);
        struct Push {
            int32_t width, height;
            float jitterX, jitterY;
            uint32_t frame, reset, reduce, pad;
        } push{int32_t(width),
               int32_t(height),
               depth->second.jitterX,
               depth->second.jitterY,
               uint32_t(d.camera.frame),
               uint32_t(d.camera.reset || !d.pathReady),
               uint32_t(d.reduceGhosting),
               0};
        d.fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(
            cb, VK_PIPELINE_BIND_POINT_COMPUTE,
            role->second == ShaderRole::SsrScene2D ? d.nativeMotion2D : d.nativeMotionArray);
        d.fn<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(
            cb, VK_PIPELINE_BIND_POINT_COMPUTE, d.nativeMotionPipelineLayout, 0, 1, &n.set, 0,
            nullptr);
        d.fn<PFN_vkCmdPushConstants>("vkCmdPushConstants")(
            cb, d.nativeMotionPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        d.fn<PFN_vkCmdDispatch>("vkCmdDispatch")(cb, (width + 7) / 8, (height + 7) / 8, 1);
        nativeDependency(d, cb);
        d.fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                         d.nativeMotionHistoryPipeline);
        d.fn<PFN_vkCmdDispatch>("vkCmdDispatch")(cb, 1, 1, 1);
        for (auto *out : {&n.depth, &n.motion, &n.reactive})
            transition(d, cb, *out, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        nativeDependency(d, cb);
        restoreCompute(d, cb, command);
        n.source = image->first;
        n.generation = image->second.generation;
        n.frame = d.camera.frame;
        n.jitterX = depth->second.jitterX;
        n.jitterY = depth->second.jitterY;
        n.captured = true;
        d.nativeMotionBlocker = nullptr;
        command.nativeHistoryWritten = true;
        if (++d.nativeMotionCaptures <= 4 || d.nativeMotionCaptures % 1000 == 0)
            log("Native renderer motion captured: source=%p frame=%llu size=%ux%u; SSR aircraft "
                "mask; GPU projection history",
                reinterpret_cast<void *>(n.source), (unsigned long long)n.frame, width, height);
    } catch (const std::exception &e) {
        // Turn off only the experiment; FSR keeps camera-derived motion.
        d.nativeMotionRequested = false;
        d.nativeMotionBlocker = "native capture failed";
        d.pathReady = false;
        if (started)
            command.failed = true;
        log("Native renderer motion disabled for this session: %s; camera-derived motion kept",
            e.what());
    }
}

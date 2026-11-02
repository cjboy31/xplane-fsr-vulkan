// SPDX-License-Identifier: MIT
// One-time motion-quality capture (developer/tester tool, off by default).
// Set XPFSR24_MOTION_CAPTURE to an empty folder before starting X-Plane. After
// XPFSR24_MOTION_CAPTURE_AFTER recorded frames (default 900) the layer copies a
// centred crop (XPFSR24_MOTION_CAPTURE_CROP, default 1024, max 2048) of two
// consecutive frames' FSR inputs: scene colour, depth, motion and reactive,
// exactly as FSR receives them. tools/motion_quality.py measures them.
// No simulator files are read or changed; nothing is captured unless asked.
struct MotionCapture {
    Device *device{};
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    void *mapped{};
    VkFence fence{};
    int phase{}; // 0 recorded, 1 fence submitted, 2 complete
    uint64_t recordedAt{}, cameraFrame{};
    float jitterX{}, jitterY{};
    bool reset{};
    xpmotion::Source source{};
    uint32_t x{}, y{}, w{}, h{}, colorBytes{};
    VkFormat colorFormat{};
    VkDeviceSize color{}, depth{}, motion{}, reactive{}, bytes{};
    ~MotionCapture() {
        if (!device)
            return;
        auto &d = *device;
        if (fence)
            d.fn<PFN_vkDestroyFence>("vkDestroyFence")(d.handle, fence, nullptr);
        if (buffer)
            d.fn<PFN_vkDestroyBuffer>("vkDestroyBuffer")(d.handle, buffer, nullptr);
        if (memory)
            d.fn<PFN_vkFreeMemory>("vkFreeMemory")(d.handle, memory, nullptr);
    }
};
inline uint32_t captureTexelBytes(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return 8;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    default:
        return 0;
    }
}
void setupMotionCapture(Device &d) {
    auto folder = app::environment(L"XPFSR24_MOTION_CAPTURE");
    if (folder.empty())
        return;
    d.motionCaptureDirectory = folder;
    auto number = [](const std::wstring &text, uint64_t fallback, uint64_t low, uint64_t high) {
        if (text.empty())
            return fallback;
        try {
            return std::clamp<uint64_t>(std::stoull(text), low, high);
        } catch (...) {
            return fallback;
        }
    };
    d.motionCaptureAfter = number(app::environment(L"XPFSR24_MOTION_CAPTURE_AFTER"), 900, 30, 1000000);
    d.motionCaptureCrop = uint32_t(number(app::environment(L"XPFSR24_MOTION_CAPTURE_CROP"), 1024, 64, 2048));
    log("Motion-quality capture armed: after %llu recorded frames, crop up to %ux%u (tester tool)",
        (unsigned long long)d.motionCaptureAfter, d.motionCaptureCrop, d.motionCaptureCrop);
}
void writeMotionCapture(Device &d) {
    auto &a = *d.motionCaptures[0], &b = *d.motionCaptures[1];
    struct Copy {
        std::vector<uint8_t> data;
        uint64_t camera;
        float jx, jy;
        bool reset;
        xpmotion::Source source;
    };
    std::vector<Copy> frames;
    for (auto *job : {&a, &b}) {
        MemoryBarrier();
        auto *bytes = static_cast<const uint8_t *>(job->mapped);
        frames.push_back({std::vector<uint8_t>(bytes, bytes + job->bytes), job->cameraFrame,
                          job->jitterX, job->jitterY, job->reset, job->source});
    }
    char header[1400];
    snprintf(header, sizeof(header),
             "{\n  \"format\": \"xp-motion-capture\",\n  \"version\": 1,\n"
             "  \"motion_contract\": %u,\n  \"width\": %u,\n  \"height\": %u,\n"
             "  \"crop_x\": %u,\n  \"crop_y\": %u,\n  \"render_width\": %u,\n"
             "  \"render_height\": %u,\n  \"color_format\": %d,\n  \"color_bytes\": %u,\n"
             "  \"depth\": {\"reverse_z\": %u, \"infinite\": %u, \"near\": %.9g, \"far\": %.9g},\n"
             "  \"layout\": {\"color\": %llu, \"depth\": %llu, \"motion\": %llu, \"reactive\": %llu, "
             "\"bytes\": %llu},\n",
             xpmotion::contractVersion, a.w, a.h, a.x, a.y, d.motionCaptureRender[0],
             d.motionCaptureRender[1], int(a.colorFormat), a.colorBytes, d.camera.reverseZ,
             d.camera.infiniteZ, d.camera.nearPlane, d.camera.farPlane,
             (unsigned long long)a.color, (unsigned long long)a.depth,
             (unsigned long long)a.motion, (unsigned long long)a.reactive,
             (unsigned long long)a.bytes);
    std::string json = header;
    json += "  \"frames\": [\n";
    for (size_t i = 0; i < frames.size(); ++i) {
        char line[320];
        snprintf(line, sizeof(line),
                 "    {\"file\": \"frame%zu.bin\", \"camera_frame\": %llu, \"jitter_x\": %.9g, "
                 "\"jitter_y\": %.9g, \"reset\": %s, \"source\": \"%s\"}%s\n",
                 i, (unsigned long long)frames[i].camera, frames[i].jx, frames[i].jy,
                 frames[i].reset ? "true" : "false", xpmotion::sourceToken(frames[i].source),
                 i + 1 < frames.size() ? "," : "");
        json += line;
    }
    json += "  ]\n}\n";
    auto folder = std::filesystem::path(d.motionCaptureDirectory) /
                  (L"motion-capture-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                   std::to_wstring(GetTickCount64()));
    // One-time tester tool: a short synchronous write (a few MB) is simpler and
    // safer than a detached thread that could outlive the layer at exit.
    try {
        std::filesystem::create_directories(folder);
        for (size_t i = 0; i < frames.size(); ++i) {
            std::ofstream out(folder / (L"frame" + std::to_wstring(i) + L".bin"), std::ios::binary);
            out.write(reinterpret_cast<const char *>(frames[i].data.data()),
                      std::streamsize(frames[i].data.size()));
        }
        std::ofstream(folder / L"capture.json", std::ios::binary) << json;
        log("Motion-quality capture written: %ls", folder.c_str());
    } catch (const std::exception &e) {
        log("Motion-quality capture could not be written: %s", e.what());
    }
}
void submitMotionCaptures(Device &d, VkQueue queue, Command &command) {
    for (auto &job : command.motionCaptures)
        if (job->phase == 0) {
            VkSubmitInfo empty{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            if (d.fn<PFN_vkQueueSubmit>("vkQueueSubmit")(queue, 1, &empty, job->fence) == VK_SUCCESS)
                job->phase = 1;
        }
    command.motionCaptures.clear();
}
void pollMotionCaptures(Device &d) {
    if (d.motionCaptureDone || d.motionCaptures.empty())
        return;
    for (auto &job : d.motionCaptures) {
        if (job->phase == 0 && d.status.recordings > job->recordedAt + 600) {
            // The recording was never submitted (re-recorded or dropped): retry.
            log("Motion-quality capture restarted: a captured frame was never submitted");
            d.motionCaptures.clear();
            return;
        }
        if (job->phase == 1 &&
            d.fn<PFN_vkGetFenceStatus>("vkGetFenceStatus")(d.handle, job->fence) == VK_SUCCESS)
            job->phase = 2;
    }
    if (d.motionCaptures.size() == 2 && d.motionCaptures[0]->phase == 2 &&
        d.motionCaptures[1]->phase == 2) {
        d.motionCaptureDone = true;
        writeMotionCapture(d);
        d.motionCaptures.clear();
    }
}
// Called inside the FSR boundary with every input in SHADER_READ_ONLY_OPTIMAL.
void recordMotionCapture(Device &d, VkCommandBuffer cb, Command &command, OwnedImage &color,
                         OwnedImage &depth, OwnedImage &motion, OwnedImage &reactive,
                         uint32_t rw, uint32_t rh, float jitterX, float jitterY, bool reset,
                         xpmotion::Source source) {
    if (d.motionCaptureDirectory.empty() || d.motionCaptureDone ||
        d.status.recordings < d.motionCaptureAfter || d.motionCaptures.size() >= 2)
        return;
    const uint32_t texel = captureTexelBytes(color.value.description.format);
    if (!texel) {
        log("Motion-quality capture skipped: scene colour format %d is not supported",
            int(color.value.description.format));
        d.motionCaptureDone = true;
        return;
    }
    if (d.motionCaptures.size() == 1) {
        auto &first = *d.motionCaptures[0];
        // Two consecutive frames of one source, with no reset in between.
        if (d.camera.frame != first.cameraFrame + 1 || reset || source != first.source ||
            first.w != std::min(rw, d.motionCaptureCrop) || first.h != std::min(rh, d.motionCaptureCrop))
            d.motionCaptures.clear();
    }
    auto job = std::make_shared<MotionCapture>();
    job->device = &d;
    job->w = std::min(rw, d.motionCaptureCrop);
    job->h = std::min(rh, d.motionCaptureCrop);
    job->x = (rw - job->w) / 2;
    job->y = (rh - job->h) / 2;
    job->colorFormat = color.value.description.format;
    job->colorBytes = texel;
    const VkDeviceSize pixels = VkDeviceSize(job->w) * job->h;
    auto align = [](VkDeviceSize v) { return (v + 255) & ~VkDeviceSize(255); };
    job->color = 0;
    job->depth = align(pixels * texel);
    job->motion = align(job->depth + pixels * 4);
    job->reactive = align(job->motion + pixels * 4);
    job->bytes = job->reactive + pixels;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = job->bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(d.fn<PFN_vkCreateBuffer>("vkCreateBuffer")(d.handle, &bi, nullptr, &job->buffer),
          "Motion capture buffer");
    VkMemoryRequirements mr{};
    d.fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(d.handle, job->buffer,
                                                                             &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = d.memoryType(
        mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(d.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(d.handle, &ai, nullptr, &job->memory),
          "Motion capture memory");
    check(d.fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(d.handle, job->buffer, job->memory, 0),
          "Bind motion capture");
    check(d.fn<PFN_vkMapMemory>("vkMapMemory")(d.handle, job->memory, 0, job->bytes, 0,
                                               &job->mapped),
          "Map motion capture");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(d.fn<PFN_vkCreateFence>("vkCreateFence")(d.handle, &fi, nullptr, &job->fence),
          "Motion capture fence");
    auto copy = [&](OwnedImage &image, VkDeviceSize offset) {
        const auto before = image.value.layout;
        transition(d, cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy r{};
        r.bufferOffset = offset;
        r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        r.imageOffset = {int32_t(job->x), int32_t(job->y), 0};
        r.imageExtent = {job->w, job->h, 1};
        d.fn<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(
            cb, image.value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, job->buffer, 1, &r);
        transition(d, cb, image, before);
    };
    copy(color, job->color);
    copy(depth, job->depth);
    copy(motion, job->motion);
    copy(reactive, job->reactive);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = job->buffer;
    host.size = VK_WHOLE_SIZE;
    d.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                           VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                                                           nullptr, 1, &host, 0, nullptr);
    job->recordedAt = d.status.recordings;
    job->cameraFrame = d.camera.frame;
    job->jitterX = jitterX;
    job->jitterY = jitterY;
    job->reset = reset;
    job->source = source;
    d.motionCaptureRender = {rw, rh};
    command.motionCaptures.push_back(job);
    d.motionCaptures.push_back(std::move(job));
    log("Motion-quality capture recorded frame %llu (%s, %ux%u crop)",
        (unsigned long long)d.camera.frame, xpmotion::sourceName(source),
        d.motionCaptures.back()->w, d.motionCaptures.back()->h);
}

// SPDX-License-Identifier: MIT
// Headless native-upscale-boundary test, through the real Vulkan loader/layer.
#define wmain unusedBaselineMain
#include "probe_support.hpp"
#undef wmain
#include "shared.hpp"
static std::vector<uint32_t> loadSpv(const wchar_t *path) {
    FILE *f = _wfopen(path, L"rb");
    require(f, "Test shader missing");
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    rewind(f);
    require(bytes > 0 && bytes % 4 == 0, "Test shader length");
    std::vector<uint32_t> result(bytes / 4);
    require(fread(result.data(), 1, bytes, f) == size_t(bytes), "Read test shader");
    fclose(f);
    return result;
}
#include "native_ssr_fixture.hpp"
int wmain(int argc, wchar_t **argv) {
    std::cout.setf(std::ios::unitbuf);
    try {
        require(argc >= 3, "layer_test.exe RUNTIME_DIRECTORY NATIVE_TEST_SPV");
        SetEnvironmentVariableW(L"XPFSR24_RUNTIME", argv[1]);
        bool nativeAA = argc > 3 && std::wstring(argv[3]) == L"native-aa",
             observe = argc > 3 && std::wstring(argv[3]) == L"observe";
        unsigned sampleCount = argc > 4 ? std::stoul(argv[4]) : 1;
        require(sampleCount == 1 || sampleCount == 2 || sampleCount == 4 || sampleCount == 8,
                "MSAA test sample count");
        auto samples = VkSampleCountFlagBits(sampleCount);
        std::wstring resolveKind = argc > 5 ? argv[5] : L"attachment";
        std::cout << "Scene MSAA samples=" << sampleCount << "\n";
        const uint32_t OW = nativeAA ? 256 : 384, OH = nativeAA ? 144 : 216;
        SetEnvironmentVariableW(L"XPFSR24_MODE", observe    ? L"observe"
                                                 : nativeAA ? L"native-aa"
                                                            : L"upscale");
        auto name = app::mappingName(GetCurrentProcessId());
        HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                            sizeof(app::Shared), name.c_str());
        require(mapping, "Create test camera mapping");
        auto *shared = static_cast<app::Shared *>(
            MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(app::Shared)));
        require(shared, "Map test camera");
        memset(shared, 0, sizeof(*shared));
        shared->tag = app::magic;
        shared->version = app::abi;
        shared->enabled = 1;
        bool nativeMotion = argc > 6;
        bool missingNative = nativeMotion && std::wstring(argv[6]) == L"native-missing";
        bool nativeArray = nativeMotion && std::wstring(argv[6]) == L"native-array";
        SetEnvironmentVariableW(L"XPFSR24_MOTION", nativeMotion ? L"native-renderer" : nullptr);
        SetEnvironmentVariableW(L"XPFSR24_COCKPIT_RESTORE", L"0");
        Vulkan vk;
        vk.open();
        GpuImage finalColor(vk), auxColor(vk), auxDepth(vk), sceneColor(vk), color(vk), output(vk),
            depth(vk), msaaColor(vk);
        finalColor.create(OW, OH, VK_FORMAT_R16G16B16A16_SFLOAT);
        auxColor.create(256, 144, VK_FORMAT_R16G16B16A16_SFLOAT, 0,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        sceneColor.create(256, 144, VK_FORMAT_R16G16B16A16_SFLOAT, 0,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        if (sampleCount > 1)
            msaaColor.create(256, 144, VK_FORMAT_R16G16B16A16_SFLOAT, 0,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, samples);
        color.create(256, 144, VK_FORMAT_R16G16B16A16_SFLOAT,
                     VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_ALIAS_BIT,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        output.create(OW, OH, VK_FORMAT_R16G16B16A16_SFLOAT);
        VkMemoryRequirements mr{};
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        auto createDepth = [&](GpuImage &depth,
                               VkSampleCountFlagBits count = VK_SAMPLE_COUNT_1_BIT) {
            auto &dc = depth.value.description;
            dc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            dc.imageType = VK_IMAGE_TYPE_2D;
            dc.extent = {256, 144, 1};
            dc.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
            dc.mipLevels = dc.arrayLayers = 1;
            dc.samples = count;
            dc.tiling = VK_IMAGE_TILING_OPTIMAL;
            dc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       (nativeMotion ? VK_IMAGE_USAGE_SAMPLED_BIT : 0);
            vkcheck(vk.dev<PFN_vkCreateImage>("vkCreateImage")(vk.device, &dc, nullptr,
                                                               &depth.value.handle),
                    "Test depth image");
            vk.dev<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(
                vk.device, depth.value.handle, &mr);
            ai.allocationSize = mr.size;
            ai.memoryTypeIndex =
                vk.memoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkcheck(vk.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(vk.device, &ai, nullptr,
                                                                     &depth.allocation),
                    "Depth memory");
            vkcheck(vk.dev<PFN_vkBindImageMemory>("vkBindImageMemory")(
                        vk.device, depth.value.handle, depth.allocation, 0),
                    "Depth bind");
        };
        createDepth(depth, samples);
        createDepth(auxDepth);
        std::unique_ptr<NativeSsrFixture> nativeSsr;
        if (nativeMotion)
            nativeSsr = std::make_unique<NativeSsrFixture>(
                vk, depth.value.handle, nativeArray,
                std::filesystem::path(argv[2]).parent_path() /
                    (nativeArray ? L"native-motion-fixture-1.spv"
                                 : L"native-motion-fixture-0.spv"));
        auto view = [&](VkImage image, VkFormat format, VkImageAspectFlags aspect,
                        VkImageViewType type) {
            VkImageView result{};
            VkImageViewCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            c.image = image;
            c.format = format;
            c.viewType = type;
            c.subresourceRange = {aspect, 0, 1, 0, 1};
            vkcheck(
                vk.dev<PFN_vkCreateImageView>("vkCreateImageView")(vk.device, &c, nullptr, &result),
                "Test image view");
            return result;
        };
        auto postRT = view(color.value.handle, VK_FORMAT_R16G16B16A16_SFLOAT,
                           VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D),
             sceneArray = view(sceneColor.value.handle, VK_FORMAT_R16G16B16A16_SFLOAT,
                               VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
        auto colorRT = view(sceneColor.value.handle, VK_FORMAT_R16G16B16A16_SFLOAT,
                            VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D),
             colorArray = view(color.value.handle, VK_FORMAT_R16G16B16A16_SFLOAT,
                               VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D_ARRAY),
             outArray = view(output.value.handle, VK_FORMAT_R16G16B16A16_SFLOAT,
                             VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D_ARRAY),
             depthRT = view(depth.value.handle, VK_FORMAT_D32_SFLOAT_S8_UINT,
                            VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D);
        VkImageView msaaRT{};
        if (sampleCount > 1)
            msaaRT = view(msaaColor.value.handle, VK_FORMAT_R16G16B16A16_SFLOAT,
                          VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D);
        auto auxRT = view(auxColor.value.handle, VK_FORMAT_R16G16B16A16_SFLOAT,
                          VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D),
             auxArray = view(auxColor.value.handle, VK_FORMAT_R16G16B16A16_SFLOAT,
                             VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D_ARRAY),
             auxDepthRT = view(auxDepth.value.handle, VK_FORMAT_D32_SFLOAT_S8_UINT,
                               VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D);
        auto finalArray = view(finalColor.value.handle, VK_FORMAT_R16G16B16A16_SFLOAT,
                               VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
        VkSampler sampler{};
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
        sci.addressModeU = sci.addressModeV = sci.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkcheck(vk.dev<PFN_vkCreateSampler>("vkCreateSampler")(vk.device, &sci, nullptr, &sampler),
                "Test sampler");
        VkDescriptorSetLayoutBinding bindings[4]{};
        VkDescriptorType types[] = {VK_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC};
        for (uint32_t i = 0; i < 4; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = types[i];
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_ALL_GRAPHICS;
        }
        VkDescriptorSetLayout layout{};
        VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        lci.bindingCount = 4;
        lci.pBindings = bindings;
        vkcheck(vk.dev<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(
                    vk.device, &lci, nullptr, &layout),
                "Test layout");
        VkPipelineLayout pipelineLayout{};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &layout;
        vkcheck(vk.dev<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(
                    vk.device, &pl, nullptr, &pipelineLayout),
                "Test pipeline layout");
        auto spv = loadSpv(argv[2]);
        VkShaderModule module{};
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        sm.codeSize = spv.size() * 4;
        sm.pCode = spv.data();
        vkcheck(vk.dev<PFN_vkCreateShaderModule>("vkCreateShaderModule")(vk.device, &sm, nullptr,
                                                                         &module),
                "Test shader");
        VkPipeline pipeline{};
        VkComputePipelineCreateInfo pc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pc.layout = pipelineLayout;
        pc.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pc.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pc.stage.module = module;
        pc.stage.pName = "main";
        vkcheck(vk.dev<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(
                    vk.device, VK_NULL_HANDLE, 1, &pc, nullptr, &pipeline),
                "Test compute pipeline");
        auto shader = [&](const wchar_t *name) {
            auto bytes = loadSpv((std::filesystem::path(argv[2]).parent_path() / name).c_str());
            VkShaderModule result{};
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize = bytes.size() * 4;
            info.pCode = bytes.data();
            vkcheck(vk.dev<PFN_vkCreateShaderModule>("vkCreateShaderModule")(vk.device, &info,
                                                                             nullptr, &result),
                    "Scene module");
            return result;
        };
        auto vertex = shader(L"scene.vert.spv"), fragment = shader(L"scene.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (auto &stage : stages) {
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.pName = "main";
        }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        VkPipelineVertexInputStateCreateInfo vi{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0, 0, 256, 144, 0, 1};
        VkRect2D scissor{{0, 0}, {256, 144}};
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        vp.pViewports = &viewport;
        vp.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &ba;
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &colorFormat;
        rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.pNext = &rendering;
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp;
        gp.pRasterizationState = &raster;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &blend;
        gp.layout = pipelineLayout;
        VkPipeline graphics{};
        vkcheck(vk.dev<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(
                    vk.device, VK_NULL_HANDLE, 1, &gp, nullptr, &graphics),
                "Static-viewport scene pipeline");
        auto graphicsSingle = graphics;
        if (sampleCount > 1) {
            ms.rasterizationSamples = samples;
            vkcheck(vk.dev<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(
                        vk.device, VK_NULL_HANDLE, 1, &gp, nullptr, &graphics),
                    "MSAA scene pipeline");
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        }
        auto postFragment = shader(L"post.frag.spv");
        stages[1].module = postFragment;
        rendering.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        ds.depthTestEnable = ds.depthWriteEnable = VK_FALSE;
        VkPipeline postPipeline{};
        vkcheck(vk.dev<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(
                    vk.device, VK_NULL_HANDLE, 1, &gp, nullptr, &postPipeline),
                "Fullscreen post pipeline");
        VkBuffer buffer{};
        VkDeviceMemory memory{};
        VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bc.size = OW * OH * 8;
        bc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        vkcheck(vk.dev<PFN_vkCreateBuffer>("vkCreateBuffer")(vk.device, &bc, nullptr, &buffer),
                "Test readback buffer");
        vk.dev<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(vk.device,
                                                                                   buffer, &mr);
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex =
            vk.memoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkcheck(vk.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(vk.device, &ai, nullptr, &memory),
                "Test host memory");
        vkcheck(vk.dev<PFN_vkBindBufferMemory>("vkBindBufferMemory")(vk.device, buffer, memory, 0),
                "Bind test host memory");
        VkDescriptorPool pool{};
        VkDescriptorPoolSize sizes[4]{};
        for (int i = 0; i < 4; ++i)
            sizes[i] = {types[i], 3};
        VkDescriptorPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolCI.maxSets = 3;
        poolCI.poolSizeCount = 4;
        poolCI.pPoolSizes = sizes;
        vkcheck(vk.dev<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(vk.device, &poolCI,
                                                                             nullptr, &pool),
                "Test descriptors");
        VkDescriptorSet set{};
        VkDescriptorSetAllocateInfo setCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setCI.descriptorPool = pool;
        setCI.descriptorSetCount = 1;
        setCI.pSetLayouts = &layout;
        vkcheck(vk.dev<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(vk.device, &setCI,
                                                                                 &set),
                "Test descriptor set");
        VkDescriptorImageInfo infos[] = {
            {sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
            {VK_NULL_HANDLE, colorArray, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, outArray, VK_IMAGE_LAYOUT_GENERAL}};
        VkDescriptorBufferInfo uniform{buffer, 0, 16};
        VkWriteDescriptorSet writes[4]{};
        for (uint32_t i = 0; i < 4; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = types[i];
            if (i < 3)
                writes[i].pImageInfo = &infos[i];
            else
                writes[i].pBufferInfo = &uniform;
        }
        vk.dev<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(vk.device, 4, writes, 0,
                                                                     nullptr);
        VkDescriptorSet postSet{};
        vkcheck(vk.dev<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(vk.device, &setCI,
                                                                                 &postSet),
                "Post descriptor set");
        VkDescriptorImageInfo sceneInfo{VK_NULL_HANDLE, sceneArray,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet postWrites[4] = {writes[0], writes[1], writes[2], writes[3]};
        for (auto &write : postWrites)
            write.dstSet = postSet;
        postWrites[1].pImageInfo = &sceneInfo;
        VkDescriptorImageInfo unusedInfo{VK_NULL_HANDLE, auxArray, VK_IMAGE_LAYOUT_GENERAL};
        postWrites[2].pImageInfo = &unusedInfo;
        vk.dev<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(vk.device, 4, postWrites, 0,
                                                                     nullptr);
        VkDescriptorSet nativePostSet{};
        vkcheck(vk.dev<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(vk.device, &setCI,
                                                                                 &nativePostSet),
                "Native post descriptor set");
        VkDescriptorImageInfo postInput{VK_NULL_HANDLE, outArray,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            postOutput{VK_NULL_HANDLE, finalArray, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet nativePostWrites[4] = {writes[0], writes[1], writes[2], writes[3]};
        for (auto &write : nativePostWrites)
            write.dstSet = nativePostSet;
        nativePostWrites[1].pImageInfo = &postInput;
        nativePostWrites[2].pImageInfo = &postOutput;
        vk.dev<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(vk.device, 4, nativePostWrites,
                                                                     0, nullptr);
        VkCommandPool commandPool{};
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cp.queueFamilyIndex = vk.family;
        cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vkcheck(vk.dev<PFN_vkCreateCommandPool>("vkCreateCommandPool")(vk.device, &cp, nullptr,
                                                                       &commandPool),
                "Test command pool");
        VkCommandBuffer cb{};
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ca.commandPool = commandPool;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;
        vkcheck(
            vk.dev<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(vk.device, &ca, &cb),
            "Test command buffer");
        VkCommandBuffer children[3]{};
        auto childAllocate = ca;
        childAllocate.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        childAllocate.commandBufferCount = 3;
        vkcheck(vk.dev<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(
                    vk.device, &childAllocate, children),
                "Secondary fixture buffers");
        VkFence fence{};
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vkcheck(vk.dev<PFN_vkCreateFence>("vkCreateFence")(vk.device, &fc, nullptr, &fence),
                "Test fence");
        auto barrier = [&](GpuImage &image, VkImageLayout target, VkImageAspectFlags aspect) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.image = image.value.handle;
            b.oldLayout = image.value.layout;
            b.newLayout = target;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.subresourceRange = {aspect, 0, 1, 0, 1};
            b.srcAccessMask = b.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
                                  ? 0
                                  : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
                cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                nullptr, 0, nullptr, 1, &b);
            image.value.layout = target;
        };
        for (uint64_t frame = 1; frame <= 18; ++frame) {
            InterlockedExchange(&shared->enabled, (frame == 9 || frame == 10) ? 0 : 1);
            app::Camera camera{};
            camera.valid = 1;
            camera.internal = 1;
            camera.bodyValid = 1;
            for (int i = 0; i < 16; ++i)
                camera.bodyReprojection[i] = (i % 5) == 0;
            camera.frame = frame;
            camera.width = OW;
            camera.height = OH;
            camera.nearPlane = .1f;
            camera.farPlane = INFINITY;
            camera.infiniteZ = camera.reverseZ = 1;
            camera.verticalFov = 1.0472f;
            camera.milliseconds = 16.667f;
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            camera.ticks = now.QuadPart;
            for (int i = 0; i < 16; ++i)
                camera.reprojection[i] = camera.inverseProjection[i] = (i % 5) == 0;
            camera.reprojection[12] = .03f;
            InterlockedIncrement(&shared->cameraSequence);
            shared->camera = camera;
            InterlockedIncrement(&shared->cameraSequence);
            void *mapped{};
            vkcheck(vk.dev<PFN_vkMapMemory>("vkMapMemory")(vk.device, memory, 0, 16, 0, &mapped),
                    "Map test uniform");
            float params[] = {float(OW), float(OH), 0, 1};
            memcpy(mapped, params, 16);
            vk.dev<PFN_vkUnmapMemory>("vkUnmapMemory")(vk.device, memory);
            for (unsigned n = 0; n < 3; ++n) {
                vkcheck(vk.dev<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(children[n], 0),
                        "Reset child");
                VkFormat inheritedColor = VK_FORMAT_R16G16B16A16_SFLOAT;
                VkCommandBufferInheritanceRenderingInfo rendering{
                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO};
                rendering.colorAttachmentCount = 1;
                rendering.pColorAttachmentFormats = &inheritedColor;
                rendering.depthAttachmentFormat =
                    n == 2 ? VK_FORMAT_UNDEFINED : VK_FORMAT_D32_SFLOAT_S8_UINT;
                rendering.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
                VkCommandBufferInheritanceInfo inherited{
                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
                inherited.pNext = &rendering;
                VkCommandBufferBeginInfo childBegin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                childBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
                                   (n == 1 ? 0 : VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT);
                if (n == 1)
                    inherited.pNext = nullptr;
                childBegin.pInheritanceInfo = &inherited;
                vkcheck(vk.dev<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(children[n],
                                                                                 &childBegin),
                        "Begin child");
                if (n == 1) {
                    VkRenderingAttachmentInfo standaloneColor{
                        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
                    standaloneColor.imageView = sampleCount > 1 ? msaaRT : colorRT;
                    if (sampleCount > 1 && resolveKind == L"attachment") {
                        standaloneColor.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                        standaloneColor.resolveImageView = colorRT;
                        standaloneColor.resolveImageLayout =
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    }
                    standaloneColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    standaloneColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    standaloneColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    VkRenderingAttachmentInfo standaloneDepth{
                        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
                    standaloneDepth.imageView = depthRT;
                    standaloneDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    standaloneDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    standaloneDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    standaloneDepth.clearValue.depthStencil.depth = .5f;
                    VkRenderingInfo standalone{VK_STRUCTURE_TYPE_RENDERING_INFO};
                    standalone.renderArea = {{0, 0}, {256, 144}};
                    standalone.layerCount = 1;
                    standalone.colorAttachmentCount = 1;
                    standalone.pColorAttachments = &standaloneColor;
                    standalone.pDepthAttachment = &standaloneDepth;
                    vk.dev<PFN_vkCmdBeginRendering>("vkCmdBeginRendering")(children[n],
                                                                           &standalone);
                }
                vk.dev<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(children[n],
                                                                   VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                   n == 2   ? postPipeline
                                                                   : n == 0 ? graphicsSingle
                                                                            : graphics);
                if (n == 2) {
                    uint32_t offset = 0;
                    vk.dev<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(
                        children[n], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                        &postSet, 1, &offset);
                }
                VkViewport multipleViewports[4] = {{0, 0, 256, 144, 0, 1},
                                                   {0, 0, 256, 144, 0, 1},
                                                   {0, 0, 256, 144, 0, 1},
                                                   {0, 0, 256, 144, 0, 1}};
                vk.dev<PFN_vkCmdSetViewport>("vkCmdSetViewport")(children[n], 0, 4,
                                                                 multipleViewports);
                vk.dev<PFN_vkCmdDraw>("vkCmdDraw")(children[n], 3, 1, 0, 0);
                if (n == 1)
                    vk.dev<PFN_vkCmdEndRendering>("vkCmdEndRendering")(children[n]);
                vkcheck(vk.dev<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(children[n]),
                        "End child");
            }
            vkcheck(vk.dev<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(cb, 0), "Test reset");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkcheck(vk.dev<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb, &begin),
                    "Test begin");
            barrier(auxColor, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            barrier(auxDepth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
            if (sampleCount > 1)
                barrier(msaaColor, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT);
            barrier(sceneColor, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT);
            barrier(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            barrier(depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
            barrier(output, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VkRenderingAttachmentInfo attachments[2]{};
            for (auto &a : attachments) {
                a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            }
            attachments[0].imageView = colorRT;
            attachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[0].clearValue.color.float32[0] = .9f;
            attachments[0].clearValue.color.float32[1] = .8f;
            attachments[0].clearValue.color.float32[2] = .7f;
            attachments[0].clearValue.color.float32[3] = 1;
            attachments[1].imageView = depthRT;
            attachments[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachments[1].clearValue.depthStencil.depth = .5f;
            VkRenderingInfo render{VK_STRUCTURE_TYPE_RENDERING_INFO};
            render.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
            render.renderArea.extent = {256, 144};
            render.layerCount = 1;
            render.colorAttachmentCount = 1;
            render.pColorAttachments = &attachments[0];
            render.pDepthAttachment = &attachments[1];
            VkRenderingAttachmentInfo auxAttachments[2] = {attachments[0], attachments[1]};
            auxAttachments[0].imageView = auxRT;
            auxAttachments[1].imageView = auxDepthRT;
            VkRenderingInfo auxRendering = render;
            auxRendering.pColorAttachments = &auxAttachments[0];
            auxRendering.pDepthAttachment = &auxAttachments[1];
            vk.dev<PFN_vkCmdBeginRendering>("vkCmdBeginRendering")(cb, &auxRendering);
            vk.dev<PFN_vkCmdExecuteCommands>("vkCmdExecuteCommands")(cb, 1, &children[0]);
            vk.dev<PFN_vkCmdEndRendering>("vkCmdEndRendering")(cb);
            barrier(auxColor, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
            vk.dev<PFN_vkCmdExecuteCommands>("vkCmdExecuteCommands")(cb, 1, &children[1]);
            if (sampleCount > 1 && resolveKind != L"attachment") {
                barrier(msaaColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
                barrier(sceneColor, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT);
                VkImageResolve region{};
                region.srcSubresource =
                    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.extent = {256, 144, 1};
                if (resolveKind == L"explicit") {
                    vk.dev<PFN_vkCmdResolveImage>("vkCmdResolveImage")(
                        cb, msaaColor.value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        sceneColor.value.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                } else {
                    VkImageResolve2 region2{VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2};
                    region2.srcSubresource = region.srcSubresource;
                    region2.dstSubresource = region.dstSubresource;
                    region2.extent = region.extent;
                    VkResolveImageInfo2 info{VK_STRUCTURE_TYPE_RESOLVE_IMAGE_INFO_2};
                    info.srcImage = msaaColor.value.handle;
                    info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    info.dstImage = sceneColor.value.handle;
                    info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    info.regionCount = 1;
                    info.pRegions = &region2;
                    vk.dev<PFN_vkCmdResolveImage2>("vkCmdResolveImage2")(cb, &info);
                }
            }
            if (nativeSsr && !missingNative)
                nativeSsr->record(cb, depth);
            barrier(depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
            VkClearDepthStencilValue discardedDepth{0, 0};
            VkImageSubresourceRange discardedRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            vk.dev<PFN_vkCmdClearDepthStencilImage>("vkCmdClearDepthStencilImage")(
                cb, depth.value.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &discardedDepth, 1,
                &discardedRange);
            barrier(depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
            barrier(sceneColor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT);
            VkRenderingAttachmentInfo postAttachment = attachments[0];
            postAttachment.imageView = postRT;
            VkRenderingInfo postRendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            postRendering.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
            postRendering.renderArea = {{0, 0}, {256, 144}};
            postRendering.layerCount = 1;
            postRendering.colorAttachmentCount = 1;
            postRendering.pColorAttachments = &postAttachment;
            vk.dev<PFN_vkCmdBeginRendering>("vkCmdBeginRendering")(cb, &postRendering);
            vk.dev<PFN_vkCmdExecuteCommands>("vkCmdExecuteCommands")(cb, 1, &children[2]);
            vk.dev<PFN_vkCmdEndRendering>("vkCmdEndRendering")(cb);
            barrier(color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            uint32_t dynamicOffset = 0;
            vk.dev<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                               pipeline);
            vk.dev<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(
                cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 1, &dynamicOffset);
            vk.dev<PFN_vkCmdDispatch>("vkCmdDispatch")(cb, 48, 27, 1);
            barrier(output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            barrier(finalColor, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
            uint32_t nativePostOffset = 0;
            vk.dev<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(
                cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &nativePostSet, 1,
                &nativePostOffset);
            vk.dev<PFN_vkCmdDispatch>("vkCmdDispatch")(cb, (OW + 7) / 8, (OH + 7) / 8, 1);
            barrier(finalColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {OW, OH, 1};
            vk.dev<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(
                cb, finalColor.value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                &copy);
            VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
                cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0,
                nullptr, 0, nullptr);
            vkcheck(vk.dev<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb), "Test end");
            vkcheck(vk.dev<PFN_vkResetFences>("vkResetFences")(vk.device, 1, &fence),
                    "Test reset fence");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &cb;
            vkcheck(vk.dev<PFN_vkQueueSubmit>("vkQueueSubmit")(vk.queue, 1, &submit, fence),
                    "Test submit");
            vkcheck(vk.dev<PFN_vkWaitForFences>("vkWaitForFences")(vk.device, 1, &fence, VK_TRUE,
                                                                   30000000000ull),
                    "Test completion");
            vkcheck(vk.dev<PFN_vkMapMemory>("vkMapMemory")(vk.device, memory, 0, OW * OH * 8, 0,
                                                           &mapped),
                    "Map output");
            auto *pixel = static_cast<uint16_t *>(mapped) + ((OH / 2) * OW + OW / 2) * 4;
            float rgb[] = {half(pixel[0]), half(pixel[1]), half(pixel[2])};
            if (frame == 18 && nativeAA && !missingNative) {
                double rawError = 0, aaError = 0;
                unsigned samples = 0;
                auto *image = static_cast<uint16_t *>(mapped);
                for (uint32_t y = 8; y < 136; ++y)
                    for (uint32_t x = 8; x < 248; ++x) {
                        if (std::abs(y + .5f - (.37f * (x + .5f) + 31.2f)) > 5)
                            continue;
                        float sum = 0;
                        for (int j = 0; j < 16; ++j)
                            for (int i = 0; i < 16; ++i)
                                sum += (y + (j + .5f) / 16) < .37f * (x + (i + .5f) / 16) + 31.2f
                                           ? .2f
                                           : .8f;
                        float reference = sum / 256,
                              raw = y + .5f < .37f * (x + .5f) + 31.2f ? .2f : .8f;
                        rawError += std::abs(raw - reference);
                        aaError += std::abs(half(image[(y * OW + x) * 4]) - reference);
                        ++samples;
                    }
                std::cout << "Layer Native AA edge error: " << aaError / samples << " vs raw "
                          << rawError / samples << ", improvement "
                          << (1 - aaError / rawError) * 100 << "%\n";
                require(aaError < rawError * .9, "Layer jitter + motion + post-processing did not "
                                                 "improve diagonal edge coverage");
            }
            vk.dev<PFN_vkUnmapMemory>("vkUnmapMemory")(vk.device, memory);
            require(std::abs(rgb[0] - .2f) < .05f && std::abs(rgb[1] - .4f) < .05f &&
                        std::abs(rgb[2] - .6f) < .05f,
                    "Layer output readback differs");
            app::Telemetry status{};
            uint64_t expected = observe || missingNative ? 0
                                : frame <= 8             ? frame
                                : frame <= 10            ? 8
                                                         : frame - 2;
            require(app::readStatus(shared, status) && status.recordings == expected,
                    "Layer replacement/toggle count differs");
            if (nativeMotion && expected && status.active)
                require(strstr(status.reason, "native renderer motion (SSR)") != nullptr,
                        "Native source status must identify SSR capture");
            if (missingNative && !observe)
                require(!status.active,
                        "Missing native inputs must not silently use camera vectors");
            std::cout << "Source frame " << frame << ": completed, RGB " << rgb[0] << ',' << rgb[1]
                      << ',' << rgb[2] << ", FSR recordings " << status.recordings << ", provider "
                      << status.provider << '\n';
        }
        vkcheck(vk.waitIdle(vk.device), "Test idle");
        vk.dev<PFN_vkDestroyPipeline>("vkDestroyPipeline")(vk.device, graphics, nullptr);
        vk.dev<PFN_vkDestroyPipeline>("vkDestroyPipeline")(vk.device, postPipeline, nullptr);
        vk.dev<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(vk.device, postFragment,
                                                                   nullptr);
        vk.dev<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(vk.device, vertex, nullptr);
        vk.dev<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(vk.device, fragment, nullptr);
        vk.dev<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(vk.device, commandPool, nullptr);
        vk.dev<PFN_vkDestroyFence>("vkDestroyFence")(vk.device, fence, nullptr);
        vk.dev<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(vk.device, pool, nullptr);
        vk.dev<PFN_vkDestroyPipeline>("vkDestroyPipeline")(vk.device, pipeline, nullptr);
        vk.dev<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(vk.device, module, nullptr);
        vk.dev<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(vk.device, pipelineLayout,
                                                                       nullptr);
        vk.dev<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(vk.device, layout,
                                                                                 nullptr);
        vk.dev<PFN_vkDestroySampler>("vkDestroySampler")(vk.device, sampler, nullptr);
        if (sampleCount > 1) {
            vk.dev<PFN_vkDestroyPipeline>("vkDestroyPipeline")(vk.device, graphicsSingle, nullptr);
            vk.dev<PFN_vkDestroyImageView>("vkDestroyImageView")(vk.device, msaaRT, nullptr);
        }
        for (auto v : {finalArray, auxRT, auxArray, auxDepthRT, colorRT, postRT, sceneArray,
                       colorArray, outArray, depthRT})
            vk.dev<PFN_vkDestroyImageView>("vkDestroyImageView")(vk.device, v, nullptr);
        vk.dev<PFN_vkDestroyBuffer>("vkDestroyBuffer")(vk.device, buffer, nullptr);
        vk.dev<PFN_vkFreeMemory>("vkFreeMemory")(vk.device, memory, nullptr);
        UnmapViewOfFile(shared);
        CloseHandle(mapping);
        std::cout << "PASS: selected mode replacement/native counts; GPU completion/readback, "
                     "disable and re-enable verified.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

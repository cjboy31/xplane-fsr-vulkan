// SPDX-License-Identifier: MIT
// Controlled Native AA image-quality check. No simulator or injection layer.
#define wmain unusedBaselineMain
#include "probe_support.hpp"
#undef wmain
#include <algorithm>
#include <fstream>
#include <iomanip>
constexpr uint32_t W = 256, H = 144;
static float scene(float x, float y) {
    if (x < 128)
        return y < .37f * x + 31.2f ? .12f : .82f;
    return std::abs(y - (.53f * (x - 128) + 26.3f)) < .6f ? .82f : .12f;
}
static void ppm(const std::filesystem::path &path, const std::vector<float> &pixels) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << W << ' ' << H << "\n255\n";
    for (auto v : pixels) {
        unsigned char b = static_cast<unsigned char>(std::round(std::clamp(v, 0.f, 1.f) * 255));
        for (int c = 0; c < 3; ++c)
            f.write(reinterpret_cast<const char *>(&b), 1);
    }
}
static void bmp(const std::filesystem::path &path, const std::vector<float> &pixels) {
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + W * H * 3;
    info.biSize = sizeof(info);
    info.biWidth = W;
    info.biHeight = -int(H);
    info.biPlanes = 1;
    info.biBitCount = 24;
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(&file), sizeof(file));
    f.write(reinterpret_cast<const char *>(&info), sizeof(info));
    for (auto v : pixels) {
        unsigned char b = static_cast<unsigned char>(std::round(std::clamp(v, 0.f, 1.f) * 255));
        for (int c = 0; c < 3; ++c)
            f.write(reinterpret_cast<const char *>(&b), 1);
    }
}
int wmain(int argc, wchar_t **argv) {
    std::cout.setf(std::ios::unitbuf);
    try {
        require(argc >= 3, "aa_test.exe RUNTIME_DIRECTORY OUTPUT_DIRECTORY");
        std::filesystem::path directory = argv[2];
        std::filesystem::create_directories(directory);
        Vulkan vk;
        vk.open();
        Runtime runtime;
        runtime.load(argv[1]);
        GpuImage color(vk), depth(vk), motion(vk), output(vk);
        color.create(W, H, VK_FORMAT_R32G32B32A32_SFLOAT);
        depth.create(W, H, VK_FORMAT_R32_SFLOAT);
        motion.create(W, H, VK_FORMAT_R16G16_SFLOAT);
        output.create(W, H, VK_FORMAT_R16G16B16A16_SFLOAT);
        VkCommandPool pool{};
        VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pc.queueFamilyIndex = vk.family;
        pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vkcheck(
            vk.dev<PFN_vkCreateCommandPool>("vkCreateCommandPool")(vk.device, &pc, nullptr, &pool),
            "AA pool");
        VkCommandBuffer cb{};
        VkCommandBufferAllocateInfo ac{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ac.commandPool = pool;
        ac.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ac.commandBufferCount = 1;
        vkcheck(
            vk.dev<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(vk.device, &ac, &cb),
            "AA command buffer");
        VkFence fence{};
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vkcheck(vk.dev<PFN_vkCreateFence>("vkCreateFence")(vk.device, &fc, nullptr, &fence),
                "AA fence");
        VkBuffer buffer{};
        VkDeviceMemory memory{};
        VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bc.size = W * H * 16;
        bc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkcheck(vk.dev<PFN_vkCreateBuffer>("vkCreateBuffer")(vk.device, &bc, nullptr, &buffer),
                "AA staging");
        VkMemoryRequirements mr{};
        vk.dev<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(vk.device,
                                                                                   buffer, &mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex =
            vk.memoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkcheck(vk.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(vk.device, &ai, nullptr, &memory),
                "AA memory");
        vkcheck(vk.dev<PFN_vkBindBufferMemory>("vkBindBufferMemory")(vk.device, buffer, memory, 0),
                "AA bind");
        {
            Context context(runtime.api);
            Config config{W, H, W, H};
            VkDeviceContext backend{vk.device, vk.physical, vk.gdpa};
            std::string wanted = argc > 3 && std::wstring(argv[3]) == L"4.1.2" ? "4.1.2" : "3.1.6";
            config.providerId = providerVersion(runtime.api, backend, wanted);
            context.create(config, backend);
            std::cout << "Native AA provider: " << context.provider() << '\n';
            std::vector<float> raw(W * H), reference(W * H), last(W * H), previous(W * H);
            double stability = 0;
            for (uint32_t y = 0; y < H; ++y)
                for (uint32_t x = 0; x < W; ++x) {
                    raw[y * W + x] = scene(x + .5f, y + .5f);
                    float sum = 0;
                    for (int j = 0; j < 16; ++j)
                        for (int i = 0; i < 16; ++i)
                            sum += scene(x + (i + .5f) / 16, y + (j + .5f) / 16);
                    reference[y * W + x] = sum / 256;
                }
            for (uint64_t sequence = 1; sequence <= 48; ++sequence) {
                auto jitter = context.jitter(sequence - 1);
                void *mapped{};
                vkcheck(vk.dev<PFN_vkMapMemory>("vkMapMemory")(vk.device, memory, 0, W * H * 16, 0,
                                                               &mapped),
                        "AA upload map");
                auto *source = static_cast<float *>(mapped);
                // A +pixel viewport translation shifts geometry by +jitter, so the
                // scene sampled at a fixed pixel center is evaluated at center-jitter.
                for (uint32_t y = 0; y < H; ++y)
                    for (uint32_t x = 0; x < W; ++x) {
                        auto i = (y * W + x) * 4;
                        float v = scene(x + .5f - jitter[0], y + .5f - jitter[1]);
                        source[i] = source[i + 1] = source[i + 2] = v;
                        source[i + 3] = 1;
                    }
                vk.dev<PFN_vkUnmapMemory>("vkUnmapMemory")(vk.device, memory);
                vkcheck(vk.dev<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(cb, 0),
                        "AA reset");
                VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkcheck(vk.dev<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb, &begin),
                        "AA begin");
                color.transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent = {W, H, 1};
                vk.dev<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(
                    cb, buffer, color.value.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                color.transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                for (auto *image : {&depth, &motion}) {
                    image->transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                    VkClearColorValue clear{};
                    if (image == &depth)
                        clear.float32[0] = .5f;
                    vk.dev<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(
                        cb, image->value.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1,
                        &range);
                    image->transition(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
                output.transition(cb, VK_IMAGE_LAYOUT_GENERAL);
                Frame frame{};
                frame.commands = cb;
                frame.color = color.value;
                frame.depth = depth.value;
                frame.motion = motion.value;
                frame.output = output.value;
                frame.jitterX = jitter[0];
                frame.jitterY = jitter[1];
                frame.motionScaleX = W;
                frame.motionScaleY = H;
                frame.deltaMilliseconds = 16.667f;
                frame.nearPlane = .1f;
                frame.verticalFovRadians = 1.0472f;
                frame.sequence = sequence;
                context.record(frame);
                output.transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                vk.dev<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(
                    cb, output.value.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                    &copy);
                VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                vk.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
                    cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0,
                    nullptr, 0, nullptr);
                vkcheck(vk.dev<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb), "AA end");
                vkcheck(vk.dev<PFN_vkResetFences>("vkResetFences")(vk.device, 1, &fence),
                        "AA reset fence");
                VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &cb;
                vkcheck(vk.dev<PFN_vkQueueSubmit>("vkQueueSubmit")(vk.queue, 1, &submit, fence),
                        "AA submit");
                vkcheck(vk.dev<PFN_vkWaitForFences>("vkWaitForFences")(vk.device, 1, &fence,
                                                                       VK_TRUE, 10000000000ull),
                        "AA completion");
                vkcheck(vk.dev<PFN_vkMapMemory>("vkMapMemory")(vk.device, memory, 0, W * H * 8, 0,
                                                               &mapped),
                        "AA readback");
                auto *values = static_cast<uint16_t *>(mapped);
                for (size_t i = 0; i < last.size(); ++i) {
                    last[i] = half(values[i * 4]);
                    require(std::isfinite(last[i]), "Nonfinite AA output");
                }
                vk.dev<PFN_vkUnmapMemory>("vkUnmapMemory")(vk.device, memory);
                if (sequence > 40) {
                    double difference = 0;
                    for (size_t i = 0; i < last.size(); ++i)
                        difference += std::abs(last[i] - previous[i]);
                    stability += difference / last.size() / 8;
                }
                previous = last;
            }
            double rawError = 0, aaError = 0;
            size_t samples = 0;
            // Evaluate a fixed five-pixel band around each known analytic edge.
            for (uint32_t y = 8; y < H - 8; ++y)
                for (uint32_t x = 8; x < W - 8; ++x) {
                    if (std::abs(float(x) - 128) < 8)
                        continue;
                    float edge =
                        x < 128 ? .37f * (x + .5f) + 31.2f : .53f * (x + .5f - 128) + 26.3f;
                    if (std::abs(y + .5f - edge) > 5)
                        continue;
                    auto i = y * W + x;
                    rawError += std::abs(raw[i] - reference[i]);
                    aaError += std::abs(last[i] - reference[i]);
                    ++samples;
                }
            rawError /= samples;
            aaError /= samples;
            ppm(directory / L"aliased.ppm", raw);
            ppm(directory / L"native-aa.ppm", last);
            ppm(directory / L"reference.ppm", reference);
            bmp(directory / L"aliased.bmp", raw);
            bmp(directory / L"native-aa.bmp", last);
            bmp(directory / L"reference.bmp", reference);
            std::ofstream result(directory / L"metrics.json");
            result << std::setprecision(10) << "{\n  \"provider\": \"" << context.provider()
                   << "\",\n  \"frames\": 48,\n  \"width\": 256,\n  \"height\": 144,\n  "
                      "\"edge_samples\": "
                   << samples << ",\n  \"raw_edge_mae\": " << rawError
                   << ",\n  \"native_aa_edge_mae\": " << aaError
                   << ",\n  \"last_eight_mean_frame_difference\": " << stability
                   << ",\n  \"improvement_percent\": " << (1 - aaError / rawError) * 100 << "\n}\n";
            std::cout << "Edge error: aliased=" << rawError << " Native AA=" << aaError
                      << " improvement=" << (1 - aaError / rawError) * 100
                      << "%; final-frame difference=" << stability << '\n';
            std::ofstream html(directory / L"comparison.html");
            html << "<!doctype html><meta charset=utf-8><title>FSR Native AA "
                    "check</title><style>body{background:#171d28;color:#e9eef6;font:18px "
                    "system-ui;margin:40px}main{display:flex;flex-wrap:wrap;gap:24px}figure{margin:"
                    "0}img{width:512px;height:288px;image-rendering:pixelated}p{max-width:1000px;"
                    "line-height:1.5}</style><h1>FSR Native AA on your GPU</h1><p>Provider "
                 << context.provider()
                 << " &middot; 256 &times; 144 &middot; 48 completed GPU "
                    "frames</p><p>Edge-coverage error improved by "
                 << std::fixed << std::setprecision(1) << (1 - aaError / rawError) * 100
                 << "% in this static analytic fixture. This measures diagonal edges and a thin "
                    "line against a 16 &times; 16 supersampled coverage reference. Images below "
                    "are enlarged 2&times; without smoothing.</p><main><figure><h2>Aliased "
                    "input</h2><img src=aliased.bmp alt='Aliased diagonal "
                    "edges'></figure><figure><h2>FSR Native AA</h2><img src=native-aa.bmp alt='FSR "
                    "reconstructed diagonal edges'></figure><figure><h2>Coverage "
                    "reference</h2><img src=reference.bmp alt='Supersampled coverage "
                    "reference'></figure></main><p>This is a standalone SDK test, not an X-Plane "
                    "screenshot or a flight stability test. Static geometry, zero motion, fixed "
                    "depth and correct jitter are supplied. Aircraft motion, moving scenery, "
                    "transparent surfaces, camera cuts and the simulator capture path require "
                    "separate validation.</p>";
            require(aaError < rawError * .95,
                    "Native AA did not improve this edge fixture by at least five percent");
        }
        vkcheck(vk.waitIdle(vk.device), "AA idle");
        vk.dev<PFN_vkDestroyBuffer>("vkDestroyBuffer")(vk.device, buffer, nullptr);
        vk.dev<PFN_vkFreeMemory>("vkFreeMemory")(vk.device, memory, nullptr);
        vk.dev<PFN_vkDestroyFence>("vkDestroyFence")(vk.device, fence, nullptr);
        vk.dev<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(vk.device, pool, nullptr);
        std::cout << "PASS: Native AA completed 48 fenced GPU frames and improved analytic edge "
                     "coverage.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

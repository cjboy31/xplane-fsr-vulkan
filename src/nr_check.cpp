// SPDX-License-Identifier: MIT
// Runs the real bridge/model on a small fenced HDR fixture, outside X-Plane.
#include "nr_api.hpp"
#include "nr_features.hpp"
#include "nrvk.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
static void need(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
static uint16_t packHalf(float value) {
    const uint16_t sign = std::signbit(value) ? 0x8000 : 0;
    value = std::abs(value);
    if (value < 0x1p-14f)
        return sign | uint16_t(std::lrint(std::ldexp(value, 24)));
    int e{};
    std::frexp(value, &e);
    const int exponent = e - 1;
    const auto mantissa = uint32_t(std::lrint(std::ldexp(value, 10 - exponent)));
    return sign | uint16_t(((exponent + 14) << 10) + mantissa);
}
static float unpackHalf(uint16_t value) {
    const int e = (value >> 10) & 31, m = value & 1023;
    if (e == 31)
        return m ? NAN : ((value & 0x8000) ? -INFINITY : INFINITY);
    const float f = e ? std::ldexp(1.f + float(m) / 1024, e - 15) : std::ldexp(float(m), -24);
    return (value & 0x8000) ? -f : f;
}
int wmain(int argc, wchar_t **argv) {
    std::cout.setf(std::ios::unitbuf);
    VkInstance instance{};
    VkDevice device{};
    HMODULE module{};
    void *context{};
    xpfsr::nr::Destroy destroy{};
    try {
        need(argc >= 3, "NRCheck.exe NR_ROOT OUTPUT_DIRECTORY [--features-only | --rgba16f-720p | --benchmark-1440p | --benchmark-4k | --pass-study] [model_percent] [study_passes]");
        const bool benchmark4k=argc>3&&std::wstring(argv[3])==L"--benchmark-4k";
        const bool benchmark=benchmark4k||(argc>3&&std::wstring(argv[3])==L"--benchmark-1440p");
        const bool study32=argc>3&&std::wstring(argv[3])==L"--pass-study-rgba32f";
        const bool passStudy=study32||(argc>3&&std::wstring(argv[3])==L"--pass-study");
        const int studyPasses=passStudy&&argc>5?std::stoi(argv[5]):1;
        need(studyPasses>=1&&studyPasses<=3,"Pass study requires one, two or three actual passes");
        const bool fp16 = (passStudy&&!study32)||benchmark||(argc > 3 && std::wstring(argv[3]) == L"--rgba16f-720p");
        const bool copyInput=argc>5&&std::wstring(argv[5])==L"--copy-input";
        bool singlePass=false;for(int i=3;i<argc;++i)singlePass|=std::wstring(argv[i])==L"--capacity-one";
        const auto root = std::filesystem::canonical(argv[1]);
        const auto output = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(output);
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "XPlane NR isolated check";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        NRVK_CHECK(vkCreateInstance(&info, nullptr, &instance));
        uint32_t count{};
        NRVK_CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
        std::vector<VkPhysicalDevice> devices(count);
        NRVK_CHECK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
        VkPhysicalDevice physical{};
        xpfsr::nr::Features features;
        for (auto p : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(p, &props);
            if (props.vendorID == 0x1002 && features.query(p, vkGetPhysicalDeviceFeatures2)) {
                physical = p;
                std::cout << "AMD GPU: " << props.deviceName << '\n';
                break;
            }
        }
        need(physical != VK_NULL_HANDLE,
             "Required AMD storage/FP16/BDA/subgroup/workgroup features unavailable");
        VkPhysicalDeviceCooperativeMatrixFeaturesKHR coop{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};
        VkPhysicalDeviceShaderFloat8FeaturesEXT fp8{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT};
        VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        query.pNext = &coop;
        coop.pNext = &fp8;
        vkGetPhysicalDeviceFeatures2(physical, &query);
        need(coop.cooperativeMatrix && fp8.shaderFloat8 && fp8.shaderFloat8CooperativeMatrix,
             "RDNA4 FP8 cooperative matrix features unavailable");
        auto matrix = reinterpret_cast<PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR>(
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR"));
        need(matrix != nullptr, "Cooperative matrix query unavailable");
        count = 0;
        NRVK_CHECK(matrix(physical, &count, nullptr));
        std::vector<VkCooperativeMatrixPropertiesKHR> matrices(
            count, {VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR});
        NRVK_CHECK(matrix(physical, &count, matrices.data()));
        bool matched = false;
        for (auto &m : matrices)
            matched |= m.MSize == 16 && m.NSize == 16 && m.KSize == 16 &&
                       m.scope == VK_SCOPE_SUBGROUP_KHR &&
                       m.AType == VK_COMPONENT_TYPE_FLOAT8_E4M3_EXT &&
                       m.BType == VK_COMPONENT_TYPE_FLOAT8_E4M3_EXT &&
                       m.CType == VK_COMPONENT_TYPE_FLOAT32_KHR &&
                       m.ResultType == VK_COMPONENT_TYPE_FLOAT32_KHR && !m.saturatingAccumulation;
        need(matched, "FP8 E4M3 16x16x16 -> FP32 subgroup matrix unavailable");
        std::cout << "PASS: FP8 matrix, buffer device address, storage, memory model, subgroup and "
                     "workgroup support\n";
        if (argc > 3 && std::wstring(argv[3]) == L"--features-only") {
            vkDestroyInstance(instance, nullptr);
            return 0;
        }
        count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
        uint32_t family = UINT32_MAX;
        for (uint32_t i = 0; i < count; ++i)
            if ((queues[i].queueFlags & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT)) ==
                (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT)) {
                family = i;
                break;
            }
        need(family != UINT32_MAX, "Graphics+compute queue unavailable");
        VkBaseOutStructure *chain = nullptr;
        VkBaseOutStructure **tail = &chain;
        auto append = [&](auto &v) {
            auto *n = reinterpret_cast<VkBaseOutStructure *>(&v);
            n->pNext = nullptr;
            *tail = n;
            tail = &n->pNext;
        };
        features.enable(chain, append);
        append(coop);
        append(fp8);
        VkPhysicalDeviceShaderFloat16Int8Features floats{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
        floats.shaderFloat16 = floats.shaderInt8 = VK_TRUE;
        append(floats);
        std::vector<const char *> extensions{
            VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME, VK_EXT_SHADER_FLOAT8_EXTENSION_NAME,
            VK_KHR_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_EXTENSION_NAME};
        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(physical, &deviceProperties);
        if (features.robust.pipelineRobustness && deviceProperties.apiVersion < VK_API_VERSION_1_4)
            extensions.push_back(VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME);
        count = 0;
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> advertised(count);
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, advertised.data());
        for (auto name : extensions) {
            bool found = false;
            for (auto &e : advertised)
                found |= !strcmp(name, e.extensionName);
            std::cout << "Extension " << name << ": " << (found ? "advertised" : "NOT ADVERTISED")
                      << '\n';
        }
        float priority = 1;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dc.pNext = chain;
        dc.queueCreateInfoCount = 1;
        dc.pQueueCreateInfos = &queueInfo;
        dc.enabledExtensionCount = uint32_t(extensions.size());
        dc.ppEnabledExtensionNames = extensions.data();
        NRVK_CHECK(vkCreateDevice(physical, &dc, nullptr, &device));
        VkQueue queue{};
        vkGetDeviceQueue(device, family, 0, &queue);
        module = LoadLibraryExW((root / L"XPFSRNeuralRendering.dll").c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        need(module != nullptr, "Cannot load NR bridge");
        auto create =
            reinterpret_cast<xpfsr::nr::Create>(GetProcAddress(module, "XPFSR_NR_Create"));
        auto record =
            reinterpret_cast<xpfsr::nr::Record>(GetProcAddress(module, "XPFSR_NR_Record"));
        destroy = reinterpret_cast<xpfsr::nr::Destroy>(GetProcAddress(module, "XPFSR_NR_Destroy"));
        auto metrics=reinterpret_cast<xpfsr::nr::Query>(GetProcAddress(module,"XPFSR_NR_Query"));
        need(create && record && destroy&&metrics, "NR bridge exports missing");
        xpfsr::nr::Host host{};
        host.instance = instance;
        host.physical = physical;
        host.device = device;
        host.queue = queue;
        host.queueFamily = family;
        host.log = [](const char *line) { std::cout << line << '\n'; };
        const uint32_t W = passStudy?512:benchmark4k?3840:benchmark?2560:fp16 ? 1280 : 256, H = passStudy?288:benchmark4k?2160:benchmark?1440:fp16 ? 720 : 144;
        auto path = root.wstring();
        xpfsr::nr::Config config{};
        config.root = path.c_str();
        config.width = W;
        config.height = H;
        config.format = fp16 ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT;
        config.strength = 1;
        config.controls[xpfsr::nr::Passes]=benchmark||singlePass?1:3; // stress fixture capacity
        if(passStudy)need(config.controls[xpfsr::nr::Passes]>=studyPasses,"Study pass count exceeds the allocated context capacity");
        if(passStudy){config.controls[xpfsr::nr::Intensity]=100;config.controls[xpfsr::nr::Style]=2;}
        if(argc>4)config.controls[xpfsr::nr::Scale]=std::stoi(argv[4]);
        need(config.controls.valid(),"Unsupported NR model percent");
        char why[512]{};
        context = create(&host, &config, why, sizeof(why));
        need(context != nullptr, why);
        std::cout << why << '\n';
        nrvk::Context gpu;
        std::string reason;
        need(gpu.adopt(instance, physical, device, queue, family, &reason), reason.c_str());
        gpu.buffer_device_address = true;
        auto image = gpu.image(W, H, config.format, true);
        gpu.transition(image, VK_IMAGE_LAYOUT_GENERAL);
        std::vector<float> original(W * H * 4), readback(original.size());
        std::vector<uint16_t> packed(fp16 ? original.size() : 0);
        double difference = 0;
        uint64_t finite = 0;
        uint32_t recorded = 0;
        const int frames = passStudy?32:benchmark?40:fp16 ? 16 : 8;
        std::vector<float>gpuTimes;
        for (int frame = 0; frame < frames; ++frame) {
            // Moving detail, dark tones, bright HDR light and an explicit mid-run reset.
            for (uint32_t y = 0; y < H; ++y)
                for (uint32_t x = 0; x < W; ++x) {
                    size_t i = (y * W + x) * 4;
                    uint32_t seed=(x*1664525u)^(y*1013904223u);seed^=seed>>13;seed*=2246822519u;
                    float v = passStudy ? .07f+float(y)/H*.25f+float(seed&255)/255*.10f+
                        (((x/2+y/2)%2)? .04f:0.f)+(((x/12+y/9)%2)? .09f:0.f) :
                        .008f + float(x) / W * .8f + (((x + frame * 3) / 9 + y / 7) % 2) * .12f;
                    original[i] = v;
                    original[i + 1] = v * .8f;
                    original[i + 2] = v * .6f;
                    original[i + 3] = 1;
                    if (x > W * 7 / 8)
                        original[i] = original[i + 1] = original[i + 2] = 2.0f;
                }
            if (fp16) {
                for (size_t i = 0; i < original.size(); ++i) {
                    packed[i] = packHalf(original[i]);
                    original[i] = unpackHalf(packed[i]);
                }
            }
            const uint32_t pixelBytes = fp16 ? 8 : 16;
            gpu.image_rows(image, W * H * pixelBytes, true,
                           [&](uint32_t y, uint32_t n, uint8_t *p) {
                               if (fp16)
                                   memcpy(p, packed.data() + y * W * 4, n * W * 8);
                               else
                                   memcpy(p, original.data() + y * W * 4, n * W * 16);
                           });
            gpu.one_shot([&](VkCommandBuffer cb) {
                xpfsr::nr::Frame f{};
                f.commands = cb;
                f.color = image.handle;
                if(!copyInput)f.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                f.reset = passStudy?frame==0:frame == 0 || frame == 4 || (!benchmark&&frame==7);
                f.controls=config.controls;
                f.controls[xpfsr::nr::Passes]=passStudy?studyPasses:1;
                f.controls[xpfsr::nr::Enabled]=passStudy||benchmark||frame!=2;
                f.reset|=!passStudy&&!benchmark&&frame==3;
                if(!passStudy&&!benchmark&&frame>=4&&frame<7){f.controls[xpfsr::nr::Intensity]=50;f.controls[xpfsr::nr::Tone]=25;f.controls[xpfsr::nr::Structure]=110;f.controls[xpfsr::nr::Skin]=50;f.controls[xpfsr::nr::History]=40;f.controls[xpfsr::nr::Style]=1;f.controls[xpfsr::nr::Passes]=singlePass?1:2;}
                if(!passStudy&&!benchmark&&!singlePass&&frame==7)f.controls[xpfsr::nr::Passes]=3;
                int result = record(context, &f, why, sizeof(why));
                need(result == (f.controls[xpfsr::nr::Enabled]?1:0), why);
                recorded+=result;
            });
            xpfsr::nr::Metrics timing{};need(metrics(context,&timing,sizeof(timing))==1,"NR timing query failed");if(frame>=10&&timing.lastGpuMs>0)gpuTimes.push_back(timing.lastGpuMs);
            gpu.image_rows(image, W * H * pixelBytes, false,
                           [&](uint32_t y, uint32_t n, uint8_t *p) {
                               if (fp16)
                                   memcpy(packed.data() + y * W * 4, p, n * W * 8);
                               else
                                   memcpy(readback.data() + y * W * 4, p, n * W * 16);
                           });
            if (fp16)
                for (size_t i = 0; i < readback.size(); ++i)
                    readback[i] = unpackHalf(packed[i]);
            for (size_t i = 0; i < readback.size(); ++i) {
                need(std::isfinite(readback[i]) && std::abs(readback[i]) < 64,
                     "NR output is non-finite or outside HDR fixture bounds");
                ++finite;
                difference += std::abs(double(readback[i]) - original[i]);
                if(!passStudy&&!benchmark&&frame==2)need(readback[i]==original[i],"Live NR Off changed the original input");
            }
        }
        need(difference > 0.001, "No measurable model output change");
        std::ofstream(output/L"NR-LAST.rgba32f",std::ios::binary).write(reinterpret_cast<const char*>(readback.data()),std::streamsize(readback.size()*sizeof(float)));
        if(passStudy){std::ofstream(output/L"NR-INPUT.rgba32f",std::ios::binary).write(reinterpret_cast<const char*>(original.data()),std::streamsize(original.size()*sizeof(float)));
            std::ofstream study(output/L"NR-PASS-STUDY.json");study<<"{\"width\":"<<W<<",\"height\":"<<H<<",\"actualPasses\":"<<studyPasses<<",\"capacity\":"<<config.controls[xpfsr::nr::Passes]<<",\"modelPercent\":"<<config.controls[xpfsr::nr::Scale]<<",\"frames\":"<<frames<<",\"syntheticOnly\":true}";}
        std::ofstream report(output / L"NR-CHECK.txt");
        report << "PASS: real NR model on GPU; " << recorded << " fenced frames; " << finite
               << " finite samples; absolute difference=" << difference << '\n'
               << W << 'x' << H << " synthetic linear HDR " << (fp16 ? "RGBA16F" : "RGBA32F")
               << " fixture, moving detail + history reset; not live-flight validation.\n";
        if(!gpuTimes.empty()){
            std::sort(gpuTimes.begin(),gpuTimes.end());float median=gpuTimes[gpuTimes.size()/2];
            report<<"Model resolution="<<config.controls[xpfsr::nr::Scale]<<"%; median NR GPU="<<median<<" ms; timestamp samples="<<gpuTimes.size()<<"\n";
            std::ofstream json(output/L"NR-GPU-TIMING.json");json<<"{\"modelPercent\":"<<config.controls[xpfsr::nr::Scale]<<",\"inputWidth\":"<<W<<",\"inputHeight\":"<<H<<",\"medianGpuMs\":"<<median<<",\"samples\":"<<gpuTimes.size()<<",\"syntheticOnly\":true}";
            std::cout<<"NR GPU median="<<median<<" ms, "<<config.controls[xpfsr::nr::Scale]<<"% model, "<<gpuTimes.size()<<" timestamp samples; excludes X-Plane and FSR/FG\n";
        }
        std::cout << "PASS: real model, " << recorded << " fenced frames; finite samples=" << finite
                  << "; difference=" << difference << ". Synthetic fixture only.\n";
        gpu.destroy(image);
        destroy(context);
        context = nullptr;
        gpu.device = VK_NULL_HANDLE;
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
        FreeLibrary(module);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "NR CHECK FAILED: " << e.what() << '\n';
        if (device) {
            vkDeviceWaitIdle(device);
            if (context && destroy)
                destroy(context);
            vkDestroyDevice(device, nullptr);
        }
        if (instance)
            vkDestroyInstance(instance, nullptr);
        if (module)
            FreeLibrary(module);
        return 1;
    }
}

// SPDX-License-Identifier: MIT
#include "nr_features.hpp"
#include "nr_models.hpp"
#include "nr_policy.hpp"
#include "nr_controls.hpp"
#include <atomic>
#include <thread>
#include <iostream>
#include <set>
#include <stdexcept>
static void require(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
static void chainTest(bool aggregates) {
    xpfsr::nr::Features request;
    request.robust.pipelineRobustness = VK_TRUE;
    request.workgroup.workgroupMemoryExplicitLayout = VK_TRUE;
    request.workgroup.workgroupMemoryExplicitLayout8BitAccess = VK_TRUE;
    request.workgroup.workgroupMemoryExplicitLayout16BitAccess = VK_TRUE;
    VkPhysicalDeviceVulkan11Features v11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan14Features v14{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
    VkPhysicalDevice8BitStorageFeatures storage{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES};
    VkPhysicalDeviceBufferDeviceAddressFeatures bda{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR wml{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR};
    VkBaseOutStructure *head = nullptr, **tail = &head;
    auto append = [&](auto &f) {
        auto *n = reinterpret_cast<VkBaseOutStructure *>(&f);
        n->pNext = nullptr;
        *tail = n;
        tail = &n->pNext;
    };
    if (aggregates) {
        v12.timelineSemaphore = VK_TRUE;
        v12.scalarBlockLayout = VK_TRUE;
        append(v11);
        append(v12);
        append(v13);
        append(v14);
    } else {
        storage.uniformAndStorageBuffer8BitAccess = VK_TRUE;
        bda.bufferDeviceAddressCaptureReplay = VK_TRUE;
        append(storage);
        append(bda);
        append(wml);
    }
    request.enable(head, append);
    std::set<VkStructureType> types;
    for (auto *p = head; p; p = p->pNext)
        require(types.insert(p->sType).second, "Duplicate feature structure");
    if (aggregates) {
        require(types.size() == 5, "Individual aliases duplicated aggregate features");
        require(v11.storageBuffer16BitAccess && v12.storageBuffer8BitAccess &&
                    v12.vulkanMemoryModel && v12.bufferDeviceAddress && v13.subgroupSizeControl &&
                    v14.pipelineRobustness,
                "Aggregate requirements not enabled");
        require(v12.timelineSemaphore && v12.scalarBlockLayout, "Existing app features lost");
    } else {
        require(storage.storageBuffer8BitAccess && storage.uniformAndStorageBuffer8BitAccess &&
                    bda.bufferDeviceAddress && bda.bufferDeviceAddressCaptureReplay,
                "Individual feature requirements/preservation");
        require(wml.workgroupMemoryExplicitLayout && wml.workgroupMemoryExplicitLayout8BitAccess &&
                    wml.workgroupMemoryExplicitLayout16BitAccess &&
                    !wml.workgroupMemoryExplicitLayoutScalarBlockLayout,
                "Workgroup feature dependency");
        require(types.size() == 7, "Missing individual feature structure");
    }
}
int main() {
    try {
        chainTest(true);
        chainTest(false);
        using namespace xpfsr::nr;
        require(acceptedModel(true, 113917248, smallModelSha) &&
                    acceptedModel(false, 147756560, originalModelSha),
                "Known model rejected");
        require(!acceptedModel(true, 113917247, smallModelSha) &&
                    !acceptedModel(true, 113917248, originalModelSha) &&
                    !acceptedModel(false, 147756560, "corrupt"),
                "Wrong model accepted");
        require(app::effectiveNeuralRendering(-1) == 0 && app::effectiveNeuralRendering(0) == 0 &&
                    app::effectiveNeuralRendering(5) == 0,
                "Invalid NR choice enabled");
        for (int n = 1; n <= 4; ++n)
            require(app::effectiveNeuralRendering(n) == (app::neuralRenderingAvailable ? n : 0),
                    "NR build gate bypassed");
        Controls c;
        require(c.valid()&&c[Order]==0,"NR must default to before upscaling");
        SharedControls shared{};Controls read;
        writeControls(&shared,c);require(readControls(&shared,read)&&read==c,"Initial NR IPC snapshot");
        c[Enabled]=1;c[Intensity]=67;c[Scale]=33;c[Tone]=45;c[AllowFG]=1;c[Order]=1;
        writeControls(&shared,c);require(readControls(&shared,read)&&read==c,"Live enable/artistic/profile/FG snapshot");
        InterlockedExchange(&shared.version,3);require(!readControls(&shared,read),"Old NR control ABI accepted");
        writeControls(&shared,c);InterlockedExchange(&shared.value[Order],2);require(!readControls(&shared,read),"Invalid NR order accepted");
        c[Enabled]=0;writeControls(&shared,c);require(readControls(&shared,read)&&!read[Enabled]&&read[Intensity]==67,"Live disable loses artistic settings");
        InterlockedExchange(&shared.value[Scale],34);require(!readControls(&shared,read),"Unsupported model scale accepted");
        writeControls(&shared,c);InterlockedIncrement(&shared.sequence);require(!readControls(&shared,read),"Torn control snapshot accepted");InterlockedIncrement(&shared.sequence);
        std::atomic<bool> done{};std::atomic<unsigned> snapshots{};
        std::thread writer([&]{for(int i=0;i<30000;++i){Controls p;p[Intensity]=i%101;p[Tone]=p[Intensity];p[Structure]=p[Intensity];writeControls(&shared,p);}done=true;});
        while(!done){Controls p;if(readControls(&shared,p)){if(p[Tone]==p[Structure])require(p[Intensity]==p[Tone],"Mixed live NR snapshot");++snapshots;}}
        writer.join();require(snapshots>0,"NR IPC fixture read no snapshots");
        performanceProfile(c,0);require(c[Scale]==33&&c[Passes]==1&&c[Intensity]==67&&c[Order]==1,"Performance budget changes strength or processing order");
        performanceProfile(c,2);require(c[Scale]==67&&c[Passes]==1,"Quality budget");
        std::cout<<"PASS: live On/Off, artistic/profile controls, torn/corrupt snapshots and concurrent IPC updates\n";
        std::cout << "PASS: NR build gate, model identity/size/corruption rejection, "
                     "aggregate/individual/core-1.4 Vulkan chains and app-feature preservation\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

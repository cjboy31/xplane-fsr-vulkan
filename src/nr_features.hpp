// SPDX-License-Identifier: MIT
#pragma once
#include <vulkan/vulkan.h>
namespace xpfsr::nr {
// Added only to the layer's private clone; never modifies X-Plane's pNext nodes.
struct Features {
    VkPhysicalDeviceFeatures2 all{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan11Features v11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDevice16BitStorageFeatures r16{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
    VkPhysicalDevice8BitStorageFeatures r8{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES};
    VkPhysicalDeviceVulkanMemoryModelFeatures memory{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES};
    VkPhysicalDeviceSubgroupSizeControlFeatures subgroup{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};
    VkPhysicalDeviceBufferDeviceAddressFeatures address{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR workgroup{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR};
    VkPhysicalDevicePipelineRobustnessFeaturesEXT robust{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT};
    bool query(VkPhysicalDevice physical, PFN_vkGetPhysicalDeviceFeatures2 fn) {
        all.pNext = &v11;
        v11.pNext = &v12;
        v12.pNext = &v13;
        v13.pNext = &workgroup;
        workgroup.pNext = &robust;
        fn(physical, &all);
        workgroup.workgroupMemoryExplicitLayoutScalarBlockLayout = VK_FALSE;
        return v11.storageBuffer16BitAccess && v12.storageBuffer8BitAccess && v12.shaderFloat16 &&
               v12.shaderInt8 && v12.vulkanMemoryModel && v12.bufferDeviceAddress &&
               v13.subgroupSizeControl && workgroup.workgroupMemoryExplicitLayout &&
               workgroup.workgroupMemoryExplicitLayout8BitAccess &&
               workgroup.workgroupMemoryExplicitLayout16BitAccess;
    }
    template <class Append> void enable(VkBaseOutStructure *head, Append append) {
        bool storage16 = false, storage8 = false, model = false, sub = false, bda = false,
             wml = false, pr = false;
        for (auto *p = head; p; p = p->pNext) {
            switch (p->sType) {
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
                reinterpret_cast<VkPhysicalDeviceVulkan11Features *>(p)->storageBuffer16BitAccess =
                    VK_TRUE;
                storage16 = true;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES:
                reinterpret_cast<VkPhysicalDevice16BitStorageFeatures *>(p)
                    ->storageBuffer16BitAccess = VK_TRUE;
                storage16 = true;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
                auto *f = reinterpret_cast<VkPhysicalDeviceVulkan12Features *>(p);
                f->storageBuffer8BitAccess = f->vulkanMemoryModel = f->bufferDeviceAddress =
                    VK_TRUE;
                storage8 = model = bda = true;
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES:
                reinterpret_cast<VkPhysicalDeviceBufferDeviceAddressFeatures *>(p)
                    ->bufferDeviceAddress = VK_TRUE;
                bda = true;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR: {
                auto *f =
                    reinterpret_cast<VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *>(p);
                f->workgroupMemoryExplicitLayout = f->workgroupMemoryExplicitLayout8BitAccess =
                    f->workgroupMemoryExplicitLayout16BitAccess = VK_TRUE;
                wml = true;
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES:
                reinterpret_cast<VkPhysicalDeviceVulkan14Features *>(p)->pipelineRobustness =
                    robust.pipelineRobustness;
                pr = true;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT:
                reinterpret_cast<VkPhysicalDevicePipelineRobustnessFeaturesEXT *>(p)
                    ->pipelineRobustness = robust.pipelineRobustness;
                pr = true;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES:
                reinterpret_cast<VkPhysicalDevice8BitStorageFeatures *>(p)
                    ->storageBuffer8BitAccess = VK_TRUE;
                storage8 = true;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES:
                reinterpret_cast<VkPhysicalDeviceVulkanMemoryModelFeatures *>(p)
                    ->vulkanMemoryModel = VK_TRUE;
                model = true;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
                reinterpret_cast<VkPhysicalDeviceVulkan13Features *>(p)->subgroupSizeControl =
                    VK_TRUE;
                sub = true;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES:
                reinterpret_cast<VkPhysicalDeviceSubgroupSizeControlFeatures *>(p)
                    ->subgroupSizeControl = VK_TRUE;
                sub = true;
                break;
            default:
                break;
            }
        }
        if (!storage16) {
            r16.storageBuffer16BitAccess = VK_TRUE;
            append(r16);
        }
        if (!storage8) {
            r8.storageBuffer8BitAccess = VK_TRUE;
            append(r8);
        }
        if (!model) {
            memory.vulkanMemoryModel = VK_TRUE;
            append(memory);
        }
        if (!sub) {
            subgroup.subgroupSizeControl = VK_TRUE;
            append(subgroup);
        }
        if (!bda) {
            address.bufferDeviceAddress = VK_TRUE;
            append(address);
        }
        if (!wml)
            append(workgroup);
        if (!pr && robust.pipelineRobustness)
            append(robust);
    }
};
} // namespace xpfsr::nr

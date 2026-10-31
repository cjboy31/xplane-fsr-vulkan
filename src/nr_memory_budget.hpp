// SPDX-License-Identifier: MIT
#pragma once
#include "nr_memory_policy.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <cstring>
#include <sstream>
namespace xpfsr::nr {
struct GpuMemoryBudget {uint64_t budget{},usage{};bool available{};};
inline GpuMemoryBudget queryGpuMemoryBudget(VkInstance instance,VkPhysicalDevice physical,PFN_vkGetInstanceProcAddr next){
    GpuMemoryBudget result;
    if(!next){
#ifdef VK_NO_PROTOTYPES
        return result;
#else
        next=vkGetInstanceProcAddr;
#endif
    }
    auto enumerate=reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(next(instance,"vkEnumerateDeviceExtensionProperties"));
    auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(next(instance,"vkGetPhysicalDeviceMemoryProperties2"));
    if(!properties)properties=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(next(instance,"vkGetPhysicalDeviceMemoryProperties2KHR"));
    if(!enumerate||!properties)return result;
    uint32_t count{};if(enumerate(physical,nullptr,&count,nullptr)!=VK_SUCCESS)return result;
    std::vector<VkExtensionProperties> extensions(count);
    if(enumerate(physical,nullptr,&count,extensions.data())!=VK_SUCCESS)return result;
    bool supported=false;for(const auto&e:extensions)supported|=!std::strcmp(e.extensionName,VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    if(!supported)return result;
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 memory{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};memory.pNext=&budget;
    properties(physical,&memory);
    // The largest device-local heap holds the network on a discrete GPU. Shared
    // system-memory heaps must never be mistaken for dedicated texture room.
    VkDeviceSize largest{};
    for(uint32_t i=0;i<memory.memoryProperties.memoryHeapCount;++i){const auto&heap=memory.memoryProperties.memoryHeaps[i];
        if((heap.flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)&&heap.size>largest&&budget.heapBudget[i]){
            largest=heap.size;result={budget.heapBudget[i],budget.heapUsage[i],true};}}
    return result;
}
inline std::string memoryAdmissionReason(const GpuMemoryBudget&memory,uint64_t estimate){
    std::ostringstream out;out<<"GPU memory reserve: NR needs an estimated "<<estimate/MiB<<" MiB plus "
        <<textureReserveBytes(memory.budget)/MiB<<" MiB of texture headroom; "
        <<(memory.budget>memory.usage?(memory.budget-memory.usage)/MiB:0)<<" MiB available. FSR remains available";
    return out.str();
}
} // namespace xpfsr::nr

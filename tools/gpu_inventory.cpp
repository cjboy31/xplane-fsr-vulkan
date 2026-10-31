// SPDX-License-Identifier: MIT
// Original development probe. No device creation, injection or GPU work.
#include "nr_features.hpp"
#include "nr_memory_budget.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
static std::string hex(const uint8_t*p,size_t n){std::ostringstream s;for(size_t i=0;i<n;++i)s<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(p[i]);return s.str();}
static std::string quote(const char*p){std::ostringstream s;s<<'"';for(;*p;++p){if(*p=='"'||*p=='\\')s<<'\\';if(static_cast<unsigned char>(*p)>=32)s<<*p;}s<<'"';return s.str();}
int main(){VkInstance instance{};try{
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="XPFSR multi-GPU capability probe";app.apiVersion=VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;
    if(vkCreateInstance(&ci,nullptr,&instance)!=VK_SUCCESS)throw std::runtime_error("Vulkan instance failed");
    uint32_t count{};vkEnumeratePhysicalDevices(instance,&count,nullptr);std::vector<VkPhysicalDevice> devices(count);vkEnumeratePhysicalDevices(instance,&count,devices.data());
    std::cout<<"{\"scope\":\"capabilities only; no offload path or performance claim\",\"adapters\":[";
    bool first=true;for(auto physical:devices){
        VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};p.pNext=&ids;vkGetPhysicalDeviceProperties2(physical,&p);
        xpfsr::nr::Features required;bool base=required.query(physical,vkGetPhysicalDeviceFeatures2);
        VkPhysicalDeviceShaderFloat8FeaturesEXT fp8{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT};
        VkPhysicalDeviceCooperativeMatrixFeaturesKHR coop{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};coop.pNext=&fp8;
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&coop;vkGetPhysicalDeviceFeatures2(physical,&features);
        bool shape=false;if(coop.cooperativeMatrix&&fp8.shaderFloat8&&fp8.shaderFloat8CooperativeMatrix){auto query=reinterpret_cast<PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR>(vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR"));uint32_t n{};
            if(query&&query(physical,&n,nullptr)==VK_SUCCESS){std::vector<VkCooperativeMatrixPropertiesKHR> m(n,{VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR});if(query(physical,&n,m.data())==VK_SUCCESS)for(const auto&v:m)shape|=v.MSize==16&&v.NSize==16&&v.KSize==16&&v.scope==VK_SCOPE_SUBGROUP_KHR&&v.AType==VK_COMPONENT_TYPE_FLOAT8_E4M3_EXT&&v.BType==VK_COMPONENT_TYPE_FLOAT8_E4M3_EXT&&v.CType==VK_COMPONENT_TYPE_FLOAT32_KHR&&v.ResultType==VK_COMPONENT_TYPE_FLOAT32_KHR&&!v.saturatingAccumulation;}}
        uint32_t n{};vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr);std::vector<VkQueueFamilyProperties> queues(n);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,queues.data());bool compute=false;for(auto&q:queues)compute|=q.queueCount&&(q.queueFlags&VK_QUEUE_COMPUTE_BIT);
        VkFormatProperties f16{},f32{};vkGetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R16G16B16A16_SFLOAT,&f16);vkGetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R32G32B32A32_SFLOAT,&f32);
        auto support=[](const VkFormatProperties&f){const auto need=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;return (f.optimalTilingFeatures&need)==need;};
        VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);uint64_t local{};for(uint32_t i=0;i<memory.memoryHeapCount;++i)if(memory.memoryHeaps[i].flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)local+=memory.memoryHeaps[i].size;
        auto budget=xpfsr::nr::queryGpuMemoryBudget(instance,physical,vkGetInstanceProcAddr);
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"name\":"<<quote(p.properties.deviceName)<<",\"vendorId\":"<<p.properties.vendorID<<",\"deviceId\":"<<p.properties.deviceID<<",\"deviceType\":"<<p.properties.deviceType
          <<",\"deviceUuid\":\""<<hex(ids.deviceUUID,VK_UUID_SIZE)<<"\",\"driverUuid\":\""<<hex(ids.driverUUID,VK_UUID_SIZE)<<"\",\"driverVersion\":"<<p.properties.driverVersion<<",\"luidValid\":"<<(ids.deviceLUIDValid?"true":"false")
          <<",\"deviceLocalHeapBytes\":"<<local<<",\"processHeapBudgetBytes\":"<<budget.budget<<",\"processHeapUsageBytes\":"<<budget.usage
          <<",\"computeQueue\":"<<(compute?"true":"false")<<",\"rgba16fStorageTransfer\":"<<(support(f16)?"true":"false")<<",\"rgba32fStorageTransfer\":"<<(support(f32)?"true":"false")
          <<",\"nrFeatureCandidate\":"<<((base&&shape)?"true":"false")<<",\"frameGenerationOffload\":\"requires SDK and presentation tests\"}";
    }std::cout<<"]}\n";vkDestroyInstance(instance,nullptr);return 0;
}catch(const std::exception&e){if(instance)vkDestroyInstance(instance,nullptr);std::cerr<<e.what()<<'\n';return 1;}}

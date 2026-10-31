// SPDX-License-Identifier: MIT
// Offscreen correctness fixtures. Synchronous fences here are not a live pacing implementation.
#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace xpfsr::gpu::worker {
inline void need(bool ok,const char*what){if(!ok)throw std::runtime_error(what);}
inline void checked(VkResult r,const char*what){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(what)+": "+std::to_string(r));}
inline std::string uuid(VkPhysicalDevice p){
    VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 prop{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};prop.pNext=&id;
    vkGetPhysicalDeviceProperties2(p,&prop);std::ostringstream out;
    for(auto b:id.deviceUUID)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return out.str();
}
struct Instance {
    VkInstance handle{};
    explicit Instance(const std::vector<const char*>&extensions={},uint32_t api=VK_API_VERSION_1_3){
        VkApplicationInfo a{VK_STRUCTURE_TYPE_APPLICATION_INFO};a.pApplicationName="XPFSR offscreen worker fixture";a.apiVersion=api;
        VkInstanceCreateInfo c{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};c.pApplicationInfo=&a;c.enabledExtensionCount=uint32_t(extensions.size());c.ppEnabledExtensionNames=extensions.data();
        checked(vkCreateInstance(&c,nullptr,&handle),"Create worker instance");
    }
    Instance(const Instance&)=delete;
    std::vector<VkPhysicalDevice> devices()const{uint32_t n{};checked(vkEnumeratePhysicalDevices(handle,&n,nullptr),"Enumerate worker GPUs");std::vector<VkPhysicalDevice> p(n);checked(vkEnumeratePhysicalDevices(handle,&n,p.data()),"Enumerate worker GPUs");p.resize(n);return p;}
    VkPhysicalDevice select(const std::string&id)const{
        need(id.size()==32&&id.find_first_not_of("0123456789abcdef")==std::string::npos,"A device UUID is required");
        VkPhysicalDevice result{};for(auto p:devices())if(uuid(p)==id){need(!result,"Ambiguous device UUID");result=p;}
        need(result!=VK_NULL_HANDLE,"Selected physical GPU is unavailable");return result;
    }
    ~Instance(){if(handle)vkDestroyInstance(handle,nullptr);}
};
struct Device {
    VkDevice handle{};VkPhysicalDevice physical{};VkQueue queue{};uint32_t family{};
    VkCommandPool pool{};VkCommandBuffer commands{};VkFence fence{};
    VkPhysicalDeviceMemoryProperties memory{};
    explicit Device(VkPhysicalDevice p,const std::vector<const char*>&extensions={},const std::function<void(void**)>&featuresHook={}):physical(p){try{
        VkPhysicalDeviceProperties prop{};vkGetPhysicalDeviceProperties(p,&prop);need(prop.apiVersion>=VK_API_VERSION_1_3,"Worker fixture requires Vulkan 1.3");
        uint32_t n{};vkGetPhysicalDeviceQueueFamilyProperties(p,&n,nullptr);std::vector<VkQueueFamilyProperties> q(n);vkGetPhysicalDeviceQueueFamilyProperties(p,&n,q.data());
        family=UINT32_MAX;for(uint32_t i=0;i<n;++i)if(q[i].queueCount&&(q[i].queueFlags&VK_QUEUE_COMPUTE_BIT)){family=i;break;}need(family!=UINT32_MAX,"Compute queue unavailable");
        VkPhysicalDeviceFeatures2 f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        f.pNext=&f11;f11.pNext=&f12;f12.pNext=&f13;vkGetPhysicalDeviceFeatures2(p,&f);
        void*chain=&f;if(featuresHook)featuresHook(&chain);
        float priority=1;VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qc.queueFamilyIndex=family;qc.queueCount=1;qc.pQueuePriorities=&priority;
        VkDeviceCreateInfo c{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};c.pNext=chain;c.queueCreateInfoCount=1;c.pQueueCreateInfos=&qc;c.enabledExtensionCount=uint32_t(extensions.size());c.ppEnabledExtensionNames=extensions.data();
        checked(vkCreateDevice(p,&c,nullptr,&handle),"Create processing device");vkGetDeviceQueue(handle,family,0,&queue);vkGetPhysicalDeviceMemoryProperties(p,&memory);
        VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pc.queueFamilyIndex=family;pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;checked(vkCreateCommandPool(handle,&pc,nullptr,&pool),"Create worker pool");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;checked(vkAllocateCommandBuffers(handle,&ca,&commands),"Allocate worker command");
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};checked(vkCreateFence(handle,&fc,nullptr,&fence),"Create worker fence");
    }catch(...){destroy();throw;}}
    Device(const Device&)=delete;
    uint32_t memoryType(uint32_t bits,VkMemoryPropertyFlags required)const{
        for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&required)==required)return i;
        throw std::runtime_error("Worker memory type unavailable");
    }
    void submit(const std::function<void(VkCommandBuffer)>&record){
        checked(vkResetFences(handle,1,&fence),"Reset worker fence");checked(vkResetCommandPool(handle,pool,0),"Reset worker commands");
        VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};b.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;checked(vkBeginCommandBuffer(commands,&b),"Begin worker commands");record(commands);checked(vkEndCommandBuffer(commands),"End worker commands");
        VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=1;s.pCommandBuffers=&commands;checked(vkQueueSubmit(queue,1,&s,fence),"Submit worker commands");
        checked(vkWaitForFences(handle,1,&fence,VK_TRUE,10000000000ull),"Complete worker commands");
    }
    void destroy(){if(handle){vkDeviceWaitIdle(handle);if(fence)vkDestroyFence(handle,fence,nullptr);if(pool)vkDestroyCommandPool(handle,pool,nullptr);vkDestroyDevice(handle,nullptr);handle=VK_NULL_HANDLE;}}
    ~Device(){destroy();}
};
struct Buffer {
    Device&d;VkBuffer handle{};VkDeviceMemory memory{};void*mapped{};VkDeviceSize bytes{};bool coherent{};
    Buffer(Device&owner,VkDeviceSize size):d(owner),bytes(size){try{
        need(size&&size<=256ull*1024*1024,"Invalid worker staging budget");
        VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};c.size=size;c.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;checked(vkCreateBuffer(d.handle,&c,nullptr,&handle),"Create staging buffer");
        VkMemoryRequirements r{};vkGetBufferMemoryRequirements(d.handle,handle,&r);auto type=d.memoryType(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);coherent=(d.memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=type;checked(vkAllocateMemory(d.handle,&a,nullptr,&memory),"Allocate staging memory");checked(vkBindBufferMemory(d.handle,handle,memory,0),"Bind staging memory");checked(vkMapMemory(d.handle,memory,0,VK_WHOLE_SIZE,0,&mapped),"Map staging memory");
    }catch(...){destroy();throw;}}
    Buffer(const Buffer&)=delete;
    void sync(bool upload){if(coherent)return;VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};r.memory=memory;r.size=VK_WHOLE_SIZE;checked(upload?vkFlushMappedMemoryRanges(d.handle,1,&r):vkInvalidateMappedMemoryRanges(d.handle,1,&r),"Synchronize staging memory");}
    void destroy(){if(mapped)vkUnmapMemory(d.handle,memory);if(handle)vkDestroyBuffer(d.handle,handle,nullptr);if(memory)vkFreeMemory(d.handle,memory,nullptr);mapped=nullptr;handle=VK_NULL_HANDLE;memory=VK_NULL_HANDLE;}
    ~Buffer(){destroy();}
};
struct Image {
    Device&d;VkImage handle{};VkImageView view{};VkDeviceMemory memory{};VkFormat format{};uint32_t width{},height{};VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
    Image(Device&owner,uint32_t w,uint32_t h,VkFormat f):d(owner),format(f),width(w),height(h){try{
        need(w&&h&&w<=4096&&h<=4096,"Invalid worker image extent");
        VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};c.imageType=VK_IMAGE_TYPE_2D;c.format=f;c.extent={w,h,1};c.mipLevels=c.arrayLayers=1;c.samples=VK_SAMPLE_COUNT_1_BIT;c.tiling=VK_IMAGE_TILING_OPTIMAL;c.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        checked(vkCreateImage(d.handle,&c,nullptr,&handle),"Create worker image");VkMemoryRequirements r{};vkGetImageMemoryRequirements(d.handle,handle,&r);VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=d.memoryType(r.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);checked(vkAllocateMemory(d.handle,&a,nullptr,&memory),"Allocate worker image");checked(vkBindImageMemory(d.handle,handle,memory,0),"Bind worker image");
        VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};v.image=handle;v.viewType=VK_IMAGE_VIEW_TYPE_2D;v.format=f;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};checked(vkCreateImageView(d.handle,&v,nullptr,&view),"Create worker view");
    }catch(...){destroy();throw;}}
    Image(const Image&)=delete;
    uint64_t byteSize()const{
        uint64_t pixel=0;switch(format){case VK_FORMAT_R16G16B16A16_SFLOAT:pixel=8;break;case VK_FORMAT_R32G32B32A32_SFLOAT:pixel=16;break;case VK_FORMAT_R16G16_SFLOAT:case VK_FORMAT_R32_SFLOAT:pixel=4;break;default:throw std::runtime_error("Worker image transfer format is unsupported");}
        return uint64_t(width)*height*pixel;
    }
    void transition(VkCommandBuffer cb,VkImageLayout next){
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=handle;b.oldLayout=layout;b.newLayout=next;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.srcAccessMask=layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);layout=next;
    }
    VkBufferImageCopy region()const{VkBufferImageCopy r{};r.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};r.imageExtent={width,height,1};return r;}
    void upload(Buffer&b){need(&d==&b.d,"Cross-device buffer handle rejected");need(b.bytes>=byteSize(),"Worker upload buffer is too small");b.sync(true);d.submit([&](auto cb){transition(cb,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);auto r=region();vkCmdCopyBufferToImage(cb,b.handle,handle,layout,1,&r);transition(cb,VK_IMAGE_LAYOUT_GENERAL);});}
    void read(Buffer&b){need(&d==&b.d,"Cross-device readback handle rejected");need(b.bytes>=byteSize(),"Worker readback buffer is too small");d.submit([&](auto cb){transition(cb,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);auto r=region();vkCmdCopyImageToBuffer(cb,handle,layout,b.handle,1,&r);VkBufferMemoryBarrier m{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};m.buffer=b.handle;m.size=VK_WHOLE_SIZE;m.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;m.dstAccessMask=VK_ACCESS_HOST_READ_BIT;m.srcQueueFamilyIndex=m.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&m,0,nullptr);});b.sync(false);}
    void destroy(){if(view)vkDestroyImageView(d.handle,view,nullptr);if(handle)vkDestroyImage(d.handle,handle,nullptr);if(memory)vkFreeMemory(d.handle,memory,nullptr);view=VK_NULL_HANDLE;handle=VK_NULL_HANDLE;memory=VK_NULL_HANDLE;}
    ~Image(){destroy();}
};
}

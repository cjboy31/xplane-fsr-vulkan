// SPDX-License-Identifier: MIT
// Fenced host-staging transport baseline. Does not inject, capture a simulator,
// run NR/FG, share opaque handles across cards, or present generated frames.
#include <vulkan/vulkan.h>
#include "gpu_frame_queue.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
static void need(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
static void check(VkResult r,const char*why){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(why)+": "+std::to_string(r));}
static std::string uuid(VkPhysicalDevice p){VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties2 prop{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};prop.pNext=&id;vkGetPhysicalDeviceProperties2(p,&prop);std::ostringstream out;for(auto b:id.deviceUUID)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return out.str();}
static bool validUuid(const std::string&id){return id.size()==32&&id.find_first_not_of("0123456789abcdef")==std::string::npos;}
struct Device {
    VkDevice handle{};VkQueue queue{};VkCommandPool pool{};VkCommandBuffer commands{};VkFence fence{};
    VkPhysicalDeviceMemoryProperties memory{};
    Device(VkPhysicalDevice physical){try{
        uint32_t n{};vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,families.data());
        uint32_t family=UINT32_MAX;for(uint32_t i=0;i<n;++i)if(families[i].queueCount&&(families[i].queueFlags&(VK_QUEUE_TRANSFER_BIT|VK_QUEUE_COMPUTE_BIT|VK_QUEUE_GRAPHICS_BIT))){family=i;break;}need(family!=UINT32_MAX,"No transfer queue");
        float priority=1;VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueFamilyIndex=family;q.queueCount=1;q.pQueuePriorities=&priority;
        VkDeviceCreateInfo c{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};c.queueCreateInfoCount=1;c.pQueueCreateInfos=&q;check(vkCreateDevice(physical,&c,nullptr,&handle),"Create processing device");vkGetDeviceQueue(handle,family,0,&queue);vkGetPhysicalDeviceMemoryProperties(physical,&memory);
        VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};p.queueFamilyIndex=family;p.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;check(vkCreateCommandPool(handle,&p,nullptr,&pool),"Create command pool");
        VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};a.commandPool=pool;a.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;a.commandBufferCount=1;check(vkAllocateCommandBuffers(handle,&a,&commands),"Allocate transfer command");
        VkFenceCreateInfo f{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(handle,&f,nullptr,&fence),"Create transfer fence");
    }catch(...){destroy();throw;}}
    Device(const Device&)=delete;
    void destroy(){if(handle){vkDeviceWaitIdle(handle);if(fence)vkDestroyFence(handle,fence,nullptr);if(pool)vkDestroyCommandPool(handle,pool,nullptr);vkDestroyDevice(handle,nullptr);handle=VK_NULL_HANDLE;}}
    ~Device(){destroy();}
    uint32_t type(uint32_t bits,bool host){int best=-1,score=-1000;for(uint32_t i=0;i<memory.memoryTypeCount;++i){const auto flags=memory.memoryTypes[i].propertyFlags;
        if(!(bits&(1u<<i))||(host?!(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT):!(flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))continue;
        int s=host?((flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)?2:0)+((flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)?4:0)-((flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)?8:0):0;
        if(s>score){score=s;best=int(i);}}need(best>=0,"Compatible memory type unavailable");return uint32_t(best);}
    void submit(const std::function<void(VkCommandBuffer)>&record){
        check(vkResetFences(handle,1,&fence),"Reset fence");check(vkResetCommandPool(handle,pool,0),"Reset pool");
        VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};b.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(commands,&b),"Begin transfer");record(commands);check(vkEndCommandBuffer(commands),"End transfer");
        VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=1;s.pCommandBuffers=&commands;
        const auto result=vkQueueSubmit(queue,1,&s,fence);if(result!=VK_SUCCESS){vkDeviceWaitIdle(handle);check(result,"Submit transfer");}
        const auto done=vkWaitForFences(handle,1,&fence,VK_TRUE,5000000000ull);if(done!=VK_SUCCESS){vkDeviceWaitIdle(handle);check(done,"Transfer completion");}
    }
};
struct Buffer {
    Device&device;VkBuffer handle{};VkDeviceMemory memory{};void*mapped{};bool coherent{};
    Buffer(Device&d,VkDeviceSize bytes,bool host):device(d){try{
        VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};c.size=bytes;c.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;check(vkCreateBuffer(d.handle,&c,nullptr,&handle),"Create transfer buffer");
        VkMemoryRequirements r{};vkGetBufferMemoryRequirements(d.handle,handle,&r);auto type=d.type(r.memoryTypeBits,host);coherent=(d.memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=type;check(vkAllocateMemory(d.handle,&a,nullptr,&memory),"Allocate transfer buffer");check(vkBindBufferMemory(d.handle,handle,memory,0),"Bind transfer buffer");if(host)check(vkMapMemory(d.handle,memory,0,VK_WHOLE_SIZE,0,&mapped),"Map staging buffer");
    }catch(...){destroy();throw;}}
    Buffer(const Buffer&)=delete;
    void destroy(){if(mapped)vkUnmapMemory(device.handle,memory);if(handle)vkDestroyBuffer(device.handle,handle,nullptr);if(memory)vkFreeMemory(device.handle,memory,nullptr);mapped=nullptr;handle=VK_NULL_HANDLE;memory=VK_NULL_HANDLE;}
    ~Buffer(){destroy();}
    void sync(bool upload){if(coherent)return;VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};r.memory=memory;r.size=VK_WHOLE_SIZE;check(upload?vkFlushMappedMemoryRanges(device.handle,1,&r):vkInvalidateMappedMemoryRanges(device.handle,1,&r),"Synchronize host staging");}
};
static void barrier(VkCommandBuffer cb,VkBuffer buffer,VkAccessFlags src,VkAccessFlags dst,VkPipelineStageFlags from,VkPipelineStageFlags to){VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.buffer=buffer;b.size=VK_WHOLE_SIZE;b.srcAccessMask=src;b.dstAccessMask=dst;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;vkCmdPipelineBarrier(cb,from,to,0,0,nullptr,1,&b,0,nullptr);}
static void roundTrip(Device&d,Buffer&upload,Buffer&local,Buffer&readback,VkDeviceSize bytes,uint32_t marker,bool process){
    upload.sync(true);d.submit([&](VkCommandBuffer cb){VkBufferCopy copy{0,0,bytes};
        barrier(cb,upload.handle,VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        barrier(cb,local.handle,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBuffer(cb,upload.handle,local.handle,1,&copy);
        barrier(cb,local.handle,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        if(process){vkCmdFillBuffer(cb,local.handle,0,4,marker);barrier(cb,local.handle,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);}
        barrier(cb,readback.handle,VK_ACCESS_HOST_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBuffer(cb,local.handle,readback.handle,1,&copy);
        barrier(cb,readback.handle,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT);
    });readback.sync(false);
}
int main(int argc,char**argv){VkInstance instance{};try{
    need(argc==5||argc==6,"MultiGpuTransportCheck RENDER_UUID PROCESSING_UUID BYTES FRAMES [--same-physical-fixture]");std::string renderId=argv[1],processId=argv[2];need(validUuid(renderId)&&validUuid(processId),"Select GPUs with their 32-character device UUIDs");
    size_t used{};auto bytes=std::stoull(argv[3],&used);need(used==std::strlen(argv[3])&&bytes>=4&&bytes<=256ull*1024*1024&&bytes%4==0,"Payload must be 4-byte aligned and at most 256 MiB");auto frames=std::stoul(argv[4],&used);need(used==std::strlen(argv[4])&&frames>=2&&frames<=120,"Frames must be 2..120");
    const bool fixture=argc==6&&std::string(argv[5])=="--same-physical-fixture";need(argc!=6||fixture,"Unknown option");need(renderId!=processId||fixture,"Two distinct physical GPUs required; same-device fixtures must be explicit");
    VkApplicationInfo a{VK_STRUCTURE_TYPE_APPLICATION_INFO};a.pApplicationName="XPFSR experimental staging transfer check";a.apiVersion=VK_API_VERSION_1_1;VkInstanceCreateInfo c{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};c.pApplicationInfo=&a;check(vkCreateInstance(&c,nullptr,&instance),"Create instance");
    uint32_t count{};check(vkEnumeratePhysicalDevices(instance,&count,nullptr),"Enumerate devices");std::vector<VkPhysicalDevice> adapters(count);check(vkEnumeratePhysicalDevices(instance,&count,adapters.data()),"Read devices");VkPhysicalDevice render{},processing{};for(auto p:adapters){auto id=uuid(p);if(id==renderId)render=p;if(id==processId)processing=p;}need(render&&processing,"Selected GPU disappeared; do not substitute an enumeration index");
    std::vector<double> samples;uint64_t checked{};
    {Device first(render),second(processing);need(first.handle!=second.handle,"Logical devices were not independent");
        Buffer aUpload(first,bytes,true),aLocal(first,bytes,false),aRead(first,bytes,true),bUpload(second,bytes,true),bLocal(second,bytes,false),bRead(second,bytes,true);
        xpfsr::gpu::FrameQueue<2> framesInFlight(bytes);std::vector<uint32_t> expected(bytes/4);for(uint32_t frame=1;frame<=frames;++frame){for(size_t i=0;i<expected.size();++i)expected[i]=uint32_t(i*1664525u+frame*1013904223u);
            xpfsr::gpu::FrameKey key{frame,bytes,0};auto token=framesInFlight.reserve(key);need(bool(token),"No bounded transport slot");
            std::memcpy(aUpload.mapped,expected.data(),bytes);auto start=std::chrono::steady_clock::now();
            roundTrip(first,aUpload,aLocal,aRead,bytes,0,false);need(framesInFlight.captureComplete(*token),"Capture became stale");std::memcpy(bUpload.mapped,aRead.mapped,bytes);
            const uint32_t marker=0x3f400000u+frame;roundTrip(second,bUpload,bLocal,bRead,bytes,marker,true);std::memcpy(aUpload.mapped,bRead.mapped,bytes);
            roundTrip(first,aUpload,aLocal,aRead,bytes,0,false);need(framesInFlight.processComplete(*token),"Processed frame became stale");auto ready=framesInFlight.take(key);need(bool(ready),"Matching completed frame unavailable; retain original instead");auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(frame>2)samples.push_back(elapsed);
            expected[0]=marker;need(!std::memcmp(aRead.mapped,expected.data(),bytes),"Returned payload or processing-GPU frame marker mismatched");checked+=bytes;need(framesInFlight.consumeComplete(*ready),"Transport output was still in use");
        }
    }
    if(samples.empty())samples.push_back(0);std::sort(samples.begin(),samples.end());
    std::cout<<"{\"result\":\"PASS\",\"transport\":\"CPU host staging\",\"distinctPhysicalDevices\":"<<(renderId!=processId?"true":"false")<<",\"twoLogicalDevices\":true,\"payloadBytes\":"<<bytes<<",\"frames\":"<<frames<<",\"verifiedBytes\":"<<checked<<",\"medianFixtureRoundTripMs\":"<<samples[samples.size()/2]<<",\"p95FixtureRoundTripMs\":"<<samples[std::min(samples.size()-1,size_t(samples.size()*.95))]<<",\"scope\":\"fenced buffer transport and GPU marker only; includes initial upload and final validation readback; no NR, FG, images, simulator injection, async pacing or speedup claim\"}\n";
    vkDestroyInstance(instance,nullptr);return 0;
}catch(const std::exception&e){if(instance)vkDestroyInstance(instance,nullptr);std::cerr<<e.what()<<'\n';return 1;}}

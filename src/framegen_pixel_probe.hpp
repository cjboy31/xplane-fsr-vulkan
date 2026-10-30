// SPDX-License-Identifier: MIT
#pragma once
#include "fsr24.hpp"
#include <ffx_framegeneration.h>
#include <memory>

namespace xpfsr {
// Opt-in diagnosis of the SDK's interpolation surface, before composition.
// No full images, files, CPU/GPU waits or swapchain ownership changes. Timestamp
// availability establishes completion; unchanged results from a reused query
// are ignored until the new GPU timestamp has actually completed.
class FramePixelProbe {
    static constexpr uint32_t columns = 16, rows = 9, samples = columns * rows;
    static constexpr VkDeviceSize imageBytes = samples * 4, bytes = imageBytes * 2;
    struct Slot {
        VkBuffer buffer{};
        VkDeviceMemory memory{};
        void *mapped{};
        uint64_t frame{}, previousTimestamp{};
        bool pending{};
    };
    VkDeviceContext device_{};
    VkQueryPool queries_{};
    std::array<Slot, 4> slots_{};
    uint32_t cursor_{};
    template<class T> T fn(const char *name) const {
        return reinterpret_cast<T>(device_.vkDeviceProcAddr(device_.vkDevice, name));
    }
    static bool supported(const FfxApiResource &r) {
        const auto f = r.description.format;
        return r.resource && r.description.width >= columns && r.description.height >= rows &&
            (f == FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM ||
             f == FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB ||
             f == FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM ||
             f == FFX_API_SURFACE_FORMAT_B8G8R8A8_SRGB) &&
            (r.state == FFX_API_RESOURCE_STATE_UNORDERED_ACCESS ||
             r.state == FFX_API_RESOURCE_STATE_COMPUTE_READ ||
             r.state == FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    }
    void cleanup() {
        auto d = device_.vkDevice;
        for (auto &s : slots_) {
            if(s.mapped) fn<PFN_vkUnmapMemory>("vkUnmapMemory")(d,s.memory);
            if(s.buffer) fn<PFN_vkDestroyBuffer>("vkDestroyBuffer")(d,s.buffer,nullptr);
            if(s.memory) fn<PFN_vkFreeMemory>("vkFreeMemory")(d,s.memory,nullptr);
            s={};
        }
        if(queries_) fn<PFN_vkDestroyQueryPool>("vkDestroyQueryPool")(d,queries_,nullptr);
        queries_={};
    }
public:
    struct Stats {
        uint64_t checked{}, generatedBlank{}, bothDark{}, busy{}, unsupported{}, errors{}, lastBlankFrame{};
        double realMean{}, generatedMean{};
    } stats;
    explicit FramePixelProbe(VkDeviceContext device, const VkPhysicalDeviceMemoryProperties &memory)
        : device_(device) {
        try {
            VkQueryPoolCreateInfo q{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            q.queryType=VK_QUERY_TYPE_TIMESTAMP;q.queryCount=uint32_t(slots_.size());
            require(fn<PFN_vkCreateQueryPool>("vkCreateQueryPool")(device_.vkDevice,&q,nullptr,&queries_)==VK_SUCCESS,"FG pixel-probe queries");
            for(auto &s:slots_){
                VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=bytes;b.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                require(fn<PFN_vkCreateBuffer>("vkCreateBuffer")(device_.vkDevice,&b,nullptr,&s.buffer)==VK_SUCCESS,"FG pixel-probe buffer");
                VkMemoryRequirements r{};fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(device_.vkDevice,s.buffer,&r);
                uint32_t index=memory.memoryTypeCount;
                for(uint32_t i=0;i<memory.memoryTypeCount;++i)
                    if((r.memoryTypeBits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){index=i;break;}
                require(index<memory.memoryTypeCount,"FG pixel probe needs coherent host memory");
                VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=index;
                require(fn<PFN_vkAllocateMemory>("vkAllocateMemory")(device_.vkDevice,&a,nullptr,&s.memory)==VK_SUCCESS,"FG pixel-probe memory");
                require(fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(device_.vkDevice,s.buffer,s.memory,0)==VK_SUCCESS,"FG pixel-probe bind");
                require(fn<PFN_vkMapMemory>("vkMapMemory")(device_.vkDevice,s.memory,0,bytes,0,&s.mapped)==VK_SUCCESS,"FG pixel-probe map");
            }
        } catch(...) {cleanup();throw;}
    }
    FramePixelProbe(const FramePixelProbe&)=delete;
    FramePixelProbe&operator=(const FramePixelProbe&)=delete;
    // Caller drains SDK presentation and GPU work before destroying this object,
    // exactly as required for the FG context and its other resources.
    ~FramePixelProbe(){cleanup();}
    void poll(){
        for(uint32_t i=0;i<slots_.size();++i){
            auto &s=slots_[i];if(!s.pending)continue;
            uint64_t result[2]{};
            auto r=fn<PFN_vkGetQueryPoolResults>("vkGetQueryPoolResults")(device_.vkDevice,queries_,i,1,sizeof(result),result,sizeof(result),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            if(r==VK_NOT_READY || (r==VK_SUCCESS && (!result[1]||result[0]==s.previousTimestamp)))continue;
            if(r!=VK_SUCCESS){++stats.errors;continue;}
            s.previousTimestamp=result[0];s.pending=false;
            auto *p=static_cast<const uint8_t*>(s.mapped);
            double means[2]{};uint32_t dark[2]{},visible[2]{};
            for(unsigned image=0;image<2;++image)
                for(uint32_t n=0;n<samples;++n){
                    auto *pixel=p+image*imageBytes+n*4;unsigned sum=unsigned(pixel[0])+pixel[1]+pixel[2];
                    means[image]+=sum/(3.0*255*samples);dark[image]+=sum<=3;visible[image]+=sum>24;
                }
            ++stats.checked;stats.realMean=means[0];stats.generatedMean=means[1];
            if(dark[1]*100>=samples*98 && means[1]<.002){
                if(means[0]>.04 && visible[0]*4>=samples){++stats.generatedBlank;stats.lastBlankFrame=s.frame;}
                else ++stats.bothDark;
            }
        }
    }
    void record(const ffxDispatchDescFrameGeneration &p){
        poll();
        if(!p.numGeneratedFrames)return;
        const FfxApiResource resources[2]={p.presentColor,p.outputs[0]};
        if(!supported(resources[0])||!supported(resources[1])){++stats.unsupported;return;}
        auto &s=slots_[cursor_];if(s.pending){++stats.busy;return;}
        const uint32_t query=cursor_;cursor_=(cursor_+1)%slots_.size();
        auto cb=static_cast<VkCommandBuffer>(p.commandList);
        fn<PFN_vkCmdResetQueryPool>("vkCmdResetQueryPool")(cb,queries_,query,1);
        std::array<VkImageMemoryBarrier,2> barriers{};
        for(unsigned i=0;i<2;++i){
            auto &b=barriers[i];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.image=reinterpret_cast<VkImage>(resources[i].resource);
            b.oldLayout=resources[i].state==FFX_API_RESOURCE_STATE_UNORDERED_ACCESS?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        }
        auto barrier=fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");
        barrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,barriers.data());
        for(unsigned image=0;image<2;++image){
            std::array<VkBufferImageCopy,samples> regions{};
            for(uint32_t y=0;y<rows;++y)for(uint32_t x=0;x<columns;++x){
                auto &r=regions[y*columns+x];r.bufferOffset=image*imageBytes+(y*columns+x)*4;
                r.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
                r.imageOffset={int32_t((2*x+1)*resources[image].description.width/(2*columns)),int32_t((2*y+1)*resources[image].description.height/(2*rows)),0};r.imageExtent={1,1,1};
            }
            fn<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(cb,barriers[image].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,s.buffer,samples,regions.data());
        }
        for(auto &b:barriers){auto original=b.oldLayout;b.oldLayout=b.newLayout;b.newLayout=original;b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;}
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,2,barriers.data());
        fn<PFN_vkCmdWriteTimestamp>("vkCmdWriteTimestamp")(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries_,query);
        s.frame=p.frameID;s.pending=true;
    }
};
} // namespace xpfsr

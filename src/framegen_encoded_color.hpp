// SPDX-License-Identifier: MIT
#pragma once
#include "fsr24.hpp"
#include <ffx_framegeneration.h>
namespace xpfsr {
inline VkFormat encodedColorFormat(VkFormat f){
    return f==VK_FORMAT_B8G8R8A8_SRGB?VK_FORMAT_B8G8R8A8_UNORM:
           f==VK_FORMAT_R8G8B8A8_SRGB?VK_FORMAT_R8G8B8A8_UNORM:f;
}
// FG's backbuffer transfer-function parameter expects encoded SDR texels.
// Sampling an sRGB view decodes them before the algorithm sees them; the
// writable output is UNORM and the presenter's no-UI copy cannot encode it
// again. Copy through private UNORM images, preserving the original encoded
// bytes, rather than reinterpreting an image view without MUTABLE_FORMAT.
// These copies and interpolation all execute on the same SDK command buffer /
// interpolation queue, so subsequent callbacks cannot overlap their accesses.
class FrameEncodedColor {
    VkDeviceContext device_{};
    Image input_{},output_{};
    VkDeviceMemory inputMemory_{},outputMemory_{};
    bool initialized_{};
    template<class T>T fn(const char*name){return reinterpret_cast<T>(device_.vkDeviceProcAddr(device_.vkDevice,name));}
    void create(Image &image,VkDeviceMemory &memory,VkFormat format,uint32_t w,uint32_t h,const VkPhysicalDeviceMemoryProperties &properties){
        auto &c=image.description;c.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;c.imageType=VK_IMAGE_TYPE_2D;c.format=format;c.extent={w,h,1};c.mipLevels=c.arrayLayers=1;c.samples=VK_SAMPLE_COUNT_1_BIT;c.tiling=VK_IMAGE_TILING_OPTIMAL;
        c.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT;
        require(fn<PFN_vkCreateImage>("vkCreateImage")(device_.vkDevice,&c,nullptr,&image.handle)==VK_SUCCESS,"FG encoded-color image");
        VkMemoryRequirements r{};fn<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(device_.vkDevice,image.handle,&r);
        uint32_t index=properties.memoryTypeCount;for(uint32_t i=0;i<properties.memoryTypeCount;++i)
            if((r.memoryTypeBits&(1u<<i)) && (properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){index=i;break;}
        require(index<properties.memoryTypeCount,"FG encoded-color device memory unavailable");
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=index;
        require(fn<PFN_vkAllocateMemory>("vkAllocateMemory")(device_.vkDevice,&a,nullptr,&memory)==VK_SUCCESS,"FG encoded-color allocation");
        require(fn<PFN_vkBindImageMemory>("vkBindImageMemory")(device_.vkDevice,image.handle,memory,0)==VK_SUCCESS,"FG encoded-color bind");
    }
    void cleanup(){
        for(auto *image:{&input_,&output_})if(image->handle)fn<PFN_vkDestroyImage>("vkDestroyImage")(device_.vkDevice,image->handle,nullptr);
        for(auto memory:{inputMemory_,outputMemory_})if(memory)fn<PFN_vkFreeMemory>("vkFreeMemory")(device_.vkDevice,memory,nullptr);
        input_={};output_={};inputMemory_=outputMemory_={};
    }
    static VkImageLayout layout(const FfxApiResource &r){return r.state==FFX_API_RESOURCE_STATE_UNORDERED_ACCESS?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;}
    void transition(VkCommandBuffer cb,VkImage image,VkImageLayout from,VkImageLayout to){
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=from;b.newLayout=to;
        b.srcAccessMask=from==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
    }
    void copy(VkCommandBuffer cb,VkImage source,VkImage destination){
        VkImageCopy c{};c.srcSubresource=c.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.extent=input_.description.extent;
        fn<PFN_vkCmdCopyImage>("vkCmdCopyImage")(cb,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,destination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&c);
    }
public:
    FrameEncodedColor(VkDeviceContext device,VkFormat format,uint32_t w,uint32_t h,const VkPhysicalDeviceMemoryProperties &memory):device_(device){
        try {create(input_,inputMemory_,format,w,h,memory);create(output_,outputMemory_,format,w,h,memory);}
        catch(...){cleanup();throw;}
    }
    FrameEncodedColor(const FrameEncodedColor&)=delete;FrameEncodedColor&operator=(const FrameEncodedColor&)=delete;
    ~FrameEncodedColor(){cleanup();}
    void before(const ffxDispatchDescFrameGeneration &original,ffxDispatchDescFrameGeneration &dispatch){
        const auto &s=original.presentColor.description;const auto &o=original.outputs[0].description;
        require(original.numGeneratedFrames==1 && original.presentColor.resource && original.outputs[0].resource &&
            original.backbufferTransferFunction==FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB && s.format==o.format &&
            s.width==input_.description.extent.width && s.height==input_.description.extent.height && o.width==s.width && o.height==s.height &&
            (s.format==FFX_API_SURFACE_FORMAT_B8G8R8A8_SRGB || s.format==FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB) &&
            (original.presentColor.state==FFX_API_RESOURCE_STATE_COMPUTE_READ || original.presentColor.state==FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ) &&
            original.outputs[0].state==FFX_API_RESOURCE_STATE_UNORDERED_ACCESS,"Unsupported FG encoded-color callback resources");
        auto cb=static_cast<VkCommandBuffer>(original.commandList);auto real=reinterpret_cast<VkImage>(original.presentColor.resource);
        transition(cb,real,layout(original.presentColor),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        transition(cb,input_.handle,initialized_?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copy(cb,real,input_.handle);
        transition(cb,real,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,layout(original.presentColor));
        transition(cb,input_.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        transition(cb,output_.handle,initialized_?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
        input_.layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;output_.layout=VK_IMAGE_LAYOUT_GENERAL;
        dispatch.presentColor=resource(input_);dispatch.outputs[0]=resource(output_);initialized_=true;
    }
    void after(const ffxDispatchDescFrameGeneration &original){
        auto cb=static_cast<VkCommandBuffer>(original.commandList);auto destination=reinterpret_cast<VkImage>(original.outputs[0].resource);
        transition(cb,output_.handle,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        transition(cb,destination,layout(original.outputs[0]),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copy(cb,output_.handle,destination);
        transition(cb,output_.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
        transition(cb,destination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,layout(original.outputs[0]));
    }
};
} // namespace xpfsr

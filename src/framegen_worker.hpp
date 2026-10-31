// SPDX-License-Identifier: MIT
// Compute-only FG. Presentation and cross-device pacing remain the caller's job.
#pragma once
#include "gpu_worker_vk.hpp"
#include "fsr24.hpp"
#include <ffx_framegeneration.h>
#include <vk/ffx_api_framegeneration_vk.h>
namespace xpfsr::gpu {
inline xpfsr::Image fsrImage(const worker::Image&i){
    xpfsr::Image v;v.handle=i.handle;v.layout=i.layout;auto&c=v.description;
    c.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;c.imageType=VK_IMAGE_TYPE_2D;c.format=i.format;c.extent={i.width,i.height,1};c.mipLevels=c.arrayLayers=1;c.samples=VK_SAMPLE_COUNT_1_BIT;c.tiling=VK_IMAGE_TILING_OPTIMAL;c.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;return v;
}
class ComputeFrameGenerator {
    worker::Device&device;Api api;ffxContext context{};uint32_t width,height;uint64_t previous{};
public:
    ComputeFrameGenerator(worker::Device&d,const Api&a,uint32_t w,uint32_t h):device(d),api(a),width(w),height(h){
        require(api.configure&&w&&h,"Invalid FG worker initialization");
        ffxCreateContextDescFrameGeneration c{};c.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;c.displaySize=c.maxRenderSize={w,h};c.backBufferFormat=FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;c.flags=FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;
        ffxCreateContextDescFrameGenerationVersion v{};v.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;v.version=FFX_FRAMEGENERATION_VERSION;
        ffxCreateBackendVKDesc backend{};backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;backend.deviceContext={d.handle,d.physical,vkGetDeviceProcAddr};
        ffxOverrideVersion provider{};provider.header.type=FFX_API_DESC_TYPE_OVERRIDE_VERSION;provider.versionId=providerVersion(api,backend.deviceContext,"3.1.6",FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION);
        c.header.pNext=&v.header;v.header.pNext=&backend.header;backend.header.pNext=&provider.header;
        xpfsr::check(api.create(&context,&c.header,nullptr),"Create compute-only FG worker");
    }
    ComputeFrameGenerator(const ComputeFrameGenerator&)=delete;
    uint32_t record(VkCommandBuffer cb,uint64_t frame,worker::Image&color,worker::Image&depth,worker::Image&motion,worker::Image&output,bool cameraCut=false){
        require(frame && cb && &color.d==&device && &depth.d==&device && &motion.d==&device && &output.d==&device,"FG worker received foreign device resources");
        for(auto*i:{&color,&depth,&motion,&output})require(i->width==width&&i->height==height,"FG worker source extent mismatch");
        require(color.format==VK_FORMAT_R16G16B16A16_SFLOAT&&output.format==color.format&&depth.format==VK_FORMAT_R32_SFLOAT&&motion.format==VK_FORMAT_R16G16_SFLOAT,"FG worker format mismatch");
        const bool reset=cameraCut||!previous||frame!=previous+1;
        ffxConfigureDescFrameGeneration config{};config.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;config.frameGenerationEnabled=true;config.flags=FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;config.frameID=frame;config.generationRect={0,0,int32_t(width),int32_t(height)};
        xpfsr::check(api.configure(&context,&config.header),"Configure compute-only FG");
        color.transition(cb,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);depth.transition(cb,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);motion.transition(cb,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);output.transition(cb,VK_IMAGE_LAYOUT_GENERAL);
        ffxDispatchDescFrameGenerationPrepareV2 prepare{};prepare.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;prepare.commandList=reinterpret_cast<void*>(cb);prepare.frameID=frame;prepare.renderSize={width,height};prepare.depth=resource(fsrImage(depth));prepare.motionVectors=resource(fsrImage(motion));prepare.motionVectorScale={1,1};prepare.frameTimeDelta=16.66667f;prepare.reset=reset;prepare.cameraNear=.1f;prepare.cameraFar=1000;prepare.cameraFovAngleVertical=1.0f;prepare.viewSpaceToMetersFactor=1;prepare.cameraUp[1]=1;prepare.cameraRight[0]=1;prepare.cameraForward[2]=-1;
        xpfsr::check(api.dispatch(&context,&prepare.header),"Prepare compute-only FG");
        ffxDispatchDescFrameGeneration generate{};generate.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;generate.commandList=reinterpret_cast<void*>(cb);generate.presentColor=resource(fsrImage(color));generate.outputs[0]=resource(fsrImage(output),true);generate.numGeneratedFrames=1;generate.reset=reset;generate.backbufferTransferFunction=FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SCRGB;generate.minMaxLuminance[1]=1000;generate.generationRect=config.generationRect;generate.frameID=frame;
        xpfsr::check(api.dispatch(&context,&generate.header),"Dispatch compute-only FG");require(generate.numGeneratedFrames<=1,"FG worker returned an unsupported output count");previous=frame;return generate.numGeneratedFrames;
    }
    ~ComputeFrameGenerator(){if(context){vkDeviceWaitIdle(device.handle);api.destroy(&context,nullptr);}}
};
}

// SPDX-License-Identifier: MIT
// Exercises the production image/cache/depth-view code on a real fenced queue.
// This is a resource-lifetime fixture, not a simulator or FSR quality benchmark.
#include "layer.cpp"
#define wmain unusedStandaloneMain
#include "probe_support.hpp"
#undef wmain
static PFN_vkGetDeviceProcAddr fixtureNativeDpa{};
static bool fixtureFailAllocation{};
static VKAPI_ATTR VkResult VKAPI_CALL fixtureAllocate(VkDevice device,const VkMemoryAllocateInfo*info,const VkAllocationCallbacks*callbacks,VkDeviceMemory*out){
    if(fixtureFailAllocation){fixtureFailAllocation=false;return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    return reinterpret_cast<PFN_vkAllocateMemory>(fixtureNativeDpa(device,"vkAllocateMemory"))(device,info,callbacks,out);
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fixtureDpa(VkDevice device,const char*name){return !strcmp(name,"vkAllocateMemory")?reinterpret_cast<PFN_vkVoidFunction>(&fixtureAllocate):fixtureNativeDpa(device,name);}
struct ReuseFixture {
    Vulkan vk;std::unique_ptr<Device> device;
    std::unique_ptr<FrameResources> frame;std::shared_ptr<DepthCapture> depth;
    VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};
    VkBuffer readback{};VkDeviceMemory readbackMemory{};void *mapped{};
    VkImageView nativeView{};
    ~ReuseFixture(){
        if(!vk.device)return;vk.waitIdle(vk.device);frame.reset();
        if(nativeView)vk.dev<PFN_vkDestroyImageView>("vkDestroyImageView")(vk.device,nativeView,nullptr);depth.reset();
        if(device){device->idleFrames.clear();device->idleDepths.clear();
            if(device->packPipeline)vk.dev<PFN_vkDestroyPipeline>("vkDestroyPipeline")(vk.device,device->packPipeline,nullptr);
            if(device->packMsaaPipeline)vk.dev<PFN_vkDestroyPipeline>("vkDestroyPipeline")(vk.device,device->packMsaaPipeline,nullptr);
            if(device->packPipelineLayout)vk.dev<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(vk.device,device->packPipelineLayout,nullptr);
            if(device->packLayout)vk.dev<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(vk.device,device->packLayout,nullptr);
            if(device->sampler)vk.dev<PFN_vkDestroySampler>("vkDestroySampler")(vk.device,device->sampler,nullptr);device.reset();}
        if(mapped)vk.dev<PFN_vkUnmapMemory>("vkUnmapMemory")(vk.device,readbackMemory);
        if(readback)vk.dev<PFN_vkDestroyBuffer>("vkDestroyBuffer")(vk.device,readback,nullptr);
        if(readbackMemory)vk.dev<PFN_vkFreeMemory>("vkFreeMemory")(vk.device,readbackMemory,nullptr);
        if(fence)vk.dev<PFN_vkDestroyFence>("vkDestroyFence")(vk.device,fence,nullptr);
        if(pool)vk.dev<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(vk.device,pool,nullptr);
    }
};
int wmain(){try{
    ReuseFixture f;f.vk.open();auto &vk=f.vk;
    f.device=std::make_unique<Device>();auto &d=*f.device;d.handle=vk.device;d.physical=vk.physical;fixtureNativeDpa=vk.gdpa;d.next=fixtureDpa;d.memory=vk.memory;d.graphicsFamily=vk.family;
    d.instance=std::make_shared<Instance>();d.instance->handle=vk.instance;d.instance->next=vk.gipa;setupPack(d);
    xpfsr::require(applicationPhysical(d)==vk.physical&&applicationPhysical(d)==vk.physical,"Loader-visible physical UUID mapping/caching failed");
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pool.queueFamilyIndex=vk.family;
    check(vk.dev<PFN_vkCreateCommandPool>("vkCreateCommandPool")(vk.device,&pool,nullptr,&f.pool),"Fixture pool");
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};alloc.commandPool=f.pool;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;alloc.commandBufferCount=1;
    check(vk.dev<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(vk.device,&alloc,&f.command),"Fixture command");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vk.dev<PFN_vkCreateFence>("vkCreateFence")(vk.device,&fence,nullptr,&f.fence),"Fixture fence");
    constexpr uint32_t width=64,height=32;constexpr uint64_t bytes=width*height*4;
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=bytes;buffer.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(vk.dev<PFN_vkCreateBuffer>("vkCreateBuffer")(vk.device,&buffer,nullptr,&f.readback),"Readback buffer");
    VkMemoryRequirements requirements{};vk.dev<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(vk.device,f.readback,&requirements);
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=requirements.size;memory.memoryTypeIndex=vk.memoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vk.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(vk.device,&memory,nullptr,&f.readbackMemory),"Readback allocation");
    check(vk.dev<PFN_vkBindBufferMemory>("vkBindBufferMemory")(vk.device,f.readback,f.readbackMemory,0),"Readback binding");
    check(vk.dev<PFN_vkMapMemory>("vkMapMemory")(vk.device,f.readbackMemory,0,bytes,0,&f.mapped),"Readback map");
    ImageInfo source{};source.generation=1;source.create.extent={width,height,1};source.create.format=VK_FORMAT_D32_SFLOAT;source.create.samples=VK_SAMPLE_COUNT_1_BIT;
    auto frameKey=frameReuseKey(vk.family,width,height,width,height,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM);
    auto depthKey=depthReuseKey(vk.family,width,height,VK_FORMAT_D32_SFLOAT,VK_SAMPLE_COUNT_1_BIT);
    VkImage firstColor{},firstDepth{};
    for(int iteration=0;iteration<64;++iteration){
        f.depth=d.idleDepths.take(depthKey);
        if(!f.depth){f.depth=std::make_shared<DepthCapture>();f.depth->create(d,source,vk.family);}
        f.depth->generation=iteration+1;f.depth->layout=VK_IMAGE_LAYOUT_UNDEFINED;
        ViewInfo view{};view.create=f.depth->view;view.create.image=f.depth->image;
        f.frame=d.idleFrames.take(frameKey);
        if(!f.frame){f.frame=std::make_unique<FrameResources>();setupFrame(d,*f.frame,width,height,width,height,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM,view,f.depth,vk.family);}
        else{discardFrameContents(*f.frame);bindFrameDepth(d,*f.frame,view,f.depth);}
        if(!iteration){firstColor=f.frame->color.value.handle;firstDepth=f.depth->image;}
        else require(firstColor==f.frame->color.value.handle&&firstDepth==f.depth->image,"Production resources were recreated on a compatible reuse");
        check(vk.dev<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(f.command,0),"Command reset");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vk.dev<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(f.command,&begin),"Command begin");
        transition(d,f.command,f.frame->color,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkClearColorValue color{};color.float32[0]=float(iteration%2);color.float32[3]=1;
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vk.dev<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(f.command,f.frame->color.value.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
        transition(d,f.command,f.frame->color,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};
        vk.dev<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(f.command,f.frame->color.value.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,f.readback,1,&copy);
        check(vk.dev<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(f.command),"Command end");
        check(vk.dev<PFN_vkResetFences>("vkResetFences")(vk.device,1,&f.fence),"Fence reset");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&f.command;
        check(vk.dev<PFN_vkQueueSubmit>("vkQueueSubmit")(vk.queue,1,&submit,f.fence),"Queue submit");
        check(vk.dev<PFN_vkWaitForFences>("vkWaitForFences")(vk.device,1,&f.fence,VK_TRUE,10000000000ull),"Fixture completion");
        check(vk.dev<PFN_vkGetFenceStatus>("vkGetFenceStatus")(vk.device,f.fence),"Completion proof");
        auto *pixels=static_cast<const uint8_t*>(f.mapped);for(size_t i=0;i<bytes;i+=4)require(pixels[i]==(iteration%2?255:0)&&pixels[i+1]==0&&pixels[i+2]==0&&pixels[i+3]==255,"Reused GPU image returned stale or corrupted data");
        retireFrame(d,f.frame);retireDepth(d,f.depth);
    }
    require(d.frameSetupCost.calls==1&&d.depthSetupCost.calls==1&&d.idleFrames.hits==63&&d.idleDepths.hits==63,"Production creation/reuse counters disagree");
    // Force a real allocation API failure before capture records GPU commands.
    // The original depth remains valid and no null map entry reaches selection.
    source.create.extent.height=64;f.depth=std::make_shared<DepthCapture>();f.depth->create(d,source,vk.family);
    check(vk.dev<PFN_vkCreateImageView>("vkCreateImageView")(vk.device,&f.depth->view,nullptr,&f.nativeView),"Native OOM fixture view");
    d.images[f.depth->image]=source;d.views[f.nativeView]={f.depth->view};
    Command fault;fault.family=vk.family;fault.activeCaptureEligible=true;fault.activeDepth=f.depth->image;fault.depths.emplace(f.depth->image,Depth{f.nativeView,f.depth->image,VK_IMAGE_LAYOUT_UNDEFINED,1});
    d.mode=Mode::Upscale;fixtureFailAllocation=true;captureDepth(d,f.command,fault);
    require(!fixtureFailAllocation&&fault.capturedDepths.empty()&&d.allocationPaused&&!d.runtimeFailed,"Pre-recording OOM poisoned capture selection instead of native bypass");
    VkMemoryRequirements originalRequirements{};vk.dev<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(vk.device,f.depth->image,&originalRequirements);require(originalRequirements.size>0,"OOM recovery destroyed the original depth image");
    d.idleFrames.limit(0);d.idleDepths.limit(0);require(!d.idleFrames.bytes()&&!d.idleDepths.bytes(),"Idle memory did not retire under pressure");
    std::cout<<"PASS: 64 actual fenced GPU readbacks, production frame/depth reuse, descriptor rebinding, discarded old contents, one allocation set, pressure trim and simulated pre-recording OOM/native-depth preservation. Resource fixture only; no FSR quality claim.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

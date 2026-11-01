// SPDX-License-Identifier: MIT
// Exercises the production image/cache/depth-view code on a real fenced queue.
// This is a resource-lifetime fixture, not a simulator or FSR quality benchmark.
#include "layer.cpp"
#define wmain unusedStandaloneMain
#include "probe_support.hpp"
#undef wmain
#include <future>
static VKAPI_ATTR VkResult VKAPI_CALL routingAcquire(VkDevice,VkSwapchainKHR,uint64_t,VkSemaphore,VkFence,uint32_t *index){*index=7;return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL routingAcquire2(VkDevice,const VkAcquireNextImageInfoKHR*,uint32_t *index){*index=8;return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL routingImages(VkDevice,VkSwapchainKHR,uint32_t *count,VkImage*){*count=3;return VK_SUCCESS;}
static std::atomic<unsigned> sdkRoutingCalls{};
static void verifyFileControls(){
    auto path=std::filesystem::temp_directory_path()/(L"FSR-control-fixture-"+std::to_wstring(GetCurrentProcessId()));
    auto previous=app::environment(L"XPFSR24_CONTROL");
    struct Restore{std::filesystem::path path;std::wstring previous;~Restore(){SetEnvironmentVariableW(L"XPFSR24_CONTROL",previous.empty()?nullptr:previous.c_str());std::error_code error;std::filesystem::remove(path,error);}}restore{path,previous};
    {std::ofstream(path)<<'1';}
    SetEnvironmentVariableW(L"XPFSR24_CONTROL",path.c_str());
    Device d;d.files.start();app::Shared shared{};d.shared=&shared;
    d.readControl();require(d.enabled,"Initial control file was not applied");
    // A newer shared live switch must not be undone by an unchanged cached file.
    d.enabled=false;shared.enabled=0;d.lastControlRead=0;d.readControl();
    require(!d.enabled&&!shared.enabled,"Cached control overrode a newer live Off switch");
    auto deadline=GetTickCount64()+3000;
    {std::ofstream(path)<<'0';}
    while(d.files.control(0)!=0&&GetTickCount64()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(d.files.control(0)==0,"Changed file control did not refresh");
    d.lastControlRead=0;d.readControl();
    d.enabled=true;shared.enabled=1;d.lastControlRead=0;d.readControl();
    require(d.enabled&&shared.enabled,"Cached control overrode a newer live On switch");
    d.files.stop();d.shared=nullptr;
    std::cout<<"PASS: production Device applies initial/changed file controls once; newer live IPC On/Off is retained.\n";
}
static VKAPI_ATTR void VKAPI_CALL routingBarrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t count,const VkImageMemoryBarrier *images){require(count==1&&images[0].oldLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,"SDK barrier layout was rewritten");++sdkRoutingCalls;}
static VKAPI_ATTR void VKAPI_CALL routingBarrier2(VkCommandBuffer,const VkDependencyInfo *info){require(info->imageMemoryBarrierCount==1&&info->pImageMemoryBarriers[0].oldLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,"SDK barrier2 layout was rewritten");++sdkRoutingCalls;}
static VKAPI_ATTR VkResult VKAPI_CALL routingGraphics(VkDevice,VkPipelineCache,uint32_t,const VkGraphicsPipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline*){++sdkRoutingCalls;return VK_SUCCESS;}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL routingDpa(VkDevice,const char *name){
    if(!strcmp(name,"vkAcquireNextImageKHR"))return reinterpret_cast<PFN_vkVoidFunction>(routingAcquire);
    if(!strcmp(name,"vkAcquireNextImage2KHR"))return reinterpret_cast<PFN_vkVoidFunction>(routingAcquire2);
    if(!strcmp(name,"vkGetSwapchainImagesKHR"))return reinterpret_cast<PFN_vkVoidFunction>(routingImages);
    if(!strcmp(name,"vkCmdPipelineBarrier"))return reinterpret_cast<PFN_vkVoidFunction>(routingBarrier);
    if(!strcmp(name,"vkCmdPipelineBarrier2"))return reinterpret_cast<PFN_vkVoidFunction>(routingBarrier2);
    if(!strcmp(name,"vkCreateGraphicsPipelines"))return reinterpret_cast<PFN_vkVoidFunction>(routingGraphics);
    return nullptr;
}
static void verifyPresenterRouting(){
    void *dispatch=&dispatch;auto handle=reinterpret_cast<VkDevice>(&dispatch);
    auto state=std::make_shared<Device>();state->handle=handle;state->next=routingDpa;
    {std::lock_guard<std::mutex> registry(registryLock);devices[dispatch]=state;}
    struct Cleanup{void *key;~Cleanup(){std::lock_guard<std::mutex> registry(registryLock);devices.erase(key);}}cleanup{dispatch};
    auto native=reinterpret_cast<VkSwapchainKHR>(uintptr_t(0x123)),proxy=reinterpret_cast<VkSwapchainKHR>(uintptr_t(0x456));
    auto generation=std::make_shared<SwapchainGeneration>();state->fgSwapchains[proxy]=generation;
    require(state->findFGSwapchain(proxy)==generation,"App-facing swapchain ownership lost");
    auto image=reinterpret_cast<VkImage>(uintptr_t(0x789));state->fgBackbuffers[image]=generation;
    for(int operation=0;operation<6;++operation){
        std::future<VkResult> result;bool completed=false;uint32_t output=0;
        {std::lock_guard<std::recursive_mutex> presenting(state->lock);
            result=std::async(std::launch::async,[&]{
                if(operation==0)return L_AcquireNextImageKHR(handle,native,0,VK_NULL_HANDLE,VK_NULL_HANDLE,&output);
                if(operation==1){VkAcquireNextImageInfoKHR info{VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR};info.swapchain=native;info.deviceMask=1;return L_AcquireNextImage2KHR(handle,&info,&output);}
                if(operation==2)return L_GetSwapchainImagesKHR(handle,native,&output,nullptr);
                SdkScope sdk;auto command=reinterpret_cast<VkCommandBuffer>(&dispatch);output=9;
                if(operation==3){VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image;barrier.oldLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;L_CmdPipelineBarrier(command,0,0,0,0,nullptr,0,nullptr,1,&barrier);}
                if(operation==4){VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};barrier.image=image;barrier.oldLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;VkDependencyInfo info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};info.imageMemoryBarrierCount=1;info.pImageMemoryBarriers=&barrier;L_CmdPipelineBarrier2(command,&info);}
                if(operation==5)return L_CreateGraphicsPipelines(handle,VK_NULL_HANDLE,0,nullptr,nullptr,nullptr);
                return VK_SUCCESS;
            });
            completed=result.wait_for(std::chrono::seconds(2))==std::future_status::ready;
        } // Release even on a regression, so the failed test can finish cleanly.
        require(result.get()==VK_SUCCESS&&output==(operation==0?7u:operation==1?8u:operation==2?3u:9u),"Native SDK operation was misrouted");
        require(completed,"SDK native swapchain routing waited for Present's renderer lock");
    }
    require(sdkRoutingCalls==3&&state->commands.empty()&&state->layouts.empty(),"SDK commands polluted application tracking");
    std::cout<<"PASS: SDK acquire/acquire2/images/barriers/graphics creation finish while Present owns the renderer lock; private SDK layouts and proxy ownership preserved.\n";
}
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
        {std::lock_guard<std::mutex> lock(registryLock);devices.erase(key(vk.device));}
        if(nativeView)vk.dev<PFN_vkDestroyImageView>("vkDestroyImageView")(vk.device,nativeView,nullptr);depth.reset();
        if(device){device->presenterFrames.clear();device->idleFrames.clear();device->idleDepths.clear();
            if(device->reclaimFence)vk.dev<PFN_vkDestroyFence>("vkDestroyFence")(vk.device,device->reclaimFence,nullptr);
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
int wmain(int argc,wchar_t**argv){try{
    verifyFileControls();
    verifyPresenterRouting();
    ReuseFixture f;f.vk.open();auto &vk=f.vk;
    f.device=std::make_unique<Device>();auto &d=*f.device;d.handle=vk.device;d.physical=vk.physical;fixtureNativeDpa=vk.gdpa;d.next=fixtureDpa;d.memory=vk.memory;d.graphicsFamily=vk.family;
    {std::lock_guard<std::mutex> lock(registryLock);devices[key(vk.device)]=std::shared_ptr<Device>(&d,[](Device*){});}
    d.instance=std::make_shared<Instance>();d.instance->handle=vk.instance;d.instance->next=vk.gipa;setupPack(d);
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pool.queueFamilyIndex=vk.family;
    check(vk.dev<PFN_vkCreateCommandPool>("vkCreateCommandPool")(vk.device,&pool,nullptr,&f.pool),"Fixture pool");
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};alloc.commandPool=f.pool;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;alloc.commandBufferCount=1;
    check(vk.dev<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(vk.device,&alloc,&f.command),"Fixture command");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vk.dev<PFN_vkCreateFence>("vkCreateFence")(vk.device,&fence,nullptr,&f.fence),"Fixture fence");
    const bool large=argc>1&&!wcscmp(argv[1],L"--large");
    const uint32_t width=large?2953:64,height=large?1661:32,outputWidth=large?3840:width,outputHeight=large?2160:height;
    const uint64_t bytes=uint64_t(width)*height*4;
    std::vector<uint8_t> checkedPixels(size_t(bytes),0);
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=bytes;buffer.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(vk.dev<PFN_vkCreateBuffer>("vkCreateBuffer")(vk.device,&buffer,nullptr,&f.readback),"Readback buffer");
    VkMemoryRequirements requirements{};vk.dev<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(vk.device,f.readback,&requirements);
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=requirements.size;memory.memoryTypeIndex=vk.memoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vk.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(vk.device,&memory,nullptr,&f.readbackMemory),"Readback allocation");
    check(vk.dev<PFN_vkBindBufferMemory>("vkBindBufferMemory")(vk.device,f.readback,f.readbackMemory,0),"Readback binding");
    check(vk.dev<PFN_vkMapMemory>("vkMapMemory")(vk.device,f.readbackMemory,0,bytes,0,&f.mapped),"Readback map");
    ImageInfo source{};source.generation=1;source.create.extent={width,height,1};source.create.format=VK_FORMAT_D32_SFLOAT;source.create.samples=VK_SAMPLE_COUNT_1_BIT;
    auto frameKey=frameReuseKey(vk.family,width,height,outputWidth,outputHeight,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM);
    auto depthKey=depthReuseKey(vk.family,width,height,VK_FORMAT_D32_SFLOAT,VK_SAMPLE_COUNT_1_BIT);
    VkImage firstColor{},firstDepth{};
    auto testStart=std::chrono::steady_clock::now();
    for(int iteration=0;iteration<128;++iteration){
        // A configured FG provider must not disable reuse after its lease
        // finishes, nor when the user switches FG Off without a restart.
        if(iteration==64)d.fgRequested="4.0.2";
        f.depth=d.idleDepths.take(depthKey);
        if(!f.depth){f.depth=std::make_shared<DepthCapture>();f.depth->create(d,source,vk.family);}
        f.depth->generation=iteration+1;f.depth->layout=VK_IMAGE_LAYOUT_UNDEFINED;
        ViewInfo view{};view.create=f.depth->view;view.create.image=f.depth->image;
        f.frame=d.idleFrames.take(frameKey);
        if(!f.frame){f.frame=std::make_unique<FrameResources>();setupFrame(d,*f.frame,width,height,outputWidth,outputHeight,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM,view,f.depth,vk.family);}
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
        memcpy(checkedPixels.data(),f.mapped,size_t(bytes));
        const auto*pixels=checkedPixels.data();for(size_t i=0;i<bytes;i+=4)require(pixels[i]==(iteration%2?255:0)&&pixels[i+1]==0&&pixels[i+2]==0&&pixels[i+3]==255,"Reused GPU image returned stale or corrupted data");
        if(iteration>=64){f.frame->fgLease=++d.fgPreparedLease;d.fgCompletedLease=d.fgPreparedLease;}
        retireFrame(d,f.frame);retireDepth(d,f.depth);
    }
    require(d.frameSetupCost.calls==1&&d.depthSetupCost.calls==1&&d.idleFrames.hits==127&&d.idleDepths.hits==127,"Production creation/reuse counters disagree with FG configured");
    std::cout<<"Reuse at "<<width<<'x'<<height<<" -> "<<outputWidth<<'x'<<outputHeight<<": 128 GPU frames, one frame/depth allocation each, "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-testStart).count()<<"ms total (includes readback/CPU checks; not FSR timing).\n";
    // Application completion does not allow overwriting asynchronous SDK work.
    f.frame=d.idleFrames.take(frameKey);f.depth=d.idleDepths.take(depthKey);
    auto pendingImage=f.frame->color.value.handle;f.frame->fgLease=100;d.fgCompletedLease=99;
    ViewInfo pendingView{};pendingView.create=f.depth->view;bindFrameDepth(d,*f.frame,pendingView,f.depth);
    const auto pendingDepth=f.depth->image;
    auto&prior=d.commands[f.command];prior.pool=f.pool;prior.family=vk.family;prior.submitted=true;prior.resources=std::move(f.frame);prior.capturedDepths[pendingDepth]=std::move(f.depth);
    VkCommandBufferBeginInfo beginAgain{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};beginAgain.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(L_BeginCommandBuffer(f.command,&beginAgain),"Production Begin with pending presenter");
    require(!d.commands[f.command].resources&&d.commands[f.command].capturedDepths.empty()&&d.presenterFrames.size()==1&&!d.idleFrames.contains(frameKey),"Begin let a new recording overwrite pending SDK images");
    check(L_EndCommandBuffer(f.command),"Production empty End with pending presenter");
    retirePresenterFrames(d);require(d.presenterFrames.size()==1,"SDK owner was discarded without a completion");
    d.enabled=false;retirePresenterFrames(d);require(d.presenterFrames.size()==1,"Live processing Off discarded a pending SDK owner");d.enabled=true;
    d.fgCompletedLease=100;retirePresenterFrames(d);
    require(d.presenterFrames.empty()&&d.idleFrames.contains(frameKey),"Presenter completion failed to return a reusable image");
    f.frame=d.idleFrames.take(frameKey);require(f.frame->color.value.handle==pendingImage&&!f.frame->fgLease,"Presenter completion changed the allocation or kept a stale lease");retireFrame(d,f.frame);
    // Exercise production retirement against a real completed GPU recording.
    // The application fence is complete, but the SDK lease is deliberately not.
    auto& cold=d.commands[f.command];cold.oneTime=cold.submitted=true;cold.recording=100;cold.submittedQueue=vk.queue;
    cold.resources=d.idleFrames.take(frameKey);require(bool(cold.resources),"FG lifetime fixture lost its GPU resources");
    cold.resources->fgLease=5;d.fgRequested="4.0.2";d.epoch=200;d.fgCompletedLease=4;
    reclaimCompletedFrames(d,vk.queue);require(cold.resources&&!d.reclaimPending,"A completed application fence released a pending SDK image");
    d.fgCompletedLease=5;d.lastReclaimCheck=0;reclaimCompletedFrames(d,vk.queue);
    require(cold.resources&&d.reclaimPending,"Cold FG resource was not fenced after SDK completion");
    check(vk.dev<PFN_vkWaitForFences>("vkWaitForFences")(vk.device,1,&d.reclaimFence,VK_TRUE,10000000000ull),"FG retirement fence");
    reclaimCompletedFrames(d,vk.queue);require(!cold.resources&&!d.reclaimPending,"Completed cold FG resource stayed pinned");
    d.commands.clear();d.fgRequested.clear();
    // Force a real allocation API failure before capture records GPU commands.
    // The original depth remains valid and no null map entry reaches selection.
    source.create.extent.height=64;f.depth=std::make_shared<DepthCapture>();f.depth->create(d,source,vk.family);
    check(vk.dev<PFN_vkCreateImageView>("vkCreateImageView")(vk.device,&f.depth->view,nullptr,&f.nativeView),"Native OOM fixture view");
    d.images[f.depth->image]=source;d.views[f.nativeView]={f.depth->view};
    Command fault;fault.family=vk.family;fault.activeCaptureEligible=true;fault.activeDepth=f.depth->image;fault.depths.emplace(f.depth->image,Depth{f.nativeView,f.depth->image,VK_IMAGE_LAYOUT_UNDEFINED,1});
    d.mode=Mode::Upscale;fixtureFailAllocation=true;captureDepth(d,f.command,fault);
    require(!fixtureFailAllocation&&fault.capturedDepths.empty()&&d.allocationPaused&&!d.runtimeFailed,"Pre-recording OOM poisoned capture selection instead of native bypass");
    const auto failedCalls=d.depthSetupCost.calls;fixtureFailAllocation=true;captureDepth(d,f.command,fault);
    require(fixtureFailAllocation&&d.depthSetupCost.calls==failedCalls,"OOM retry ignored backoff and allocated again during the same frame");fixtureFailAllocation=false;
    VkMemoryRequirements originalRequirements{};vk.dev<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(vk.device,f.depth->image,&originalRequirements);require(originalRequirements.size>0,"OOM recovery destroyed the original depth image");
    d.idleFrames.limit(0);d.idleDepths.limit(0);require(!d.idleFrames.bytes()&&!d.idleDepths.bytes(),"Idle memory did not retire under pressure");
    std::cout<<"PASS: 128 actual fenced GPU readbacks, FG-configured frame/depth reuse, descriptor rebinding, pending SDK owner retention through live Off, cache admission after both completions, cold retirement, pressure trim and pre-recording OOM/backoff/native-depth preservation. Resource fixture only; no FSR quality claim.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

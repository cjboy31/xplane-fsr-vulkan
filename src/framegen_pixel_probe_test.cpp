// SPDX-License-Identifier: MIT
#define VK_USE_PLATFORM_WIN32_KHR
#define wmain unusedBaselineMain
#include "probe_support.hpp"
#undef wmain
#include "framegen_pixel_probe.hpp"
int wmain(){try{
    Vulkan vk;vk.open();
    FramePixelProbe probe({vk.device,vk.physical,vk.gdpa},vk.memory);
    GpuImage real(vk),generated(vk);real.create(128,72,VK_FORMAT_R8G8B8A8_UNORM);generated.create(128,72,VK_FORMAT_R8G8B8A8_UNORM);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=vk.family;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool{};vkcheck(vk.dev<PFN_vkCreateCommandPool>("vkCreateCommandPool")(vk.device,&pi,nullptr,&pool),"probe pool");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    VkCommandBuffer cb{};vkcheck(vk.dev<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(vk.device,&ai,&cb),"probe commands");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};vkcheck(vk.dev<PFN_vkCreateFence>("vkCreateFence")(vk.device,&fi,nullptr,&fence),"probe fence");
    for(uint64_t batch=0;batch<3;++batch){
        vkcheck(vk.dev<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(cb,0),"probe reset");
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(vk.dev<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb,&bi),"probe begin");
        for(uint64_t phase=0;phase<4;++phase){
            for(auto *image:{&real,&generated}){
                image->transition(cb,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                VkClearColorValue color{};float value=phase==2 || (phase==1 && image==&generated)?0.f:.5f;
                color.float32[0]=color.float32[1]=color.float32[2]=value;color.float32[3]=1;
                VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                vk.dev<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(cb,image->value.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
                image->transition(cb,image==&real?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL);
            }
            ffxDispatchDescFrameGeneration p{};p.commandList=cb;p.frameID=batch*4+phase+1;p.numGeneratedFrames=1;p.presentColor=resource(real.value);p.outputs[0]=resource(generated.value);
            probe.record(p);
            if(phase==3)probe.record(p); // Saturated ring skips; never blocks/overwrites pending GPU data.
        }
        vkcheck(vk.dev<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb),"probe end");
        vkcheck(vk.dev<PFN_vkResetFences>("vkResetFences")(vk.device,1,&fence),"probe reset fence");
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cb;
        vkcheck(vk.dev<PFN_vkQueueSubmit>("vkQueueSubmit")(vk.queue,1,&si,fence),"probe submit");
        vkcheck(vk.dev<PFN_vkWaitForFences>("vkWaitForFences")(vk.device,1,&fence,VK_TRUE,10000000000ull),"probe fixture completion");
        probe.poll();
        require(probe.stats.checked==(batch+1)*4 && probe.stats.generatedBlank==batch+1 && probe.stats.bothDark==batch+1,"Stale reused-query data, missed blank generation, or legitimate dark-frame misclassification");
    }
    ffxDispatchDescFrameGeneration unsupported{};unsupported.numGeneratedFrames=1;probe.record(unsupported);
    require(probe.stats.checked==12 && probe.stats.generatedBlank==3 && probe.stats.bothDark==3 && probe.stats.busy==3 && probe.stats.unsupported==1 && probe.stats.errors==0,"Probe counts differ");
    vkcheck(vk.waitIdle(vk.device),"probe final completion");
    vk.dev<PFN_vkDestroyFence>("vkDestroyFence")(vk.device,fence,nullptr);vk.dev<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(vk.device,pool,nullptr);
    std::cout<<"PASS: 12 GPU sample pairs; three injected blank generated frames distinguished from three dark scenes; query-ring reuse, full-ring skip and unsupported formats\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

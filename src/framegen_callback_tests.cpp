// SPDX-License-Identifier: MIT
// Drive the production callback through a captured SDK configuration; no GPU.
#define VK_USE_PLATFORM_WIN32_KHR
#include "framegen.hpp"
#include "framegen_policy.hpp"
#include <iostream>
static FfxApiFrameGenerationDispatchFunc callback{};static void*user{};
static bool failDispatch{},lastReset{},expectedAsync{};static unsigned dispatches{},createFlags{};
static uint64_t configuredFrame{};static unsigned configureCalls{};
static ffxReturnCode_t create(ffxContext*c,ffxCreateContextDescHeader*h,const ffxAllocationCallbacks*){*c=reinterpret_cast<void*>(1);createFlags=reinterpret_cast<ffxCreateContextDescFrameGeneration*>(h)->flags;return FFX_API_RETURN_OK;}
static ffxReturnCode_t destroy(ffxContext*c,const ffxAllocationCallbacks*){*c=nullptr;return FFX_API_RETURN_OK;}
static ffxReturnCode_t configure(ffxContext*,const ffxConfigureDescHeader*h){
 auto*c=reinterpret_cast<const ffxConfigureDescFrameGeneration*>(h);
 xpfsr::require(c->allowAsyncWorkloads==expectedAsync&&!c->onlyPresentGenerated,"SDK scheduling mode mismatch");
 configuredFrame=c->frameID;++configureCalls;
 callback=c->frameGenerationCallback;user=c->frameGenerationCallbackUserContext;return FFX_API_RETURN_OK;
}
static ffxReturnCode_t dispatch(ffxContext*,const ffxDispatchDescHeader*h){
 if(h->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2)
  xpfsr::require(reinterpret_cast<const ffxDispatchDescFrameGenerationPrepareV2*>(h)->frameID==configuredFrame,"PrepareV2 ran before configuring its frame");
 if(h->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION){++dispatches;lastReset=reinterpret_cast<const ffxDispatchDescFrameGeneration*>(h)->reset;return failDispatch?FFX_API_RETURN_ERROR_PARAMETER:FFX_API_RETURN_OK;}
 return FFX_API_RETURN_OK;
}
int main(){try{
 xpfsr::require(xpfsr::encodedColorFormat(VK_FORMAT_B8G8R8A8_SRGB)==VK_FORMAT_B8G8R8A8_UNORM && xpfsr::encodedColorFormat(VK_FORMAT_R8G8B8A8_SRGB)==VK_FORMAT_R8G8B8A8_UNORM && xpfsr::encodedColorFormat(VK_FORMAT_R16G16B16A16_SFLOAT)==VK_FORMAT_R16G16B16A16_SFLOAT && xpfsr::encodedColorFormat(VK_FORMAT_A2B10G10R10_UNORM_PACK32)==VK_FORMAT_A2B10G10R10_UNORM_PACK32,"sRGB adapter changed an HDR/ordinary format");
 xpfsr::require(std::wstring(app::frameGenerationProvider(3))==L"4.0.2" && std::wstring(app::frameGenerationProvider(4))==L"3.1.6" && app::synchronizedFrameGeneration(3) && app::synchronizedFrameGeneration(4) && !app::synchronizedFrameGeneration(2) && !app::frameGenerationProvider(5),"Compatibility FG mode mapping changed or invalid mode enabled");
 xpfsr::Api api{};api.create=create;api.destroy=destroy;api.configure=configure;api.dispatch=dispatch;
 xpfsr::FrameGenerator generator(api);VkDeviceContext device{};generator.create(device,32,32,32,32,VK_FORMAT_B8G8R8A8_UNORM,true,true,"");
 xpfsr::Frame input{};input.commands=reinterpret_cast<VkCommandBuffer>(1);input.depth.handle=reinterpret_cast<VkImage>(2);input.motion.handle=reinterpret_cast<VkImage>(3);
 input.depth.description.extent={32,32,1};input.motion.description.extent={32,32,1};input.deltaMilliseconds=16.667f;
 auto prepare=[&](uint64_t id){generator.prepare(reinterpret_cast<VkSwapchainKHR>(4),id,input,xpfsr::CameraBasis{});};
 auto call=[&](uint64_t id){ffxDispatchDescFrameGeneration p{};p.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;p.frameID=id;p.numGeneratedFrames=1;auto r=callback(&p,user);return std::pair<ffxReturnCode_t,uint32_t>{r,p.numGeneratedFrames};};
 prepare(1);xpfsr::require(call(1)==std::make_pair(FFX_API_RETURN_OK,1u)&&lastReset,"Initial SDK history was not primed");
 prepare(2);xpfsr::require(call(2)==std::make_pair(FFX_API_RETURN_OK,1u)&&!lastReset,"Normal callback did not generate");
 auto configs=configureCalls;generator.configure(reinterpret_cast<VkSwapchainKHR>(4),2,true);
 xpfsr::require(configs==configureCalls,"Matching source Present configured the same frame twice");
 unsigned before=dispatches;xpfsr::require(call(999)==std::make_pair(FFX_API_RETURN_OK,0u)&&dispatches==before&&!generator.failure,"Missing source advertised uninitialized output or poisoned FG");
 xpfsr::require(call(2)==std::make_pair(FFX_API_RETURN_OK,0u)&&dispatches==before,"Duplicate callback reused consumed source");
 prepare(3);xpfsr::require(call(3)==std::make_pair(FFX_API_RETURN_OK,1u)&&lastReset,"Missing-source recovery did not reset history");
 failDispatch=true;prepare(4);auto failed=call(4);xpfsr::require(failed.first!=FFX_API_RETURN_OK&&failed.second==0&&generator.failure,"Failed SDK dispatch advertised an unwritten output");
 xpfsr::require(generator.generated==3&&generator.skipped==3,"Generated/safe-hold counts differ");
 expectedAsync=true;failDispatch=false;xpfsr::FrameGenerator experiment(api);experiment.create(device,32,32,32,32,VK_FORMAT_B8G8R8A8_UNORM,true,true,"",true);
 xpfsr::require(createFlags&FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT,"Async context creation capability missing");
 experiment.prepare(reinterpret_cast<VkSwapchainKHR>(4),10,input,xpfsr::CameraBasis{});
 xpfsr::require(call(10)==std::make_pair(FFX_API_RETURN_OK,1u)&&call(999)==std::make_pair(FFX_API_RETURN_OK,0u),"Async experiment bypassed the same safe fallback");
 std::cout<<"PASS: production FG callback normal/reset, missing/duplicate source, no unwritten output on SDK error, recovery history and synchronous real-frame presentation\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

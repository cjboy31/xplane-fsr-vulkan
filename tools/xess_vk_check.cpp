// SPDX-License-Identifier: MIT
// Original offscreen XeSS fixture. No Intel SDK code/runtime is redistributed.
#include "gpu_worker_vk.hpp"
#include "xess_vk_adapter.hpp"
#include <cmath>
#include <iostream>
using namespace xpfsr::gpu;
static float halfValue(uint16_t h){const int e=(h>>10)&31,f=h&1023;if(e==31)return NAN;float v=e?std::ldexp(1.0f+f/1024.0f,e-15):std::ldexp(float(f),-24);return h>>15?-v:v;}
static xess_vk_image_view_info view(const worker::Image&i){return {i.view,i.handle,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},i.format,i.width,i.height};}
int wmain(int argc,wchar_t**argv){try{
    worker::need(argc==3,"XeSSVulkanCheck ABSOLUTE_LIBXESS_DLL DEVICE_UUID");
    xpfsr::intel::Api api(std::filesystem::absolute(argv[1]));uint32_t minimum{};auto extensions=api.instanceExtensions(minimum);
    worker::Instance instance(extensions,std::max(minimum,VK_API_VERSION_1_3));std::wstring id=argv[2];auto physical=instance.select(std::string(id.begin(),id.end()));
    auto deviceExtensions=api.deviceExtensions(instance.handle,physical);
    worker::Device device(physical,deviceExtensions,[&](void**chain){xpfsr::intel::checked(api.featureRequirements(instance.handle,physical,chain),"Intel required device features");});
    xpfsr::intel::Context context(api,instance.handle,physical,device.handle);
    constexpr uint32_t ow=384,oh=216;auto size=context.initialize(ow,oh,XESS_QUALITY_SETTING_QUALITY);
    worker::need(size.x&&size.y&&size.x<=ow&&size.y<=oh,"Intel optimal input resolution invalid");
    worker::Image color(device,size.x,size.y,VK_FORMAT_R16G16B16A16_SFLOAT),depth(device,size.x,size.y,VK_FORMAT_R32_SFLOAT),motion(device,size.x,size.y,VK_FORMAT_R16G16_SFLOAT),output(device,ow,oh,VK_FORMAT_R16G16B16A16_SFLOAT);
    worker::Buffer staging(device,size_t(ow)*oh*8),guide(device,size_t(size.x)*size.y*4);
    auto*pixel=static_cast<uint16_t*>(staging.mapped);for(size_t i=0;i<size_t(size.x)*size.y;++i){pixel[i*4]=0x3c00;pixel[i*4+1]=0x3800;pixel[i*4+2]=0x3400;pixel[i*4+3]=0x3c00;}color.upload(staging);
    std::fill_n(static_cast<float*>(guide.mapped),size_t(size.x)*size.y,.5f);depth.upload(guide);std::memset(guide.mapped,0,size_t(size.x)*size.y*4);motion.upload(guide);
    uint32_t checked{};for(uint32_t frame=0;frame<6;++frame){device.submit([&](auto cb){
        color.transition(cb,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);depth.transition(cb,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);motion.transition(cb,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);output.transition(cb,VK_IMAGE_LAYOUT_GENERAL);
        xess_vk_execute_params_t p{};p.colorTexture=view(color);p.velocityTexture=view(motion);p.depthTexture=view(depth);p.outputTexture=view(output);p.exposureScale=1;p.resetHistory=frame==0;p.inputWidth=size.x;p.inputHeight=size.y;context.record(cb,p);
    });output.read(staging);if(frame>=2){auto*p=static_cast<uint16_t*>(staging.mapped);for(size_t i=0;i<size_t(ow)*oh;++i){for(unsigned c=0;c<3;++c){const float expected=std::ldexp(1.0f,-int(c)),value=halfValue(p[i*4+c]);worker::need(std::isfinite(value)&&value>expected*.5f&&value<expected*1.5f,"XeSS output is black, nonfinite or has incorrect brightness");}}++checked;}}
    xess_version_t v{};xpfsr::intel::checked(api.version(&v),"Read Intel runtime version");VkPhysicalDeviceProperties prop{};vkGetPhysicalDeviceProperties(physical,&prop);
    std::cout<<"{\"result\":\"PASS\",\"sdkTag\":\"v3.0.2\",\"runtimeVersion\":\""<<v.major<<'.'<<v.minor<<'.'<<v.patch<<"\",\"vendorId\":"<<prop.vendorID<<",\"inputWidth\":"<<size.x<<",\"inputHeight\":"<<size.y<<",\"outputWidth\":"<<ow<<",\"outputHeight\":"<<oh<<",\"checkedFrames\":"<<checked<<",\"scope\":\"actual Vulkan XeSS offscreen HDR readbacks; no simulator injection or Intel hardware certification\"}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

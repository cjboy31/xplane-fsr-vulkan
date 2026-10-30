// SPDX-License-Identifier: MIT
// Test-only deterministic backend. Never packaged. The real Vulkan layer/FSR
// must consume the selected stage's pixels; corrupt scratch on bypass/failure
// must never reach the simulator output.
#include "nr_api.hpp"
#include <cstdio>
#include <memory>
struct Probe { xpfsr::nr::Config config; PFN_vkCmdClearColorImage clear; };
extern "C" __declspec(dllexport) void* XPFSR_NR_Create(const xpfsr::nr::Host*h,const xpfsr::nr::Config*c,char*why,uint32_t bytes){
    if(!h||!c||h->version!=xpfsr::nr::apiVersion||c->version!=xpfsr::nr::apiVersion||c->size!=sizeof(*c))return nullptr;
    wchar_t native[4]{};GetEnvironmentVariableW(L"XPFSR_TEST_NR_NATIVEAA",native,4);
    const bool after=c->controls[xpfsr::nr::Order]!=0;
    const uint32_t w=after&&native[0]!=L'1'?384:256,hgt=after&&native[0]!=L'1'?216:144;
    if(c->width!=w||c->height!=hgt||c->format!=VK_FORMAT_R16G16B16A16_SFLOAT){std::snprintf(why,bytes,"Wrong stage dimensions/format");return nullptr;}
    auto gdpa=reinterpret_cast<PFN_vkGetDeviceProcAddr>(h->physicalDispatch(h->instance,"vkGetDeviceProcAddr"));
    auto p=std::make_unique<Probe>();p->config=*c;p->clear=reinterpret_cast<PFN_vkCmdClearColorImage>(gdpa(h->device,"vkCmdClearColorImage"));return p.release();
}
extern "C" __declspec(dllexport) int XPFSR_NR_Record(void*context,const xpfsr::nr::Frame*f,char*why,uint32_t bytes){
    auto*p=static_cast<Probe*>(context);using namespace xpfsr::nr;
    if(!p||!f||f->size!=sizeof(*f)||f->version!=apiVersion||f->controls[Order]!=p->config.controls[Order])return -1;
    const int strength=f->controls[Intensity];VkClearColorValue color{};
    float before[]={.1f,.3f,.5f,1},after[]={.3f,.1f,.2f,1};
    for(int i=0;i<4;++i)color.float32[i]=strength!=25?1.f:(f->controls[Order]?after[i]:before[i]);
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};p->clear(f->commands,f->color,VK_IMAGE_LAYOUT_GENERAL,&color,1,&range);
    std::snprintf(why,bytes,"NR order probe: %s; %ux%u; result=%d",f->controls[Order]?"after":"before",p->config.width,p->config.height,strength==25?1:strength==26?0:-1);
    return strength==25?1:strength==26?0:-1;
}
extern "C" __declspec(dllexport) void XPFSR_NR_Destroy(void*p){delete static_cast<Probe*>(p);}

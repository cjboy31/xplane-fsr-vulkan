// SPDX-License-Identifier: MIT
// An unrelated Vulkan application must see its unmodified device calls.
#define wmain unusedProbeMain
#include "probe_support.hpp"
#undef wmain
int wmain(int argc,wchar_t**argv){try{
    require(argc==3,"passive_layer_test LAYER_DLL UNCREATED_LOG");
    const std::filesystem::path dll=argv[1],log=argv[2];
    require(!std::filesystem::exists(log),"Fresh log filename required");
    SetEnvironmentVariableW(L"XPFSR24_RUNTIME",nullptr);SetEnvironmentVariableW(L"XPFSR24_SETTINGS",nullptr);
    SetEnvironmentVariableW(L"XPFSR24_LOG",log.c_str());
    for(int cycle=0;cycle<2;++cycle){
        Vulkan vk;vk.open();require(GetModuleHandleW(dll.c_str())!=nullptr,"Layer was not loaded; pass-through was not exercised");
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=256;info.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer buffer{};vkcheck(vk.dev<PFN_vkCreateBuffer>("vkCreateBuffer")(vk.device,&info,nullptr,&buffer),"Passive CreateBuffer");
        VkMemoryRequirements memory{};vk.dev<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(vk.device,buffer,&memory);require(memory.size>=info.size,"Invalid native buffer requirements");
        vk.dev<PFN_vkDestroyBuffer>("vkDestroyBuffer")(vk.device,buffer,nullptr);
        auto instanceCreateImage=vk.inst<PFN_vkCreateImage>("vkCreateImage");
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;image.format=VK_FORMAT_R8G8B8A8_UNORM;image.extent={16,16,1};image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VkImage result{};vkcheck(instanceCreateImage(vk.device,&image,nullptr,&result),"Passive instance-resolved CreateImage");vk.dev<PFN_vkDestroyImage>("vkDestroyImage")(vk.device,result,nullptr);
        vkcheck(vk.waitIdle(vk.device),"Passive wait idle");
    }
    require(!std::filesystem::exists(log),"Unrelated application wrote renderer diagnostics");
    std::cout<<"PASS: loaded layer passes unrelated Vulkan instance/device/resource calls through over two lifecycles, with no renderer session log.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

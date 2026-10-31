// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace xpfsr::gpu {
struct Identity {
    std::array<uint8_t,16> device{},driver{};
    bool valid()const{return std::any_of(device.begin(),device.end(),[](uint8_t b){return b!=0;})&&std::any_of(driver.begin(),driver.end(),[](uint8_t b){return b!=0;});}
    bool operator==(const Identity&o)const{return valid()&&o.valid()&&device==o.device&&driver==o.driver;}
};
inline size_t uniqueIdentity(const Identity&wanted,const std::vector<Identity>&available){
    if(!wanted.valid())throw std::runtime_error("Vulkan device/driver UUID is unavailable");
    size_t found=available.size();
    for(size_t i=0;i<available.size();++i)if(wanted==available[i]){
        if(found!=available.size())throw std::runtime_error("Vulkan physical device identity is ambiguous");
        found=i;
    }
    if(found==available.size())throw std::runtime_error("The selected physical GPU is unavailable");
    return found;
}
}

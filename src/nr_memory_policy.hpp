// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>
namespace xpfsr::nr {
inline constexpr uint64_t MiB=1024ull*1024,Gib=1024ull*MiB;
inline uint64_t saturatedMultiply(uint64_t a,uint64_t b){return b&&a>UINT64_MAX/b?UINT64_MAX:a*b;}
inline uint64_t saturatedAdd(uint64_t a,uint64_t b){return a>UINT64_MAX-b?UINT64_MAX:a+b;}
// Conservative admission estimate, not an allocation measurement. It includes
// model intermediates, per-pass histories and full-frame adapter images. Future
// scenery allocations can still change the available budget.
inline uint64_t estimatedProfileBytes(uint32_t w,uint32_t h,int percent,int passes){
    auto model=[](uint32_t size,int p){return ((uint64_t(size)*std::clamp(p,25,100)+799)/800)*8;};
    auto pixels=saturatedMultiply(model(w,percent),model(h,percent));
    auto network=saturatedMultiply(pixels,512+32*std::clamp(passes,1,3));
    return saturatedAdd(192*MiB,saturatedAdd(network,saturatedMultiply(uint64_t(w)*h,64)));
}
inline uint64_t textureReserveBytes(uint64_t budget){return std::max(512*MiB,std::min(4*Gib,budget/10));}
inline bool profileFits(uint64_t budget,uint64_t usage,uint64_t need){
    if(budget<=usage)return false;
    const auto free=budget-usage,reserve=textureReserveBytes(budget);
    return free>=reserve&&need<=free-reserve;
}
inline bool completedOneTimeRecording(bool fenceComplete,bool oneTime,bool submitted,uint64_t snapshot,uint64_t current){
    return fenceComplete&&oneTime&&submitted&&snapshot&&snapshot==current;
}
inline uint64_t coldFrameCutoff(std::vector<uint64_t> recordings,size_t keep=12){
    keep=std::max(size_t(1),keep);if(recordings.size()<=keep)return 0;
    auto cutoff=recordings.end()-keep;std::nth_element(recordings.begin(),cutoff,recordings.end());return *cutoff;
}
inline bool unusedPrivateFrame(bool ended,bool processed,bool failed,bool frameGeneration){return ended&&!processed&&!failed&&!frameGeneration;}
inline uint64_t frameCacheTargetBytes(uint64_t budget){return budget?std::max(128*MiB,std::min(512*MiB,budget/16)):256*MiB;}
inline size_t frameCacheKeep(uint64_t budget,uint64_t largestFrame){return largestFrame?size_t(std::clamp(frameCacheTargetBytes(budget)/largestFrame,uint64_t(3),uint64_t(12))):12;}
inline uint64_t estimatedFrameBytes(uint32_t rw,uint32_t rh,uint32_t ow,uint32_t oh){
    return saturatedAdd(4*MiB,saturatedAdd(saturatedMultiply(uint64_t(rw)*rh,32),saturatedMultiply(uint64_t(ow)*oh,16)));
}
inline uint64_t estimatedUpscalerBytes(uint32_t rw,uint32_t rh,uint32_t ow,uint32_t oh){
    return saturatedAdd(384*MiB,saturatedAdd(saturatedMultiply(uint64_t(rw)*rh,16),saturatedMultiply(uint64_t(ow)*oh,8)));
}
} // namespace xpfsr::nr

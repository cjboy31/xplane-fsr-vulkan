// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <utility>
#include <algorithm>
namespace xpfsr {
// Learn only from a validated, current-camera color/depth relationship at the
// native reconstruction boundary. Keep two extents for recent resize/input
// changes. This extent check is combined with SceneDepthSources below. A new
// input falls back natively while its identity/dimensions are learned.
class SceneCaptureExtents {
    using Extent=std::array<uint32_t,2>;
    std::array<Extent,2> recent{};
public:
    static constexpr uint32_t maximumSide=4096;
    bool accepts(uint32_t w,uint32_t h)const{Extent size{w,h};return w&&h&&w<=maximumSide&&h<=maximumSide&&(!recent[0][0]||size==recent[0]||size==recent[1]);}
    bool observe(uint32_t w,uint32_t h,bool currentScene){
        if(!currentScene||!w||!h||w>maximumSide||h>maximumSide)return false;Extent size{w,h};
        if(size==recent[0])return false;
        recent[1]=recent[0];recent[0]=size;return true;
    }
};
// Identity is learned from current-camera color/depth provenance, not just a
// matching size. Metadata tracking continues for unknown passes; the native
// boundary discovers new sources without making speculative GPU copies. A new
// source can use native rendering for one recording while it is learned.
class SceneDepthSources {
    struct Source { uint64_t image{}, generation{}; };
    std::array<Source,32> sources{};
    std::size_t next{};
public:
    bool accepts(uint64_t image,uint64_t generation) const {
        return image&&generation&&std::any_of(sources.begin(),sources.end(),[&](const Source&s){return s.image==image&&s.generation==generation;});
    }
    void observe(uint64_t image,uint64_t generation,bool currentScene) {
        if(!currentScene||!image||!generation||accepts(image,generation))return;
        for(auto&s:sources)if(s.image==image){s={image,generation};return;}
        sources[next]={image,generation};next=(next+1)%sources.size();
    }
    void erase(uint64_t image){for(auto&s:sources)if(s.image==image)s={};}
};
} // namespace xpfsr

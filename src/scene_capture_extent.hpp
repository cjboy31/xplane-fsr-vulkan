// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <utility>
namespace xpfsr {
// Learn only from a validated, current-camera color/depth relationship at the
// native reconstruction boundary. Keep two extents for recent resize/input
// changes. Unknown input captures broadly; a new input can fall back natively
// for one frame while its actual dimensions are learned from tracked metadata.
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
} // namespace xpfsr

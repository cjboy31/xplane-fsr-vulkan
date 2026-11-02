// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace xpfsr {
// A completed application fence alone does not finish SDK presentation work.
inline bool presenterFinished(uint64_t lease,uint64_t completed){return !lease||lease<=completed;}
// Keep CPU/GPU overlap bounded without treating an application fence as SDK
// completion. The caller must finish the SDK wait before advancing completed.
inline bool presenterDrainRequired(uint64_t prepared,uint64_t completed,bool synchronized,bool memoryPressure){
    return prepared>completed&&(synchronized||memoryPressure||prepared-completed>=3);
}
inline bool coldPresent(uint64_t used,uint64_t present){return present>used&&present-used>120;}
struct AllocationBackoff {
    uint64_t retryAt{};
    bool allowed(uint64_t now)const{return now>=retryAt;}
    void failed(uint64_t now){retryAt=now>UINT64_MAX-3000?UINT64_MAX:now+3000;}
    void recovered(){retryAt=0;}
};
}

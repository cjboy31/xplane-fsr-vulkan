// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
namespace app {
// Wait for an actual coherent scene before competing with initial simulator
// loading. Latches once ready; ordinary later camera cuts do not rebuild NR.
struct StartupReadiness {
    uint64_t firstTick{},lastTick{},lastFrame{};uint32_t frames{};bool started{},ready{};
    bool observe(uint64_t frame,float milliseconds,uint64_t now){
        if(ready)return true;
        const bool valid=frame&&std::isfinite(milliseconds)&&milliseconds>0&&milliseconds<=100;
        if(!valid){started=false;frames=0;lastFrame=frame;return false;}
        if(frame==lastFrame)return false;
        if(!started||frame<lastFrame||now<firstTick||now-lastTick>500){started=true;firstTick=now;frames=0;}
        // Eligible scene IDs can skip while the host loads or draws other views.
        // Require fresh, forward-moving samples, not an unbroken numeric sequence.
        lastFrame=frame;lastTick=now;++frames;ready=frames>=30&&now-firstTick>=3000;return ready;
    }
};
}

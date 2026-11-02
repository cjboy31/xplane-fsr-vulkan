// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace app {
// Source frames are the camera publisher's once-per-frame sequence. Generated
// frames are successful SDK generation callbacks, never the selected FG mode.
// This measures production rates, not display scanout or CPU/GPU execution time.
struct FrameCounter {
    uint64_t started{}, source{}, generated{};
    double realFPS{}, generatedFPS{};
    bool seeded{}, ready{}, rendererKnown{};
    void reset(){*this={};}
    bool due(uint64_t now)const{return !seeded||now<started||now-started>=1000;}
    void sample(uint64_t now,uint64_t real,uint64_t extra,bool known){
        if(!seeded||now<started||real<source||now-started>5000){
            reset();seeded=true;started=now;source=real;generated=extra;rendererKnown=known;return;
        }
        if(now-started<1000)return;
        const double seconds=double(now-started)/1000.;
        realFPS=double(real-source)/seconds;
        generatedFPS=known&&rendererKnown&&extra>=generated?double(extra-generated)/seconds:0.;
        ready=true;started=now;source=real;generated=extra;rendererKnown=known;
    }
};
} // namespace app

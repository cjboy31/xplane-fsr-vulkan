// SPDX-License-Identifier: MIT
#pragma once
#include "capture_compat.hpp"
namespace app {
inline launcher::Preferences runningProfile(const launcher::Preferences &saved,const graphics::Options &current,bool obsLoaded){
    auto p=automaticCaptureProfile(saved,obsLoaded);
    // X-Plane or another launcher can change input scale independently of our
    // pending flag. Always match the running mode to the actual source while
    // preserving the provider family, saved intent and simulator preferences.
    p.manualPending=false;
    if(p.mode>=1&&p.mode<=4)p.mode=(p.mode>=3?3:1)+(current.scale>0?1:0);
    // The original scene's display restore cannot be applied to interpolated
    // frames. Pause that feature for this FG session, keeping its saved choice.
    if(p.frameGeneration){p.cockpitRestore=false;p.alignmentTint=false;}
    return p;
}
inline std::wstring runningGraphicsNotice(const launcher::Preferences &saved,const launcher::Preferences &running){
    if(saved.mode!=running.mode)return running.mode==1||running.mode==3?
        L"Running Native AA at X-Plane's current full input scale. Saved upscaling choice retained; use Image quality and Apply after exit to change the input scale.":
        L"Running FSR upscaling at X-Plane's current reduced input scale. Saved Native AA choice retained; select Full input and Apply after exit for Native AA.";
    return saved.manualPending?L"FSR follows the current simulator input scale. Your pending graphics choices are retained; use Apply after exit to apply them.":L"";
}
} // namespace app

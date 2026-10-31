// SPDX-License-Identifier: MIT
#pragma once
#include "launcher_preferences.hpp"
namespace app {
inline launcher::Preferences automaticCaptureProfile(launcher::Preferences p,bool obsLoaded){
    // A loaded capture layer does not prove active capture or an incompatible
    // swapchain. Keep the selected FG algorithm, using the existing drained
    // presenter path when the hook is present. Never edit global capture setup.
    if(obsLoaded){
        if(p.frameGeneration==1)p.frameGeneration=4;
        else if(p.frameGeneration==2)p.frameGeneration=3;
    }
    return p;
}
} // namespace app

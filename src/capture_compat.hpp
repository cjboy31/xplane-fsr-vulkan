// SPDX-License-Identifier: MIT
#pragma once
#include "launcher_preferences.hpp"
namespace app {
inline launcher::Preferences automaticCaptureProfile(launcher::Preferences p,bool obsLoaded){
    // Loader discovery has already run. Setting OBS's disable variable now is
    // too late. Never change its global registry/configuration or saved FG.
    if(obsLoaded)p.frameGeneration=0;
    return p;
}
} // namespace app

// SPDX-License-Identifier: MIT
#pragma once
#include "capture_compat.hpp"
namespace app {
inline launcher::Preferences runningProfile(const launcher::Preferences &saved,const graphics::Options &current,bool obsLoaded){
    auto p=automaticCaptureProfile(saved,obsLoaded);
    // Pending display/AA edits are next-session intent. They must not switch
    // off reconstruction that can run on this session's actual simulator input.
    // The saved profile and simulator preferences remain untouched.
    p.manualPending=false;
    if(p.mode&&saved.manualPending)p.mode=(p.mode>=3?3:1)+(current.scale>0?1:0);
    return p;
}
} // namespace app

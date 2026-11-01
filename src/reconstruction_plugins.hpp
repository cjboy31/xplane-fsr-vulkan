// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <string>
namespace app {
// This runs before plugin loading. A binary on disk is a conflict candidate,
// not proof that another renderer has started. Empty/support-only directories,
// other-platform binaries and renamed .disabled files are not candidates.
inline std::string reconstructionConflict(const std::filesystem::path& simulator){
    const auto plugins=simulator/"Resources/plugins";
    for(const auto* name:{"LuminaXP","MotionVectors"}){
        const std::filesystem::path folder=plugins/name;
        const std::filesystem::path candidates[]={folder/"64/win.xpl",folder/"win_x64"/(std::string(name)+".xpl"),folder/"win.xpl",plugins/(std::string(name)+".xpl")};
        for(const auto& file:candidates){
            if(!std::filesystem::is_regular_file(file))continue;
            const auto relative=file.lexically_relative(simulator).generic_u8string();
            return "FSR startup paused: another reconstruction plugin file was found: "+relative+
                ". This does not confirm it is running. Close X-Plane and move that plugin outside Resources/plugins to test FSR alone, then restart. Your choices are saved.";
        }
    }
    return {};
}
}

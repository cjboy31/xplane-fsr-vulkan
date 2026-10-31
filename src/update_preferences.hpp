// SPDX-License-Identifier: MIT
#pragma once
#include "launcher_preferences.hpp"
#include "update_feed.hpp"
namespace updates {
inline Channel loadChannel(const graphics::fs::path &file) {
    auto name = launcher::readSetting(file, L"Updates", L"Channel");
    if (name == L"normal") return Channel::Normal;
    if (name == L"experimental") return Channel::Experimental;
    // The old preview checkbox mixed both channels and cannot express intent.
    return installedChannel();
}
inline void saveChannel(const graphics::fs::path &file, Channel selected) {
    auto name = selected == Channel::Experimental ? L"experimental" : L"normal";
    graphics::require(launcher::writeSetting(L"Updates", L"Channel", name, file.c_str()) != FALSE,
                      "Could not save the release channel");
    launcher::writeSetting(nullptr, nullptr, nullptr, file.c_str());
    graphics::require(loadChannel(file) == selected,
                      "Saved release channel could not be verified");
}
} // namespace updates

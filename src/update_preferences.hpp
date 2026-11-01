// SPDX-License-Identifier: MIT
#pragma once
#include "launcher_preferences.hpp"
#include "update_feed.hpp"
namespace updates {
inline Channel loadChannel(const graphics::fs::path &file) {
    auto name = launcher::readSetting(file, L"Updates", L"Channel");
    if (name == L"release" || name == L"normal") return Channel::Release;
    if (name == L"beta" || name == L"experimental") return Channel::Beta;
    // The old preview checkbox mixed both channels and cannot express intent.
    return installedChannel();
}
inline void saveChannel(const graphics::fs::path &file, Channel selected) {
    auto name = selected == Channel::Beta ? L"beta" : L"release";
    graphics::require(launcher::writeSetting(L"Updates", L"Channel", name, file.c_str()) != FALSE,
                      "Could not save the release channel");
    launcher::writeSetting(nullptr, nullptr, nullptr, file.c_str());
    graphics::require(loadChannel(file) == selected,
                      "Saved release channel could not be verified");
}
} // namespace updates

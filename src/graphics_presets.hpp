// SPDX-License-Identifier: MIT
#pragma once
#include "graphics_settings.hpp"
#include <array>
namespace graphics {
struct OutputChoice {
    int width, height;
    const wchar_t *label;
};
inline constexpr std::array<OutputChoice, 4> outputChoices{
    {{0, 0, L"Keep current output"},
     {1920, 1080, L"1920 x 1080 (1080p)"},
     {2560, 1440, L"2560 x 1440 (1440p)"},
     {3840, 2160, L"3840 x 2160 (4K)"}}};
struct Preset {
    const char *id;
    const wchar_t *label;
    Options settings;
    int launcherMode;
};
// Quality uses a 1.5x scale and Performance uses 2x. The 1440p Quality input
// width is implementation-rounded, so its label intentionally says ~960p.
inline constexpr std::array<Preset, 14> resolutionPresets{
    {{"1080-native", L"FSR 3 - 1080p Native AA", {0, 0, 0, 1920, 1080}, 1},
     {"1080-quality", L"FSR 3 - 1080p Quality (720p input)", {0, 0, 2, 1920, 1080}, 2},
     {"1440-native", L"FSR 3 - 1440p Native AA", {0, 0, 0, 2560, 1440}, 1},
     {"1440-quality", L"FSR 3 - 1440p Quality (~960p input)", {0, 0, 2, 2560, 1440}, 2},
     {"4k-native", L"FSR 3 - 4K Native AA", {0, 0, 0, 3840, 2160}, 1},
     {"4k-quality", L"FSR 3 - 4K Quality (1440p input)", {0, 0, 2, 3840, 2160}, 2},
     {"4k-performance", L"FSR 3 - 4K Performance (1080p input)", {0, 0, 4, 3840, 2160}, 2},
     {"fsr4-1080-native", L"FSR 4 - 1080p Native AA (preview)", {0, 0, 0, 1920, 1080}, 3},
     {"fsr4-1080-quality",
      L"FSR 4 - 1080p Quality (720p input; preview)",
      {0, 0, 2, 1920, 1080},
      4},
     {"fsr4-1440-native", L"FSR 4 - 1440p Native AA (preview)", {0, 0, 0, 2560, 1440}, 3},
     {"fsr4-1440-quality",
      L"FSR 4 - 1440p Quality (~960p input; preview)",
      {0, 0, 2, 2560, 1440},
      4},
     {"fsr4-4k-native", L"FSR 4 - 4K Native AA (preview)", {0, 0, 0, 3840, 2160}, 3},
     {"fsr4-4k-quality", L"FSR 4 - 4K Quality (1440p input; preview)", {0, 0, 2, 3840, 2160}, 4},
     {"fsr4-4k-performance",
      L"FSR 4 - 4K Performance (1080p input; preview)",
      {0, 0, 4, 3840, 2160},
      4}}};
template <class Supported>
std::vector<const Preset *> availableResolutionPresets(Supported supported) {
    std::vector<const Preset *> result;
    for (auto &preset : resolutionPresets)
        if (supported(preset.settings.outputWidth, preset.settings.outputHeight))
            result.push_back(&preset);
    return result;
}
inline const Preset &presetById(const std::string &id) {
    for (auto &preset : resolutionPresets)
        if (id == preset.id)
            return preset;
    throw std::runtime_error("Unknown resolution preset");
}
} // namespace graphics

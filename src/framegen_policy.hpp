// SPDX-License-Identifier: MIT
#pragma once
namespace app {
// Public builds must not create the experimental SDK swapchain, even if an old
// settings file or inherited environment variable still requests it.
#ifdef XPFSR_FG_DEVELOPMENT
inline constexpr bool frameGenerationAvailable = true;
#else
inline constexpr bool frameGenerationAvailable = false;
#endif
inline constexpr const wchar_t *frameGenerationSuspension =
    L"Built-in frame generation is temporarily disabled in regular releases while "
    L"flicker, artifacts and pacing are investigated. Your earlier choice is retained "
    L"in settings, but this build always launches with FG Off. Development remains on "
    L"the separate experimental branch. FSR AA/upscaling still work.";
inline int effectiveFrameGeneration(int selection) {
    return frameGenerationAvailable && selection >= 0 && selection <= 2 ? selection : 0;
}
} // namespace app

// SPDX-License-Identifier: MIT
#pragma once
namespace app {
// Both Windows channels ship the corrected FG path. Off remains the default;
// the release channel no longer controls feature availability.
inline constexpr bool frameGenerationAvailable = true;
inline constexpr const wchar_t *frameGenerationSuspension =
    L"Frame generation is available in both Windows channels. Start with FG Off "
    L"and stable source performance. GPU/runtime checks still determine support.";
inline int effectiveFrameGeneration(int selection) {
    return frameGenerationAvailable && selection >= 0 && selection <= 4 ? selection : 0;
}
// Preserve the published Off/3/4 indices. The two additional Experimental
// choices request the older official presenter and drain paced work at Present.
inline const wchar_t *frameGenerationProvider(int selection){
    return selection==1||selection==4?L"3.1.6":selection==2||selection==3?L"4.0.2":nullptr;
}
inline bool synchronizedFrameGeneration(int selection){return selection==3||selection==4;}
} // namespace app

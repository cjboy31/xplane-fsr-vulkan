// SPDX-License-Identifier: MIT
#pragma once
namespace insim {
inline const char* changelogText() {
    return "What's new in 0.7.4\n\nThe in-sim panel is now the place to change rendering settings. The desktop program is a plugin updater, with no flight or image settings.\n\nThis page replaces the startup video. Open Changelog whenever you want to see what changed.\n\nSetting names and ? help have been simplified. Your saved choices are kept.\n\nThe Renderer choice lets you use FSR, independent Native AA, or the original simulator on your next launch. Only one mod processes the scene. Restart X-Plane after changing it.\n\nFSR offers Native AA, upscaling, frame generation and optional neural rendering. Neural rendering needs supported hardware and your own model.\n\nThe cockpit display option is called Cockpit screen protection. It only works with supported screen layouts; turn it off if alignment looks wrong.\n\nGraphics changes marked After restart apply after you close X-Plane normally. Live controls can be changed during your flight.";
}
}

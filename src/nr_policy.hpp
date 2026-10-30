// SPDX-License-Identifier: MIT
#pragma once
namespace app {
// Channel selection changes the update feed, not access to the current NR
// implementation. Supported hardware and a verified local model are required.
inline constexpr bool neuralRenderingAvailable = true;
inline int effectiveNeuralRendering(int selected) {
    return neuralRenderingAvailable && selected >= 1 && selected <= 4 ? selected : 0;
}
} // namespace app

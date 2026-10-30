// SPDX-License-Identifier: MIT
#pragma once
namespace app {
#ifdef XPFSR_NR_DEVELOPMENT
inline constexpr bool neuralRenderingAvailable = true;
#else
inline constexpr bool neuralRenderingAvailable = false;
#endif
inline int effectiveNeuralRendering(int selected) {
    return neuralRenderingAvailable && selected >= 1 && selected <= 4 ? selected : 0;
}
} // namespace app

// SPDX-License-Identifier: MIT
// Container hashes published by the pinned DLSSNR-AMD installer, not weights.
#pragma once
#include <cstdint>
#include <string>
namespace xpfsr::nr {
inline constexpr const char *smallModelSha =
    "7e966e2639dbec6596aa0bac3b8761137d2c40ca8cb4aca30df7ca10043c6e33";
inline constexpr const char *originalModelSha =
    "2b41c888cf4155b8958c665ba64018ab0bd25c85fc71a2b6db86d0d04d1f7fbd";
inline bool acceptedModel(bool small, uint64_t bytes, const std::string &sha) {
    return bytes == (small ? 113917248ull : 147756560ull) &&
           sha == (small ? smallModelSha : originalModelSha);
}
} // namespace xpfsr::nr

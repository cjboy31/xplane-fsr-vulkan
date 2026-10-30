// SPDX-License-Identifier: MIT
#pragma once
#include <string>
namespace intro {
inline bool shouldPresent(bool enabled, const std::wstring &lastPresented,
                          const std::wstring &installed, bool verifiedMedia, bool simulatorRunning) {
    return enabled && verifiedMedia && !simulatorRunning && lastPresented != installed;
}
inline bool validDigest(const std::wstring &digest) {
    return digest.size() == 64 && digest.find_first_not_of(L"0123456789abcdef") == std::wstring::npos;
}
inline bool safeVersion(const std::wstring &version) {
    return !version.empty() && version.size() <= 64 &&
           version.find_first_not_of(L"0123456789abcdefghijklmnopqrstuvwxyz.-") == std::wstring::npos;
}
} // namespace intro

// SPDX-License-Identifier: MIT
#include "intro_policy.hpp"
#include <cassert>
#include <iostream>
int main() {
    using intro::shouldPresent;
    assert(shouldPresent(true, L"", L"0.6.2-alpha", true, false));
    assert(!shouldPresent(true, L"0.6.2-alpha", L"0.6.2-alpha", true, false));
    assert(shouldPresent(true, L"0.6.1-alpha", L"0.6.2-alpha", true, false));
    assert(!shouldPresent(false, L"0.6.1-alpha", L"0.6.2-alpha", true, false));
    assert(!shouldPresent(true, L"", L"0.6.2-alpha", false, false));
    assert(!shouldPresent(true, L"", L"0.6.2-alpha", true, true));
    assert(intro::validDigest(std::wstring(64, L'a')));
    assert(!intro::validDigest(std::wstring(64, L'A')));
    assert(!intro::validDigest(L"missing"));
    assert(intro::safeVersion(L"0.6.2-alpha"));
    for (auto bad : {L"../video", L"https://example.com", L"0.6.2/evil", L""})
        assert(!intro::safeVersion(bad));
    std::cout << "PASS: first launch, same version, update, opt-out, missing media, active simulator, digest and local filename policy\n";
}

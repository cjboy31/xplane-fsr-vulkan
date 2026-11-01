// SPDX-License-Identifier: MIT
// Fixture-only guard. A Vulkan layer name is not proof of which binary loaded.
#pragma once
#include "runtime_integrity.hpp"
#include <tlhelp32.h>
#include <cwctype>
#include <iostream>
namespace fixture {
inline void verifyLayerIdentity() {
    wchar_t expected[32768]{}, hash[65]{};
    if (!GetEnvironmentVariableW(L"XP_TEST_LAYER_PATH", expected, 32768) ||
        GetEnvironmentVariableW(L"XP_TEST_LAYER_SHA256", hash, 65) != 64)
        throw std::runtime_error("TEST_LAYER_EXPECTATION_MISSING");
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("TEST_LAYER_ENUMERATION_FAILED");
    struct Close { HANDLE value; ~Close() { CloseHandle(value); } } close{snapshot};
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    unsigned found = 0;
    if (!Module32FirstW(snapshot, &entry)) throw std::runtime_error("TEST_LAYER_ENUMERATION_FAILED");
    do {
        std::wstring name = entry.szModule;
        for (auto& ch : name) ch = wchar_t(towlower(ch));
        if (name != L"xpfsr24layer.dll" && name.rfind(L"native-aa-", 0) != 0) continue;
        ++found;
        const auto actual = std::filesystem::weakly_canonical(entry.szExePath);
        std::wcout << L"Tested layer=" << actual.wstring() << L'\n';
        if (CompareStringOrdinal(actual.c_str(), -1,
                std::filesystem::weakly_canonical(expected).c_str(), -1, TRUE) != CSTR_EQUAL)
            throw std::runtime_error("TEST_LAYER_PATH_MISMATCH");
        const auto digest = app::fileHash(actual);
        std::cout << "Tested layer SHA256=" << digest << '\n';
        std::string wanted;
        for (auto ch : std::wstring(hash)) wanted += char(towlower(ch));
        if (digest != wanted) throw std::runtime_error("TEST_LAYER_HASH_MISMATCH");
    } while (Module32NextW(snapshot, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) throw std::runtime_error("TEST_LAYER_ENUMERATION_FAILED");
    if (found != 1) throw std::runtime_error("TEST_LAYER_COUNT_MISMATCH");
}
} // namespace fixture

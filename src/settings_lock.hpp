// SPDX-License-Identifier: MIT
#pragma once
#include "named_lock.hpp"
#include <filesystem>
namespace launcher {
inline std::wstring settingsLockName(const std::filesystem::path &file){return app::lockName(std::filesystem::weakly_canonical(std::filesystem::absolute(file)).wstring());}
inline BOOL writeSetting(const wchar_t *section,const wchar_t *key,const wchar_t *value,const wchar_t *file){
    app::NamedLock lock(settingsLockName(file));return WritePrivateProfileStringW(section,key,value,file);
}
} // namespace launcher

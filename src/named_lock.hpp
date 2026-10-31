// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <cstdint>
#include <cwctype>
#include <stdexcept>
#include <string>
namespace app {
inline std::wstring lockName(const std::wstring &identity){
    uint64_t hash=14695981039346656037ull;
    for(wchar_t c:identity){auto v=uint16_t(std::towlower(c));hash^=v&255;hash*=1099511628211ull;hash^=v>>8;hash*=1099511628211ull;}
    return L"Local\\XPFSR24-settings-"+std::to_wstring(hash);
}
class NamedLock {
    HANDLE handle{};bool held{};
public:
    explicit NamedLock(const std::wstring &name,DWORD milliseconds=250){
        handle=CreateMutexW(nullptr,FALSE,name.c_str());
        if(!handle)throw std::runtime_error("Cannot open the settings writer lock");
        auto wait=WaitForSingleObject(handle,milliseconds);
        held=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;
        if(!held){CloseHandle(handle);handle=nullptr;throw std::runtime_error("Settings are busy; retry this change");}
    }
    NamedLock(const NamedLock&)=delete;NamedLock&operator=(const NamedLock&)=delete;
    ~NamedLock(){if(held)ReleaseMutex(handle);if(handle)CloseHandle(handle);}
};
} // namespace app

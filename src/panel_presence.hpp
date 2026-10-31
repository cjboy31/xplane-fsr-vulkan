// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <string>
namespace app {
struct PanelPresence {volatile LONG magic{},version{},pid{},visible{};};
inline std::wstring panelPresenceName(DWORD pid){return L"Local\\XPFSR24-Panel-v1-"+std::to_wstring(pid);}
class PanelPresenceChannel {
    HANDLE mapping{};PanelPresence *view{};DWORD process{};bool owner{};ULONGLONG lastAttempt{};
public:
    PanelPresenceChannel()=default;PanelPresenceChannel(const PanelPresenceChannel&)=delete;
    ~PanelPresenceChannel(){close();}
    void close(){if(view&&owner)InterlockedExchange(&view->visible,0);if(view)UnmapViewOfFile(view);if(mapping)CloseHandle(mapping);mapping=nullptr;view=nullptr;owner=false;}
    bool create(DWORD pid){close();auto name=panelPresenceName(pid);mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(PanelPresence),name.c_str());if(!mapping)return false;
        view=static_cast<PanelPresence*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(PanelPresence)));if(!view){close();return false;}owner=true;process=pid;
        InterlockedExchange(&view->visible,0);InterlockedExchange(&view->pid,LONG(pid));InterlockedExchange(&view->version,1);InterlockedExchange(&view->magic,0x58504650);return true;}
    void setVisible(bool visible){if(view&&owner)InterlockedExchange(&view->visible,visible?1:0);}
    bool visible(DWORD pid,ULONGLONG now=GetTickCount64()){
        if(process&&process!=pid)close();process=pid;
        if(!view){if(lastAttempt&&now-lastAttempt<500)return false;lastAttempt=now;auto name=panelPresenceName(pid);mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());if(!mapping)return false;
            view=static_cast<PanelPresence*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(PanelPresence)));if(!view){close();return false;}}
        // Aligned 32-bit volatile loads are atomic on supported Windows x64.
        // A read-only observer must not issue a write-capable interlocked opcode.
        MemoryBarrier();return view->magic==0x58504650&&view->version==1&&DWORD(view->pid)==pid&&view->visible==1;
    }
};
} // namespace app

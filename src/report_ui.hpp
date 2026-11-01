// SPDX-License-Identifier: MIT
#pragma once
#include "support_report.hpp"
#include <shellapi.h>
#include <thread>
namespace support_report {
inline std::wstring wide(const std::string& source){std::string s;for(char c:source){if(c=='\n'&&(s.empty()||s.back()!='\r'))s+='\r';s+=c;}int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring out(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
inline std::string text(HWND h){int n=GetWindowTextLengthW(h);graphics::require(n>=0&&n<=1200,"Report description is too long");std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);int length=WideCharToMultiByte(CP_UTF8,0,s.data(),n,nullptr,0,nullptr,nullptr);std::string out(length,0);if(length)WideCharToMultiByte(CP_UTF8,0,s.data(),n,out.data(),length,nullptr,nullptr);return out;}
struct ReportDialog{
    std::filesystem::path root;std::wstring token;std::string preview,result;
    HWND window{},destination{},description{},consent{},includeXPlane{},previewBox{},status{},sendButton{},queueButton{};std::string xplanePreview;std::vector<File> files;DWORD pid{};
    std::thread worker;bool busy{},closeAfter{};HFONT font{};
    void message(const std::string& s){SetWindowTextW(status,wide(s).c_str());}
    std::string selected(){return SendMessageW(destination,CB_GETCURSEL,0,0)==1?"bugs":"logs";}
    void begin(){
        if(busy)return;graphics::require(SendMessageW(consent,BM_GETCHECK,0,0)==BST_CHECKED,"Check the sharing agreement before sending or queuing a report.");
        const auto target=selected(),summary=text(description);graphics::require(summary.size()<=1000,"Keep the description under 1,000 characters");const bool include=SendMessageW(includeXPlane,BM_GETCHECK,0,0)==BST_CHECKED;const auto xp=include?xplanePreview:std::string{};
        if(worker.joinable())worker.join();busy=true;for(auto control:{sendButton,queueButton,destination,description,includeXPlane,consent})EnableWindow(control,FALSE);message("Sending the filtered copy. Your original log stays on this computer.");
        worker=std::thread([this,target,summary,xp,include]{try{send(root,token,preview,target,include&&xp.empty()?limitText(summary,900)+"\nX-Plane Log.txt omitted: session not verified.":summary,xp,include?files:std::vector<File>{});result="Sent. The receipt is in the local reporting status file. Your original logs are kept.";}catch(const std::exception& e){result=e.what();try{localStatus(root,token,std::string("Not confirmed. ")+result);}catch(...){}}PostMessageW(window,WM_APP+1,0,0);});
    }
};
inline LRESULT CALLBACK reportProcedure(HWND h,UINT msg,WPARAM w,LPARAM l){
    auto* d=reinterpret_cast<ReportDialog*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(msg==WM_NCCREATE){d=(ReportDialog*)((CREATESTRUCTW*)l)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)d);d->window=h;}
    if(!d)return DefWindowProcW(h,msg,w,l);
    if(msg==WM_APP+1){d->busy=false;d->message(d->result);for(auto control:{d->sendButton,d->queueButton,d->destination,d->description,d->includeXPlane,d->consent})EnableWindow(control,TRUE);SendMessageW(d->consent,BM_SETCHECK,BST_UNCHECKED,0);if(d->closeAfter)DestroyWindow(h);return 0;}
    if(msg==WM_CLOSE){if(d->busy){d->closeAfter=true;ShowWindow(h,SW_HIDE);}else DestroyWindow(h);return 0;}
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    if(msg==WM_COMMAND){try{switch(LOWORD(w)){
        case 1:d->begin();break;
        case 2:{graphics::require(!d->busy&&SendMessageW(d->consent,BM_GETCHECK,0,0)==BST_CHECKED,"Check the sharing agreement first.");HANDLE session=OpenProcess(SYNCHRONIZE,FALSE,d->pid);const bool running=session&&WaitForSingleObject(session,0)==WAIT_TIMEOUT;if(session)CloseHandle(session);graphics::require(running,"This session has ended. Use Send report now or Save bundle instead.");queue(d->root,d->token,d->selected(),text(d->description),SendMessageW(d->includeXPlane,BM_GETCHECK,0,0)==BST_CHECKED);d->message("Queued for this session only. Log.txt will be checked again after exit if selected. Cancel automatic report turns this Off before exit.");break;}
        case 3:graphics::require(!d->busy,"A report is already sending. Wait for its result.");cancel(d->root,d->token);d->message("Automatic reporting is Off. Your local log is kept.");break;
        case 4:SendMessageW(h,WM_CLOSE,0,0);break;
        case 5:{const bool include=SendMessageW(d->includeXPlane,BM_GETCHECK,0,0)==BST_CHECKED;const auto saved=saveBundle(d->root,d->token,d->preview,include?d->xplanePreview:std::string{},include?d->files:std::vector<File>{});ShellExecuteW(h,L"open",saved.c_str(),nullptr,nullptr,SW_SHOWNORMAL);ShellExecuteW(h,L"open",site,nullptr,nullptr,SW_SHOWNORMAL);d->message("Filtered files saved under X-Plane / Output / FSR-Logs / reports. Choose those files on the report website.");break;}
        case 6:{auto preview=d->preview;if(SendMessageW(d->includeXPlane,BM_GETCHECK,0,0)==BST_CHECKED){preview+="\n\n--- X-Plane Log.txt ---\n"+(d->xplanePreview.empty()?"Not verified yet; checked again after exit.":d->xplanePreview);for(const auto& f:d->files)preview+="\n\n--- "+f.name+" ---\n"+redact(f.text);}SetWindowTextW(d->previewBox,wide(preview).c_str());SendMessageW(d->consent,BM_SETCHECK,BST_UNCHECKED,0);break;}
        case 7:if(HIWORD(w)==CBN_SELCHANGE)SendMessageW(d->consent,BM_SETCHECK,BST_UNCHECKED,0);break;
        case 8:if(HIWORD(w)==EN_CHANGE)SendMessageW(d->consent,BM_SETCHECK,BST_UNCHECKED,0);break;
    }}catch(const std::exception& e){d->message(e.what());}return 0;}
    return DefWindowProcW(h,msg,w,l);
}
inline void show(const std::filesystem::path& root,DWORD pid,const std::wstring& token){
    graphics::require(pid&&app::validSessionToken(token),"Invalid reporting session");
    const auto mutexName=L"Local\\XPFSRReport-"+token;
    HANDLE mutex=CreateMutexW(nullptr,TRUE,mutexName.c_str());const auto error=GetLastError();
    graphics::require(mutex!=nullptr,"Report window lock unavailable");
    struct Release{HANDLE h;~Release(){ReleaseMutex(h);CloseHandle(h);}}release{mutex};
    if(error==ERROR_ALREADY_EXISTS){MessageBoxW(nullptr,L"The report window for this session is already open. Check the taskbar.",L"X-Plane diagnostic report",MB_OK|MB_ICONINFORMATION);return;}
    const auto p=launcher::load(root/L"settings.ini");HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);graphics::require(process!=nullptr,"This simulator session has ended. Use the report website or its exported log.");
    wchar_t exe[32768]{};DWORD size=32768;const bool exact=QueryFullProcessImageNameW(process,0,exe,&size)&&app::exactSimulator(std::filesystem::path(std::wstring(exe,size)),p);CloseHandle(process);graphics::require(exact,"Report request is not from the selected simulator");
    ReportDialog d;d.root=root;d.token=token;d.pid=pid;d.preview=currentLog(root,token);d.xplanePreview=xplaneLog(root,pid);d.files=diagnosticFiles(root,token,pid);
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=reportProcedure;cls.lpszClassName=L"XPFSRSupportReport";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&cls);
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,L"Share renderer log — X-Plane community support",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,880,760,nullptr,nullptr,cls.hInstance,&d);graphics::require(window!=nullptr,"Report window could not open");
    d.font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto control=[&](const wchar_t* type,const wchar_t* title,DWORD style,int x,int y,int width,int height,int id=0){auto h=CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,(HMENU)(INT_PTR)id,cls.hInstance,nullptr);SendMessageW(h,WM_SETFONT,(WPARAM)d.font,TRUE);return h;};
    control(L"STATIC",L"Share a renderer log",0,24,18,800,25);
    control(L"STATIC",L"A filtered copy goes to the selected community Discord channel. People with access to that channel can read it. Check the preview: filtering cannot find every private detail. Original preferences, models and DLLs stay on your PC.",0,24,50,810,48);
    control(L"STATIC",L"Destination",0,24,106,90,20);d.destination=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,120,102,710,150,7);
    SendMessageW(d.destination,CB_ADDSTRING,0,(LPARAM)L"#render-logs — share a diagnostic log");SendMessageW(d.destination,CB_ADDSTRING,0,(LPARAM)L"#bug-reports — report a problem");SendMessageW(d.destination,CB_SETCURSEL,0,0);
    control(L"STATIC",L"What happened? Optional — leave personal information out.",0,24,142,800,20);d.description=control(L"EDIT",L"",WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,24,166,806,64,8);SendMessageW(d.description,EM_SETLIMITTEXT,1000,0);
    d.includeXPlane=control(L"BUTTON",L"Also include Log.txt and available support diagnostics (preview lists each text file)",BS_AUTOCHECKBOX|WS_TABSTOP,24,236,806,26,6);
    d.previewBox=control(L"EDIT",wide(d.preview).c_str(),WS_BORDER|ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_HSCROLL|ES_AUTOHSCROLL,24,268,806,260);
    d.consent=control(L"BUTTON",L"I checked the preview and agree to share the selected logs and description in this channel.",BS_AUTOCHECKBOX|WS_TABSTOP,24,542,810,26);
    d.sendButton=control(L"BUTTON",L"Send report now",WS_TABSTOP|BS_DEFPUSHBUTTON,24,582,170,34,1);d.queueButton=control(L"BUTTON",L"Send after this session",WS_TABSTOP,204,582,200,34,2);
    control(L"BUTTON",L"Cancel automatic report",WS_TABSTOP,414,582,220,34,3);control(L"BUTTON",L"Save bundle / website",WS_TABSTOP,644,582,186,34,5);
    d.status=control(L"STATIC",L"Reporting starts Off. Sending does not change image settings or interrupt the flight.",0,24,634,640,68);control(L"BUTTON",L"Close",WS_TABSTOP,700,652,130,34,4);
    ShowWindow(window,SW_SHOWNORMAL);UpdateWindow(window);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(d.worker.joinable())d.worker.join();
}
}

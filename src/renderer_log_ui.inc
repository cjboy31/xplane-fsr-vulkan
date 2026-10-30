// SPDX-License-Identifier: MIT
// Bounded, read-only live view; does not keep a stale editor tab or lock the log.
static HWND rendererLogWindow{},rendererLogText{},rendererLogState{},rendererLogFollow{};
static std::string rendererLogSnapshot;
static void refreshRendererLog(){
    if(!IsWindow(rendererLogWindow))return;
    const auto path=appDirectory/L"logs/renderer.log";
    const auto token=running()?activeSessionToken:std::string{};
    auto next=session::logTail(path,token);
    if(next!=rendererLogSnapshot){
        DWORD begin{},end{};SendMessageW(rendererLogText,EM_GETSEL,WPARAM(&begin),LPARAM(&end));
        int first=int(SendMessageW(rendererLogText,EM_GETFIRSTVISIBLELINE,0,0));
        SetWindowTextW(rendererLogText,wide(next.c_str()).c_str());rendererLogSnapshot=std::move(next);
        if(SendMessageW(rendererLogFollow,BM_GETCHECK,0,0)==BST_CHECKED){SendMessageW(rendererLogText,EM_SETSEL,WPARAM(-1),-1);SendMessageW(rendererLogText,EM_SCROLLCARET,0,0);}
        else{SendMessageW(rendererLogText,EM_SETSEL,begin,end);SendMessageW(rendererLogText,EM_LINESCROLL,0,first);}
    }
    auto label=(running()?L"Current simulator session · Refreshes every second":L"Last recorded session · No simulator monitored")+
        std::wstring(L"\r\n")+path.wstring();SetWindowTextW(rendererLogState,label.c_str());
}
static void rendererLogLayout(){
    RECT r;GetClientRect(rendererLogWindow,&r);theme::Layout layout;
    layout.place(rendererLogState,theme::px(20),theme::px(16),r.right-theme::px(40),theme::px(50));
    layout.place(rendererLogText,theme::px(20),theme::px(76),r.right-theme::px(40),std::max(theme::px(60),int(r.bottom)-theme::px(146)));
    layout.place(rendererLogFollow,theme::px(20),r.bottom-theme::px(52),theme::px(170),theme::px(32));
    layout.place(GetDlgItem(rendererLogWindow,902),r.right-theme::px(420),r.bottom-theme::px(52),theme::px(112),theme::px(32));
    layout.place(GetDlgItem(rendererLogWindow,903),r.right-theme::px(296),r.bottom-theme::px(52),theme::px(152),theme::px(32));
    layout.place(GetDlgItem(rendererLogWindow,IDCANCEL),r.right-theme::px(132),r.bottom-theme::px(52),theme::px(112),theme::px(32));layout.commit(rendererLogWindow);
}
static LRESULT CALLBACK rendererLogProcedure(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_SIZE:rendererLogLayout();return 0;
    case WM_GETMINMAXINFO:{auto*p=reinterpret_cast<MINMAXINFO*>(l);p->ptMinTrackSize={theme::px(720),theme::px(350)};return 0;}
    case WM_TIMER:refreshRendererLog();return 0;
    case WM_COMMAND:if(LOWORD(w)==IDCANCEL){DestroyWindow(h);return 0;}
        if(LOWORD(w)==902||LOWORD(w)==901){refreshRendererLog();return 0;}
        if(LOWORD(w)==903){ShellExecuteW(h,L"open",(appDirectory/L"logs").c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;}break;
    case WM_DRAWITEM:return theme::drawItem(reinterpret_cast<DRAWITEMSTRUCT*>(l));
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORBTN:return theme::colors(m,w,l);
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:KillTimer(h,1);rendererLogWindow=rendererLogText=rendererLogState=rendererLogFollow=nullptr;rendererLogSnapshot.clear();return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
static void showRendererLog(){
    if(IsWindow(rendererLogWindow)){ShowWindow(rendererLogWindow,SW_RESTORE);SetForegroundWindow(rendererLogWindow);refreshRendererLog();return;}
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=rendererLogProcedure;cls.lpszClassName=L"XPFSRLiveRendererLog";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=theme::cardBrush;RegisterClassW(&cls);
    auto size=modalSize(1100,760);
    rendererLogWindow=CreateWindowExW(WS_EX_COMPOSITED|WS_EX_CONTROLPARENT,cls.lpszClassName,L"Live renderer log — FSR 3/4 + Frame Gen",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,size.cx,size.cy,window,nullptr,cls.hInstance,nullptr);
    ensure(rendererLogWindow!=nullptr,"Cannot open the renderer log viewer");theme::caption(rendererLogWindow);
    RECT owner{},bounds{};GetWindowRect(window,&owner);GetWindowRect(rendererLogWindow,&bounds);
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    const int x=std::clamp(int(owner.left+(owner.right-owner.left-width)/2),int(monitor.rcWork.left),int(std::max(monitor.rcWork.left,monitor.rcWork.right-width)));
    const int y=std::clamp(int(owner.top+(owner.bottom-owner.top-height)/2),int(monitor.rcWork.top),int(std::max(monitor.rcWork.top,monitor.rcWork.bottom-height)));
    SetWindowPos(rendererLogWindow,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER);
    rendererLogState=control(L"STATIC",L"",SS_NOPREFIX,0,0,1,1,900,rendererLogWindow);
    rendererLogText=control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|ES_AUTOHSCROLL|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP,0,0,1,1,904,rendererLogWindow);
    SendMessageW(rendererLogText,EM_SETLIMITTEXT,1024*1024,0);theme::name(rendererLogText,L"Live renderer log contents");
    rendererLogFollow=control(L"BUTTON",L"Follow latest",BS_AUTOCHECKBOX|WS_TABSTOP,0,0,1,1,901,rendererLogWindow);SendMessageW(rendererLogFollow,BM_SETCHECK,BST_CHECKED,0);
    control(L"BUTTON",L"Refresh",BS_PUSHBUTTON|WS_TABSTOP,0,0,1,1,902,rendererLogWindow);control(L"BUTTON",L"Open log folder",BS_PUSHBUTTON|WS_TABSTOP,0,0,1,1,903,rendererLogWindow);control(L"BUTTON",L"Close",BS_PUSHBUTTON|WS_TABSTOP,0,0,1,1,IDCANCEL,rendererLogWindow);
    rendererLogLayout();refreshRendererLog();SetTimer(rendererLogWindow,1,1000,nullptr);ShowWindow(rendererLogWindow,SW_SHOW);
}

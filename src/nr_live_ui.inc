// SPDX-License-Identifier: MIT
// Live appearance controls and asynchronous model budgets; model import stays offline.
static HWND nrBody{},nrLiveLabel{},nrBudget{},nrTooltip{};
static HWND nrFields[xpfsr::nr::Count]{},nrValues[xpfsr::nr::Count]{};
struct NRWidget { HWND handle;int x,y,width,height; };
static std::vector<NRWidget> nrWidgets;
static int nrScroll{};static bool nrSyncing{};
static int nrWheelRemainder{};
static void nrLayout(){
    if(!IsWindow(nrWindow)||!nrBody)return;
    RECT r;GetClientRect(nrWindow,&r);int page=std::max(1,int(r.bottom)-theme::px(78));
    int limit=std::max(0,theme::px(814)-page);nrScroll=std::clamp(nrScroll,0,limit);
    theme::Layout moves;moves.place(nrBody,0,0,r.right,page);
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,theme::px(814)-1,UINT(page),nrScroll,0};SetScrollInfo(nrWindow,SB_VERT,&info,FALSE);
    for(auto&w:nrWidgets)moves.place(w.handle,theme::px(w.x),theme::px(w.y)-nrScroll,theme::px(w.width),theme::px(w.height));
    auto place=[&](int id,int x,int width){moves.place(GetDlgItem(nrWindow,id),theme::px(x),r.bottom-theme::px(56),theme::px(width),theme::px(34));};
    place(162,24,218);place(163,254,164);place(165,430,164);place(164,606,150);
    moves.commit(nrWindow);
}
static void nrScrollTo(int next){
    RECT r;GetClientRect(nrWindow,&r);
    int limit=std::max(0,theme::px(814)-std::max(1,int(r.bottom)-theme::px(78)));
    next=std::clamp(next,0,limit);if(next==nrScroll)return;nrScroll=next;nrLayout();
}
static HWND nrWidget(const wchar_t*type,const wchar_t*label,DWORD style,int x,int y,int width,int height,int id=0){
    auto h=control(type,label,style,0,0,1,1,id,nrBody);nrWidgets.push_back({h,x,y,width,height});return h;
}
static void nrFieldHelp(xpfsr::nr::Field field,int x,int y){
    auto h=nrWidget(L"BUTTON",L"?",BS_PUSHBUTTON|WS_TABSTOP,x,y,24,24,700+field);
    theme::name(h,(std::wstring(L"Help: ")+xpfsr::nr::settings[field].label).c_str());
    TOOLINFOW tool{sizeof(tool)};tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tool.hwnd=nrBody;tool.uId=UINT_PTR(h);tool.lpszText=const_cast<wchar_t*>(xpfsr::nr::settings[field].help);SendMessageW(nrTooltip,TTM_ADDTOOLW,0,LPARAM(&tool));
}
static const xpfsr::nr::Field nrSliders[]={xpfsr::nr::Intensity,xpfsr::nr::Tone,xpfsr::nr::Structure,xpfsr::nr::Skin,xpfsr::nr::Colour,xpfsr::nr::Detail,xpfsr::nr::Highlight,xpfsr::nr::History};
static void nrSyncFields(){
    nrSyncing=true;auto&c=preferences.nr;
    for(auto f:nrSliders){if(nrFields[f])SendMessageW(nrFields[f],TBM_SETPOS,TRUE,c[f]);if(nrValues[f])SetWindowTextW(nrValues[f],f==xpfsr::nr::Skin&&c[f]<0?L"Auto":(std::to_wstring(c[f])+L"%").c_str());}
    for(auto f:{xpfsr::nr::Style,xpfsr::nr::Transfer,xpfsr::nr::Scaler,xpfsr::nr::Model,xpfsr::nr::Scope,xpfsr::nr::Order})if(nrFields[f])SendMessageW(nrFields[f],CB_SETCURSEL,c[f],0);
    if(nrFields[xpfsr::nr::Passes])SendMessageW(nrFields[xpfsr::nr::Passes],CB_SETCURSEL,c[xpfsr::nr::Passes]-1,0);
    int scales[]={25,33,50,67,100};for(int i=0;i<5;++i)if(c[xpfsr::nr::Scale]==scales[i])SendMessageW(nrFields[xpfsr::nr::Scale],CB_SETCURSEL,i,0);
    for(auto f:{xpfsr::nr::Enabled,xpfsr::nr::Mask,xpfsr::nr::Grade,xpfsr::nr::AllowFG})if(nrFields[f])SendMessageW(nrFields[f],BM_SETCHECK,(f==xpfsr::nr::Enabled?preferences.neuralRendering!=0:c[f]!=0)?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(nrBudget,CB_SETCURSEL,c[xpfsr::nr::Scale]==33?0:c[xpfsr::nr::Scale]==50?1:c[xpfsr::nr::Scale]==67?2:3,0);
    nrSyncing=false;
}
static void nrChangeSetting(int id){
    if(nrSyncing)return;
    using namespace xpfsr::nr;
    auto c=preferences.nr;c[Enabled]=preferences.neuralRendering!=0;
    if(id==190){int profile=int(SendMessageW(nrBudget,CB_GETCURSEL,0,0));if(profile<3)performanceProfile(c,profile);else return;}
    else if(id==192){const bool on=c[Enabled],allow=c[AllowFG];c=Controls{};c[Enabled]=on;c[AllowFG]=allow&&running()&&launchedFG;}
    else{
        int field=id-170;if(field<0||field>=Count)return;auto f=Field(field);
        bool slider=false;for(auto s:nrSliders)slider|=s==f;
        if(slider)c[f]=int(SendMessageW(nrFields[f],TBM_GETPOS,0,0));
        else if(f==Enabled||f==Mask||f==Grade||f==AllowFG)c[f]=SendMessageW(nrFields[f],BM_GETCHECK,0,0)==BST_CHECKED;
        else{int selected=int(SendMessageW(nrFields[f],CB_GETCURSEL,0,0));if(f==Scale){int scales[]={25,33,50,67,100};ensure(selected>=0&&selected<5,"Invalid model scale");c[f]=scales[selected];}else c[f]=selected+(f==Passes?1:0);}
    }
    ensure(c.valid(),"Invalid NR settings");
    ensure(!c[Enabled]||!running()||(!launchedNative&&launchedMode!=0),"This session has no FSR reconstruction path for NR. Launch with an FSR Native AA or upscaling mode to use NR live.");
    ensure(!c[Enabled]||!running()||!launchedFG||c[AllowFG],"Enable the experimental NR + FG option before turning NR On in an FG session.");
    preferences.nr=c;SendMessageW(neuralBox,CB_SETCURSEL,c[Enabled]?4:0,0);
    if(!running()&&c[Enabled]&&!c[AllowFG])SendMessageW(fgBox,CB_SETCURSEL,0,0);
    saveChoices();nrSyncFields();
}
static LRESULT CALLBACK nrBodyProcedure(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_COMMAND||m==WM_HSCROLL||m==WM_DRAWITEM||m==WM_MEASUREITEM||m==WM_MOUSEWHEEL||m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT||m==WM_CTLCOLORBTN)return SendMessageW(GetParent(h),m,w,l);
    return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK nrProcedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_TIMER:
        nrFinishJob();if(nrLiveLabel)SetWindowTextW(nrLiveLabel,nrLiveStatus().c_str());return 0;
    case WM_SIZE:nrLayout();return 0;
    case WM_MOUSEWHEEL:nrWheelRemainder+=GET_WHEEL_DELTA_WPARAM(w);if(int steps=nrWheelRemainder/WHEEL_DELTA){nrWheelRemainder%=WHEEL_DELTA;nrScrollTo(nrScroll-steps*theme::px(64));}return 0;
    case WM_VSCROLL:{SCROLLINFO i{sizeof(i),SIF_ALL};GetScrollInfo(hwnd,SB_VERT,&i);int next=nrScroll;switch(LOWORD(w)){case SB_THUMBTRACK:next=i.nTrackPos;break;case SB_LINEUP:next-=theme::px(32);break;case SB_LINEDOWN:next+=theme::px(32);break;case SB_PAGEUP:next-=int(i.nPage);break;case SB_PAGEDOWN:next+=int(i.nPage);break;case SB_TOP:next=0;break;case SB_BOTTOM:next=theme::px(814);break;}nrScrollTo(next);return 0;}
    case WM_DRAWITEM:return theme::drawItem(reinterpret_cast<DRAWITEMSTRUCT*>(l));
    case WM_MEASUREITEM:if(auto*i=reinterpret_cast<MEASUREITEMSTRUCT*>(l);i->CtlType==ODT_COMBOBOX){i->itemHeight=theme::px(28);return TRUE;}break;
    case WM_HSCROLL:try{nrChangeSetting(GetDlgCtrlID(reinterpret_cast<HWND>(l)));}catch(const std::exception&e){nrSyncFields();MessageBoxW(hwnd,wide(e.what()).c_str(),L"NR settings",MB_OK|MB_ICONINFORMATION);}return 0;
    case WM_COMMAND:
        try{
            int id=LOWORD(w),event=HIWORD(w);
            if(id>=700&&id<700+xpfsr::nr::Count){auto&s=xpfsr::nr::settings[id-700];modernHelp(hwnd,s.label,s.help);}
            else if(id==162)nrImportModel();
            else if(id==163)nrStartJob(appDirectory/L"nr/NRCheck.exe",quote((appDirectory/L"nr").wstring())+L" "+quote((appDirectory/L"nr-check").wstring()),false);
            else if(id==164||id==IDCANCEL)DestroyWindow(hwnd);
            else if(id==165)modernHelp(hwnd,L"NR setup and live controls",nrSetupText().c_str());
            else if(id==192||((id>=170&&id<170+xpfsr::nr::Count)||id==190)&&(event==CBN_SELCHANGE||event==BN_CLICKED))nrChangeSetting(id);
        }catch(const std::exception&e){nrSyncFields();MessageBoxW(hwnd,wide(e.what()).c_str(),L"Neural rendering",MB_OK|MB_ICONINFORMATION);}
        return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORBTN:return theme::colors(message,w,l);
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:KillTimer(hwnd,1);nrWindow=nullptr;nrBody=nullptr;nrLiveLabel=nullptr;nrWidgets.clear();EnableWindow(window,TRUE);SetActiveWindow(window);return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
static void showNRSetup(){
    if(IsWindow(nrWindow)){SetForegroundWindow(nrWindow);return;}
    INITCOMMONCONTROLSEX init{sizeof(init),ICC_BAR_CLASSES};InitCommonControlsEx(&init);
    WNDCLASSW cls{};cls.lpfnWndProc=nrProcedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"XPFSRLiveNRSettings";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=theme::cardBrush;RegisterClassW(&cls);
    nrWindow=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_COMPOSITED,cls.lpszClassName,L"Neural rendering — Live settings (Experimental)",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VSCROLL|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,modalSize(920,860).cx,modalSize(920,860).cy,window,nullptr,cls.hInstance,nullptr);
    ensure(nrWindow!=nullptr,"Cannot open NR settings");theme::caption(nrWindow);
    RECT owner{},bounds{};GetWindowRect(window,&owner);GetWindowRect(nrWindow,&bounds);
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
    int modalWidth=bounds.right-bounds.left,modalHeight=bounds.bottom-bounds.top;
    int modalX=std::clamp(int(owner.left+(owner.right-owner.left-modalWidth)/2),int(monitor.rcWork.left),int(std::max(monitor.rcWork.left,monitor.rcWork.right-modalWidth)));
    int modalY=std::clamp(int(owner.top+(owner.bottom-owner.top-modalHeight)/2),int(monitor.rcWork.top),int(std::max(monitor.rcWork.top,monitor.rcWork.bottom-modalHeight)));
    SetWindowPos(nrWindow,nullptr,modalX,modalY,0,0,SWP_NOSIZE|SWP_NOZORDER);
    cls.lpfnWndProc=nrBodyProcedure;cls.lpszClassName=L"XPFSRLiveNRBody";RegisterClassW(&cls);
    nrBody=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"Live NR controls",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1,1,nrWindow,nullptr,cls.hInstance,nullptr);
    nrWidgets.clear();nrScroll=0;nrWheelRemainder=0;std::fill(std::begin(nrFields),std::end(nrFields),nullptr);std::fill(std::begin(nrValues),std::end(nrValues),nullptr);
    RECT r;GetClientRect(nrWindow,&r);int width=MulDiv(r.right,96,theme::dpi),half=(width-72)/2;
    nrTooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,nrWindow,nullptr,cls.hInstance,nullptr);SendMessageW(nrTooltip,TTM_SETMAXTIPWIDTH,0,440);
    nrWidget(L"STATIC",L"Adjust live. Profiles/models prepare in the background; FSR continues.",0,24,18,width-48,30);
    nrLiveLabel=nrWidget(L"STATIC",nrLiveStatus().c_str(),0,24,52,width-48,44);
    nrText=nrWidget(L"EDIT",nrModelReady()?L"Local model verified. Start with Subtle strength and one pass.":L"Import your own model before the first flight. See Setup help.",ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP,24,102,half,60,161);
    nrWidget(L"STATIC",L"Scene scope",0,48+half,100,half-32,21);nrFieldHelp(xpfsr::nr::Scope,24+2*half,97);
    nrFields[xpfsr::nr::Scope]=nrWidget(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,48+half,125,half,160,170+xpfsr::nr::Scope);
    for(auto label:{L"External views only — Default",L"Full scene — Cockpit + exterior"})SendMessageW(nrFields[xpfsr::nr::Scope],CB_ADDSTRING,0,LPARAM(label));theme::name(nrFields[xpfsr::nr::Scope],L"Scene scope");
    nrWidget(L"STATIC",L"Processing order",0,24,176,half-32,21);nrFieldHelp(xpfsr::nr::Order,24+half-24,173);
    nrFields[xpfsr::nr::Order]=nrWidget(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,24,201,half,180,170+xpfsr::nr::Order);
    for(auto label:{L"Before upscaling — Recommended",L"After upscaling — Higher GPU cost"})SendMessageW(nrFields[xpfsr::nr::Order],CB_ADDSTRING,0,LPARAM(label));theme::name(nrFields[xpfsr::nr::Order],L"Processing order");
    nrWidget(L"STATIC",L"Saved automatically. Changes apply live when ready.",0,48+half,201,half,38);
    for(int i=0;i<8;++i){auto f=nrSliders[i];auto&s=xpfsr::nr::settings[f];int x=24+(i%2)*(half+24),y=246+(i/2)*54;
        nrWidget(L"STATIC",s.label,0,x,y,half-106,21);nrValues[f]=nrWidget(L"STATIC",L"",0,x+half-100,y,64,21);nrFieldHelp(f,x+half-24,y-3);
        nrFields[f]=nrWidget(TRACKBAR_CLASSW,L"",TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,x,y+22,half,26,170+f);SendMessageW(nrFields[f],TBM_SETRANGEMIN,FALSE,s.minimum);SendMessageW(nrFields[f],TBM_SETRANGEMAX,FALSE,s.maximum);theme::name(nrFields[f],s.label);
    }
    auto combo=[&](xpfsr::nr::Field f,int position,std::initializer_list<const wchar_t*>labels){int x=24+(position%2)*(half+24),y=470+(position/2)*62;nrWidget(L"STATIC",xpfsr::nr::settings[f].label,0,x,y,half-32,21);nrFieldHelp(f,x+half-24,y-3);nrFields[f]=nrWidget(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,x,y+23,half,220,170+f);for(auto label:labels)SendMessageW(nrFields[f],CB_ADDSTRING,0,LPARAM(label));theme::name(nrFields[f],xpfsr::nr::settings[f].label);};
    combo(xpfsr::nr::Scale,0,{L"25% — Lowest network cost",L"33% — Performance",L"50% — Balanced",L"67% — Quality",L"100% — Full input / highest cost"});
    combo(xpfsr::nr::Model,1,{L"Auto — Prefer small model",L"Small FP8",L"Original FP8"});
    combo(xpfsr::nr::Style,2,{L"Standard",L"Natural",L"Cinematic"});combo(xpfsr::nr::Passes,3,{L"1 — Recommended for flight",L"2 — Double network work",L"3 — Triple network work"});
    combo(xpfsr::nr::Transfer,4,{L"Matched residual — Default",L"Edge-aware lighting / color",L"Classic enlargement"});combo(xpfsr::nr::Scaler,5,{L"Bilinear",L"Catmull-Rom",L"Lanczos3",L"FSR 1 EASU"});
    nrWidget(L"STATIC",L"Performance profile (retains artistic strengths)",0,24,660,half-20,21);
    nrBudget=nrWidget(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,24,684,half,180,190);for(auto label:{L"Performance — 33%, small/auto, one pass",L"Balanced — 50%, small/auto, one pass",L"Quality — 67%, small/auto, one pass",L"Custom model budget"})SendMessageW(nrBudget,CB_ADDSTRING,0,LPARAM(label));
    nrWidget(L"BUTTON",L"Reset recommended NR settings",BS_PUSHBUTTON|WS_TABSTOP,48+half,684,half,32,192);
    int i=0;for(auto f:{xpfsr::nr::Enabled,xpfsr::nr::Mask,xpfsr::nr::Grade,xpfsr::nr::AllowFG}){int x=24+(i%2)*(half+24),y=736+(i/2)*34;nrFields[f]=nrWidget(L"BUTTON",xpfsr::nr::settings[f].label,BS_AUTOCHECKBOX|WS_TABSTOP,x,y,half-32,28,170+f);nrFieldHelp(f,x+half-24,y);++i;}
    nrImportButton=control(L"BUTTON",L"Import model DLL...",BS_PUSHBUTTON|WS_TABSTOP,0,0,1,1,162,nrWindow);nrCheckButton=control(L"BUTTON",L"Run NR check",BS_PUSHBUTTON|WS_TABSTOP,0,0,1,1,163,nrWindow);
    control(L"BUTTON",L"Setup help",BS_PUSHBUTTON|WS_TABSTOP,0,0,1,1,165,nrWindow);control(L"BUTTON",L"Close",BS_PUSHBUTTON|WS_TABSTOP,0,0,1,1,164,nrWindow);
    nrSyncFields();EnableWindow(nrImportButton,!nrJobRunning()&&!running());EnableWindow(nrCheckButton,nrModelReady()&&!running()&&!nrJobRunning());nrLayout();EnableWindow(window,FALSE);SetTimer(nrWindow,1,500,nullptr);ShowWindow(nrWindow,SW_SHOW);SetFocus(nrBody);
}

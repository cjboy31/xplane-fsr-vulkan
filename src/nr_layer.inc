// SPDX-License-Identifier: MIT
// Live settings use separate IPC; camera/plugin ABI remains unchanged.
static bool neuralSessionCapable(){
    wchar_t v[256]{};return GetEnvironmentVariableW(L"XPFSR24_NR_CONTROL",v,256)>0;
}
static int requestedNeuralRendering(){
    wchar_t v[16]{};GetEnvironmentVariableW(L"XPFSR24_NR",v,16);
    return v[0]>=L'1'&&v[0]<=L'4'&&!v[1]?int(v[0]-L'0'):0;
}
static bool allowNeuralFrameGeneration(){wchar_t v[4]{};GetEnvironmentVariableW(L"XPFSR24_NR_ALLOW_FG",v,4);return v[0]==L'1'&&!v[1];}
class NeuralRenderer {
    using Controls=xpfsr::nr::Controls;using Key=std::array<uint32_t,7>;
    struct Backend {
        HMODULE module{};xpfsr::nr::Create create{};xpfsr::nr::Record record{};xpfsr::nr::Destroy destroy{};
        ~Backend(){if(module)FreeLibrary(module);}
    };
    struct Session {
        std::shared_ptr<Backend> backend;void*context{};Key key{};
        ~Session(){if(context){SdkScope scope;backend->destroy(context);}}
    };
    struct Job {xpfsr::nr::Host host;uint32_t w,h;VkFormat format;Controls controls;Key key;};
    xpfsr::nr::ControlChannel channel;Controls controls;LONG revision=-1;
    std::shared_ptr<Backend> backend;
    bool featureSupport{},frameFailed{},processingAllowed=true,interrupted=true,resetHistory=true;
    std::mutex messageLock;
    std::string message="NR requested; waiting for an eligible scene";
    std::string processingPauseReason="FSR processing disabled";
    xpfsr::nr::ProfileSessions<Job,Session> sessions{
        [this](const Job&job){return prepare(job);},[this](std::string text){note(std::move(text));}};
    uint64_t lastRecord{},lastCamera{},records{},lastReport{};
    static Key makeKey(uint32_t w,uint32_t h,VkFormat f,const Controls&c){return {w,h,uint32_t(f),uint32_t(c[xpfsr::nr::Scale]),uint32_t(c[xpfsr::nr::Model]),uint32_t(c[xpfsr::nr::Order]),uint32_t(c[xpfsr::nr::Passes])};}
    void note(std::string text){std::lock_guard<std::mutex> lock(messageLock);message=std::move(text);}
    std::shared_ptr<Session> prepare(const Job&work){
        SdkScope scope;
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
                wchar_t root[32768]{};GetEnvironmentVariableW(L"XPFSR24_NR_ROOT",root,32768);
                std::filesystem::path path=root;
                if(!root[0]||std::filesystem::exists(path/L"vulkan-1.dll"))throw std::runtime_error("Invalid NR folder or conflicting loader proxy");
                if(!backend){
                    auto b=std::make_shared<Backend>();
                    b->module=LoadLibraryExW((path/L"XPFSRNeuralRendering.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
                    if(!b->module)throw std::runtime_error("NR backend missing or cannot load");
                    b->create=reinterpret_cast<xpfsr::nr::Create>(GetProcAddress(b->module,"XPFSR_NR_Create"));
                    b->record=reinterpret_cast<xpfsr::nr::Record>(GetProcAddress(b->module,"XPFSR_NR_Record"));
                    b->destroy=reinterpret_cast<xpfsr::nr::Destroy>(GetProcAddress(b->module,"XPFSR_NR_Destroy"));
                    if(!b->create||!b->record||!b->destroy)throw std::runtime_error("NR backend ABI exports missing");
                    backend=std::move(b);
                }
                xpfsr::nr::Config config{};config.root=root;config.width=work.w;config.height=work.h;config.format=work.format;config.controls=work.controls;
                auto result=std::make_shared<Session>();result->backend=backend;result->key=work.key;
                char why[512]{};result->context=backend->create(&work.host,&config,why,sizeof(why));
                if(!result->context)throw std::runtime_error(why[0]?why:"NR initialization failed");
                return result;
    }
public:
    void features(bool supported){
        featureSupport=supported;wchar_t name[256]{};GetEnvironmentVariableW(L"XPFSR24_NR_CONTROL",name,256);
        if(neuralSessionCapable())channel.open(name);poll();
    }
    void poll(){
        Controls next;LONG nextRevision=revision;
        if(channel.read(next,&nextRevision)&&nextRevision!=revision){
            if(next!=controls){resetHistory=true;interrupted=true;
                if(!controls[xpfsr::nr::Enabled]&&next[xpfsr::nr::Enabled]){
                    if(frameFailed){
                        frameFailed=false;
                    }
                }
                controls=next;
            }revision=nextRevision;
        }
        sessions.enable(processingAllowed&&featureSupport&&!frameFailed&&controls[xpfsr::nr::Enabled]&&controls[xpfsr::nr::Intensity]);
        if(!controls[xpfsr::nr::Enabled]||!controls[xpfsr::nr::Intensity])note(sessions.released()?"NR Off: model GPU resources released; original FSR input retained":"NR Off: original FSR input retained; releasing model resources after pending GPU owners finish");
        else if(!processingAllowed)note("NR paused: "+processingPauseReason+(sessions.released()?"; model GPU resources released":"; releasing model resources after pending GPU owners finish"));
        else if(!featureSupport)note("NR unavailable: required Vulkan features were not enabled");
    }
    bool enabled(){poll();return processingAllowed&&featureSupport&&!frameFailed&&controls[xpfsr::nr::Enabled]&&controls[xpfsr::nr::Intensity];}
    bool retiring()const{return sessions.retiring();}
    void processingEnabled(bool enabled,const char*why="FSR processing disabled"){if(processingAllowed!=enabled)interrupted=resetHistory=true;processingPauseReason=why;processingAllowed=enabled;poll();}
    uint64_t memoryEstimate(uint32_t rw,uint32_t rh,uint32_t ow,uint32_t oh)const{return xpfsr::nr::estimatedProfileBytes(controls[xpfsr::nr::Order]?ow:rw,controls[xpfsr::nr::Order]?oh:rh,controls[xpfsr::nr::Scale],controls[xpfsr::nr::Passes]);}
    bool allowFG()const{return controls[xpfsr::nr::AllowFG]!=0;}
    bool fullScene()const{return controls[xpfsr::nr::Scope]!=0;}
    bool afterUpscaling()const{return controls[xpfsr::nr::Order]!=0;}
    bool consumeReset(){bool result=resetHistory;resetHistory=false;return result;}
    bool ready(const xpfsr::nr::Host&host,uint32_t w,uint32_t h,VkFormat f){
        // enabled() polls once at the frame boundary. Keep that snapshot for
        // stage choice, context selection and the dispatch; never mix revisions.
        if(!controls[xpfsr::nr::Enabled]||!controls[xpfsr::nr::Intensity]||!featureSupport||frameFailed)return false;
        const auto key=makeKey(w,h,f,controls);
        bool changed{};auto active=sessions.select(Job{host,w,h,f,controls,key},changed);
        if(changed)interrupted=resetHistory=true;
        return bool(active);
    }
    bool apply(VkCommandBuffer cb,VkImage color,uint64_t camera,bool reset,std::shared_ptr<void>&owner){
        auto active=sessions.current();
        if(!active||frameFailed)return false;
        owner=active; // Kept until the host legally rerecords/frees this command buffer.
        SdkScope scope;xpfsr::nr::Frame frame{};frame.commands=cb;frame.color=color;frame.controls=controls;
        frame.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        frame.reset=reset||interrupted||!lastCamera||camera!=lastCamera+1;
        char why[512]{};int result=active->backend->record(active->context,&frame,why,sizeof(why));
        if(result<0){frameFailed=true;sessions.enable(false);note(std::string("NR stopped; original FSR input retained: ")+why);return false;}
        note(why);if(result>0){lastRecord=GetTickCount64();lastCamera=camera;++records;interrupted=false;}
        return result>0;
    }
    void pause(const char*why){if(enabled()&&!frameFailed){interrupted=true;note(why);}}
    void report(xpfsr::RendererFiles&files){
        poll();if(!neuralSessionCapable())return;
        auto now=GetTickCount64();if(now-lastReport<500||!files.hasOutput(1))return;
        wchar_t token[128]{};
        GetEnvironmentVariableW(L"XPFSR24_SESSION",token,128);
        std::string value;{std::lock_guard<std::mutex> lock(messageLock);value=message;}
        if(sessions.building()&&processingAllowed&&controls[xpfsr::nr::Enabled])value="NR preparing selected profile in background; FSR continues";
        else if(!interrupted&&lastRecord&&now-lastRecord>2000)value="NR waiting: no recent neural recording";
        std::ostringstream out;out<<GetCurrentProcessId()<<' '<<now<<' '<<records<<'\n';for(auto c:std::wstring(token))out<<char(c);out<<'\n'<<value<<'\n'<<revision<<'\n';
        if(files.offer(1,out.str()))lastReport=now;
    }
    void join(){sessions.join();}
    void stop(){join();sessions.clear();backend.reset();channel.close();}
    ~NeuralRenderer(){stop();}
};

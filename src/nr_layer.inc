// SPDX-License-Identifier: MIT
// Live settings use separate IPC; camera/plugin ABI remains unchanged.
static bool neuralSessionCapable(){
#ifndef XPFSR_NR_DEVELOPMENT
    return false;
#else
    wchar_t v[256]{};return GetEnvironmentVariableW(L"XPFSR24_NR_CONTROL",v,256)>0;
#endif
}
static int requestedNeuralRendering(){
#ifndef XPFSR_NR_DEVELOPMENT
    return 0;
#else
    wchar_t v[16]{};GetEnvironmentVariableW(L"XPFSR24_NR",v,16);
    return v[0]>=L'1'&&v[0]<=L'4'&&!v[1]?int(v[0]-L'0'):0;
#endif
}
static bool allowNeuralFrameGeneration(){wchar_t v[4]{};GetEnvironmentVariableW(L"XPFSR24_NR_ALLOW_FG",v,4);return v[0]==L'1'&&!v[1];}
class NeuralRenderer {
    using Controls=xpfsr::nr::Controls;using Key=std::array<uint32_t,5>;
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
    std::shared_ptr<Session> active,pending;std::vector<std::shared_ptr<Session>> retired;
    std::shared_ptr<Backend> backend;std::optional<Job> job;
    Key requested{};bool requestedOnce{},quitting{},featureSupport{},frameFailed{},interrupted=true,resetHistory=true;
    std::thread worker;std::mutex jobs,messageLock;std::condition_variable wake;
    std::atomic<bool> building{};std::string message="NR Off";
    uint64_t lastRecord{},lastCamera{},records{},lastReport{};
    static Key makeKey(uint32_t w,uint32_t h,VkFormat f,const Controls&c){return {w,h,uint32_t(f),uint32_t(c[xpfsr::nr::Scale]),uint32_t(c[xpfsr::nr::Model])};}
    void note(std::string text){std::lock_guard<std::mutex> lock(messageLock);message=std::move(text);}
    void buildLoop(){
        SdkScope scope;
        for(;;){
            std::optional<Job> work;std::vector<std::shared_ptr<Session>> garbage;
            {
                std::unique_lock<std::mutex> lock(jobs);
                wake.wait_for(lock,std::chrono::milliseconds(200),[&]{return quitting||job.has_value();});
                for(auto it=retired.begin();it!=retired.end();){
                    if(it->use_count()==1){garbage.push_back(std::move(*it));it=retired.erase(it);}else ++it;
                }
                if(quitting)break;
                work=std::move(job);job.reset();
            }
            garbage.clear();if(!work)continue;building=true;
            try{
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
                xpfsr::nr::Config config{};config.root=root;config.width=work->w;config.height=work->h;config.format=work->format;config.controls=work->controls;
                auto result=std::make_shared<Session>();result->backend=backend;result->key=work->key;
                char why[512]{};result->context=backend->create(&work->host,&config,why,sizeof(why));
                if(!result->context)throw std::runtime_error(why[0]?why:"NR initialization failed");
                {std::lock_guard<std::mutex> lock(jobs);if(pending)retired.push_back(std::move(pending));pending=std::move(result);}
                note("NR prepared; waiting for the next eligible frame");
            }catch(const std::exception&e){note(std::string("NR bypassed: ")+e.what()+"; toggle Off/On to retry");}
            building=false;
        }
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
                    requestedOnce=false;
                    if(frameFailed){
                        std::lock_guard<std::mutex> lock(jobs);
                        if(active)retired.push_back(std::move(active));
                        if(pending)retired.push_back(std::move(pending));
                        frameFailed=false;
                    }
                }
                controls=next;
            }revision=nextRevision;
        }
        if(!controls[xpfsr::nr::Enabled]||!controls[xpfsr::nr::Intensity])note("NR Off: original FSR input retained");
        else if(!featureSupport)note("NR unavailable: required Vulkan features were not enabled");
    }
    bool enabled(){poll();return controls[xpfsr::nr::Enabled]&&controls[xpfsr::nr::Intensity];}
    bool allowFG()const{return controls[xpfsr::nr::AllowFG]!=0;}
    bool fullScene()const{return controls[xpfsr::nr::Scope]!=0;}
    bool consumeReset(){bool result=resetHistory;resetHistory=false;return result;}
    bool ready(const xpfsr::nr::Host&host,uint32_t w,uint32_t h,VkFormat f){
        if(!enabled()||!featureSupport||frameFailed)return false;
        const auto key=makeKey(w,h,f,controls);
        std::lock_guard<std::mutex> lock(jobs);
        if(pending){
            if(pending->key==key){if(active)retired.push_back(std::move(active));active=std::move(pending);interrupted=resetHistory=true;}
            else retired.push_back(std::move(pending));
        }
        if(active&&active->key==key)return true;
        if(!requestedOnce||requested!=key){
            requested=key;requestedOnce=true;job=Job{host,w,h,f,controls,key};
            note("NR preparing selected profile in background; FSR continues");
            if(!worker.joinable())worker=std::thread([this]{buildLoop();});wake.notify_one();
        }
        return false;
    }
    bool apply(VkCommandBuffer cb,VkImage color,uint64_t camera,bool reset,std::shared_ptr<void>&owner){
        if(!active||frameFailed)return false;
        owner=active; // Kept until the host legally rerecords/frees this command buffer.
        SdkScope scope;xpfsr::nr::Frame frame{};frame.commands=cb;frame.color=color;frame.controls=controls;
        frame.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        frame.reset=reset||interrupted||!lastCamera||camera!=lastCamera+1;
        char why[512]{};int result=active->backend->record(active->context,&frame,why,sizeof(why));
        if(result<0){frameFailed=true;note(std::string("NR stopped; original FSR input retained: ")+why);return false;}
        note(why);if(result>0){lastRecord=GetTickCount64();lastCamera=camera;++records;interrupted=false;}
        return result>0;
    }
    void pause(const char*why){if(enabled()&&!frameFailed){interrupted=true;note(why);}}
    void report(){
        poll();if(!neuralSessionCapable())return;
        auto now=GetTickCount64();if(now-lastReport<500)return;lastReport=now;
        wchar_t path[32768]{},token[128]{};if(!GetEnvironmentVariableW(L"XPFSR24_NR_STATUS",path,32768))return;
        GetEnvironmentVariableW(L"XPFSR24_SESSION",token,128);
        std::string value;{std::lock_guard<std::mutex> lock(messageLock);value=message;}
        if(building&&controls[xpfsr::nr::Enabled])value="NR preparing selected profile in background; FSR continues";
        else if(!interrupted&&lastRecord&&now-lastRecord>2000)value="NR waiting: no recent neural recording";
        std::filesystem::path target=path,temp=std::wstring(path)+L".tmp";
        {std::ofstream out(temp);out<<GetCurrentProcessId()<<' '<<now<<' '<<records<<'\n';for(auto c:std::wstring(token))out<<char(c);out<<'\n'<<value<<'\n'<<revision<<'\n';if(!out)return;}
        MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }
    void join(){ {std::lock_guard<std::mutex> lock(jobs);quitting=true;job.reset();}wake.notify_one();if(worker.joinable())worker.join(); }
    void stop(){join();active.reset();pending.reset();retired.clear();backend.reset();channel.close();}
    ~NeuralRenderer(){stop();}
};

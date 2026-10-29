// SPDX-License-Identifier: MIT
// Loads the real native plugin with instrumented host stubs, not a simulator.
#include <dlfcn.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <unistd.h>
static float (*callback)(float,float,int,void*);
static int registered{}, unregistered{}, removed{}, destroyed{};
static bool invalid{};
extern "C" {
void XPLMEnableFeature(const char*,int) {}
void* XPLMFindDataRef(const char*) { return reinterpret_cast<void*>(1); }
int XPLMGetDatavf(void*,float* out,int,int count) {
    for(int i=0;i<count;++i) out[i]=i%5==0 ? 1.f : 0.f;
    if(invalid) out[0]=std::numeric_limits<float>::quiet_NaN();
    return count;
}
int XPLMGetDatai(void*) { return 1440; }
void XPLMGetVersions(int* sim,int* sdk,int* host) { *sim=124400;*sdk=440;*host=1; }
void XPLMDebugString(const char* text) { std::fputs(text,stdout); }
void XPLMGetSystemPath(char* out) { out[0]=0; }
void* XPLMFindPluginsMenu() { return reinterpret_cast<void*>(2); }
int XPLMAppendMenuItem(void*,const char*,void*,int) { return 0; }
void* XPLMCreateMenu(const char*,void*,int,void(*)(void*,void*),void*) { return reinterpret_cast<void*>(3); }
void XPLMDestroyMenu(void*) { ++destroyed; }
void XPLMRemoveMenuItem(void*,int) { ++removed; }
void XPLMRegisterFlightLoopCallback(float(*fn)(float,float,int,void*),float,void*) { callback=fn; ++registered; }
void XPLMUnregisterFlightLoopCallback(float(*)(float,float,int,void*),void*) { ++unregistered; }
}
int main(int argc,char** argv) {
    assert(argc==2);
    const std::string path="probe-test-"+std::to_string(getpid())+".json";
    setenv("XPFSR_NATIVE_REPORT",path.c_str(),1);
    void* library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!library) { std::fputs(dlerror(),stderr); return 1; }
    auto start=reinterpret_cast<int(*)(char*,char*,char*)>(dlsym(library,"XPluginStart"));
    auto enable=reinterpret_cast<int(*)()>(dlsym(library,"XPluginEnable"));
    auto disable=reinterpret_cast<void(*)()>(dlsym(library,"XPluginDisable"));
    auto stop=reinterpret_cast<void(*)()>(dlsym(library,"XPluginStop"));
    assert(start && enable && disable && stop);
    char name[256]{},signature[256]{},description[256]{};
    assert(start(name,signature,description)==1 && enable()==1 && enable()==1);
    assert(registered==1 && callback);
    assert(callback(2,2,1,nullptr)==2);
    std::ifstream report(path);
    std::string text((std::istreambuf_iterator<char>(report)),{});
    assert(text.find("\"fsr_renderer\": false")!=std::string::npos);
    assert(text.find("\"camera_matrices_finite\": true")!=std::string::npos);
    invalid=true;
    callback(2,2,2,nullptr);
    std::ifstream second(path);
    text.assign(std::istreambuf_iterator<char>(second),{});
    assert(text.find("\"camera_matrices_finite\": false")!=std::string::npos);
    disable(); stop();
    assert(unregistered==1 && removed==1 && destroyed==1);
    dlclose(library);
    std::remove(path.c_str());
    std::puts("PASS native probe load/enable/report/invalid-camera/disable/unload; no render/input APIs linked");
}

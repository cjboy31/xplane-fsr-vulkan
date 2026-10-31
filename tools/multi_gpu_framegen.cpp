// SPDX-License-Identifier: MIT
// Two-device image round trip plus actual FSR 3.1.6 interpolation, offscreen only.
#include "gpu_worker_vk.hpp"
#include "gpu_frame_queue.hpp"
#include "framegen_worker.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
using namespace xpfsr::gpu;
static float half(uint16_t h){const int sign=h>>15,e=(h>>10)&31,f=h&1023;if(e==31)return NAN;float v=e?std::ldexp(1.0f+f/1024.0f,e-15):std::ldexp(float(f),-24);return sign?-v:v;}
int wmain(int argc,wchar_t**argv){try{
    worker::need(argc==4||argc==5,"MultiGpuFrameGenCheck AMD_RUNTIME RENDER_UUID PROCESS_UUID [--same-physical-fixture]");
    const bool fixture=argc==5&&std::wstring(argv[4])==L"--same-physical-fixture";worker::need(argc==4||fixture,"Unknown fixture option");
    auto narrow=[](const wchar_t*p){std::wstring w=p;return std::string(w.begin(),w.end());};auto rID=narrow(argv[2]),pID=narrow(argv[3]);worker::need(rID!=pID||fixture,"Two distinct physical GPUs required; same-device fixture must be explicit");
    worker::Instance instance;worker::Device render(instance.select(rID)),process(instance.select(pID));
    xpfsr::Runtime runtime;runtime.load(std::filesystem::absolute(argv[1]),true);
    constexpr uint32_t w=384,h=216;constexpr size_t bytes=size_t(w)*h*8;
    worker::Image source(render,w,h,VK_FORMAT_R16G16B16A16_SFLOAT),returned(render,w,h,VK_FORMAT_R16G16B16A16_SFLOAT);
    worker::Image color(process,w,h,VK_FORMAT_R16G16B16A16_SFLOAT),depth(process,w,h,VK_FORMAT_R32_SFLOAT),motion(process,w,h,VK_FORMAT_R16G16_SFLOAT),output(process,w,h,VK_FORMAT_R16G16B16A16_SFLOAT);
    worker::Buffer renderStaging(render,bytes),processStaging(process,bytes),guide(process,size_t(w)*h*4);
    std::fill_n(static_cast<float*>(guide.mapped),size_t(w)*h,.5f);depth.upload(guide);std::memset(guide.mapped,0,size_t(w)*h*4);motion.upload(guide);
    ComputeFrameGenerator fg(process,runtime.api,w,h);FrameQueue<2>queue(bytes);uint32_t checked{},generated{};std::vector<double>times;
    auto rejects=[](auto f){try{f();}catch(...){return;}throw std::runtime_error("Foreign/undersized worker resource was accepted");};
    rejects([&]{source.upload(processStaging);});worker::Buffer tiny(render,4);rejects([&]{source.read(tiny);});
    rejects([&]{fg.record(process.commands,1,source,depth,motion,output);});
    for(uint64_t frame=1;frame<=10;++frame){auto*pixel=static_cast<uint16_t*>(renderStaging.mapped);for(size_t i=0;i<size_t(w)*h;++i){pixel[i*4]=0x3c00;pixel[i*4+1]=0x3800;pixel[i*4+2]=0x3400;pixel[i*4+3]=0x3c00;}
        FrameKey key{frame,bytes,(uint64_t(w)<<32)|h};auto token=queue.reserve(key);worker::need(bool(token),"No bounded FG worker slot");auto start=std::chrono::steady_clock::now();
        source.upload(renderStaging);source.read(renderStaging);std::memcpy(processStaging.mapped,renderStaging.mapped,bytes);color.upload(processStaging);worker::need(queue.captureComplete(*token),"Stale captured frame");
        uint32_t count{};process.submit([&](auto cb){count=fg.record(cb,frame,color,depth,motion,output,frame==6);});
        if(!count){queue.reset();queue.processComplete(*token);continue;}generated+=count;output.read(processStaging);std::memcpy(renderStaging.mapped,processStaging.mapped,bytes);returned.upload(renderStaging);returned.read(renderStaging);
        worker::need(queue.processComplete(*token),"Stale generated frame");auto ready=queue.take(key);worker::need(bool(ready),"Late/mismatched result must retain the real frame");
        if(frame>2&&frame!=6){double mean{};auto*p=static_cast<uint16_t*>(renderStaging.mapped);for(size_t i=0;i<size_t(w)*h;++i){auto value=half(p[i*4]);for(unsigned c=0;c<3;++c){const float expected=std::ldexp(1.0f,-int(c)),channel=half(p[i*4+c]);worker::need(std::isfinite(channel)&&channel>expected*.5f&&channel<expected*1.5f,"FG worker returned black/nonfinite/wrong-color pixels");}mean+=value;}worker::need(mean/(w*h)>.9,"FG worker brightness mismatch");++checked;}
        worker::need(queue.consumeComplete(*ready),"Output still owned by rendering device");times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    }
    worker::need(checked>=5&&generated>=5,"Too few validated generated frames");std::sort(times.begin(),times.end());
    std::cout<<"{\"result\":\"PASS\",\"distinctPhysicalDevices\":"<<(rID!=pID?"true":"false")<<",\"twoLogicalDevices\":true,\"renderUuid\":\""<<rID<<"\",\"processingUuid\":\""<<pID<<"\",\"checkedFrames\":"<<checked<<",\"generatedFrames\":"<<generated<<",\"medianFixtureMs\":"<<times[times.size()/2]<<",\"scope\":\"offscreen HDR image round trip and real FSR 3.1.6 compute; no simulator injection, asynchronous scheduling, presentation or speedup claim\"}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

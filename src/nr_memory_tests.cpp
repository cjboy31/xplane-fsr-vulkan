// SPDX-License-Identifier: MIT
#include "nr_sessions.hpp"
#include "nr_memory_policy.hpp"
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;
static void need(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
template<class F>void until(F f,const char*why){auto end=std::chrono::steady_clock::now()+5s;while(!f()){if(std::chrono::steady_clock::now()>end)throw std::runtime_error(why);std::this_thread::sleep_for(2ms);}}
struct Counts {std::atomic<int>created{},destroyed{},alive{},peak{},started{};std::atomic<bool>gate{},fail{},memoryError{};};
struct Job {int key;};
struct Session {int key;Counts&counts;Session(int k,Counts&c):key(k),counts(c){++c.created;int n=++c.alive;int old=c.peak.load();while(old<n&&!c.peak.compare_exchange_weak(old,n)){} }~Session(){--counts.alive;++counts.destroyed;}};
using Pool=xpfsr::nr::ProfileSessions<Job,Session>;
static auto builder(Counts&c){return [&c](const Job&j){++c.started;while(c.gate)std::this_thread::sleep_for(2ms);if(c.fail)throw std::runtime_error(c.memoryError?"GPU memory reserve: fixture":"fixture budget refusal");return std::make_shared<Session>(j.key,c);};}
static auto ready(Pool&p,int key){std::shared_ptr<Session> result;until([&]{bool changed{};result=p.select(Job{key},changed);return bool(result);},"Prepared profile never became ready");return result;}
int main(){try{
    using namespace xpfsr::nr;
    need(completedOneTimeRecording(true,true,true,4,4),"Completed one-time recording not reclaimable");
    need(!completedOneTimeRecording(false,true,true,4,4),"In-flight recording reclaimed");
    need(!completedOneTimeRecording(true,false,true,4,4),"Reusable command buffer invalidated");
    need(!completedOneTimeRecording(true,true,false,4,4),"Unsubmitted recording reclaimed");
    need(!completedOneTimeRecording(true,true,true,4,5),"New recording reclaimed by an old completion fence");
    need(coldFrameCutoff({60,10,20,50,30,40},3)==40,"Frame cache did not retain its three newest recordings");
    need(coldFrameCutoff({60,10,20},3)==0,"Small frame cache was evicted unnecessarily");
    need(!profileFits(32*Gib,31*Gib,estimatedProfileBytes(2953,1661,67,3)),"Nearly full heap admitted NR");
    need(profileFits(32*Gib,20*Gib,estimatedProfileBytes(2953,1661,67,3)),"Headroom-rich heap rejected NR");
    need(!profileFits(Gib,2*Gib,MiB),"Over-budget heap underflow");
    need(!profileFits(UINT64_MAX,0,UINT64_MAX),"Admission arithmetic overflow");
    need(estimatedProfileBytes(3840,2160,100,3)>estimatedProfileBytes(1920,1080,50,1),"Profile estimate does not track dimensions");
    need(estimatedProfileBytes(UINT32_MAX,UINT32_MAX,100,3)==UINT64_MAX,"Large profile estimate overflowed");
    {
        Counts c;Pool p(builder(c),[](std::string){});p.enable(true);
        auto gpu=ready(p,1);bool changed{};p.select(Job{2},changed);
        std::this_thread::sleep_for(120ms);need(c.started==1&&c.destroyed==0,"Profile replaced while GPU owner was live");
        gpu.reset();gpu=ready(p,2);need(c.destroyed==1&&c.peak==1,"Replacement overlapped old model resources");
        p.enable(false);std::this_thread::sleep_for(120ms);need(c.alive==1,"Off destroyed an in-flight GPU owner");
        gpu.reset();until([&]{return c.alive==0;},"Off retained model resources");p.join();p.clear();
    }
    {
        Counts c;c.gate=true;Pool p(builder(c),[](std::string){});p.enable(true);bool changed{};p.select(Job{3},changed);
        until([&]{return c.started==1;},"Blocked construction never started");
        p.enable(false);p.enable(true);p.select(Job{4},changed);c.gate=false;
        auto gpu=ready(p,4);need(gpu->key==4&&c.created==2&&c.destroyed==1&&c.peak==1,"Cancelled preparation resurfaced or overlapped replacement");
        p.enable(false);gpu.reset();until([&]{return c.alive==0;},"Cancelled result retained");p.join();p.clear();
    }
    {
        Counts c;Pool p(builder(c),[](std::string){});p.enable(true);auto gpu=ready(p,1);bool changed{};
        for(int i=2;i<=50;++i)p.select(Job{i},changed);
        std::this_thread::sleep_for(120ms);need(c.started==1,"Rapid changes allocated over an active GPU owner");
        gpu.reset();gpu=ready(p,50);need(c.created==2&&c.peak==1,"Superseded queued profiles were allocated");
        p.enable(false);gpu.reset();until([&]{return c.alive==0;},"Rapid profile sequence leaked resources");p.join();p.clear();
    }
    {
        Counts c;c.fail=true;c.memoryError=true;
        Pool p(builder(c),[&](std::string text){if(text.find("GPU memory reserve:")!=std::string::npos)c.fail=false;},50ms);
        p.enable(true);auto gpu=ready(p,1);need(c.started==2,"Recovered memory budget did not automatically retry");
        p.enable(false);gpu.reset();until([&]{return c.alive==0;},"Automatic retry retained resources");p.join();p.clear();
    }
    {
        Counts c;c.fail=true;std::atomic<int>errors{};Pool p(builder(c),[&](std::string text){if(text.find("fixture budget refusal")!=std::string::npos)++errors;});
        p.enable(true);bool changed{};p.select(Job{1},changed);until([&]{return errors==1;},"Budget failure not visible");
        for(int i=0;i<50;++i)p.select(Job{1},changed);need(c.started==1,"Failed profile retried every frame");
        c.fail=false;p.enable(false);p.enable(true);auto gpu=ready(p,1);need(c.started==2,"Live retry did not prepare a new profile");
        p.enable(false);gpu.reset();until([&]{return c.alive==0;},"Retry retained resources");p.join();p.clear();
    }
    std::cout<<"PASS: deferred GPU ownership, release on Off, no overlapping profiles, stale-build cancellation, rapid edits, budget refusal/retry and overflow-safe texture reserve.\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

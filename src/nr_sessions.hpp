// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
namespace xpfsr::nr {
// A frame owns its session until the host can legally rerecord/free that command
// buffer. Never destroy that session early, or build a second large profile
// while the first still has frame owners. Destruction/preparation stay off the
// render thread. No GPU waits or forced host-command-buffer invalidation.
template<class Job,class Session> class ProfileSessions {
    using Ptr=std::shared_ptr<Session>;
    using Key=decltype(std::declval<Job>().key);
    struct Work {Job job;uint64_t revision;};
    std::function<Ptr(const Job&)> prepare;
    std::function<void(std::string)> note;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::optional<Work> queued;
    Ptr active,pending;
    std::vector<Ptr> retired;
    Key requested{};
    uint64_t revision{};
    std::chrono::steady_clock::time_point retryAt{};
    std::chrono::milliseconds retryDelay;
    bool on{},requestedOnce{},quitting{};
    std::atomic<bool> building_{},reclaiming_{};
    void retire(Ptr &p){if(p)retired.push_back(std::move(p));}
    void start(){if(!worker.joinable())worker=std::thread([this]{run();});wake.notify_one();}
    void run(){
        for(;;){
            std::optional<Work> work;
            std::vector<Ptr> garbage;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if(quitting)break;
                for(auto it=retired.begin();it!=retired.end();){
                    if(it->use_count()==1){garbage.push_back(std::move(*it));it=retired.erase(it);}else ++it;
                }
                reclaiming_=!garbage.empty();
                // Destroy released profiles before allocating the replacement.
                if(garbage.empty()&&retired.empty()&&queued){work=std::move(queued);queued.reset();building_=true;}
                else if(garbage.empty()){wake.wait_for(lock,std::chrono::milliseconds(100));continue;}
            }
            garbage.clear();reclaiming_=false;
            if(!work)continue;
            Ptr result;std::string error;
            try{result=prepare(work->job);if(!result)error="NR preparation returned no session";}
            catch(const std::exception&e){error=e.what();}
            catch(...){error="Unknown NR preparation failure";}
            {
                std::lock_guard<std::mutex> lock(mutex);
                if(quitting||!on||work->revision!=revision){if(result)retired.push_back(std::move(result));}
                else if(!error.empty()){
                    const bool memory=error.find("GPU memory reserve:")!=std::string::npos;
                    if(memory)retryAt=std::chrono::steady_clock::now()+retryDelay;
                    note("NR bypassed: "+error+(memory?"; budget will be checked again in the background":"; lower the NR work budget or toggle Off/On to retry"));
                }
                else {pending=std::move(result);note("NR prepared; waiting for the next eligible frame");}
                building_=false;
            }
        }
    }
public:
    ProfileSessions(std::function<Ptr(const Job&)> build,std::function<void(std::string)> message,std::chrono::milliseconds retry=std::chrono::seconds(5)):prepare(std::move(build)),note(std::move(message)),retryDelay(retry){}
    ~ProfileSessions(){join();}
    void enable(bool value){
        std::lock_guard<std::mutex> lock(mutex);
        if(on==value)return;
        on=value;
        if(!on){++revision;queued.reset();requestedOnce=false;retryAt={};retire(active);retire(pending);if(!retired.empty()||building_)start();}
    }
    Ptr select(const Job&job,bool&changed){
        std::lock_guard<std::mutex> lock(mutex);changed=false;
        if(!on||quitting)return {};
        if(retryAt.time_since_epoch().count()&&std::chrono::steady_clock::now()>=retryAt){requestedOnce=false;retryAt={};}
        if(pending&&requestedOnce&&requested==job.key){active=std::move(pending);changed=true;}
        if(active&&active->key==job.key)return active;
        if(!requestedOnce||requested!=job.key){
            retire(active);retire(pending);requested=job.key;requestedOnce=true;
            retryAt={};
            queued=Work{job,++revision};
            note(retired.empty()?"NR preparing selected profile in background; FSR continues":"NR releasing the previous profile after its GPU owners finish; FSR continues");
            start();
        }
        return {};
    }
    Ptr current()const{std::lock_guard<std::mutex> lock(mutex);return active;}
    bool building()const{return building_.load();}
    bool retiring()const{std::lock_guard<std::mutex> lock(mutex);return !retired.empty();}
    bool released()const{std::lock_guard<std::mutex> lock(mutex);return !active&&!pending&&retired.empty()&&!queued&&!building_&&!reclaiming_;}
    void join(){
        {std::lock_guard<std::mutex> lock(mutex);quitting=true;queued.reset();wake.notify_one();}
        if(worker.joinable())worker.join();
    }
    // Only after the host completed submitted commands and released frame owners.
    void clear(){std::lock_guard<std::mutex> lock(mutex);active.reset();pending.reset();retired.clear();}
};
} // namespace xpfsr::nr

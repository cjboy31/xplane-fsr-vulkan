// SPDX-License-Identifier: MIT
#include "completed_resource_cache.hpp"
#include "scene_capture_extent.hpp"
#include <array>
#include <memory>
#include <stdexcept>
#include <iostream>
static void check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
struct Resource{int &destroyed;explicit Resource(int&n):destroyed(n){}~Resource(){++destroyed;}};
int main(){try{
    xpfsr::SceneDepthSources sources;
    check(!sources.accepts(1,1),"Unknown pass caused speculative depth copying");
    sources.observe(1,1,false);check(!sources.accepts(1,1),"Unvalidated/stale camera learned a source");
    for(uint64_t i=1;i<=4;++i)sources.observe(i,1,true);
    for(uint64_t i=1;i<=4;++i)check(sources.accepts(i,1),"Rotating scene sources evicted each other");
    check(!sources.accepts(9,1),"Unrelated same-size image became a scene source");
    sources.observe(1,2,true);check(sources.accepts(1,2)&&!sources.accepts(1,1),"Recycled image handle retained a stale generation");
    sources.erase(1);check(!sources.accepts(1,2),"Destroyed source stayed eligible");
    for(uint64_t i=5;i<=100;++i)sources.observe(i,1,true);
    check(!sources.accepts(2,1)&&sources.accepts(100,1),"Source learning grew without a bound or failed to recover after resize");
    sources.observe(0,1,true);sources.observe(1,0,true);check(!sources.accepts(0,1)&&!sources.accepts(1,0),"Invalid source identity was accepted");
    xpfsr::SceneCaptureExtents extents;
    check(extents.accepts(2953,1661)&&extents.accepts(1920,1080)&&!extents.accepts(0,0),"Unknown/invalid scene extent policy failed");
    check(!extents.accepts(8192,4320)&&!extents.observe(8192,4320,true),"Unsupported oversized scene created a capture guide");
    check(!extents.observe(800,450,false)&&extents.accepts(2953,1661),"Unvalidated source changed depth capture");
    check(extents.observe(2953,1661,true)&&extents.accepts(2953,1661)&&!extents.accepts(1920,1080),"Unrelated depth extent remained eligible");
    check(!extents.observe(3840,2160,false)&&!extents.accepts(3840,2160),"Stale camera/graph relationship changed capture");
    check(extents.observe(3840,2160,true)&&extents.accepts(2953,1661)&&extents.accepts(3840,2160),"Input/resize recovery discarded a recent compatible extent");
    check(extents.observe(2560,1440,true)&&!extents.accepts(2953,1661)&&extents.accepts(3840,2160)&&extents.accepts(2560,1440),"Extent history was unbounded or failed to adapt");
    check(!extents.observe(0,0,true)&&!extents.observe(2560,1440,true),"Invalid/unchanged extent reset history");
    using Key=std::array<int,3>;using Owner=std::unique_ptr<Resource>;
    int destroyed=0;xpfsr::CompletedResourceCache<Key,Owner> cache(3);cache.limit(300);
    Key frame{0,1920,1080},otherFamily{1,1920,1080},resize{0,3840,2160};
    auto pending=std::make_unique<Resource>(destroyed);auto *identity=pending.get();
    check(!cache.store(frame,pending,100,false)&&pending.get()==identity&&!destroyed,"Pending GPU owner was moved or destroyed");
    check(cache.store(frame,pending,100,true)&&!pending&&cache.bytes()==100,"Completed owner was not retained");
    check(!cache.take(otherFamily)&&!cache.take(resize)&&cache.bytes()==100,"Incompatible queue family or extent reused an image");
    auto reused=cache.take(frame);check(reused.get()==identity&&cache.bytes()==0&&!destroyed,"Taking an idle resource recreated or destroyed it");
    check(cache.store(frame,reused,100,true),"Returning a completed lease failed");
    for(int i=0;i<3;++i){auto next=std::make_unique<Resource>(destroyed);check(cache.store(Key{0,i,i},next,100,true),"Bounded store failed");}
    check(cache.bytes()==300&&cache.size()==3&&destroyed==1&&cache.evictions==1,"Idle byte/count budget did not retire oldest completed owner");
    auto borrowed=cache.take(Key{0,2,2});int before=destroyed;cache.limit(0);
    check(cache.bytes()==0&&cache.size()==0&&destroyed==before+2&&borrowed,"Pressure trim destroyed a borrowed GPU owner");
    auto tooLarge=std::make_unique<Resource>(destroyed);cache.limit(64);
    check(!cache.store(frame,tooLarge,65,true)&&tooLarge,"Oversized store lost caller ownership");
    cache.limit(UINT64_MAX);auto huge=std::make_unique<Resource>(destroyed);check(cache.store(frame,huge,UINT64_MAX,true),"Maximum-size fixture failed");
    auto small=std::make_unique<Resource>(destroyed);check(cache.store(resize,small,1,true)&&cache.bytes()==1,"Budget arithmetic overflowed");
    cache.clear();check(cache.bytes()==0&&cache.size()==0&&borrowed&&tooLarge,"Clear lost live non-cache owners");
    cache.limit(300);auto keep=std::make_unique<Resource>(destroyed),drop=std::make_unique<Resource>(destroyed);auto *dropId=drop.get();
    check(cache.store(frame,keep,100,true)&&cache.store(resize,drop,100,true),"Selective trim setup failed");
    cache.eraseIf([&](const Key&,const Owner&p){return p.get()==dropId;});
    check(cache.bytes()==100&&cache.contains(frame)&&!cache.contains(resize),"Selective NR-scratch trim removed unrelated reusable images");cache.clear();
    xpfsr::CompletedResourceCache<Key,std::shared_ptr<Resource>> shared;shared.limit(100);
    auto depth=std::make_shared<Resource>(destroyed);auto observer=depth;check(shared.store(frame,depth,100,true),"Shared completed depth store failed");shared.clear();check(observer.use_count()==1,"Shared depth lifetime was not retained by its live owner");
    std::cout<<"PASS: completed-only resource reuse, family/extent keys, ownership, bounded idle bytes, pressure trim, overflow and shared depth lifetime.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

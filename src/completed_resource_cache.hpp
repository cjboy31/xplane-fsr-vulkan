// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>
namespace xpfsr {
// Caller serializes access and supplies GPU-completion proof. A refused store
// leaves ownership with the caller; pending work is never destroyed here.
template<class Key,class Owner> class CompletedResourceCache {
    struct Entry{Key key;Owner owner;uint64_t bytes;};
    std::vector<Entry> entries;
    uint64_t maximum{},used{};
    size_t countLimit;
    void evict(){used-=entries.front().bytes;entries.erase(entries.begin());++evictions;}
public:
    uint64_t hits{},misses{},evictions{};
    explicit CompletedResourceCache(size_t count=16):countLimit(count){}
    void limit(uint64_t bytes){maximum=bytes;while(!entries.empty()&&(used>maximum||entries.size()>countLimit))evict();}
    bool contains(const Key&key)const{for(const auto&e:entries)if(e.key==key)return true;return false;}
    Owner take(const Key&key){for(auto i=entries.begin();i!=entries.end();++i)if(i->key==key){auto out=std::move(i->owner);used-=i->bytes;entries.erase(i);++hits;return out;}++misses;return {};}
    bool store(const Key&key,Owner &owner,uint64_t bytes,bool gpuComplete){
        if(!gpuComplete||!owner||!bytes||!countLimit||bytes>maximum)return false;
        while(!entries.empty()&&(entries.size()>=countLimit||used>maximum-bytes))evict();
        entries.push_back({key,std::move(owner),bytes});used+=bytes;return true;
    }
    uint64_t bytes()const{return used;}
    size_t size()const{return entries.size();}
    template<class Predicate>void eraseIf(Predicate predicate){for(auto i=entries.begin();i!=entries.end();)if(predicate(i->key,i->owner)){used-=i->bytes;i=entries.erase(i);++evictions;}else ++i;}
    void clear(){entries.clear();used=0;}
};
} // namespace xpfsr

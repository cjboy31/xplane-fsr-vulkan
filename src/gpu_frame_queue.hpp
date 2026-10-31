// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <optional>
namespace xpfsr::gpu {
struct FrameKey {
    uint64_t frame{},bytes{},layout{};
    bool operator==(const FrameKey&o)const{return frame==o.frame&&bytes==o.bytes&&layout==o.layout;}
};
struct FrameToken {size_t slot{};uint64_t generation{},epoch{};};
// Completion methods are acknowledgements AFTER their respective GPU fences.
// Cancellation does not recycle capture/processing/presentation work in flight.
// Missing/stale results return none so a caller can retain the real source frame.
template<size_t N=2>class FrameQueue {
    static_assert(N>0);
    enum class Stage {Idle,Capturing,Processing,Ready,Consuming};
    struct Slot {Stage stage=Stage::Idle;FrameKey key{};uint64_t generation{},epoch{};bool cancelled{};};
    std::array<Slot,N> slots{};
    mutable std::mutex mutex;
    uint64_t generation{},epoch=1,maxPayload;
    Slot*match(const FrameToken&t,Stage expected){if(t.slot>=N)return nullptr;auto&s=slots[t.slot];return s.generation==t.generation&&s.epoch==t.epoch&&s.stage==expected?&s:nullptr;}
public:
    explicit FrameQueue(uint64_t maximumBytes=256ull*1024*1024):maxPayload(maximumBytes){}
    std::optional<FrameToken>reserve(const FrameKey&key){
        std::lock_guard<std::mutex>lock(mutex);if(!key.frame||!key.bytes||key.bytes>maxPayload)return {};
        for(size_t i=0;i<N;++i)if(slots[i].stage==Stage::Idle){auto&s=slots[i];if(!++generation)++generation;s={Stage::Capturing,key,generation,epoch,false};return FrameToken{i,generation,epoch};}return {};
    }
    bool captureComplete(const FrameToken&t){std::lock_guard<std::mutex>lock(mutex);auto*s=match(t,Stage::Capturing);if(!s)return false;if(s->cancelled){s->stage=Stage::Idle;return false;}s->stage=Stage::Processing;return true;}
    bool processComplete(const FrameToken&t){std::lock_guard<std::mutex>lock(mutex);auto*s=match(t,Stage::Processing);if(!s)return false;if(s->cancelled){s->stage=Stage::Idle;return false;}s->stage=Stage::Ready;return true;}
    std::optional<FrameToken>take(const FrameKey&expected){std::lock_guard<std::mutex>lock(mutex);for(size_t i=0;i<N;++i){auto&s=slots[i];if(s.stage==Stage::Ready&&!s.cancelled&&s.key==expected){s.stage=Stage::Consuming;return FrameToken{i,s.generation,s.epoch};}}return {};}
    bool consumeComplete(const FrameToken&t){std::lock_guard<std::mutex>lock(mutex);auto*s=match(t,Stage::Consuming);if(!s)return false;s->stage=Stage::Idle;return true;}
    void reset(){std::lock_guard<std::mutex>lock(mutex);if(!++epoch)++epoch;for(auto&s:slots)if(s.stage==Stage::Ready)s.stage=Stage::Idle;else if(s.stage!=Stage::Idle)s.cancelled=true;}
    void discardReadyBefore(uint64_t frame){std::lock_guard<std::mutex>lock(mutex);for(auto&s:slots)if(s.stage==Stage::Ready&&s.key.frame<frame)s.stage=Stage::Idle;}
    size_t held()const{std::lock_guard<std::mutex>lock(mutex);size_t count{};for(const auto&s:slots)count+=s.stage!=Stage::Idle;return count;}
};
} // namespace xpfsr::gpu

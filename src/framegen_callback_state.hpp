// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <mutex>
namespace xpfsr {
// Presentation can query an earlier prepared source after another source has
// been prepared. Keep reset metadata with that source, not mutable latest state.
class FrameCallbackState {
    struct Entry {uint64_t id{};bool reset{},valid{};};
    std::array<Entry,16> entries{};std::mutex mutex;
public:
    void prepared(uint64_t frame,bool reset){std::lock_guard<std::mutex> lock(mutex);entries[frame%entries.size()]={frame,reset,true};}
    bool read(uint64_t frame,bool&reset){std::lock_guard<std::mutex> lock(mutex);auto&e=entries[frame%entries.size()];if(!e.valid||e.id!=frame)return false;reset=e.reset;return true;}
};
}

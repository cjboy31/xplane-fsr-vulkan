// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
namespace xpfsr {
template <class Function> struct DispatchCacheEntry {
    uint64_t identity{};
    const char *name{};
    Function function{};
};
template <class Function> DispatchCacheEntry<Function> &dispatchCacheEntry() {
    static thread_local DispatchCacheEntry<Function> cached;
    return cached;
}
inline uint64_t nextDispatchIdentity() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}
// Vulkan function addresses are stable for a live device. Include an identity
// independent of the device's address so a recycled handle cannot reuse a cache.
template <class Function, class Lookup>
Function cachedDeviceFunction(uint64_t identity, const char *name, Lookup lookup) {
    auto &cached = dispatchCacheEntry<Function>();
    if (cached.identity != identity || !cached.name || std::strcmp(cached.name, name)) {
        cached = {identity, name, reinterpret_cast<Function>(lookup())};
    }
    return cached.function;
}
} // namespace xpfsr

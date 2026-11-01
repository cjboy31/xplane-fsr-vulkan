// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace xpfsr {
// The rendering thread offers snapshots, never performs file I/O or waits for
// the worker. Each output has one replaceable pending snapshot, not a queue of
// frames. Callbacks own only file paths; they must not enter Vulkan or Device.
class BackgroundFiles {
public:
    using Controls = std::array<int, 2>; // -1 unknown, 0 Off, 1 On
    using Read = std::function<Controls()>;
    using Write = std::function<void(size_t, const std::string &)>;
    static constexpr size_t outputCount = 2, maximumSnapshot = 8192;
private:
    std::mutex mutex;
    std::condition_variable wake;
    std::array<std::string, outputCount> pending;
    std::array<bool, outputCount> dirty{};
    // Value + revision in one atomic: consumers apply only a changed file
    // value, so a cached old file cannot repeatedly override newer live IPC.
    std::array<std::atomic<uint64_t>, 2> cached{{0, 0}};
    std::atomic<bool> running{false};
    bool stopping{};
    Read read;
    Write write;
    std::thread worker;
    void refresh() noexcept {
        try {
            if (read) {
                auto values = read();
                for (size_t i = 0; i < values.size(); ++i)
                    if (values[i] == 0 || values[i] == 1) {
                        auto old = cached[i].load(std::memory_order_relaxed);
                        if (int(old & 3) - 1 != values[i])
                            cached[i].store(((old + 4) & ~uint64_t(3)) | uint64_t(values[i] + 1),
                                            std::memory_order_relaxed);
                    }
            }
        } catch (...) {} // An unavailable control file retains the last value.
    }
    void run() noexcept {
        using Clock = std::chrono::steady_clock;
        auto nextRead = Clock::now();
        for (;;) {
            std::array<std::string, outputCount> batch;
            std::array<bool, outputCount> ready{};
            bool finish{};
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_until(lock, nextRead, [&] {
                    return stopping || dirty[0] || dirty[1];
                });
                for (size_t i = 0; i < outputCount; ++i) {
                    ready[i] = dirty[i];
                    if (dirty[i]) batch[i].swap(pending[i]);
                    dirty[i] = false;
                }
                finish = stopping;
            }
            // No mailbox lock across reads, writes, flushes or rename calls.
            if (!finish && Clock::now() >= nextRead) {
                refresh();
                nextRead = Clock::now() + std::chrono::milliseconds(100);
            }
            for (size_t i = 0; i < outputCount; ++i)
                if (ready[i]) {
                    try { if (write) write(i, batch[i]); } catch (...) {}
                }
            if (finish) return;
        }
    }
public:
    BackgroundFiles() = default;
    BackgroundFiles(const BackgroundFiles &) = delete;
    BackgroundFiles &operator=(const BackgroundFiles &) = delete;
    // Startup only: the initial controls are read before any intercepted frame.
    // start/stop are owned by device creation/destruction, not the frame path.
    void start(Read reader, Write writer) {
        if (worker.joinable()) return;
        read = std::move(reader);
        write = std::move(writer);
        refresh();
        stopping = false;
        worker = std::thread([this] { run(); });
        running.store(true, std::memory_order_release);
    }
    uint64_t controlSnapshot(size_t index) const noexcept {
        return index < cached.size() ? cached[index].load(std::memory_order_relaxed) : 0;
    }
    int control(size_t index) const noexcept {
        return int(controlSnapshot(index) & 3) - 1;
    }
    bool offer(size_t output, std::string snapshot) {
        if (output >= outputCount || snapshot.size() > maximumSnapshot ||
            !running.load(std::memory_order_acquire)) return false;
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        if (!lock.owns_lock() || stopping) return false;
        pending[output].swap(snapshot);
        dirty[output] = true;
        lock.unlock();
        wake.notify_one();
        return true;
    }
    void stop() noexcept {
        running.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        wake.notify_one();
        if (worker.joinable()) worker.join(); // outside any rendering callback
    }
    ~BackgroundFiles() { stop(); }
};
}

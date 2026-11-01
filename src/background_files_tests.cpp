// SPDX-License-Identifier: MIT
#include "background_files.hpp"
#include <future>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include "renderer_files.hpp"
#include "renderer_status_file.hpp"
#endif
using namespace std::chrono_literals;
static void require(bool value, const char *why) { if (!value) throw std::runtime_error(why); }
template<class Test> static void eventually(Test test, const char *why) {
    auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!test()) {
        require(std::chrono::steady_clock::now() < deadline, why);
        std::this_thread::sleep_for(1ms);
    }
}
static void stalledFiles() {
    xpfsr::BackgroundFiles files;
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::atomic<unsigned> writes{};
    std::mutex outputMutex;
    std::array<std::string, 2> latest;
    files.start([] { return xpfsr::BackgroundFiles::Controls{1, 0}; },
        [&](size_t slot, const std::string &value) {
            if (++writes == 1) { entered.set_value(); gate.wait(); }
            std::lock_guard<std::mutex> lock(outputMutex);
            latest[slot] = value;
        });
    require(files.control(0) == 1 && files.control(1) == 0, "startup controls not captured");
    require(files.offer(0, "first"), "first offer refused");
    auto started = entered.get_future().wait_for(3s) == std::future_status::ready;
    if (!started) { release.set_value(); require(false, "worker did not enter I/O"); }
    auto render = std::async(std::launch::async, [&] {
        for (int i = 0; i < 10000; ++i) {
            require(files.offer(0, std::to_string(i)), "mailbox locked across I/O");
            require(files.control(1) == 0, "control read waited/changed during I/O");
        }
        require(files.offer(1, "NR snapshot"), "independent NR slot refused");
    });
    bool returned = render.wait_for(500ms) == std::future_status::ready;
    release.set_value();
    render.get();
    require(returned, "render offers waited for deliberately stalled disk operation");
    files.stop();
    require(latest[0] == "9999" && latest[1] == "NR snapshot", "stop lost latest pending status");
    require(writes <= 3, "snapshots formed an unbounded backlog");
    require(!files.offer(0, "after stop"), "stopped worker accepted a snapshot");
    std::cout << "PASS: 10,000 render offers/control reads finish with file write blocked; bounded queue and shutdown drain.\n";
}
static void controlsAndFailures() {
    xpfsr::BackgroundFiles files;
    std::atomic<int> phase{};
    std::atomic<unsigned> writes{};
    std::atomic<unsigned> readFailures{};
    files.start([&] {
        auto p = phase.load();
        if (p == 2) { ++readFailures; throw std::runtime_error("read failure"); }
        return p == 0 ? xpfsr::BackgroundFiles::Controls{0, 1} :
               p == 1 ? xpfsr::BackgroundFiles::Controls{1, 0} :
                        xpfsr::BackgroundFiles::Controls{-1, 9};
    }, [&](size_t, const std::string &) { ++writes; throw std::runtime_error("write failure"); });
    require(!files.offer(2, "bad slot") && !files.offer(0, std::string(xpfsr::BackgroundFiles::maximumSnapshot + 1, 'x')),
            "unbounded or invalid output accepted");
    phase = 1;
    eventually([&] { return files.control(0) == 1 && files.control(1) == 0; }, "live controls did not refresh");
    auto revision = files.controlSnapshot(0);
    phase = 2;
    require(files.offer(0, "failure"), "test status refused");
    eventually([&] { return writes.load() >= 1; }, "write failure not exercised");
    eventually([&] { return readFailures.load() >= 1; }, "read failure not exercised");
    phase = 3;
    std::this_thread::sleep_for(220ms);
    require(files.control(0) == 1 && files.control(1) == 0, "invalid control erased last good state");
    require(files.controlSnapshot(0) == revision, "unchanged/stale file generated a fresh control event");
    require(files.offer(1, "second failure"), "worker stopped after exception");
    files.stop();
    require(writes == 2 && files.control(10) == -1, "I/O exception recovery/bounds failed");
    std::cout << "PASS: live controls, malformed values, read/write exceptions and bounded snapshots.\n";
}
static void stalledRead() {
    xpfsr::BackgroundFiles files;
    std::atomic<bool> block{false};
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    files.start([&] {
        if (block.exchange(false)) { entered.set_value(); gate.wait(); }
        return xpfsr::BackgroundFiles::Controls{0, 1};
    }, [](size_t, const std::string &) {});
    block = true;
    bool started = entered.get_future().wait_for(3s) == std::future_status::ready;
    if (!started) { release.set_value(); require(false, "worker did not enter control read"); }
    auto render = std::async(std::launch::async, [&] {
        require(files.control(0) == 0 && files.offer(0, "during blocked read"), "render path waited on control file");
    });
    bool returned = render.wait_for(500ms) == std::future_status::ready;
    release.set_value(); render.get(); files.stop();
    require(returned, "render control/status path waited for blocked file read");
    std::cout << "PASS: render-side cached controls/status offers complete while file read is blocked.\n";
}
#ifdef _WIN32
static void productionFiles() {
    auto root = std::filesystem::temp_directory_path() / (L"FSR file bridge \u00e9 " + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const std::array<const wchar_t *, 4> names{L"XPFSR24_CONTROL", L"XPFSR24_FG_CONTROL", L"XPFSR24_STATUS", L"XPFSR24_NR_STATUS"};
    std::array<std::wstring, 4> previous;
    struct Cleanup {
        const std::array<const wchar_t *, 4> &names;
        std::array<std::wstring, 4> &previous;
        std::filesystem::path root;
        ~Cleanup() {
            for (size_t i = 0; i < names.size(); ++i)
                SetEnvironmentVariableW(names[i], previous[i].empty() ? nullptr : previous[i].c_str());
            std::error_code error; std::filesystem::remove_all(root, error);
        }
    } cleanup{names, previous, root};
    for (size_t i = 0; i < names.size(); ++i) {
        wchar_t value[32768]{}; GetEnvironmentVariableW(names[i], value, 32768); previous[i] = value;
        SetEnvironmentVariableW(names[i], (root / std::to_wstring(i)).c_str());
    }
    { std::ofstream(root / L"0") << '1'; std::ofstream(root / L"1") << '0'; }
    xpfsr::RendererFiles files; files.start();
    require(files.control(0) == 1 && files.control(1) == 0, "production startup controls failed");
    app::Telemetry status{}; status.active = 1; status.recordings = 17;
    strcpy(status.reason, "FSR active"); strcpy(status.provider, "fixture");
    std::string token(32, 'a'); std::ostringstream out;
    launcher::writeRendererStatus(out, status, GetCurrentProcessId(), GetTickCount64(), token);
    require(files.offer(0, out.str()) && files.offer(1, "123 456 7\nmodel-session\nNR active\n9\n"), "production status offer failed");
    { std::ofstream(root / L"0") << '0'; std::ofstream(root / L"1") << '1'; }
    eventually([&] { return files.control(0) == 0 && files.control(1) == 1; }, "production live switch failed");
    files.stop();
    app::Telemetry found{};
    require(launcher::readRendererStatus(root / L"2", GetCurrentProcessId(), token, GetTickCount64(), found) &&
            found.active == 1 && found.recordings == 17, "status identity/format changed");
    std::ifstream nr(root / L"3"); std::string line; std::getline(nr, line);
    require(line == "123 456 7", "NR status format changed");
    require(!std::filesystem::exists(root / L"2.tmp") && !std::filesystem::exists(root / L"3.tmp"), "status replacement did not finish");
    std::cout << "PASS: production Unicode paths, atomic status replacement, identity parsing and live controls.\n";
}
#endif
int main() {
    try {
        stalledFiles(); stalledRead(); controlsAndFailures();
#ifdef _WIN32
        productionFiles();
#endif
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

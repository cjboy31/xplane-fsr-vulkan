// SPDX-License-Identifier: MIT
#include "session_logs.hpp"
#include "scene_frame.hpp"
#include "ground_camera.hpp"
#include "runtime_status.hpp"
#include <iostream>
static void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
static std::string read(const session::fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
int wmain(int argc, wchar_t **argv) {
    try {
        require(argc == 2, "Fresh fixture folder required");
        session::fs::path directory = argv[1];
        require(!session::fs::exists(directory), "Refusing to reuse a log fixture");
        session::fs::create_directories(directory);
        for (auto name : {L"renderer.log", L"renderer-status.txt", L"exit-status.txt",
                          L"launch.txt", L"renderer-status.txt.tmp"})
            session::write(directory / name, "old-session-contents\n");
        session::write(directory / L"keep-user-file.txt", "unrelated\n");
        auto first = session::prepare(directory, "mode=upscale provider=4.1.2 fg=Off");
        require(first.size() == 32, "Session token");
        for (auto name :
             {L"renderer.log", L"renderer-status.txt", L"exit-status.txt", L"launch.txt"})
            require(read((directory / name).wstring() + L".previous") == "old-session-contents\n",
                    "Previous diagnostics not preserved");
        require(read(directory / L"renderer.log").find("old-session") == std::string::npos &&
                    read(directory / L"renderer.log").find(first) != std::string::npos,
                "Old renderer session leaked");
        require(read(directory / L"renderer-status.txt").empty() &&
                    read(directory / L"exit-status.txt").empty() &&
                    !session::fs::exists(directory / L"renderer-status.txt.tmp"),
                "Stale status/exit/temp survived");
        session::write(directory / L"renderer.log", "first-session-only\n");
        auto second = session::prepare(directory, "mode=native");
        require(first != second &&
                    read(directory / L"renderer.log.previous") == "first-session-only\n",
                "Second session rotation");
        require(read(directory / L"keep-user-file.txt") == "unrelated\n",
                "Unrelated user log touched");
        auto file = directory / L"renderer.log";
        HANDLE locked =
            CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "Cannot lock reset fixture");
        bool refused = false;
        try {
            session::prepare(directory, "must refuse");
        } catch (...) {
            refused = true;
        }
        CloseHandle(locked);
        require(refused, "Locked log reset silently succeeded");
        app::SceneCameraStamp coherent;
        coherent.observe(17);
        coherent.observe(17);
        require(coherent.matches(17) && !coherent.matches(18),
                "Frame stamp matched a different camera");
        coherent.observe(18);
        require(!coherent.matches(18) && !coherent.matches(17), "Mixed-frame command accepted");
        app::SceneCameraStamp unknown;
        unknown.observe(0);
        unknown.observe(17);
        require(!unknown.matches(17), "Unknown scene stamp became trusted");
        app::Camera c{};
        c.valid = 1;
        c.internal = 0;
        app::GroundCameraSafety guard;
        require(!guard.update(c), "Legacy untagged metadata must be ignored");
        app::groundMetadata(c, true, 1.5f);
        require(guard.update(c), "Ground-contact entry");
        app::groundMetadata(c, true, 2.f);
        require(guard.update(c), "Ground-contact hysteresis hold");
        app::groundMetadata(c, true, NAN);
        require(guard.update(c), "Unknown clearance must not release an active guard");
        app::groundMetadata(c, true, 2.5f);
        require(!guard.update(c), "Ground-contact release");
        app::groundMetadata(c, true, .1f);
        require(guard.update(c), "Ground-contact second entry");
        app::groundMetadata(c, false, .1f);
        require(!guard.update(c), "Airborne views must remain unrestricted");
        app::groundMetadata(c, true, .1f);
        c.internal = 1;
        require(!guard.update(c), "Cockpit must remain unrestricted");
        launcher::RuntimeMonitor monitor;
        app::Telemetry status{};
        strcpy(status.reason, "Ground camera safety: native rendering");
        status.presents = 1;
        require(monitor.update(true, false, true, status, true, 100) ==
                    launcher::RuntimeState::groundSafety,
                "Ground fallback must have a clear status");
        std::cout << "PASS: ground-contact entry/hold/release, invalid clearance, airborne/cockpit "
                     "exclusions and clear status\n";
        std::cout << "PASS: per-session reset, unique IDs, bounded previous diagnostics, stale "
                     "status/exit/temp removal, locked-file refusal, unrelated-file preservation "
                     "and immutable scene camera tags\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

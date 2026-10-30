// SPDX-License-Identifier: MIT
#pragma once
#include "update_feed.hpp"
#include <cstdio>
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace session {
namespace fs = std::filesystem;
inline std::string utc() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    char s[40]{};
    snprintf(s, sizeof(s), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", t.wYear, t.wMonth, t.wDay,
             t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return s;
}
inline std::string id() {
    unsigned char bytes[16]{};
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Could not create a renderer session identifier");
    std::ostringstream s;
    for (auto x : bytes)
        s << std::hex << std::setw(2) << std::setfill('0') << unsigned(x);
    return s.str();
}
inline void write(const fs::path &path, const std::string &text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        throw std::runtime_error("Cannot reset session logs. Close other log writers or choose a "
                                 "writable application folder.");
    stream.write(text.data(), std::streamsize(text.size()));
    stream.flush();
    if (!stream)
        throw std::runtime_error(
            "Session log reset could not be flushed; simulator was not started.");
    stream.close();
    if (!stream || fs::file_size(path) != text.size())
        throw std::runtime_error(
            "Session log reset could not be verified; simulator was not started.");
}
inline std::string header(const std::string &token, const char *component) {
    return "XPFSR24 session=" + token + "\nversion=" + updates::currentVersion +
           " component=" + component + " pid=" + std::to_string(GetCurrentProcessId()) +
           "\nstarted_utc=" + utc() + "\n";
}
inline std::string prepare(const fs::path &directory, const std::string &choices) {
    fs::create_directories(directory);
    const auto token = id();
    // Copy every previous diagnostic first. Only these owned, known filenames
    // are touched. Opening the launcher again never calls this function.
    for (auto name : {L"renderer.log", L"renderer-status.txt", L"exit-status.txt", L"launch.txt"}) {
        auto path = directory / name;
        if (fs::exists(path))
            fs::copy_file(path, path.wstring() + L".previous",
                          fs::copy_options::overwrite_existing);
    }
    write(directory / L"renderer.log", header(token, "launcher-prepared"));
    for (auto name : {L"renderer-status.txt", L"exit-status.txt"})
        write(directory / name, "");
    auto temporary = directory / L"renderer-status.txt.tmp";
    if (fs::exists(temporary) && !fs::remove(temporary))
        throw std::runtime_error(
            "Cannot clear the previous renderer status temporary file; simulator was not started.");
    write(directory / L"launch.txt", header(token, "launcher") + choices + "\n");
    return token;
}
inline std::string rendererToken() {
    wchar_t token[40]{};
    auto length = GetEnvironmentVariableW(L"XPFSR24_SESSION", token, 40);
    if (length != 32)
        return id();
    std::string result;
    for (size_t i = 0; i < 32; ++i) {
        if (!((token[i] >= L'0' && token[i] <= L'9') || (token[i] >= L'a' && token[i] <= L'f')))
            return id();
        result += char(token[i]);
    }
    return result;
}
} // namespace session

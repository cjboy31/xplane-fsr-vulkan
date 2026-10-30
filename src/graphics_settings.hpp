// SPDX-License-Identifier: MIT
// Narrow, reversible preference edits. No private datarefs or simulator hooks.
#pragma once
#include <atomic>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
namespace graphics {
namespace fs = std::filesystem;
struct Options {
    int msaa{}, fxaa{}, scale{};
    int outputWidth{}, outputHeight{};
    bool keepUISize{true};
};
struct Change {
    std::string file, key, before, after;
};
inline void require(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}
inline std::string read(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    require(bool(f), "Cannot read graphics preferences; run X-Plane once first");
    return {std::istreambuf_iterator<char>(f), {}};
}
inline std::pair<size_t, size_t> valueRange(const std::string &bytes, const std::string &key) {
    size_t found = std::string::npos, end = 0, count = 0;
    for (size_t start = 0; start < bytes.size();) {
        size_t stop = bytes.find('\n', start);
        if (stop == std::string::npos)
            stop = bytes.size();
        size_t split = bytes.find_first_of(" \t\r", start);
        if (split < stop && bytes.substr(start, split - start) == key) {
            size_t value = bytes.find_first_not_of(" \t", split);
            size_t last = stop;
            if (last > start && bytes[last - 1] == '\r')
                --last;
            require(value <= last, "Empty graphics preference");
            found = value;
            end = last;
            ++count;
        }
        start = stop + 1;
    }
    require(count == 1, "Graphics preference missing or duplicated; unsupported preference format");
    return {found, end};
}
inline std::string get(const std::string &bytes, const std::string &key) {
    auto r = valueRange(bytes, key);
    return bytes.substr(r.first, r.second - r.first);
}
inline std::string set(std::string bytes, const std::string &key, const std::string &value) {
    auto r = valueRange(bytes, key);
    bytes.replace(r.first, r.second - r.first, value);
    return bytes;
}
inline void write(const fs::path &path, const std::string &bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    require(bool(f), "Cannot write graphics backup/preferences");
    f.write(bytes.data(), std::streamsize(bytes.size()));
    f.close();
    require(bool(f), "Writing graphics backup/preferences failed");
}
inline void atomicWrite(const fs::path &path, const std::string &bytes) {
    auto tmp = path;
    tmp += L".xpfsr24-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    require(!fs::exists(tmp),
            "A previous graphics temporary file remains; inspect it before retrying");
    write(tmp, bytes);
    if (!MoveFileExW(tmp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(tmp);
        throw std::runtime_error("Could not replace graphics preferences; backup is retained");
    }
}
inline void ensureSimulatorClosed() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    require(snapshot != INVALID_HANDLE_VALUE, "Cannot check whether X-Plane is running");
    PROCESSENTRY32W p{sizeof(p)};
    bool found = false;
    if (Process32FirstW(snapshot, &p))
        do {
            if (!_wcsicmp(p.szExeFile, L"X-Plane.exe")) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &p));
    CloseHandle(snapshot);
    require(!found,
            "Close every X-Plane process before applying or restoring graphics preferences");
}
inline bool supportsOutput(int width, int height) {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    for (DWORD n = 0; EnumDisplaySettingsW(nullptr, n, &mode); ++n)
        if (int(mode.dmPelsWidth) == width && int(mode.dmPelsHeight) == height)
            return true;
    return false;
}
inline fs::path prefs(const fs::path &root) { return root / L"Output/preferences"; }
inline void validateMappings(const fs::path &root) {
    auto settings = read(root / L"Resources/settings.txt");
    for (auto *mapping :
         {"SETTING_1 renopt_MSAA 0 hdr/msaa_hw 0", "SETTING_1 renopt_FXAA 0 hdr/use_post_aa 0",
          "SETTING_1 renopt_FSR_04 2 fsr/enable 1 fsr/quality 2"})
        require(settings.find(mapping) != std::string::npos,
                "X-Plane graphics mapping changed; automatic setup is disabled for this build");
}
inline Options current(const fs::path &root) {
    auto text = read(prefs(root) / L"X-Plane.prf");
    Options o;
    try {
        o.msaa = std::stoi(get(text, "renopt_MSAA"));
        o.fxaa = std::stoi(get(text, "renopt_FXAA"));
        o.scale = std::stoi(get(text, "renopt_FSR_04"));
    } catch (...) {
        throw std::runtime_error("Invalid X-Plane AA/render-scale preferences");
    }
    return o;
}
inline bool allowed(const Change &c) {
    if(c.file=="Miscellaneous.prf")return c.key=="pixel_multiplier";
    if (c.file == "X-Plane.prf")
        return c.key == "renopt_MSAA" || c.key == "renopt_FXAA" || c.key == "renopt_FSR_04";
    if (c.file == "X-Plane Window Positions.prf")
        return c.key == "monitor/0/m_x_res_full" || c.key == "monitor/0/m_y_res_full" ||
               c.key == "monitor/0/m_is_fullscreen";
    return false;
}
inline void commit(const fs::path &root, const std::vector<Change> &changes) {
    std::map<std::string, std::string> before, after;
    for (auto &c : changes) {
        require(allowed(c), "Unrecognized graphics backup entry");
        if (!before.count(c.file)) {
            before[c.file] = read(prefs(root) / c.file);
            after[c.file] = before[c.file];
        }
        require(get(after[c.file], c.key) == c.before,
                "A graphics preference changed after it was read; no overwrite was attempted");
        after[c.file] = set(after[c.file], c.key, c.after);
    }
    std::vector<std::string> done;
    try {
        for (auto &pair : after) {
            atomicWrite(prefs(root) / pair.first, pair.second);
            done.push_back(pair.first);
        }
    } catch (...) {
        for (auto &file : done)
            atomicWrite(prefs(root) / file, before[file]);
        throw;
    }
}
inline fs::path apply(const fs::path &root, const fs::path &backupRoot, const Options &o,
                      bool checkDisplay = true) {
    ensureSimulatorClosed();
    validateMappings(root);
    require(o.msaa >= 0 && o.msaa <= 3 && o.fxaa >= 0 && o.fxaa <= 1 && o.scale >= 0 &&
                o.scale <= 4,
            "Invalid graphics option");
    require((!o.outputWidth && !o.outputHeight) ||
                (o.outputWidth == 1920 && o.outputHeight == 1080) ||
                (o.outputWidth == 2560 && o.outputHeight == 1440) ||
                (o.outputWidth == 3840 && o.outputHeight == 2160),
            "Unsupported output preset");
    std::vector<Change> changes;
    std::map<std::string, std::string> originals;
    auto change = [&](const std::string &file, const std::string &key, const std::string &value) {
        if (!originals.count(file))
            originals[file] = read(prefs(root) / file);
        auto old = get(originals[file], key);
        if (old != value)
            changes.push_back({file, key, old, value});
    };
    change("X-Plane.prf", "renopt_MSAA", std::to_string(o.msaa));
    change("X-Plane.prf", "renopt_FXAA", std::to_string(o.fxaa));
    change("X-Plane.prf", "renopt_FSR_04", std::to_string(o.scale));
    if (o.outputWidth) {
        auto windows = read(prefs(root) / L"X-Plane Window Positions.prf");
        require(get(windows, "num_monitors") == "1",
                "Automatic output setup supports a single X-Plane monitor only");
        require(get(windows, "monitor/0/m_monitor") == "0",
                "Output preset requires X-Plane's primary monitor");
        require(
            !checkDisplay || supportsOutput(o.outputWidth, o.outputHeight),
            "The primary display does not advertise this resolution; use a supported output mode");
        change("X-Plane Window Positions.prf", "monitor/0/m_x_res_full",
               std::to_string(o.outputWidth));
        change("X-Plane Window Positions.prf", "monitor/0/m_y_res_full",
               std::to_string(o.outputHeight));
        change("X-Plane Window Positions.prf", "monitor/0/m_is_fullscreen", "wmgr_mode_fullscreen");
        // Preserve the logical UI canvas when this app changes output. Leave
        // font size and unrecognized/older accessibility layouts untouched.
        const auto misc=prefs(root)/L"Miscellaneous.prf";
        if(o.keepUISize&&fs::is_regular_file(misc)){
            auto text=read(misc);double oldHeight=0,multiplier=0;
            try{
                multiplier=std::stod(get(text,"pixel_multiplier"));
                if(get(windows,"monitor/0/m_is_fullscreen")=="wmgr_mode_fullscreen")oldHeight=std::stod(get(windows,"monitor/0/m_y_res_full"));
                else oldHeight=std::stod(get(windows,"monitor/0/m_window_bounds/3"))-std::stod(get(windows,"monitor/0/m_window_bounds/1"));
            }catch(...){oldHeight=0;}
            if(std::isfinite(multiplier)&&multiplier>=1&&multiplier<=4&&oldHeight>=720&&oldHeight<=8640&&oldHeight!=o.outputHeight){
                double scaled=std::clamp(multiplier*o.outputHeight/oldHeight,1.0,4.0);
                scaled=std::min(scaled,std::min(o.outputWidth/1024.0,o.outputHeight/768.0));
                scaled=std::max(1.0,scaled);
                std::ostringstream value;value<<std::fixed<<std::setprecision(6)<<scaled;
                change("Miscellaneous.prf","pixel_multiplier",value.str());
            }
        }
    }
    if (changes.empty())
        return {};
    static std::atomic<uint64_t> backupSequence{0};
    auto backup = backupRoot / (L"graphics-" + std::to_wstring(GetTickCount64()) + L"-" +
                                std::to_wstring(GetCurrentProcessId()) + L"-" +
                                std::to_wstring(backupSequence.fetch_add(1)));
    fs::create_directories(backupRoot);
    require(fs::create_directory(backup), "Graphics backup collision");
    for (auto &pair : originals)
        write(backup / pair.first, pair.second);
    write(backup / L"simulator.txt", fs::weakly_canonical(root).u8string());
    std::ostringstream manifest;
    for (auto &c : changes)
        manifest << std::quoted(c.file) << ' ' << std::quoted(c.key) << ' ' << std::quoted(c.before)
                 << ' ' << std::quoted(c.after) << '\n';
    write(backup / L"changes.txt", manifest.str());
    commit(root, changes);
    return backup;
}
inline void restore(const fs::path &root, const fs::path &backup) {
    ensureSimulatorClosed();
    require(read(backup / L"simulator.txt") == fs::weakly_canonical(root).u8string(),
            "That graphics backup belongs to another simulator folder");
    std::istringstream manifest(read(backup / L"changes.txt"));
    std::vector<Change> undo;
    Change c;
    while (manifest >> std::quoted(c.file) >> std::quoted(c.key) >> std::quoted(c.before) >>
           std::quoted(c.after)) {
        require(allowed(c), "Unrecognized graphics backup entry");
        undo.push_back({c.file, c.key, c.after, c.before});
    }
    require(manifest.eof() && !undo.empty(), "Graphics backup manifest is incomplete");
    commit(root, undo);
}
} // namespace graphics

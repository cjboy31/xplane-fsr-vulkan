// SPDX-License-Identifier: MIT
#pragma once
#include "update_feed.hpp"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
namespace updates {
namespace fs = std::filesystem;
struct Internet {
    HINTERNET h{};
    explicit Internet(HINTERNET value) : h(value) {
        require(h != nullptr, "Could not initialize the update connection");
    }
    ~Internet() { WinHttpCloseHandle(h); }
    Internet(const Internet &) = delete;
};
inline std::wstring widen(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    require(n > 0, "Invalid UTF-8 update metadata");
    std::wstring w(n, 0);
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), w.data(),
                                n) == n,
            "Invalid update text");
    return w;
}
inline std::wstring header(HINTERNET request, DWORD id) {
    DWORD bytes{};
    WinHttpQueryHeaders(request, id, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &bytes, nullptr);
    require(GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes <= 32768,
            "Missing or oversized update HTTP header");
    std::wstring value(bytes / sizeof(wchar_t), 0);
    require(WinHttpQueryHeaders(request, id, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &bytes,
                                nullptr),
            "Could not read update HTTP header");
    value.resize(bytes / sizeof(wchar_t));
    return value;
}
inline std::string transfer(std::wstring url, const fs::path &destination, uint64_t expected,
                            std::atomic<bool> &cancel, std::atomic<uint64_t> &progress) {
    bool binary = !destination.empty();
    Internet session(WinHttpOpen(L"XPlane-FSR3-4-FrameGen-Updater/0.6.1",
                                 WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0));
    require(WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 15000),
            "Could not set update timeouts");
    auto started = GetTickCount64();
    for (unsigned redirect = 0; redirect < 6; ++redirect) {
        require(!cancel, "Update canceled");
        URL_COMPONENTS u{sizeof(u)};
        u.dwHostNameLength = u.dwUrlPathLength = u.dwExtraInfoLength = u.dwUserNameLength =
            u.dwPasswordLength = DWORD(-1);
        require(WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &u), "Invalid update URL");
        std::wstring host(u.lpszHostName, u.dwHostNameLength);
        bool trusted =
            binary ? (host == L"github.com" || host == L"release-assets.githubusercontent.com" ||
                      host == L"objects.githubusercontent.com")
                   : host == L"api.github.com";
        require(trusted && u.nScheme == INTERNET_SCHEME_HTTPS && u.nPort == 443 &&
                    !u.dwUserNameLength && !u.dwPasswordLength &&
                    url.find(L'#') == std::wstring::npos,
                "Update redirect left the permitted GitHub HTTPS hosts");
        std::wstring path(u.lpszUrlPath, u.dwUrlPathLength);
        if (u.dwExtraInfoLength)
            path.append(u.lpszExtraInfo, u.dwExtraInfoLength);
        Internet connection(WinHttpConnect(session.h, host.c_str(), 443, 0));
        Internet request(WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE));
        DWORD disabled = WINHTTP_DISABLE_REDIRECTS, logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        require(
            WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &disabled,
                             sizeof(disabled)) &&
                WinHttpSetOption(request.h, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon)),
            "Could not restrict the update connection");
        const wchar_t *headers =
            binary ? L"Accept: application/octet-stream\r\n"
                   : L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        require(
            WinHttpSendRequest(request.h, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(request.h, nullptr),
            "Cannot reach GitHub Releases. Check the internet/proxy connection and try again");
        DWORD status{}, size = sizeof(status);
        require(WinHttpQueryHeaders(request.h,
                                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, nullptr),
                "Cannot read GitHub response");
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            // A renamed repository answers its old API URL with a redirect to
            // api.github.com/repositories/<id>/... Follow it; the host check at
            // the top of the loop still rejects anything but api.github.com.
            url = header(request.h, WINHTTP_QUERY_LOCATION);
            continue;
        }
        require(status != 403 && status != 429,
                "GitHub update checks are temporarily rate limited. Try again later");
        require(status == 200, "GitHub returned an error. No update was installed");
        std::ofstream file;
        if (binary) {
            file.open(destination, std::ios::binary | std::ios::trunc);
            require(bool(file), "Cannot create the update download file");
        }
        std::string body;
        uint64_t total{};
        char buffer[65536];
        for (;;) {
            require(!cancel, "Update canceled");
            require(GetTickCount64() - started < 600000, "Update download timed out");
            DWORD got{};
            require(WinHttpReadData(request.h, buffer, sizeof(buffer), &got),
                    "Update download was interrupted");
            if (!got)
                break;
            total += got;
            require(total <= (binary ? expected : 8 * 1024 * 1024),
                    "Update response exceeds the expected size");
            if (binary) {
                file.write(buffer, got);
                require(bool(file), "Cannot write the update download");
            } else
                body.append(buffer, got);
            progress = total;
        }
        if (binary) {
            file.close();
            require(bool(file) && total == expected, "Update download is incomplete");
        }
        return body;
    }
    throw std::runtime_error("Too many GitHub download redirects");
}
inline std::string sha256(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "Cannot read the downloaded installer");
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE object{};
    struct Cleanup {
        BCRYPT_ALG_HANDLE &a;
        BCRYPT_HASH_HANDLE &h;
        ~Cleanup() {
            if (h)
                BCryptDestroyHash(h);
            if (a)
                BCryptCloseAlgorithmProvider(a, 0);
        }
    } clean{algorithm, object};
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
            "SHA-256 unavailable");
    DWORD bytes{}, used{};
    require(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&bytes),
                              sizeof(bytes), &used, 0) >= 0,
            "SHA-256 object unavailable");
    std::vector<UCHAR> storage(bytes);
    require(BCryptCreateHash(algorithm, &object, storage.data(), bytes, nullptr, 0, 0) >= 0,
            "SHA-256 initialization failed");
    char buffer[65536];
    while (file) {
        file.read(buffer, sizeof(buffer));
        auto n = file.gcount();
        if (n)
            require(BCryptHashData(object, reinterpret_cast<PUCHAR>(buffer), ULONG(n), 0) >= 0,
                    "SHA-256 failed");
    }
    require(file.eof(), "Cannot read the complete downloaded installer");
    UCHAR digest[32]{};
    require(BCryptFinishHash(object, digest, sizeof(digest), 0) >= 0,
            "SHA-256 verification failed");
    std::ostringstream result;
    for (auto b : digest)
        result << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
    return result.str();
}
inline void verify(const fs::path &file, const Release &release) {
    require(fs::file_size(file) == release.bytes && "sha256:" + sha256(file) == release.digest,
            "Update verification failed. The installer will not run");
    std::ifstream in(file, std::ios::binary);
    char mz[2]{};
    in.read(mz, 2);
    require(mz[0] == 'M' && mz[1] == 'Z', "The update is not a Windows installer");
}
inline Release check(const std::string &current, Channel requested, std::atomic<bool> &cancel,
                     std::atomic<uint64_t> &progress) {
    return select(
        transfer(L"https://api.github.com/repos/cjboy31/xplane12-fsr-framegen/releases?per_page=100",
                 {}, 0, cancel, progress),
        current, requested, true);
}
} // namespace updates

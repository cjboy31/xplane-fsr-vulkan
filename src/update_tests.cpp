// SPDX-License-Identifier: MIT
#include "update_http.hpp"
#include <iostream>
using namespace updates;
static std::string release(const std::string &tag, bool preview = true,
                           const std::string &prefix = "XPlane-FSR3-4-FrameGen-") {
    auto filename = prefix + tag.substr(1) + "-Setup.exe";
    return "{\"draft\":false,\"prerelease\":" + std::string(preview ? "true" : "false") +
           ",\"tag_name\":\"" + tag +
           "\",\"name\":\"Update "
           "\\u2708\\ud83d\\ude80\",\"body\":\"Changes\\nNext\",\"html_url\":\"" +
           repository + "/releases/tag/" + tag + "\",\"assets\":[{\"name\":\"" + filename +
           "\",\"state\":\"uploaded\",\"size\":2048,\"digest\":\"sha256:" + std::string(64, 'a') +
           "\",\"browser_download_url\":\"" + repository + "/releases/download/" + tag + "/" +
           filename + "\"}]}";
}
template <class F> static void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (...) {
        rejected = true;
    }
    require(rejected, "Unsafe metadata was accepted");
}
int wmain(int argc, wchar_t **argv) {
    try {
        require(version("v0.5.10-alpha").key() > version("0.5.9").key(), "Numeric version order");
        require(version("0.5.4").key() > version("0.5.4-rc.9").key(), "Stable version order");
        auto feed = "[" + release("v0.5.4-alpha") + "," + release("v0.5.10-alpha") + "," +
                    release("v0.5.5", false) + "]";
        require(select(feed, "0.5.3-alpha", true).tag == "v0.5.10-alpha", "Preview selection");
        require(select(feed, "0.5.3-alpha", false).tag == "v0.5.5", "Stable channel selection");
        require(!select(feed, "0.6.0-alpha", true), "Downgrade rejected");
        require(!select("[" + release("v0.5.4-alpha") + "]", "0.5.4-alpha", true),
                "Equal version rejected");
        auto mixed = "[" + release("v0.5.8-alpha") + "," + release("v0.5.9-experimental.1") + "," +
                     release("v0.5.10-alpha") + "," + release("v0.5.9-experimental.2") + "]";
        require(select(mixed, "0.5.7-alpha", true).tag == "v0.5.10-alpha",
                "Regular preview must never select experimental builds");
        require(select(mixed, "0.5.8-experimental.1", true).tag == "v0.5.9-experimental.2",
                "Experimental channel must never select regular builds");
        require(!select(mixed, "0.5.8-experimental.1", false), "Experimental preview opt-out");
        auto legacy = release("v0.5.5-alpha", true, "XPlane-FSR-Ghosting-Fix-");
        auto branded = release("v0.5.5-alpha");
        auto legacyJson = Parser(legacy).parse();
        auto assetStart = legacy.find("[{\"name\"");
        auto legacyAsset = legacy.substr(assetStart + 1, legacy.size() - assetStart - 3);
        auto both = branded;
        both.insert(both.size() - 2, "," + legacyAsset);
        require(select("[" + legacy + "]", "0.5.4-alpha", true)
                        .assetURL.find("XPlane-FSR-Ghosting-Fix-") != std::string::npos,
                "Legacy filename accepted");
        require(select("[" + both + "]", "0.5.4-alpha", true)
                        .assetURL.find("XPlane-FSR3-4-FrameGen-") != std::string::npos,
                "New filename preferred when both aliases are present");
        auto missing = release("v0.5.4-alpha");
        missing.replace(missing.find("-Setup.exe"), 10, "-Other.exe");
        require(!select("[" + missing + "]", "0.5.3-alpha", true), "Missing installer skipped");
        auto checkBad = [&](std::string from, std::string to) {
            auto bad = release("v0.5.4-alpha");
            auto at = bad.find(from);
            require(at != std::string::npos, "Test replacement missing");
            bad.replace(at, from.size(), to);
            rejects([&] { select("[" + bad + "]", "0.5.3-alpha", true); });
        };
        checkBad("https://github.com/", "http://github.com/");
        checkBad("sha256:", "sha512:");
        checkBad("\"size\":2048", "\"size\":268435457");
        checkBad("/releases/download/", "/releases/download/../");
        checkBad("\"draft\":false", "\"draft\":false,\"draft\":false");
        rejects([] { Parser("[1,]").parse(); });
        rejects([] { Parser("{\"x\":01}").parse(); });
        rejects([] { Parser("\"\\ud800a\"").parse(); });
        rejects([] { Parser("[]x").parse(); });
        rejects([] { Parser(std::string(49, '[') + std::string(49, ']')).parse(); });
        rejects([] { version("1.2.3-alpha.01"); });
        rejects([] { version("1.2.3/evil"); });
        auto unicode = select("[" + release("v0.5.4-alpha") + "]", "0.5.3-alpha", true);
        require(unicode.notes == "Changes\nNext" && widen(unicode.name).size() > 8,
                "Escaped release notes");
        wchar_t temp[MAX_PATH]{}, filename[MAX_PATH]{};
        require(GetTempPathW(MAX_PATH, temp) && GetTempFileNameW(temp, L"xpu", 0, filename),
                "Cannot create integrity fixture");
        fs::path fixture = filename;
        struct RemoveFixture {
            fs::path path;
            ~RemoveFixture() {
                std::error_code ec;
                fs::remove(path, ec);
            }
        } cleanup{fixture};
        std::string content(2048, 'x');
        content[0] = 'M';
        content[1] = 'Z';
        {
            std::ofstream file(fixture, std::ios::binary | std::ios::trunc);
            file << content;
        }
        Release integrity;
        integrity.bytes = content.size();
        integrity.digest = "sha256:" + sha256(fixture);
        verify(fixture, integrity);
        {
            std::fstream file(fixture, std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(5);
            file.put('z');
        }
        rejects([&] { verify(fixture, integrity); });
        integrity.digest = "sha256:" + sha256(fixture);
        ++integrity.bytes;
        rejects([&] { verify(fixture, integrity); });
        --integrity.bytes;
        {
            std::fstream file(fixture, std::ios::binary | std::ios::in | std::ios::out);
            file.put('X');
        }
        integrity.digest = "sha256:" + sha256(fixture);
        rejects([&] { verify(fixture, integrity); });
        if (argc > 1 && std::wstring(argv[1]) == L"--live") {
            std::atomic<bool> cancel{};
            std::atomic<uint64_t> progress{};
            auto latest = check("0.5.3-alpha", true, cancel, progress);
            std::cout << "Live public feed: " << progress
                      << " bytes; newer compatible installer: " << latest.tag << '\n';
            if (argc > 2 && latest) {
                fs::path path = argv[2];
                require(!fs::exists(path), "Test download destination already exists");
                transfer(widen(latest.assetURL), path, latest.bytes, cancel, progress);
                verify(path, latest);
                std::cout << "Live download verified: " << latest.tag << "; " << latest.bytes
                          << " bytes; " << sha256(path) << '\n';
            }
        }
        std::cout << "PASS: updater release selection, channels, version ordering, strict "
                     "metadata, Unicode and rejection cases\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

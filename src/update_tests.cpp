// SPDX-License-Identifier: MIT
#include "update_http.hpp"
#include "update_preferences.hpp"
#include "framegen_policy.hpp"
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
        require(select(feed, "0.5.3-alpha", Channel::Release).tag == "v0.5.10-alpha", "Preview selection");
        require(select(feed, "0.5.3-alpha", Channel::Release).tag == "v0.5.10-alpha", "Normal includes alpha releases");
        require(!select(feed, "0.6.0-alpha", Channel::Release), "Downgrade rejected");
        require(!select("[" + release("v0.5.4-alpha") + "]", "0.5.4-alpha", Channel::Release),
                "Equal version rejected");
        {
            // v2 private Test channel: draft releases and test-* tags are never
            // offered to users, even if a test release is published by mistake.
            auto draft = release("v9.0.0-rc.1", true);
            draft.replace(draft.find("\"draft\":false"), 14, "\"draft\":true");
            std::string testTag = release("v2.0.0-beta.windows.9");
            for (std::size_t at; (at = testTag.find("v2.0.0-beta.windows.9")) != std::string::npos;)
                testTag.replace(at, 21, "test-2.0.0.9");
            const auto hidden = "[" + draft + "," + testTag + "]";
            require(!select(hidden, "2.0.0-rc.1", Channel::Release), "Draft test build reached Release Candidates");
            require(!select(hidden, "2.0.0-beta.windows.1", Channel::Beta), "Test tag reached Betas");
            require(select("[" + release("v2.0.0-rc.2") + "]", "2.0.0-rc.1", Channel::Release).tag == "v2.0.0-rc.2",
                    "Release Candidates upgrade in order");
        }
        {
            // A feed reported under the pre-v2 repository name is still accepted,
            // with its download URL checked against that same name.
            auto legacy = release("v2.0.0-rc.2");
            const std::string now = repository, old = legacyRepository;
            for (std::size_t at; (at = legacy.find(now)) != std::string::npos;) legacy.replace(at, now.size(), old);
            require(select("[" + legacy + "]", "2.0.0-rc.1", Channel::Release).tag == "v2.0.0-rc.2", "Legacy repository name rejected");
            auto mixed = legacy;
            mixed.replace(mixed.find(old + "/releases/download/"), old.size(), now);
            rejects([&] { select("[" + mixed + "]", "2.0.0-rc.1", Channel::Release); });
        }
        auto tracks = "[" + release("v0.6.3-alpha") + "," + release("v0.6.0-experimental.1") +
                      "," + release("v0.5.9-experimental.2") + "]";
        require(select(tracks, "0.6.2-alpha", Channel::Release).tag == "v0.6.3-alpha",
                "Experimental never leaks into Normal");
        require(!select(tracks, "0.6.3-alpha", Channel::Beta),
                "Older channel requires explicit channel-switch opt-in");
        auto switchTo = select(tracks, "0.6.3-alpha", Channel::Beta, true);
        require(switchTo.tag == "v0.6.0-experimental.1" && switchTo.channelSwitch,
                "Explicit switch offers latest Experimental even when numerically older");
        auto switchBack = select(tracks, "0.6.0-experimental.1", Channel::Release, true);
        require(switchBack.tag == "v0.6.3-alpha" && switchBack.channelSwitch,
                "Explicit return to Normal");
        require(!select(tracks, "0.6.1-experimental.1", Channel::Beta, true),
                "Ordinary Experimental update never downgrades");
        require(!select(tracks, "0.6.0-experimental.1", Channel::Beta, true),
                "Experimental equal version never loops");
        auto revisions = "[" + release("v0.6.4-experimental.2") + "," +
                         release("v0.6.4-experimental.10") + "," + release("v9.0.0-alpha") + "]";
        require(select(revisions, "0.6.4-experimental.1", Channel::Beta).tag ==
                    "v0.6.4-experimental.10", "Experimental revision ordering and isolation");
        require(!select("[" + release("v0.6.4-experimental.2", false) + "]",
                        "0.6.4-experimental.1", Channel::Beta),
                "Experimental must be marked prerelease");
        rejects([] { version("1.2.3-experimental.01"); });
        rejects([] { version("1.2.3-experimental-evil.1"); });
        // Numeric Release entries must have the matching production badge.
        auto finalEntry=release("v0.7.8",false);
        auto betaTracks="["+finalEntry+","+release("v0.7.9-beta.windows.2")+","+release("v0.7.9-beta.windows.10")+","+release("v0.7.8-experimental.9")+"]";
        require(select(betaTracks,"0.7.7-alpha",Channel::Release).tag=="v0.7.8","Beta must never leak into Release");
        require(select(betaTracks,"0.7.7-experimental.1",Channel::Beta).tag=="v0.7.9-beta.windows.10","Legacy Experimental upgrades into Beta in numeric revision order");
        require(select(betaTracks,"0.7.8",Channel::Beta,true).channelSwitch,"Release can switch to Beta");
        require(select(betaTracks,"0.7.9-beta.windows.10",Channel::Release,true).tag=="v0.7.8","Explicit Beta return can install an older Release");
        require(!select(betaTracks,"0.7.9-beta.windows.10",Channel::Beta),"Beta equal version must not reinstall");
        require(channel(version("1.2.3-beta.1"))==Channel::Beta,"Ordinary beta tags stay out of Release");
        rejects([] { version("1.2.3-beta.linux.1"); });
        rejects([] { version("1.2.3-beta.windows.01"); });
        auto legacy = release("v0.5.5-alpha", true, "XPlane-FSR-Ghosting-Fix-");
        auto branded = release("v0.5.5-alpha");
        auto legacyJson = Parser(legacy).parse();
        auto assetStart = legacy.find("[{\"name\"");
        auto legacyAsset = legacy.substr(assetStart + 1, legacy.size() - assetStart - 3);
        auto both = branded;
        both.insert(both.size() - 2, "," + legacyAsset);
        require(select("[" + legacy + "]", "0.5.4-alpha", Channel::Release)
                        .assetURL.find("XPlane-FSR-Ghosting-Fix-") != std::string::npos,
                "Legacy filename accepted");
        require(select("[" + both + "]", "0.5.4-alpha", Channel::Release)
                        .assetURL.find("XPlane-FSR3-4-FrameGen-") != std::string::npos,
                "New filename preferred when both aliases are present");
        auto missing = release("v0.5.4-alpha");
        missing.replace(missing.find("-Setup.exe"), 10, "-Other.exe");
        require(!select("[" + missing + "]", "0.5.3-alpha", Channel::Release), "Missing installer skipped");
        auto checkBad = [&](std::string from, std::string to) {
            auto bad = release("v0.5.4-alpha");
            auto at = bad.find(from);
            require(at != std::string::npos, "Test replacement missing");
            bad.replace(at, from.size(), to);
            rejects([&] { select("[" + bad + "]", "0.5.3-alpha", Channel::Release); });
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
        auto unicode = select("[" + release("v0.5.4-alpha") + "]", "0.5.3-alpha", Channel::Release);
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
        graphics::write(fixture, "[Updates]\r\nPreviewReleases=0\r\n[FeatureVideo]\r\nMuted=1\r\n");
        require(loadChannel(fixture) == installedChannel(), "Legacy preference migration");
        for (auto old : {L"normal", L"experimental"}) {
            launcher::writeSetting(L"Updates",L"Channel",old,fixture.c_str());
            require(loadChannel(fixture)==(!wcscmp(old,L"normal")?Channel::Release:Channel::Beta),"Legacy channel choice migration");
        }
        for (auto c : {Channel::Release, Channel::Beta, Channel::Release}) {
            saveChannel(fixture, c);
            require(loadChannel(fixture) == c, "Release channel must survive reopen");
            require(launcher::readSetting(fixture,L"Updates",L"Channel")==(c==Channel::Beta?L"beta":L"release"),"Save only Release/Beta channel names");
            launcher::save(fixture, launcher::Preferences{});
            require(loadChannel(fixture) == c &&
                        launcher::readSetting(fixture, L"FeatureVideo", L"Muted") == L"1",
                    "Launcher saves preserve channel and other independent preferences");
        }
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
            auto latest = check("0.5.3-alpha", Channel::Release, cancel, progress);
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
            auto experiment = check("0.6.3-alpha", Channel::Beta, cancel, progress);
            require(bool(experiment) && experiment.channelSwitch &&
                        version(experiment.tag).beta,
                    "Published Experimental installer must be discoverable across channels");
            auto regular = check(experiment.tag.substr(1), Channel::Release, cancel, progress);
            require(bool(regular) && regular.channelSwitch && !version(regular.tag).beta,
                    "Published Normal return installer must be discoverable");
            std::cout << "Live channel switch: " << experiment.tag << " -> " << regular.tag << '\n';
        }
        std::cout << "PASS: updater release selection, channels, version ordering, strict "
                     "metadata, Unicode and rejection cases\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

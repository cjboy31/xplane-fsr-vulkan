// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
namespace updates {
inline constexpr char currentVersion[] = "0.5.9-experimental.1";
inline constexpr char repository[] = "https://github.com/cjboy31/xplane-fsr-vulkan";
inline void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
// Small bounded JSON reader for the public release feed; no executable/script content.
struct Json {
    enum Type { null, boolean, number, string, array, object } type{null};
    std::string value;
    std::vector<Json> items;
    std::map<std::string, Json> fields;
    const Json &at(const char *key) const {
        auto i = fields.find(key);
        require(type == object && i != fields.end(), "Release metadata is incomplete");
        return i->second;
    }
    std::string str() const {
        require(type == string, "Expected a release text value");
        return value;
    }
    bool yes() const {
        require(type == boolean, "Expected a release boolean");
        return value == "true";
    }
    uint64_t size() const {
        require(type == number && !value.empty() &&
                    value.find_first_not_of("0123456789") == std::string::npos,
                "Invalid update size");
        return std::stoull(value);
    }
};
class Parser {
    const std::string &s;
    size_t i{};
    void space() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\r' || s[i] == '\n' || s[i] == '\t'))
            ++i;
    }
    char take() {
        require(i < s.size(), "Truncated release feed");
        return s[i++];
    }
    unsigned hex4() {
        unsigned n{};
        for (int k = 0; k < 4; ++k) {
            char c = take();
            auto p = std::string("0123456789abcdef")
                         .find(char(std::tolower(static_cast<unsigned char>(c))));
            require(p != std::string::npos, "Invalid JSON unicode escape");
            n = n * 16 + unsigned(p);
        }
        return n;
    }
    std::string quoted() {
        require(take() == '"', "Invalid JSON string");
        std::string r;
        for (;;) {
            unsigned char c = take();
            if (c == '"')
                return r;
            require(c >= 32, "Invalid JSON control character");
            if (c != '\\') {
                r += char(c);
                continue;
            }
            c = take();
            switch (c) {
            case '"':
            case '\\':
            case '/':
                r += char(c);
                break;
            case 'b':
                r += '\b';
                break;
            case 'f':
                r += '\f';
                break;
            case 'n':
                r += '\n';
                break;
            case 'r':
                r += '\r';
                break;
            case 't':
                r += '\t';
                break;
            case 'u': {
                unsigned n = hex4();
                if (n >= 0xd800 && n <= 0xdbff) {
                    require(take() == '\\' && take() == 'u', "Invalid JSON surrogate");
                    unsigned low = hex4();
                    require(low >= 0xdc00 && low <= 0xdfff, "Invalid JSON surrogate");
                    n = 0x10000 + ((n - 0xd800) << 10) + (low - 0xdc00);
                } else
                    require(n < 0xdc00 || n > 0xdfff, "Invalid JSON surrogate");
                if (n < 0x80)
                    r += char(n);
                else if (n < 0x800) {
                    r += char(0xc0 | (n >> 6));
                    r += char(0x80 | (n & 63));
                } else if (n < 0x10000) {
                    r += char(0xe0 | (n >> 12));
                    r += char(0x80 | ((n >> 6) & 63));
                    r += char(0x80 | (n & 63));
                } else {
                    r += char(0xf0 | (n >> 18));
                    r += char(0x80 | ((n >> 12) & 63));
                    r += char(0x80 | ((n >> 6) & 63));
                    r += char(0x80 | (n & 63));
                }
                break;
            }
            default:
                throw std::runtime_error("Invalid JSON escape");
            }
        }
    }
    Json read(unsigned depth) {
        require(depth < 48, "Release feed is too deeply nested");
        space();
        require(i < s.size(), "Empty release feed");
        Json j;
        char c = s[i];
        if (c == '"') {
            j.type = Json::string;
            j.value = quoted();
        } else if (c == '[' || c == '{') {
            ++i;
            j.type = c == '[' ? Json::array : Json::object;
            space();
            char end = c == '[' ? ']' : '}';
            if (i < s.size() && s[i] == end) {
                ++i;
                return j;
            }
            for (;;) {
                if (c == '[')
                    j.items.push_back(read(depth + 1));
                else {
                    space();
                    auto key = quoted();
                    space();
                    require(take() == ':', "Invalid JSON object");
                    require(j.fields.emplace(key, read(depth + 1)).second,
                            "Duplicate release metadata key");
                }
                space();
                char sep = take();
                if (sep == end)
                    break;
                require(sep == ',', "Invalid JSON separator");
            }
        } else if (c == 't' || c == 'f' || c == 'n') {
            std::string token = c == 't' ? "true" : c == 'f' ? "false" : "null";
            require(s.compare(i, token.size(), token) == 0, "Invalid JSON token");
            i += token.size();
            j.type = c == 'n' ? Json::null : Json::boolean;
            j.value = token;
        } else {
            j.type = Json::number;
            size_t start = i;
            if (s[i] == '-')
                ++i;
            require(i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])),
                    "Invalid JSON number");
            if (s[i] == '0')
                ++i;
            else
                while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
                    ++i;
            if (i < s.size() && s[i] == '.') {
                ++i;
                size_t b = i;
                while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
                    ++i;
                require(i > b, "Invalid JSON fraction");
            }
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                ++i;
                if (i < s.size() && (s[i] == '+' || s[i] == '-'))
                    ++i;
                size_t b = i;
                while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
                    ++i;
                require(i > b, "Invalid JSON exponent");
            }
            j.value = s.substr(start, i - start);
        }
        return j;
    }

  public:
    explicit Parser(const std::string &text) : s(text) {}
    Json parse() {
        require(s.size() <= 8 * 1024 * 1024, "Release feed exceeds size limit");
        auto j = read(0);
        space();
        require(i == s.size(), "Trailing release data");
        return j;
    }
};
struct Version {
    int major{}, minor{}, patch{}, stage{3}, revision{};
    bool experimental{};
    auto key() const { return std::make_tuple(major, minor, patch, stage, revision); }
};
inline Version version(std::string s) {
    if (!s.empty() && s[0] == 'v')
        s.erase(0, 1);
    require(!s.empty() && s.size() < 80, "Invalid update version");
    Version v;
    size_t i{};
    auto num = [&] {
        size_t b = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9')
            ++i;
        require(i > b && i - b <= 6 && (i - b == 1 || s[b] != '0'),
                "Invalid update version number");
        return std::stoi(s.substr(b, i - b));
    };
    v.major = num();
    require(i < s.size() && s[i++] == '.', "Invalid update version");
    v.minor = num();
    require(i < s.size() && s[i++] == '.', "Invalid update version");
    v.patch = num();
    if (i < s.size()) {
        require(s[i++] == '-', "Invalid update suffix");
        size_t b = i;
        while (i < s.size() && s[i] != '.')
            ++i;
        auto stage = s.substr(b, i - b);
        v.experimental = stage == "experimental";
        v.stage = stage == "alpha" || v.experimental ? 0
                  : stage == "beta"                  ? 1
                  : stage == "rc"                    ? 2
                                                     : -1;
        require(v.stage >= 0, "Unsupported update channel");
        if (i < s.size()) {
            ++i;
            v.revision = num();
        }
    }
    require(i == s.size(), "Invalid update version");
    return v;
}
struct Release {
    std::string tag, name, notes, url, assetURL, digest;
    uint64_t bytes{};
    explicit operator bool() const { return !tag.empty(); }
};
inline Release select(const std::string &feed, const std::string &current, bool previews) {
    auto root = Parser(feed).parse();
    require(root.type == Json::array, "GitHub did not return a release list");
    auto best = version(current);
    const bool experimental = best.experimental;
    Release chosen;
    for (const auto &r : root.items) {
        if (r.at("draft").yes())
            continue;
        auto tag = r.at("tag_name").str();
        Version v;
        try {
            v = version(tag);
        } catch (...) {
            continue;
        }
        if (tag.empty() || tag[0] != 'v' || v.key() <= best.key())
            continue;
        if (v.experimental != experimental)
            continue;
        bool preview = r.at("prerelease").yes();
        if (preview != (v.stage < 3) || (preview && !previews))
            continue;
        auto url = std::string(repository) + "/releases/tag/" + tag;
        require(r.at("html_url").str() == url, "Update links to an unexpected repository");
        const auto &assets = r.at("assets");
        require(assets.type == Json::array, "Invalid update assets");
        Release candidate;
        // New branding first; retain the 0.5.4 asset name during migration.
        for (const auto *prefix : {"XPlane-FSR3-4-FrameGen-", "XPlane-FSR-Ghosting-Fix-"}) {
            auto filename = std::string(prefix) + tag.substr(1) + "-Setup.exe";
            for (const auto &a : assets.items) {
                if (a.at("name").str() != filename)
                    continue;
                require(!candidate, "Duplicate update installer");
                if (a.at("state").str() != "uploaded")
                    continue;
                candidate = {tag,
                             r.at("name").str(),
                             r.at("body").type == Json::null ? "" : r.at("body").str(),
                             url,
                             a.at("browser_download_url").str(),
                             a.at("digest").str(),
                             a.at("size").size()};
                require(candidate.assetURL ==
                            std::string(repository) + "/releases/download/" + tag + "/" + filename,
                        "Unexpected update download URL");
                require(candidate.digest.size() == 71 &&
                            candidate.digest.substr(0, 7) == "sha256:" &&
                            candidate.digest.substr(7).find_first_not_of("0123456789abcdef") ==
                                std::string::npos,
                        "Update has no valid GitHub SHA-256 digest");
                require(candidate.bytes >= 1024 && candidate.bytes <= 256 * 1024 * 1024,
                        "Update installer size is outside limits");
            }
            if (candidate)
                break;
        }
        if (candidate) {
            best = v;
            chosen = std::move(candidate);
        }
    }
    return chosen;
}
} // namespace updates

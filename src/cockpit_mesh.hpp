// SPDX-License-Identifier: MIT
// Reads the user's installed SR22 geometry; no aircraft assets are embedded.
#pragma once
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace cockpit {
struct Point {
    float x{}, y{}, z{};
};
struct Quad {
    std::array<Point, 4> corners;
};
inline void require(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
inline std::string read(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    require(bool(f), "SR22 display geometry file unavailable");
    return {std::istreambuf_iterator<char>(f), {}};
}
inline std::vector<Quad> parseDisplays(const std::string &bytes, Point translation) {
    struct Vertex {
        Point p;
        float u, v;
    };
    std::vector<Vertex> vertices;
    std::vector<unsigned> indices;
    std::vector<Quad> quads;
    std::set<std::string> devices;
    std::istringstream lines(bytes);
    std::string line, device;
    unsigned animation = 0;
    while (std::getline(lines, line)) {
        std::istringstream row(line);
        std::string op;
        row >> op;
        if (op.empty() || op[0] == '#')
            continue;
        if (op == "VT") {
            Vertex v{};
            float nx, ny, nz;
            require(bool(row >> v.p.x >> v.p.y >> v.p.z >> nx >> ny >> nz >> v.u >> v.v),
                    "Invalid SR22 vertex");
            require(vertices.size() < 4096, "SR22 geometry limit");
            for (float value : {v.p.x, v.p.y, v.p.z, v.u, v.v})
                require(std::isfinite(value), "Nonfinite SR22 vertex");
            vertices.push_back(v);
        } else if (op == "IDX" || op == "IDX10") {
            unsigned n;
            for (unsigned i = 0; i < (op == "IDX" ? 1u : 10u); ++i) {
                require(bool(row >> n), "Invalid SR22 index");
                require(indices.size() < 16384, "SR22 index limit");
                indices.push_back(n);
            }
        } else if (op == "ANIM_begin")
            ++animation;
        else if (op == "ANIM_end") {
            require(animation > 0, "Unbalanced display animation");
            --animation;
        } else if (op == "ATTR_cockpit_device") {
            row >> device;
            require(device == "G1000_PFD1" || device == "G1000_MFD",
                    "Unsupported SR22 display device");
        } else if (op == "ATTR_no_cockpit")
            device.clear();
        else if (op == "TRIS" && !device.empty()) {
            unsigned first, count;
            require(bool(row >> first >> count) && count == 6 && first <= indices.size() &&
                        count <= indices.size() - first,
                    "Display must be a two-triangle quad");
            require(!animation, "Animated screen geometry is unsupported");
            require(devices.insert(device).second, "Duplicate display mesh");
            Quad q{};
            std::array<bool, 4> seen{};
            std::set<unsigned> unique;
            for (unsigned i = first; i < first + count; ++i) {
                unsigned index = indices[i];
                require(index < vertices.size(), "Display index outside vertex table");
                unique.insert(index);
                auto v = vertices[index];
                require((std::abs(v.u) < .001f || std::abs(v.u - 1) < .001f) &&
                            (std::abs(v.v) < .001f || std::abs(v.v - 1) < .001f),
                        "Unexpected display UV layout");
                unsigned corner = v.v > .5f ? (v.u > .5f ? 2 : 3) : (v.u > .5f ? 1 : 0);
                seen[corner] = true;
                q.corners[corner] = {v.p.x + translation.x, v.p.y + translation.y,
                                     v.p.z + translation.z};
            }
            require(unique.size() == 4 && seen[0] && seen[1] && seen[2] && seen[3],
                    "Display quad topology mismatch");
            quads.push_back(q);
        }
    }
    require(!animation && quads.size() == 2 && devices.size() == 2,
            "Expected the SR22 PFD and MFD");
    return quads;
}
inline std::vector<Quad> loadSr22(const std::filesystem::path &aircraft) {
    require(aircraft.filename() == L"Cirrus SR22.acf",
            "Cockpit restore currently supports the default Cirrus SR22 only");
    std::istringstream lines(read(aircraft));
    std::map<std::string, std::string> fields;
    std::string line, attachment;
    while (std::getline(lines, line)) {
        std::istringstream row(line);
        std::string p, key, value;
        if (row >> p >> key >> value && p == "P")
            fields[key] = value;
    }
    for (auto &pair : fields)
        if (pair.first.rfind("_obja/", 0) == 0 &&
            pair.first.find("/_v10_att_file_stl") != std::string::npos &&
            pair.second == "garmins.obj") {
            require(attachment.empty(), "Ambiguous SR22 display attachment");
            attachment = pair.first.substr(0, pair.first.find("/_v10_att_file_stl"));
        }
    require(!attachment.empty(), "SR22 G1000 attachment missing");
    auto number = [&](const std::string &key) {
        auto i = fields.find(key);
        require(i != fields.end(), "SR22 attachment field missing");
        size_t used = 0;
        float v = std::stof(i->second, &used);
        require(used == i->second.size() && std::isfinite(v), "Invalid SR22 attachment field");
        return v;
    };
    for (auto *name : {"/_v10_att_phi_ref", "/_v10_att_psi_ref", "/_v10_att_the_ref"})
        require(std::abs(number(attachment + name)) < .0001f,
                "Rotated display attachment unsupported");
    for (auto *name : {"/_v10_att_body", "/_v10_att_gear", "/_v10_att_wing"})
        require(number(attachment + name) == -1, "Moving display attachment unsupported");
    constexpr float feet = .3048f;
    Point translation{number(attachment + "/_v10_att_x_acf_prt_ref") * feet,
                      (number(attachment + "/_v10_att_y_acf_prt_ref") - number("acf/_cgY")) * feet,
                      (number(attachment + "/_v10_att_z_acf_prt_ref") - number("acf/_cgZ")) * feet};
    return parseDisplays(read(aircraft.parent_path() / L"objects/garmins.obj"), translation);
}
inline bool project(const std::vector<Quad> &quads, const double *bodyVP, float (&clip)[8][4]) {
    if (quads.size() != 2)
        return false;
    for (unsigned q = 0; q < 2; ++q)
        for (unsigned v = 0; v < 4; ++v) {
            auto p = quads[q].corners[v];
            double point[] = {p.x, p.y, p.z, 1};
            for (unsigned row = 0; row < 4; ++row) {
                double value = 0;
                for (unsigned c = 0; c < 4; ++c)
                    value += bodyVP[c * 4 + row] * point[c];
                if (!std::isfinite(value))
                    return false;
                clip[q * 4 + v][row] = float(value);
            }
            if (clip[q * 4 + v][3] <= .0001f)
                return false;
        }
    return true;
}
inline bool stableProjection(const float (&now)[8][4], const float (&previous)[8][4], unsigned w,
                             unsigned h) {
    for (unsigned i = 0; i < 8; ++i) {
        if (now[i][3] <= 0 || previous[i][3] <= 0)
            return false;
        float dx = (now[i][0] / now[i][3] - previous[i][0] / previous[i][3]) * w * .5f,
              dy = (now[i][1] / now[i][3] - previous[i][1] / previous[i][3]) * h * .5f;
        if (!std::isfinite(dx) || !std::isfinite(dy) || dx * dx + dy * dy > 4)
            return false;
    }
    return true;
}
} // namespace cockpit

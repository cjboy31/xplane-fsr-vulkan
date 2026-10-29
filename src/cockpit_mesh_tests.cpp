// SPDX-License-Identifier: MIT
#include "cockpit_mesh.hpp"
#include <iostream>
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (...) {
        failed = true;
    }
    cockpit::require(failed, "Expected invalid mesh to fail closed");
}
int wmain(int argc, wchar_t **argv) {
    try {
        std::string vertices =
            "I\n800\nOBJ\nVT -.8 -.5 .5 0 0 1 0 0\nVT -.2 -.5 .5 0 0 1 1 0\nVT -.2 .5 .5 0 0 1 1 "
            "1\nVT -.8 .5 .5 0 0 1 0 1\nVT .2 -.5 .5 0 0 1 0 0\nVT .8 -.5 .5 0 0 1 1 0\nVT .8 .5 "
            ".5 0 0 1 1 1\nVT .2 .5 .5 0 0 1 0 1\nIDX10 0 1 2 2 3 0 4 5 6 6\nIDX 7\nIDX 4\n";
        std::string commands = "ATTR_cockpit_device G1000_PFD1 4 -1 1\nTRIS 0 "
                               "6\nATTR_cockpit_device G1000_MFD 1 -1 1\nTRIS 6 6\n";
        auto quads = cockpit::parseDisplays(vertices + commands, {});
        cockpit::require(quads.size() == 2, "Two display quads");
        auto shifted = cockpit::parseDisplays(vertices + commands, {1, 2, 3});
        cockpit::require(std::abs(shifted[0].corners[0].x - .2f) < 1e-6 &&
                             std::abs(shifted[0].corners[0].y - 1.5f) < 1e-6,
                         "Attachment translation");
        rejects([&] {
            cockpit::parseDisplays(vertices + "ANIM_begin\n" + commands + "ANIM_end\n", {});
        });
        rejects([&] { cockpit::parseDisplays(vertices + commands + commands, {}); });
        auto invalid = commands;
        invalid.replace(invalid.find("TRIS 0 6"), 8, "TRIS 0 5");
        rejects([&] { cockpit::parseDisplays(vertices + invalid, {}); });
        double matrix[16]{};
        for (unsigned i = 0; i < 16; i += 5)
            matrix[i] = 1;
        float projected[8][4]{}, previous[8][4]{};
        cockpit::require(cockpit::project(quads, matrix, projected), "Projection");
        memcpy(previous, projected, sizeof(previous));
        cockpit::require(cockpit::stableProjection(projected, previous, 3840, 2160),
                         "Stable screen projection");
        projected[0][0] += .1f;
        cockpit::require(!cockpit::stableProjection(projected, previous, 3840, 2160),
                         "Camera jump bypass");
        matrix[15] = -1;
        cockpit::require(!cockpit::project(quads, matrix, projected), "Behind-camera bypass");
        if (argc > 1) {
            auto actual = cockpit::loadSr22(std::filesystem::path(argv[1]));
            cockpit::require(actual.size() == 2, "Actual SR22 mesh");
            std::cout
                << "PASS: user's SR22 PFD/MFD parsed at runtime; no aircraft geometry embedded.\n";
        }
        std::cout << "PASS: quad parsing, topology/animation refusal, attachment translation, "
                     "projection, camera stability and behind-camera bypass.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

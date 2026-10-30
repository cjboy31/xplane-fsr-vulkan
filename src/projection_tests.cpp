// SPDX-License-Identifier: MIT
#include "camera_math.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using Matrix = std::array<double, 16>;
using Point = std::array<double, 4>;
static void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
static Matrix identity() {
    Matrix m{};
    for (int i = 0; i < 16; i++)
        m[i] = (i % 5) == 0;
    return m;
}
static Point transformPoint(const Matrix &m, const Point &p) {
    Point r{};
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            r[y] += m[x * 4 + y] * p[x];
    return r;
}
static Point ndc(Point p) {
    for (int i = 0; i < 3; i++)
        p[i] /= p[3];
    p[3] = 1;
    return p;
}
static Matrix projection(float clipNear, float clipFar, bool asym = false) {
    float gl[16] = {1.56969f,        0,  0,  0, 0, 2.79055f,      0, 0, asym ? .2f : 0,
                    asym ? -.1f : 0, -1, -1, 0, 0, -2 * clipNear, 0};
    if (std::isfinite(clipFar)) {
        gl[10] = -(clipFar + clipNear) / (clipFar - clipNear);
        gl[14] = -2 * clipFar * clipNear / (clipFar - clipNear);
    }
    float vk[16];
    check(app::vulkanProjection(vk, gl) == app::ProjectionEncoding::ForwardOpenGL,
          "Forward OpenGL source recognized");
    Matrix m;
    for (int i = 0; i < 16; i++)
        m[i] = vk[i];
    return m;
}
int main() {
    try {
        unsigned points = 0;
        for (float clipNear : {.00807304f, .1f, 1.f})
            for (float clipFar : {INFINITY, 10000.f})
                for (bool asym : {false, true}) {
                    auto p = projection(clipNear, clipFar, asym);
                    Matrix inv{};
                    check(app::inverse(inv.data(), p.data()), "Native inverse");
                    check(std::abs(ndc(transformPoint(p, {0, 0, -clipNear, 1}))[2] - 1) < 2e-6,
                          "Native clipNear depth must be one");
                    check(ndc(transformPoint(p, {0, 0, -1000000, 1}))[2] < 1e-4,
                          "Far/sky depth tends to zero");
                    float native[16], again[16];
                    for (int i = 0; i < 16; i++)
                        native[i] = float(p[i]);
                    check(app::vulkanProjection(again, native) ==
                              app::ProjectionEncoding::ReverseVulkan,
                          "Already-native projection recognized");
                    check(!memcmp(native, again, sizeof(native)), "No double depth conversion");
                    auto previous = identity(), current = identity();
                    previous[12] = -.03;
                    current[12] = -.04;
                    current[13] = .01;
                    previous[13] = -.02;
                    Matrix currVP{}, prevVP{}, inverseCurrent{}, reprojection{};
                    auto pp = projection(clipNear * .8f, clipFar,
                                         asym); // Near changes with camera clearance.
                    app::multiply(currVP.data(), p.data(), current.data());
                    app::multiply(prevVP.data(), pp.data(), previous.data());
                    check(app::inverse(inverseCurrent.data(), currVP.data()),
                          "View-projection inverse");
                    app::multiply(reprojection.data(), prevVP.data(), inverseCurrent.data());
                    for (double z : {-1., -4., -10., -100., -1000.})
                        for (double x : {-.1, .1}) {
                            Point world{x, .05, z, 1};
                            auto clip = ndc(transformPoint(currVP, world));
                            auto recovered = ndc(transformPoint(inverseCurrent, clip));
                            for (int i = 0; i < 3; i++)
                                check(std::abs(recovered[i] - world[i]) < 1e-7,
                                      "Native depth reconstructs actual world position");
                            auto predicted = ndc(transformPoint(reprojection, clip)),
                                 expected = ndc(transformPoint(prevVP, world));
                            for (int i = 0; i < 2; i++)
                                check(std::abs(predicted[i] - expected[i]) * 1920 < 1e-6,
                                      "Motion matches direct previous projection");
                            points++;
                        }
                }
        // Live projection + measured reverse depth: ground height is metres, not mm.
        auto live = projection(.00807304f, INFINITY);
        Matrix inv{};
        app::inverse(inv.data(), live.data());
        auto eye = ndc(transformPoint(inv, {0, -.8, .0015, 1}));
        check(-eye[2] > 5 && -eye[2] < 6, "Live depth restores metre-scale distance");
        float bad[16]{}, out[16]{};
        check(app::vulkanProjection(out, bad) == app::ProjectionEncoding::Invalid,
              "Orthographic/empty input rejected");
        auto valid = projection(.1f, INFINITY);
        for (int i = 0; i < 16; i++)
            bad[i] = float(valid[i]);
        bad[0] = NAN;
        check(app::vulkanProjection(out, bad) == app::ProjectionEncoding::Invalid,
              "Nonfinite source rejected");
        std::cout << "PASS: " << points
                  << " world-depth/motion round trips, finite/infinite/asymmetric projections, "
                     "changing near plane, native pass-through and invalid input.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

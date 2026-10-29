// SPDX-License-Identifier: MIT
#include "camera_math.hpp"
#include <array>
#include <iostream>
#include <vector>
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
using Matrix = std::array<double, 16>;
static Matrix pose(unsigned n) {
    double angle = n * .003, c = double(float(std::cos(angle))), s = double(float(std::sin(angle)));
    // Rounded rotations, scale and large world translations expose a transpose approximation.
    return {c * 1.01,      0,    -s * 1.01,     0, 0, .99, 0, 0, s, 0, c, 0,
            1e7 + n * .25, 2500, -1e7 + n * .5, 1};
}
static double calculate(const float *pf, const Matrix &view, const Matrix &body,
                        const Matrix &previous, bool optimized, app::ProjectionCache &cache) {
    double p[16], vp[16], inverseVP[16], inverseP[16], inverseView[16], bodyVP[16], inverseBody[16],
        r[16], br[16];
    for (int i = 0; i < 16; ++i)
        p[i] = pf[i];
    app::multiply(vp, p, view.data());
    app::multiply(bodyVP, p, body.data());
    if (optimized) {
        check(cache.update(pf) && app::inverseTransform(inverseView, view.data()),
              "Optimized view inverse");
        memcpy(inverseP, cache.inverseMatrix, sizeof(inverseP));
        app::multiply(inverseVP, inverseView, inverseP);
        double inverseBodyView[16];
        check(app::inverseTransform(inverseBodyView, body.data()), "Optimized body inverse");
        app::multiply(inverseBody, inverseBodyView, inverseP);
    } else
        check(app::inverse(inverseVP, vp) && app::inverse(inverseP, p) &&
                  app::inverse(inverseView, view.data()) && app::inverse(inverseBody, bodyVP),
              "Baseline inverses");
    app::multiply(r, previous.data(), inverseVP);
    app::multiply(br, previous.data(), inverseBody);
    double sum = 0;
    for (int i = 0; i < 16; ++i)
        sum += r[i] + br[i] + inverseP[i] + inverseView[i];
    return sum;
}
int main() {
    try {
        float pf[16] = {1.3f, 0, 0, 0, 0, 1.7f, 0, 0, 0, 0, 0, -1, 0, 0, .1f, 0};
        app::ProjectionCache cache;
        check(cache.update(pf) && cache.update(pf), "Projection cache reuse");
        pf[0] = 1.8f;
        check(cache.update(pf) && std::abs(cache.matrix[0] - 1.8) < 1e-6, "Zoom invalidates cache");
        pf[0] = NAN;
        check(!cache.update(pf), "Nonfinite projection rejected");
        pf[0] = 1.3f;
        std::vector<Matrix> poses;
        for (unsigned n = 0; n < 1000; ++n) {
            auto m = pose(n);
            poses.push_back(m);
            double reference[16], fast[16], product[16];
            check(app::inverse(reference, m.data()) && app::inverseTransform(fast, m.data()),
                  "Invertible affine transform");
            for (int i = 0; i < 16; ++i)
                check(std::abs(reference[i] - fast[i]) <
                          1e-9 * std::max(1., std::abs(reference[i])),
                      "Affine inverse differs from double general inverse");
            app::multiply(product, m.data(), fast);
            for (int i = 0; i < 16; ++i)
                check(std::abs(product[i] - double(i % 5 == 0)) < 1e-7,
                      "Large-world inverse residual");
            app::ProjectionCache oldCache, newCache;
            auto old = calculate(pf, m, pose(n + 1), pose(n + 2), false, oldCache);
            auto now = calculate(pf, m, pose(n + 1), pose(n + 2), true, newCache);
            check(std::abs(old - now) < 1e-6 * std::max(1., std::abs(old)),
                  "Factored camera output differs");
        }
        double p[16], fallback[16], reference[16];
        for (int i = 0; i < 16; ++i)
            p[i] = pf[i];
        check(app::inverse(p, p) && app::inverseTransform(fallback, p) &&
                  app::inverse(reference, p),
              "Perspective fallback");
        for (int i = 0; i < 16; ++i)
            check(std::abs(fallback[i] - reference[i]) < 1e-10, "Fallback inverse mismatch");
        double singular[16]{};
        check(!app::inverseTransform(fallback, singular), "Singular transform rejected");
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        volatile double observed = 0;
        for (unsigned run = 0; run < 7; ++run)
            for (bool optimized : {false, true}) {
                app::ProjectionCache timed;
                LARGE_INTEGER start, end;
                QueryPerformanceCounter(&start);
                double total = 0;
                constexpr unsigned iterations = 50000;
                for (unsigned n = 0; n < iterations; ++n)
                    total += calculate(pf, poses[n % 1000], poses[(n + 1) % 1000],
                                       poses[(n + 2) % 1000], optimized, timed);
                QueryPerformanceCounter(&end);
                observed = total;
                std::cout << "CPU_TIMING run=" << run << " optimized=" << optimized << " ns="
                          << double(end.QuadPart - start.QuadPart) * 1e9 / frequency.QuadPart /
                                 iterations
                          << "\n";
            }
        std::cout << "PASS: 1000 rounded/scaled large-world transforms, projection cache, zoom, "
                     "invalid input and numerical equivalence. "
                  << observed << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}

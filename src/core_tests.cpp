// SPDX-License-Identifier: MIT
#include "shared.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
static void expect(bool value, const char *name) {
    if (!value)
        throw std::runtime_error(name);
}
int main() {
    try {
        double p[16] = {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, -1, 0, 0, .1, 0};
        double v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -53000, 2200, -41000, 1};
        double vp[16], inv[16], identity[16];
        app::multiply(vp, p, v);
        expect(app::inverse(inv, vp), "Perspective matrix inversion failed");
        app::multiply(identity, vp, inv);
        for (int i = 0; i < 16; ++i)
            expect(std::abs(identity[i] - double(i % 5 == 0)) < 1e-8,
                   "World-space inversion error");
        double singular[16]{};
        expect(!app::inverse(inv, singular), "Singular matrix accepted");
        double current[16], previous[16], reprojection[16];
        memcpy(current, vp, sizeof(vp));
        v[12] -= 2;
        app::multiply(previous, p, v);
        expect(app::inverse(inv, current), "Current camera inversion");
        app::multiply(reprojection, previous, inv);
        double world[4] = {53002, -2200, 40990, 1}, currClip[4]{}, oldClip[4]{}, calculated[4]{};
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k) {
                currClip[r] += current[k * 4 + r] * world[k];
                oldClip[r] += previous[k * 4 + r] * world[k];
            }
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k)
                calculated[r] += reprojection[k * 4 + r] * currClip[k];
        for (int i = 0; i < 4; ++i)
            expect(std::abs(calculated[i] - oldClip[i]) < 1e-7,
                   "Reprojection does not reproduce independent previous projection");
        for (int i = 1; i <= 64; ++i) {
            expect(app::halton(i, 2) >= -.5 && app::halton(i, 2) < .5, "Jitter X range");
            expect(app::halton(i, 3) >= -.5 && app::halton(i, 3) < .5, "Jitter Y range");
        }
        app::Shared shared{};
        shared.tag = app::magic;
        shared.version = app::abi;
        std::atomic<bool> done = false;
        std::thread writer([&] {
            for (uint64_t i = 1; i <= 100000; ++i) {
                app::Camera camera{};
                camera.valid = 1;
                camera.bodyValid = 1;
                camera.bodyReprojection[0] = float(i);
                camera.frame = i;
                camera.width = uint32_t(i);
                camera.height = uint32_t(i ^ 0xa5a5);
                InterlockedIncrement(&shared.cameraSequence);
                MemoryBarrier();
                shared.camera = camera;
                MemoryBarrier();
                InterlockedIncrement(&shared.cameraSequence);
                if (i % 128 == 0)
                    Sleep(1);
            }
            done = true;
        });
        unsigned reads = 0;
        while (!done) {
            app::Camera camera{};
            if (app::readCamera(&shared, camera)) {
                expect(camera.bodyValid && camera.bodyReprojection[0] == float(camera.frame) &&
                           camera.width == camera.frame &&
                           camera.height == (uint32_t(camera.frame) ^ 0xa5a5),
                       "Torn shared-memory camera snapshot");
                ++reads;
            }
        }
        writer.join();
        expect(reads >= 100, "No coherent camera samples read");
        std::cout << "PASS: matrix inversion, large-coordinate reprojection, jitter range and "
                     "100000 concurrent IPC publications ("
                  << reads << " coherent reads).\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

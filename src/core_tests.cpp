// SPDX-License-Identifier: MIT
#include "shared.hpp"
#include "camera_continuity.hpp"
#include "frame_sequence.hpp"
#include "framegen_callback_state.hpp"
#include "startup_readiness.hpp"
#include "frame_diagnostics.hpp"
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
        xpfsr::FrameDiagnostics resetLog;
        uint64_t detailed{}, resetCount{}, suppressedCount{};
        for (uint64_t i=1;i<=60000;++i) {
            if (resetLog.record(i,true,i*16)) {
                ++detailed;
                resetCount += resetLog.reportedResets;
                suppressedCount += resetLog.reportedSuppressed;
            }
        }
        expect(detailed<550,"Reset diagnostics wrote at frame rate");
        expect(resetCount+resetLog.resets==60000,"Rate-limited diagnostics lost reset counts");
        expect(suppressedCount+resetLog.suppressed==60000-detailed,"Suppressed reset detail count is wrong");
        xpfsr::FrameDiagnostics ordinary;
        expect(ordinary.record(1,false,0)&&!ordinary.record(5,false,1),"Normal diagnostic cadence changed");
        expect(ordinary.record(1000,false,2),"Periodic diagnostics were suppressed");
        expect(!ordinary.record(1001,true,3)&&ordinary.record(1002,true,2002),"Reset detail interval is wrong");
        expect(ordinary.reportedResets==2&&ordinary.reportedSuppressed==1,"Reset summary is incomplete");
        expect(ordinary.record(1003,true,1),"A clock discontinuity suppressed diagnostics forever");
        xpfsr::FrameDiagnostics otherDevice;
        expect(otherDevice.record(7,true,3),"Another device inherited the diagnostic throttle");
        double p[16] = {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, -1, 0, 0, .1, 0};
        app::Camera before{}, after{};
        app::StartupReadiness startup;
        for(uint64_t i=1;i<=29;++i)expect(!startup.observe(i,16.667f,i*110),"Startup enabled optional work too early");
        expect(startup.observe(30,16.667f,3300),"Coherent startup never became ready");
        expect(startup.observe(50,200.f,3400),"Later loading/cuts cleared completed startup readiness");
        app::StartupReadiness slow;expect(!slow.observe(1,200.f,1)&&!slow.observe(2,NAN,2),"Invalid/very slow startup enabled optional work");
        for(uint64_t i=3;i<80;++i)expect(!slow.observe(i,16.667f,3+i),"Fast scene samples bypassed the startup time budget");
        expect(!slow.observe(100,16.667f,4000),"A source gap retained a partial startup streak");
        app::StartupReadiness skippedIds;for(uint64_t i=1;i<=29;++i)expect(!skippedIds.observe(i*2,33.3f,i*110),"Skipped-ID startup enabled too soon");
        expect(skippedIds.observe(60,33.3f,3300),"Valid skipped scene IDs prevented startup completion");
        xpfsr::FrameSequence sequence;
        xpfsr::FrameCallbackState callbacks;bool reset=false;
        callbacks.prepared(10,false);callbacks.prepared(11,true);
        expect(callbacks.read(10,reset)&&!reset,"Delayed FG callback used a newer frame's reset or was rejected");
        expect(callbacks.read(11,reset)&&reset,"NR/camera reset lost in FG callback");
        expect(callbacks.consume(10,reset)&&!callbacks.consume(10,reset),"A repeated callback reused one frame's interpolation metadata");
        callbacks.prepared(27,false);
        expect(!callbacks.read(11,reset)&&!callbacks.read(999,reset),"Overwritten/unknown FG metadata accepted");
        expect(sequence.canPrepare() && !sequence.ready(), "Initial FG source state");
        sequence.prepared = 10;
        expect(!sequence.canPrepare() && !sequence.ready(), "Unsubmitted prepare was presented");
        sequence.submit(10, 10);
        expect(sequence.ready(), "Matching submitted FG source rejected");
        sequence.submit(11, 0);
        expect(!sequence.ready(), "Stale FG source matched a newer scene");
        sequence.finishPresent();
        expect(sequence.canPrepare(), "Skipped scene did not retire stale prepare");
        sequence.prepared = 12;
        sequence.submit(11, 0);
        sequence.finishPresent();
        expect(!sequence.canPrepare() && !sequence.ready(),
               "Future prepare was prematurely consumed");
        sequence.submit(12, 12);
        expect(sequence.ready(), "Future source did not become ready on submission");
        sequence.finishPresent();
        expect(!sequence.ready(), "One prepare was generated twice");
        before.valid = 1;
        before.width = 1920;
        before.height = 1080;
        before.forward[2] = -1;
        before.verticalFov = 1;
        after = before;
        after.position[0] = 2;
        expect(!app::cameraCut(before, after), "Ordinary camera movement reset history");
        after.position[0] = 101;
        expect(app::cameraCut(before, after), "Teleport retained history");
        after = before;
        after.internal = 1;
        expect(app::cameraCut(before, after), "View switch retained history");
        after = before;
        after.forward[0] = 1;
        after.forward[2] = 0;
        expect(app::cameraCut(before, after), "Rapid pan retained history");
        after = before;
        after.verticalFov += .2f;
        expect(app::cameraCut(before, after), "Abrupt zoom retained history");
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

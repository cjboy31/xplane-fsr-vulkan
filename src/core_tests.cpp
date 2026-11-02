// SPDX-License-Identifier: MIT
#include "shared.hpp"
#include "camera_continuity.hpp"
#include "frame_sequence.hpp"
#include "framegen_callback_state.hpp"
#include "startup_readiness.hpp"
#include "frame_diagnostics.hpp"
#include "render_safety.hpp"
#include "frame_counter.hpp"
#include <algorithm>
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
        app::FrameCounter counter;
        expect(counter.due(0),"Frame counter did not start");counter.sample(0,100,200,true);
        expect(!counter.ready&&!counter.due(999),"Counter displayed a partial sampling interval");
        counter.sample(2000,180,280,true);
        expect(counter.ready&&counter.realFPS==40&&counter.generatedFPS==40,"Counter did not divide actual source/generated deltas by elapsed time");
        counter.sample(3000,220,280,true);expect(counter.realFPS==40&&counter.generatedFPS==0,"FG selected but inactive produced fictitious extra FPS");
        counter.sample(4000,260,1,true);expect(counter.generatedFPS==0,"Restarted generated counter underflowed");
        counter.sample(5000,300,41,false);expect(counter.realFPS==40&&counter.generatedFPS==0,"Unavailable renderer reused stale FG rate");
        counter.sample(11000,600,400,true);expect(!counter.ready,"Counter included a loading/stalled sampling gap");
        counter.sample(10000,10,0,true);expect(!counter.ready,"Clock/source reset caused a bogus FPS spike");
        counter.reset();expect(!counter.ready&&!counter.seeded&&counter.realFPS==0&&counter.generatedFPS==0,"Hidden counter retained stale values");
        xpfsr::AllocationBackoff backoff;
        expect(backoff.allowed(100),"Initial allocation blocked");backoff.failed(100);
        expect(!backoff.allowed(101)&&!backoff.allowed(3099)&&backoff.allowed(3100),"Failed allocation retried at frame rate or never recovered");
        backoff.failed(UINT64_MAX-10);expect(!backoff.allowed(UINT64_MAX-1),"Retry timestamp overflowed");backoff.recovered();expect(backoff.allowed(0),"Allocation recovery remained blocked");
        expect(!xpfsr::presenterFinished(2,1)&&xpfsr::presenterFinished(2,2)&&xpfsr::presenterFinished(0,0),"Application completion substituted for SDK ownership");
        expect(!xpfsr::presenterDrainRequired(0,0,true,true),"Empty presenter caused an unnecessary drain");
        expect(!xpfsr::presenterDrainRequired(2,0,false,false)&&xpfsr::presenterDrainRequired(3,0,false,false),"Asynchronous source window was unbounded or disabled overlap");
        expect(xpfsr::presenterDrainRequired(1,0,true,false)&&xpfsr::presenterDrainRequired(1,0,false,true),"Compatibility or memory-pressure drain was skipped");
        // Exercise a long session with 4K-sized images and delayed completion.
        // A host fence alone must not free them; a successful SDK drain must.
        uint64_t issued=0,completed=0,peakPending=0,drains=0;
        for(uint64_t frame=0;frame<120000;++frame){
            ++issued;const auto pending=issued-completed;peakPending=std::max(peakPending,pending);
            expect(!xpfsr::presenterFinished(issued,completed),"Pending SDK source released by a host frame");
            if(xpfsr::presenterDrainRequired(issued,completed,false,false)){
                expect(pending==3,"Delayed SDK completion accumulated more than three source image sets");
                completed=issued;++drains;
            }
        }
        expect(peakPending==3&&drains==40000&&completed==issued,"Long-flight source ownership did not remain bounded");
        expect(xpfsr::presenterDrainRequired(UINT64_MAX,UINT64_MAX-3,false,false)&&!xpfsr::presenterDrainRequired(1,UINT64_MAX,false,false),"Presenter-window arithmetic overflowed");
        expect(!xpfsr::coldPresent(10,130)&&xpfsr::coldPresent(10,131)&&!xpfsr::coldPresent(UINT64_MAX,1),"Cold resource age overflowed or evicted active images");
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
        {
            // Airborne regression: 3.6 m forward per frame with a 2 cm near plane gave a
            // clip-space delta of ~180 (> the old bound of 100) on every frame.
            const double n = .02, f = 1.7320508;
            double proj[16] = {f / 1.7777778, 0, 0, 0, 0, f, 0, 0, 0, 0, 0, -1, 0, 0, n, 0};
            double v0[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, v1[16];
            memcpy(v1, v0, sizeof(v1));
            v1[14] = 3.6;
            double vp0[16], vp1[16], inv1[16], rep[16];
            app::multiply(vp0, proj, v0);
            app::multiply(vp1, proj, v1);
            expect(app::inverse(inv1, vp1), "Flight projection inversion failed");
            app::multiply(rep, vp0, inv1);
            double delta = 0;
            for (int i = 0; i < 16; ++i)
                delta = std::max(delta, std::abs(rep[i] - ((i % 5) == 0)));
            expect(delta > 100 && !app::reprojectionUnusable(rep),
                   "Forward flight reprojection treated as a camera cut");
            after = before;
            after.position[2] = -3.6f;
            expect(!app::cameraCut(before, after), "Forward flight step reset history");
            rep[5] = std::numeric_limits<double>::quiet_NaN();
            expect(app::reprojectionUnusable(rep), "Non-finite reprojection accepted");
        }
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

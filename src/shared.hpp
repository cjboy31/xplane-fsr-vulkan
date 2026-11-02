// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <windows.h>
namespace app {
constexpr uint32_t magic = 0x34524658, abi = 5;
struct Camera {
    uint64_t frame{}, ticks{};
    float reprojection[16]{}, inverseProjection[16]{};
    float bodyReprojection[16]{};
    float position[3]{}, up[3]{}, right[3]{}, forward[3]{};
    float nearPlane{}, farPlane{}, verticalFov{}, milliseconds{};
    float ownSphere[4]{}; // Deprecated sphere; tagged ground metadata in ground_camera.hpp.
    uint32_t width{}, height{}, reset{}, reverseZ{}, infiniteZ{}, valid{}, internal{}, bodyValid{};
    float screenClip[8][4]{};
    uint32_t screenCount{}, screenReady{};
};
struct Telemetry {
    uint64_t presents{}, recordings{}, bypasses{};
    uint32_t renderWidth{}, renderHeight{}, outputWidth{}, outputHeight{}, active{};
    char provider[64]{}, reason[240]{};
    uint64_t fgPrepared{}, fgGenerated{}, displayedPresents{};
    uint32_t fgActive{};
    char fgProvider[64]{}, fgReason[160]{};
    uint64_t cockpitRecordings{};
    uint32_t cockpitActive{}, cockpitScreens{};
    char cockpitReason[160]{};
    // v2 (ABI 5) per-frame CPU timers, averaged over about 2 s of presents:
    // the layer's own work on X-Plane's render thread, its deliberate waits
    // (frame cap, synchronized FG drain), time inside the present call
    // (driver / SDK pacing) and the present-to-present interval. Microseconds.
    uint32_t cpuWorkUs{}, cpuWaitUs{}, presentCallUs{}, frameIntervalUs{};
    // v2: driver-assisted pacing check for the FG swapchain ('1' met, '0' not;
    // then the reasons). Empty until an FG swapchain is created.
    char pacingCheck[240]{};
    // v2: opt-in CPU scheduling helper ('0' off; '1' then what it did).
    char cpuScheduling[200]{};
};
struct Shared {
    uint32_t tag{}, version{};
    volatile LONG cameraSequence{}, statusSequence{}, enabled{1};
    Camera camera{};
    Telemetry status{};
};
inline std::wstring mappingName(DWORD process) {
    return L"Local\\XPlaneFSR24_ABI5_" + std::to_wstring(process);
}
inline bool readCamera(Shared *shared, Camera &result) {
    if (!shared || shared->tag != magic || shared->version != abi)
        return false;
    for (int attempt = 0; attempt < 4; ++attempt) {
        LONG before = InterlockedCompareExchange(&shared->cameraSequence, 0, 0);
        if (before & 1)
            continue;
        MemoryBarrier();
        memcpy(&result, &shared->camera, sizeof(result));
        MemoryBarrier();
        if (before == InterlockedCompareExchange(&shared->cameraSequence, 0, 0))
            return result.valid != 0;
    }
    return false;
}
inline bool readStatus(Shared *shared, Telemetry &result) {
    if (!shared || shared->tag != magic || shared->version != abi)
        return false;
    for (int attempt = 0; attempt < 4; ++attempt) {
        LONG before = InterlockedCompareExchange(&shared->statusSequence, 0, 0);
        if (before & 1)
            continue;
        MemoryBarrier();
        memcpy(&result, &shared->status, sizeof(result));
        MemoryBarrier();
        if (before == InterlockedCompareExchange(&shared->statusSequence, 0, 0))
            return true;
    }
    return false;
}
inline void multiply(double *result, const double *a, const double *b) {
    double temp[16]{};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k)
                temp[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    memcpy(result, temp, sizeof(temp));
}
inline bool inverse(double *result, const double *a) {
    double m[4][8]{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            m[r][c] = a[c * 4 + r];
            m[r][c + 4] = r == c;
        }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::abs(m[r][c]) > std::abs(m[pivot][c]))
                pivot = r;
        if (std::abs(m[pivot][c]) < 1e-14)
            return false;
        for (int k = 0; k < 8; ++k) {
            double tmp = m[pivot][k];
            m[pivot][k] = m[c][k];
            m[c][k] = tmp;
        }
        double scale = m[c][c];
        for (double &v : m[c])
            v /= scale;
        for (int r = 0; r < 4; ++r)
            if (r != c) {
                double v = m[r][c];
                for (int k = 0; k < 8; ++k)
                    m[r][k] -= v * m[c][k];
            }
    }
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            result[c * 4 + r] = m[r][c + 4];
    return true;
}
inline float halton(uint64_t index, int base) {
    float value = 0, scale = 1;
    while (index) {
        scale /= base;
        value += scale * (index % base);
        index /= base;
    }
    return value - 0.5f;
}
} // namespace app

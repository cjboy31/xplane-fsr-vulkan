// SPDX-License-Identifier: MIT
// Camera/metadata publisher. Architecture informed by MotionVectors V2;
// new implementation, no private controls, texture-budget patches or binary hooks.
#define XPLM200 1
#define XPLM210 1
#define XPLM300 1
#define XPLM301 1
#define IBM 1
#include "cockpit_mesh.hpp"
#include "shared.hpp"
#include "camera_continuity.hpp"
#include "camera_math.hpp"
#include "ground_camera.hpp"
#include <XPLMDataAccess.h>
#include <XPLMMenus.h>
#include <XPLMPlanes.h>
#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
static HANDLE mapping{};
static app::Shared *shared{};
#include "in_sim_ui.inc"
static XPLMDataRef projection{}, world{}, bodyRef{}, widthRef{}, heightRef{}, viewRef{}, pauseRef{},
    externalRef{}, cameraAglRef{}, onGroundRef{};
static XPLMMenuID menu{};
static bool previousValid{};
static app::Camera previousCamera{};
static double previousVP[16]{};
static app::ProjectionCache projectionCache;
static app::ProjectionEncoding projectionEncoding = app::ProjectionEncoding::Invalid;
static double previousBodyVP[16]{};
static bool previousBodyValid{};
static int previousView{}, previousPaused{};
static uint64_t frame{};
static bool loadScreens = true, previousScreenValid = false;
static float previousScreenClip[8][4]{};
static std::vector<cockpit::Quad> displayMesh;
static void loadDisplayProfile() {
    loadScreens = false;
    previousScreenValid = false;
    displayMesh.clear();
    wchar_t enabled[8]{};
    if (!GetEnvironmentVariableW(L"XPFSR24_COCKPIT_RESTORE", enabled, 8) || !wcscmp(enabled, L"0"))
        return;
    char model[256]{}, path[2048]{};
    XPLMGetNthAircraftModel(0, model, path);
    try {
        displayMesh = cockpit::loadSr22(std::filesystem::u8path(path));
        XPLMDebugString(
            "XPFSR24: Supported cockpit display mesh loaded from user's aircraft; two screens.\n");
    } catch (const std::exception &e) {
        std::string message = "XPFSR24: cockpit restore inactive: ";
        message += e.what();
        message += '\n';
        XPLMDebugString(message.c_str());
    }
}
static float update(float elapsed, float, int, void *) {
    setupPresence.setVisible(setupVisible());setupPoll();
    if (loadScreens)
        loadDisplayProfile();
    if (!projection) {
        projection = XPLMFindDataRef("sim/graphics/view/projection_matrix_3d");
        if (!projection)
            projection = XPLMFindDataRef("sim/graphics/view/projection_matrix");
        world = XPLMFindDataRef("sim/graphics/view/world_matrix");
        bodyRef = XPLMFindDataRef("sim/graphics/view/acf_matrix");
        widthRef = XPLMFindDataRef("sim/graphics/view/window_width");
        heightRef = XPLMFindDataRef("sim/graphics/view/window_height");
        viewRef = XPLMFindDataRef("sim/graphics/view/view_type");
        pauseRef = XPLMFindDataRef("sim/time/paused");
        externalRef = XPLMFindDataRef("sim/graphics/view/view_is_external");
        cameraAglRef = XPLMFindDataRef("sim/graphics/view/view_elevation_agl_mtrs");
        onGroundRef = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    }
    if (!shared)
        return -1;
    app::Camera packet{};
    packet.frame = ++frame;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    packet.ticks = now.QuadPart;
    float sourceProjection[16]{}, pf[16]{}, vf[16]{};
    auto encoding = app::ProjectionEncoding::Invalid;
    if (projection && world && XPLMGetDatavf(projection, sourceProjection, 0, 16) == 16 &&
        XPLMGetDatavf(world, vf, 0, 16) == 16 &&
        (encoding = app::vulkanProjection(pf, sourceProjection)) !=
            app::ProjectionEncoding::Invalid) {
        if (projectionEncoding != encoding) {
            XPLMDebugString(encoding == app::ProjectionEncoding::ForwardOpenGL
                                ? "XPFSR24: camera projection OpenGL [-1,1] -> Vulkan reversed "
                                  "[1,0]; world/body motion and FSR depth share this convention.\n"
                                : "XPFSR24: camera projection already Vulkan reversed [1,0]; no "
                                  "second conversion.\n");
            projectionEncoding = encoding;
        }
        double p[16], v[16], vp[16], inv[16], invP[16], inverseView[16], reproj[16];
        for (int i = 0; i < 16; ++i) {
            p[i] = pf[i];
            v[i] = vf[i];
        }
        app::multiply(vp, p, v);
        if (projectionCache.update(pf) && app::inverseTransform(inverseView, v)) {
            memcpy(invP, projectionCache.inverseMatrix, sizeof(invP));
            app::multiply(inv, inverseView, invP);
            const int view = viewRef ? XPLMGetDatai(viewRef) : 0,
                      paused = pauseRef ? XPLMGetDatai(pauseRef) : 0;
            packet.reset = !previousValid || view != previousView || paused != previousPaused ||
                           elapsed <= 0 || elapsed > 0.25f;
            if (previousValid)
                app::multiply(reproj, previousVP, inv);
            else
                for (int i = 0; i < 16; ++i)
                    reproj[i] = (i % 5) == 0;
            double difference = 0;
            for (int i = 0; i < 16; ++i)
                difference = std::max(difference, std::abs(reproj[i] - ((i % 5) == 0)));
            if (difference > 100)
                packet.reset = 1;
            for (int i = 0; i < 16; ++i) {
                packet.reprojection[i] = float(reproj[i]);
                packet.inverseProjection[i] = float(invP[i]);
            }
            for (int k = 0; k < 3; ++k) {
                packet.position[k] = float(inverseView[12 + k]);
                packet.right[k] = float(inverseView[k]);
                packet.up[k] = float(inverseView[4 + k]);
                packet.forward[k] = float(-inverseView[8 + k]);
            }
            auto plane = [&](double z) {
                double wz = invP[10] * z + invP[14], ww = invP[11] * z + invP[15];
                return std::abs(ww) < 1e-10 ? INFINITY : float(std::abs(wz / ww));
            };
            float zero = plane(0), one = plane(1);
            packet.reverseZ = one < zero;
            packet.nearPlane = std::min(zero, one);
            packet.farPlane = std::max(zero, one);
            packet.infiniteZ = !std::isfinite(packet.farPlane) || packet.farPlane > 1e8f;
            packet.verticalFov = float(2 * std::atan(1 / std::abs(p[5])));
            packet.milliseconds = elapsed * 1000;
            packet.width = widthRef ? XPLMGetDatai(widthRef) : 0;
            packet.height = heightRef ? XPLMGetDatai(heightRef) : 0;
            packet.internal = externalRef ? !XPLMGetDatai(externalRef) : 0;
            if (!packet.internal && externalRef && cameraAglRef && onGroundRef) {
                const bool onGround = XPLMGetDatai(onGroundRef) != 0;
                // Cached public datarefs only: no terrain probing, camera
                // ownership, collision override or controller writes.
                app::groundMetadata(packet, onGround, onGround ? XPLMGetDataf(cameraAglRef) : 0.f);
            }
            float af[16]{};
            if (bodyRef && XPLMGetDatavf(bodyRef, af, 0, 16) == 16 &&
                std::abs(af[12]) + std::abs(af[13]) + std::abs(af[14]) > .05f) {
                double body[16], bodyVP[16], inverseBody[16], inverseBodyView[16],
                    bodyReprojection[16];
                for (int i = 0; i < 16; ++i)
                    body[i] = af[i];
                app::multiply(bodyVP, p, body);
                if (app::inverseTransform(inverseBodyView, body)) {
                    app::multiply(inverseBody, inverseBodyView, invP);
                    if (previousBodyValid && previousValid)
                        app::multiply(bodyReprojection, previousBodyVP, inverseBody);
                    else
                        for (int i = 0; i < 16; ++i)
                            bodyReprojection[i] = (i % 5) == 0;
                    packet.bodyValid = 1;
                    for (int i = 0; i < 16; ++i) {
                        packet.bodyReprojection[i] = float(bodyReprojection[i]);
                        if (!std::isfinite(packet.bodyReprojection[i]))
                            packet.bodyValid = 0;
                    }
                    if (!displayMesh.empty() && packet.internal &&
                        cockpit::project(displayMesh, bodyVP, packet.screenClip)) {
                        packet.screenCount = 2;
                        packet.screenReady =
                            previousScreenValid &&
                            cockpit::stableProjection(packet.screenClip, previousScreenClip,
                                                      packet.width, packet.height);
                        memcpy(previousScreenClip, packet.screenClip, sizeof(previousScreenClip));
                        previousScreenValid = true;
                    } else
                        previousScreenValid = false;
                    memcpy(previousBodyVP, bodyVP, sizeof(bodyVP));
                }
            }
            previousBodyValid = packet.bodyValid;
            packet.valid = std::isfinite(packet.nearPlane) && packet.nearPlane > 0 &&
                           packet.verticalFov > 0 && packet.width && packet.height;
            packet.reset |= app::cameraCut(previousCamera, packet);
            if (packet.reset)
                packet.screenReady = 0;
            previousCamera = packet;
            memcpy(previousVP, vp, sizeof(vp));
            previousView = view;
            previousPaused = paused;
            previousValid = packet.valid;
        }
    }
    if (!packet.valid) {
        previousValid = false;
        previousBodyValid = false;
        previousScreenValid = false;
        packet.screenReady = 0;
    }
    InterlockedIncrement(&shared->cameraSequence);
    MemoryBarrier();
    shared->camera = packet;
    MemoryBarrier();
    InterlockedIncrement(&shared->cameraSequence);
    return -1;
}
static void command(void *, void *item) {
    if(reinterpret_cast<intptr_t>(item)==1){setupShow();return;}
    if(reinterpret_cast<intptr_t>(item)==2){setupShow();setupPanel.act({insim::Kind::Changelog});return;}
    try{
        if(!setupPanel.configured())setupBind();
        if(setupPanel.configured()){setupPanel.act({insim::Kind::Processing});return;}
    }catch(const std::exception&e){XPLMDebugString((std::string("XPFSR24: processing change failed: ")+e.what()+"\n").c_str());return;}
    if (shared) {
        bool value = !InterlockedCompareExchange(&shared->enabled, 0, 0);
        InterlockedExchange(&shared->enabled, value);
        wchar_t path[32768]{};
        if (GetEnvironmentVariableW(L"XPFSR24_CONTROL", path, 32768))
            std::ofstream(std::filesystem::path(path)) << (value ? '1' : '0');
    }
}
PLUGIN_API int XPluginStart(char *name, char *signature, char *description) {
    strcpy(name, "FSR 3/4 + Frame Gen for X-Plane");
    strcpy(signature, "org.xpfsr24.alpha");
    strcpy(description, "FSR anti-aliasing, upscaling, frame generation and live settings");
    auto key = app::mappingName(GetCurrentProcessId());
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                 sizeof(app::Shared), key.c_str());
    if (!mapping)
        return 0;
    bool fresh = GetLastError() != ERROR_ALREADY_EXISTS;
    shared = static_cast<app::Shared *>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(app::Shared)));
    if (!shared) {
        CloseHandle(mapping);
        mapping = nullptr;
        return 0;
    }
    if (fresh) {
        memset(shared, 0, sizeof(*shared));
        shared->tag = app::magic;
        shared->version = app::abi;
        shared->enabled = 1;
        try{auto settings=setupEnvironment(L"XPFSR24_SETTINGS");if(!settings.empty())shared->enabled=launcher::load(settings).enabled?1:0;}catch(...){}
    }
    setupPresence.create(GetCurrentProcessId());
    int item = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "FSR 3/4 + Frame Gen", nullptr, 0);
    menu = XPLMCreateMenu("FSR 3/4 + Frame Gen", XPLMFindPluginsMenu(), item, command, nullptr);
    XPLMAppendMenuItem(menu,"Open settings",reinterpret_cast<void*>(1),0);
    XPLMAppendMenuItem(menu,"View changelog",reinterpret_cast<void*>(2),0);
    XPLMAppendMenuItem(menu, "Enable / disable temporal upscaling", nullptr, 0);
    XPLMDebugString("XPFSR24: alpha camera publisher loaded; Vulkan layer performs upscaling.\n");
    // Diagnose/recover the selected installation at plugin startup, even when
    // the setup window stays closed. Never let a setup failure disable camera
    // metadata or escape across the plugin ABI.
    try{setupBind();}catch(const std::exception&e){setupPanel.notice=e.what();XPLMDebugString((std::string("XPFSR24 setup startup: ")+e.what()+"\n").c_str());}catch(...){XPLMDebugString("XPFSR24 setup startup: unknown binding failure\n");}
    return 1;
}
PLUGIN_API int XPluginEnable() {
    previousValid = false;
    loadScreens = true;
    previousScreenValid = false;
    XPLMRegisterFlightLoopCallback(update, -1, nullptr);
    return 1;
}
PLUGIN_API void XPluginDisable() {
    setupHide();
    XPLMUnregisterFlightLoopCallback(update, nullptr);
    if (shared) {
        InterlockedIncrement(&shared->cameraSequence);
        shared->camera.valid = 0;
        InterlockedIncrement(&shared->cameraSequence);
    }
}
PLUGIN_API void XPluginStop() {
    setupStop();
    if (menu)
        XPLMDestroyMenu(menu);
    if (shared)
        UnmapViewOfFile(shared);
    if (mapping)
        CloseHandle(mapping);
    shared = nullptr;
    mapping = nullptr;
}
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int message, void *parameter) {
    if (message == XPLM_MSG_PLANE_LOADED && reinterpret_cast<intptr_t>(parameter) == 0)
        loadScreens = true;
}

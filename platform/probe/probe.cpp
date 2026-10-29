// SPDX-License-Identifier: MIT
// Read-only native-plugin/camera diagnostic. This is NOT an FSR renderer.
#include <XPLMDataAccess.h>
#include <XPLMMenus.h>
#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

static XPLMDataRef projection{}, world{}, width{}, height{};
static XPLMMenuID menu{};
static int menuItem = -1;
static bool registered{};
static std::string destination;
static unsigned samples{};

static bool validMatrix(XPLMDataRef ref) {
    float values[16]{};
    if (!ref || XPLMGetDatavf(ref, values, 0, 16) != 16)
        return false;
    double sum = 0;
    for (float x : values) {
        if (!std::isfinite(x)) return false;
        sum += std::abs(x);
    }
    return sum > 0.5;
}

static void report() {
    int simulator{}, sdk{};
    XPLMHostApplicationID host{};
    XPLMGetVersions(&simulator, &sdk, &host);
    const bool camera = validMatrix(projection) && validMatrix(world);
    // No user path, aircraft, controller, account or position is included.
    const std::string temporary = destination + ".new";
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << "{\n  \"schema\": 1,\n  \"version\": \"0.6.2-platform-preview.1\",\n"
            << "  \"plugin_loaded\": true,\n  \"fsr_renderer\": false,\n"
            << "  \"frame_generation\": false,\n  \"platform\": \""
#if APL
            << "macOS\",\n  \"renderer_required\": \"Metal\",\n"
#else
            << "Linux\",\n  \"renderer_required\": \"Vulkan\",\n"
#endif
            << "  \"simulator_version\": " << simulator << ",\n  \"sdk_version\": " << sdk
            << ",\n  \"camera_matrices_finite\": " << (camera ? "true" : "false")
            << ",\n  \"window_width\": " << (width ? XPLMGetDatai(width) : 0)
            << ",\n  \"window_height\": " << (height ? XPLMGetDatai(height) : 0)
            << ",\n  \"samples\": " << ++samples << "\n}\n";
        if (!out) {
            XPLMDebugString("XPFSR Native Probe: report write failed; check folder permissions.\n");
            return;
        }
    }
    if (std::rename(temporary.c_str(), destination.c_str()) != 0)
        XPLMDebugString("XPFSR Native Probe: atomic report replacement failed.\n");
}

static float tick(float, float, int, void*) {
    if (!projection) {
        projection = XPLMFindDataRef("sim/graphics/view/projection_matrix_3d");
        if (!projection) projection = XPLMFindDataRef("sim/graphics/view/projection_matrix");
        world = XPLMFindDataRef("sim/graphics/view/world_matrix");
        width = XPLMFindDataRef("sim/graphics/view/window_width");
        height = XPLMFindDataRef("sim/graphics/view/window_height");
    }
    report();
    return 2.0f;
}

static void clicked(void*, void*) { report(); }

PLUGIN_API int XPluginStart(char* name, char* signature, char* description) {
    std::strcpy(name, "FSR Native Compatibility Probe");
    std::strcpy(signature, "org.xplanefsr.nativeprobe");
    std::strcpy(description, "Read-only platform/camera diagnostics. No Metal/FSR injection or controller changes.");
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
    if (const char* path = std::getenv("XPFSR_NATIVE_REPORT"); path && *path)
        destination = path;
    else {
        char root[4096]{};
        XPLMGetSystemPath(root);
        destination = std::string(root) + "Output/preferences/XPFSRNativeReport.json";
    }
    menuItem = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "FSR Native Probe (diagnostics only)", nullptr, 0);
    menu = XPLMCreateMenu("FSR Native Probe", XPLMFindPluginsMenu(), menuItem, clicked, nullptr);
    XPLMAppendMenuItem(menu, "Write compatibility report (no FSR injection)", nullptr, 0);
    XPLMDebugString("XPFSR Native Probe: loaded. Diagnostics only; this plugin does not enable FSR or frame generation.\n");
    return 1;
}

PLUGIN_API int XPluginEnable() {
    if (!registered) {
        XPLMRegisterFlightLoopCallback(tick, 2.0f, nullptr);
        registered = true;
    }
    return 1;
}
PLUGIN_API void XPluginDisable() {
    if (registered) XPLMUnregisterFlightLoopCallback(tick, nullptr);
    registered = false;
}
PLUGIN_API void XPluginStop() {
    XPluginDisable();
    if (menu) XPLMDestroyMenu(menu);
    if (menuItem >= 0) XPLMRemoveMenuItem(XPLMFindPluginsMenu(), menuItem);
    menu = nullptr;
    menuItem = -1;
}
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void*) {}

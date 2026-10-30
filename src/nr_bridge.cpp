// SPDX-License-Identifier: MIT
// Original X-Plane adapter for the separately built MIT DLSSNR-AMD runtime.
// No NGX/D3D/ReShade/OptiScaler hook, NVIDIA DLL or model is linked or bundled.
#include "nr_api.hpp"
#include "nr_runtime.hpp"
#include "nr_pipeline_binary.hpp"
#include "nr_log.hpp"
#include "nr_build_cfg.hpp"
#include "nr_models.hpp"
#include "update_http.hpp"
#include <filesystem>
#include <algorithm>
#include <memory>
#include <mutex>
#include <cstdio>
#include <stdexcept>
namespace {
struct Context {
    xpfsr::nr::Host host;
    xpfsr::nr::Config config;
    std::wstring root;
    std::unique_ptr<nr::Runtime> runtime;
    nr::Controls controls;
};
std::mutex createLock;
std::once_flag initializeRuntime;
void error(char *out, uint32_t bytes, const char *message) {
    if (out && bytes)
        std::snprintf(out, bytes, "%s", message);
}
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
extern "C" __declspec(dllexport) void *XPFSR_NR_Create(const xpfsr::nr::Host *host,
                                                       const xpfsr::nr::Config *config,
                                                       char *reason, uint32_t bytes) noexcept {
    try {
        std::lock_guard<std::mutex> guard(createLock);
        require(host && config && host->size == sizeof(*host) && config->size == sizeof(*config) &&
                    host->version == xpfsr::nr::apiVersion &&
                    config->version == xpfsr::nr::apiVersion,
                "NR adapter ABI mismatch");
        require(config->root && config->width && config->height && config->controls.valid(),
                "Invalid NR configuration");
        const auto root = std::filesystem::canonical(config->root);
        const auto text = root.wstring();
        require(std::all_of(text.begin(), text.end(), [](wchar_t c) { return c < 128; }),
                "This experimental NR backend requires an ASCII installation path");
        const auto data = root / L"dlssnr-amd";
        const bool small = config->controls[xpfsr::nr::Model]==1 ||
            (config->controls[xpfsr::nr::Model]==0 && std::filesystem::is_regular_file(data / L"dlssnr-small.bin"));
        require(small || std::filesystem::is_regular_file(data / L"dlssnr.bin"),
                "NR model missing; import your own supported nvngx_dlssnr.dll in NR setup");
        auto model = data / (small ? L"dlssnr-small.bin" : L"dlssnr.bin");
        require(xpfsr::nr::acceptedModel(small, std::filesystem::file_size(model),
                                         updates::sha256(model)),
                "NR model integrity mismatch; import a supported local model again");
        // Never import ACO machine code or take settings from another mod's
        // environment. This build uses ordinary SPIR-V through the installed driver.
        std::call_once(initializeRuntime,[&]{nr::binary::disable("XPFSR uses driver-compiled SPIR-V only");nr::set_log_sink(host->log);});
        auto c = std::make_unique<Context>();
        c->host = *host;
        c->config = *config;
        c->root = text;
        c->config.root = c->root.c_str();
        nr::HostDevice device{};
        device.instance = host->instance;
        device.physical = host->physical;
        device.device = host->device;
        device.queue = host->queue;
        device.queue_family = host->queueFamily;
        device.physical_dispatch = host->physicalDispatch;
        device.buffer_device_address = true;
        if (host->lockQueue)
            device.queue_lock = [h = *host] { h.lockQueue(h.user); };
        if (host->unlockQueue)
            device.queue_unlock = [h = *host] { h.unlockQueue(h.user); };
        nr::RuntimeConfig options{};
        options.root = root.string();
        options.width = config->width;
        options.height = config->height;
        options.colour_format = config->format;
        options.linear_input = true;
        options.model_scale = config->controls[xpfsr::nr::Scale]/100.f;
        options.network = small ? 1 : 0;
        // Allocate only the selected pass budget; one-pass flight should not
        // reserve three passes' histories/intermediates at first load.
        options.max_passes = config->controls[xpfsr::nr::Passes];
        options.preprocess = false;
        nr::TemporalConfig temporal{};
        temporal.enable = true;
        // The fallback computes image motion on the GPU. It is never described
        // as exact engine/object motion, and does not enable SDK frame generation.
        temporal.history_strength = 0.5f;
        const std::map<std::string, std::string> noOverrides;
        nr::BuildCfgScope buildSettings(&noOverrides);
        c->runtime =
            std::make_unique<nr::Runtime>(device, options, nr::ControlMaskConfig{}, temporal);
        c->controls.noise_every_frame = false;
        const auto ready = std::string("NR ready: ") + (small ? "CC_SMALL_FP8, " : "CC_SILVER_AARDWOLD, ") +
                           std::to_string(config->controls[xpfsr::nr::Scale]) +
                           "% model resolution, estimated image motion";
        error(reason, bytes, ready.c_str());
        return c.release();
    } catch (const std::exception &e) {
        error(reason, bytes, e.what());
        return nullptr;
    } catch (...) {
        error(reason, bytes, "Unknown NR initialization failure");
        return nullptr;
    }
}
extern "C" __declspec(dllexport) int XPFSR_NR_Record(void *context, const xpfsr::nr::Frame *frame,
                                                     char *reason, uint32_t bytes) noexcept {
    try {
        auto *c = static_cast<Context *>(context);
        require(c && frame && frame->size == sizeof(*frame) &&
                    frame->version == xpfsr::nr::apiVersion && frame->commands && frame->color,
                "Invalid NR frame/ABI");
        require(frame->controls.valid(),"Invalid live NR controls");
        using namespace xpfsr::nr;
        auto &v=frame->controls;
        if(!v[Enabled]||!v[Intensity]){error(reason,bytes,"NR Off: original FSR input retained");return 0;}
        c->controls.intensity=v[Intensity]/100.f;c->controls.local_tone=v[Tone]/100.f;
        c->controls.local_structure=v[Structure]/100.f;c->controls.skin_structure=v[Skin]/100.f;
        c->controls.colour_strength=v[Colour]/100.f;c->controls.detail_strength=v[Detail]/100.f;
        c->controls.max_ratio=v[Highlight]/100.f;c->controls.style=v[Style];
        c->controls.passes=v[Passes];c->controls.transfer=v[Transfer];c->controls.classic_scaler=v[Scaler];
        c->controls.automatic_mask=v[Mask]!=0;
        c->runtime->set_history_strength(v[History]/100.f);c->runtime->set_style_grade(v[Grade]!=0);
        nr::ColourFrame color{};
        color.image = frame->color;
        color.format = c->config.format;
        color.width = c->config.width;
        color.height = c->config.height;
        color.before = color.after = frame->layout;
        color.usage=frame->usage;
        auto result = c->runtime->record_temporal(frame->commands, color, c->controls,
                                                  nr::TemporalFrame{frame->reset != 0, 0});
        const bool applied = result.frame.applied && result.frame.network_dispatches > 0;
        if(applied&&reason&&bytes)std::snprintf(reason,bytes,"NR recorded: %s; %s upscaling at %ux%u; %d%% model, %d pass(es); GPU %.2f ms (network %.2f ms); estimated motion",
            v[Scope]?"full scene":"external only",v[Order]?"after":"before",c->config.width,c->config.height,c->config.controls[Scale],v[Passes],c->runtime->average_gpu_ms(),c->runtime->average_network_ms());
        else error(reason,bytes,"NR bypassed: no neural dispatch recorded");
        return applied ? 1 : 0;
    } catch (const std::exception &e) {
        error(reason, bytes, e.what());
        return -1;
    } catch (...) {
        error(reason, bytes, "Unknown NR recording failure");
        return -1;
    }
}
extern "C" __declspec(dllexport) void XPFSR_NR_Destroy(void *context) noexcept {
    try {
        delete static_cast<Context *>(context);
    } catch (...) {
    }
}
extern "C" __declspec(dllexport) int XPFSR_NR_Query(void*context,xpfsr::nr::Metrics*out,uint32_t bytes) noexcept {
    auto*c=static_cast<Context*>(context);if(!c||!out||bytes!=sizeof(*out))return 0;
    *out={c->runtime->last_gpu_ms(),c->runtime->average_gpu_ms(),c->runtime->average_network_ms()};return 1;
}

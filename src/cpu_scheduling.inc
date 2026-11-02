// v2 opt-in CPU scheduling helper (Windows), included by layer.cpp.
// XPFSR24_CPU_SCHEDULING=1 (from the saved "CPU scheduling" choice) asks for:
//  - this X-Plane process's default CPU sets: P-cores on Intel hybrid, or the
//    larger-cache chiplet on dual-CCD Ryzen X3D (see cpu_scheduling.hpp);
//  - Windows power throttling (EcoQoS) off for this process.
// Only this process is changed, nothing is stored, and both are cleared when
// the renderer device is destroyed. Threads X-Plane pins itself are unaffected.
// cpu_scheduling.hpp is included at the top of layer.cpp (global namespace).
namespace cpusched {
// Windows 10 functions, looked up at run time so the layer still loads if
// they are missing (the helper then reports that and changes nothing).
using GetSetsFn = BOOL(WINAPI *)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
using SetDefaultFn = BOOL(WINAPI *)(HANDLE, const ULONG *, ULONG);
using SetInfoFn = BOOL(WINAPI *)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
template <class T> static T kernel(const char *name) {
    return reinterpret_cast<T>(reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name)));
}
static std::mutex lock;
static bool applied{}, throttlingChanged{};
static std::string summary;

static std::vector<xpfsr::cpu::LogicalCpu> readProcessors() {
    std::vector<xpfsr::cpu::LogicalCpu> out;
    const auto getSets = kernel<GetSetsFn>("GetSystemCpuSetInformation");
    if (!getSets)
        return out;
    ULONG length = 0;
    getSets(nullptr, 0, &length, GetCurrentProcess(), 0);
    if (!length)
        return out;
    std::vector<unsigned char> sets(length);
    if (!getSets(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(sets.data()), length, &length,
                                    GetCurrentProcess(), 0))
        return out;
    // Last-level cache sizes, matched to processors by group and mask.
    struct Cache { WORD group; KAFFINITY mask; DWORD size; };
    std::vector<Cache> caches;
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &bytes);
    std::vector<unsigned char> info(bytes);
    if (bytes && GetLogicalProcessorInformationEx(
                     RelationCache, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(info.data()), &bytes)) {
        for (DWORD at = 0; at + sizeof(DWORD) * 2 <= bytes;) {
            auto *entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(info.data() + at);
            if (!entry->Size)
                break;
            if (entry->Relationship == RelationCache && entry->Cache.Level == 3)
                caches.push_back({entry->Cache.GroupMask.Group, entry->Cache.GroupMask.Mask, entry->Cache.CacheSize});
            at += entry->Size;
        }
    }
    for (ULONG at = 0; at < length;) {
        auto *entry = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(sets.data() + at);
        if (!entry->Size)
            break;
        if (entry->Type == CpuSetInformation) {
            const auto &c = entry->CpuSet;
            xpfsr::cpu::LogicalCpu cpu{c.Id, c.EfficiencyClass, c.LastLevelCacheIndex, 0, bool(c.Parked),
                                       bool(c.Allocated) && !bool(c.AllocatedToTargetProcess)};
            for (const auto &cache : caches)
                if (cache.group == c.Group && c.LogicalProcessorIndex < sizeof(KAFFINITY) * 8 &&
                    (cache.mask >> c.LogicalProcessorIndex) & 1)
                    cpu.cacheKb = cache.size / 1024;
            out.push_back(cpu);
        }
        at += entry->Size;
    }
    return out;
}

static void apply() {
    std::lock_guard<std::mutex> guard(lock);
    if (applied || app::environment(L"XPFSR24_CPU_SCHEDULING") != L"1")
        return;
    applied = true;
    const auto setDefault = kernel<SetDefaultFn>("SetProcessDefaultCpuSets");
    const auto setInfo = kernel<SetInfoFn>("SetProcessInformation");
    if (!setDefault || !setInfo) {
        summary = "needs Windows 10 or newer; nothing changed";
        log("CPU scheduling helper: %s", summary.c_str());
        return;
    }
    const auto processors = readProcessors();
    const auto plan = xpfsr::cpu::plan(processors);
    std::string result = plan.description;
    if (!plan.ids.empty()) {
        std::vector<ULONG> ids(plan.ids.begin(), plan.ids.end());
        if (!setDefault(GetCurrentProcess(), ids.data(), ULONG(ids.size()))) {
            result += "; Windows refused (error " + std::to_string(GetLastError()) + "), unchanged";
        }
    }
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0;  // controlled and not throttled
    throttlingChanged = setInfo(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state)) != 0;
    result += throttlingChanged ? "; power throttling off" : "; power throttling unchanged";
    summary = result;
    log("CPU scheduling helper: %s (%zu processors reported)", result.c_str(), processors.size());
}

static void restore() {
    std::lock_guard<std::mutex> guard(lock);
    if (!applied)
        return;
    applied = false;
    if (const auto setDefault = kernel<SetDefaultFn>("SetProcessDefaultCpuSets"))
        setDefault(GetCurrentProcess(), nullptr, 0);
    const auto setInfo = kernel<SetInfoFn>("SetProcessInformation");
    if (throttlingChanged && setInfo) {
        PROCESS_POWER_THROTTLING_STATE state{};
        state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;  // masks 0: Windows decides again
        setInfo(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
        throttlingChanged = false;
    }
    log("CPU scheduling helper: restored Windows defaults for this process");
}
static std::string status() {
    std::lock_guard<std::mutex> guard(lock);
    return summary;
}
}  // namespace cpusched

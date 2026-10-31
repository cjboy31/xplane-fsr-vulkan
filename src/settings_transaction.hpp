// SPDX-License-Identifier: MIT
#pragma once
#include "launcher_preferences.hpp"
#include "named_lock.hpp"
#include <functional>
namespace launcher {
// baseline is this editor's last submitted/displayed snapshot, rather than an
// externally merged result. Only explicit changes overwrite another editor.
inline Preferences mergeChoices(const Preferences &latest,const Preferences &baseline,const Preferences &proposed){
    auto result=latest;
#define PATCH(field) if(proposed.field!=baseline.field)result.field=proposed.field
    PATCH(simulator);PATCH(arguments);PATCH(presetId);PATCH(mode);PATCH(frameGeneration);PATCH(lastFrameGeneration);PATCH(neuralRendering);
    PATCH(keepSimUISize);PATCH(enabled);PATCH(ghostReduction);PATCH(cockpitRestore);PATCH(alignmentTint);
    PATCH(manual.msaa);PATCH(manual.fxaa);PATCH(manual.scale);PATCH(manual.outputWidth);PATCH(manual.outputHeight);
    PATCH(manual.cloudQuality);
    PATCH(manualApplied);PATCH(manualPending);
#undef PATCH
    for(int i=0;i<xpfsr::nr::Count;++i)if(proposed.nr.value[i]!=baseline.nr.value[i])result.nr.value[i]=proposed.nr.value[i];
    result.nr[xpfsr::nr::Enabled]=result.neuralRendering!=0;
    graphics::require(result.nr.valid(),"Invalid merged NR controls");
    return result;
}
inline Preferences transactChoices(const graphics::fs::path &file,const Preferences &baseline,
                                   const Preferences &proposed,const std::function<void(const Preferences&)> &publish={}){
    app::NamedLock guard(settingsLockName(file));
    auto merged=mergeChoices(load(file),baseline,proposed);
    static volatile LONG serial{};
    auto temporary=file;temporary+=L".transaction-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(InterlockedIncrement(&serial));
    HANDLE staging=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr);
    graphics::require(staging!=INVALID_HANDLE_VALUE,"Cannot create private settings staging file");CloseHandle(staging);
    struct Cleanup {graphics::fs::path path;~Cleanup(){std::error_code error;graphics::fs::remove(path,error);}} cleanup{temporary};
    // Preserve every unrelated INI section. The original is replaced only after
    // both owned sections have been written and verified in the private copy.
    if(graphics::fs::exists(file)){
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,file.c_str());
        graphics::require(CopyFileW(file.c_str(),temporary.c_str(),FALSE)!=FALSE,"Cannot stage settings transaction");
    }else graphics::write(temporary,std::string("\xff\xfe",2));
    save(temporary,merged);
    graphics::require(MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"Cannot commit settings transaction");
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,file.c_str());
    // Publish inside the same transaction so concurrent field edits cannot reach
    // the GPU in an order different from the saved settings.
    if(publish)publish(merged);
    return merged;
}
} // namespace launcher
